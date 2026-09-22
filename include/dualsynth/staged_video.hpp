#pragma once

#include <dualsynth/video_filter.hpp>
#include <cstring>

namespace ds {

// Opt in by declaring RequestState and advance(ctx, request), plus
// process(ctx, request). Existing request/process filters keep their old path.
template<class Filter, class = void> struct HasVideoStages : std::false_type {};
template<class Filter> struct HasVideoStages<Filter, std::void_t<typename Filter::RequestState>>
  : std::true_type {};

enum class VideoStageResult { RequestFrames, Ready };
struct VideoFrameReleaseResult {};
// Only staged dependencies can be explicitly forgotten. get() still returns an
// owning snapshot: forgetting the provider's reference never revokes a copy.
class VideoStageFrameProvider : public VideoFrameProvider {
public:
  virtual Result<VideoFrameReleaseResult> release_frame(int input, int n) = 0;
};
struct VideoStageContext : VideoRequestContext {
  VideoStageFrameProvider& frames; // Only frames acquired by earlier stages.
  OutputOrigin& origin;      // Resolved after Ready; never fetched speculatively.
  VideoStageContext(int n, std::vector<VideoFrameRequest>& requests,
                    Span<const VideoInputInfo> inputs, const void* state,
                    VideoStageFrameProvider& frames, OutputOrigin& origin)
    : VideoRequestContext{n,requests,inputs,state}, frames(frames), origin(origin) {}
  Result<VideoFrameReleaseResult> release_frame(int input, int n) {
    return frames.release_frame(input,n);
  }
};

// One object per output request, never shared through instance-level state.
// Hosts acquire pending() and call accept(), then resume advance().
template<class Filter>
class StagedVideoRequest final : public VideoStageFrameProvider {
  static_assert(FilterRequirements<Filter>::value.frame_services,
                "staged filters must opt into owning frame services");
public:
  explicit StagedVideoRequest(int n, const VideoFilterState<Filter>& state)
    : n_(n), origin_(resolve_output_origin<Filter>(n,state)) {}

  Result<RequestedVideoFrame> get(int input, int n) override {
    for (const auto& frame : frames_)
      if (frame.input_index == input && frame.frame_number == n)
        return Result<RequestedVideoFrame>::success(frame);
    return Result<RequestedVideoFrame>::failure({ErrorCode::InvalidArgument,
      "DualSynth: staged frame was not acquired"});
  }
  const std::vector<VideoFrameRequest>& pending() const { return pending_; }
  Result<VideoFrameReleaseResult> release_frame(int input, int n) override {
    if (!advancing_)
      return Result<VideoFrameReleaseResult>::failure({ErrorCode::InvalidArgument,
        "DualSynth: staged frames can only be released during advance"});
    const auto found = std::find_if(frames_.begin(),frames_.end(),[&](const auto& frame) {
      return frame.input_index == input && frame.frame_number == n;
    });
    if (found == frames_.end())
      return Result<VideoFrameReleaseResult>::failure({ErrorCode::InvalidArgument,
        "DualSynth: released staged frame was not acquired"});
    frames_.erase(found);
    return Result<VideoFrameReleaseResult>::success({});
  }
  void accept(RequestedVideoFrame frame) {
    const auto index = frames_received_;
    if (index >= pending_.size() ||
        !(pending_[index] == VideoFrameRequest{frame.input_index,frame.frame_number}) || !frame.owner)
      throw std::logic_error("DualSynth: invalid staged frame delivery");
    frames_.push_back(std::move(frame));
    ++frames_received_;
  }
  // true: ready to allocate/process; false: pending contains a nonempty batch.
  bool advance(Span<const VideoInputInfo> inputs, const VideoFilterState<Filter>& state) {
    if (frames_received_ != pending_.size())
      throw std::logic_error("DualSynth: staged frames are not ready");
    pending_.clear();
    frames_received_ = 0;
    if (ready_) return true;
    VideoStageContext context{n_,pending_,inputs,&state,*this,origin_};
    const auto result = [&] {
      advancing_ = true;
      try {
        auto result = Filter::advance(context,request_);
        advancing_ = false;
        return result;
      } catch (...) {
        advancing_ = false;
        throw;
      }
    }();
    if (!result.has_value()) throw std::runtime_error(result.error().message);
    if (result.value() == VideoStageResult::Ready) {
      if (!pending_.empty()) throw std::logic_error("DualSynth: Ready stage requested more frames");
      ready_ = true;
      const auto dependencies = request_output_origin_frame(origin_,n_,inputs,pending_);
      if (!dependencies.has_value()) throw std::invalid_argument(dependencies.error().message);
    } else if (result.value() != VideoStageResult::RequestFrames) {
      throw std::logic_error("DualSynth: invalid stage result");
    }
    std::vector<VideoFrameRequest> unique;
    for (const auto& r : pending_) {
      if (r.input_index < 0 || static_cast<std::size_t>(r.input_index) >= inputs.size() ||
          r.frame_number < 0 || r.frame_number >= inputs[r.input_index].num_frames)
        throw std::invalid_argument("DualSynth: staged frame request is out of range");
      if (video_request_pattern<Filter>(r.input_index,state) == VideoRequestPattern::StrictSpatial && r.frame_number != n_)
        throw std::invalid_argument("DualSynth: temporal request violates strict spatial dependency");
      if (!get(r.input_index,r.frame_number).has_value() &&
          std::find(unique.begin(),unique.end(),r) == unique.end()) unique.push_back(r);
    }
    pending_ = std::move(unique);
    if (!ready_ && pending_.empty())
      throw std::logic_error("DualSynth: stage made no new frame requests");
    return ready_ && pending_.empty();
  }

  FrameRef finish(const VideoOutputInfo& output, Span<const VideoInputInfo> inputs,
                  VideoFilterState<Filter>& state, FrameFactory& factory) {
    if (!ready_ || frames_received_ != pending_.size())
      throw std::logic_error("DualSynth: staged output is not ready");
    auto source = [&](int input, int n) {
      auto result = get(input,n < 0 ? n_ : n);
      if (!result.has_value()) throw std::logic_error(result.error().message);
      return result.value().owner;
    };
    FrameRef pixels, props;
    if (origin_.pixels != OutputPixelPolicy::Fresh) {
      const int input = origin_.pixel_input_index;
      if (input < 0 || static_cast<std::size_t>(input) >= inputs.size() ||
          inputs[input].format != output.format || inputs[input].width != output.width ||
          inputs[input].height != output.height)
        throw std::invalid_argument("DualSynth: invalid staged pixel origin");
      pixels = source(input,origin_.pixel_frame);
    }
    if (origin_.prop_input_index >= 0) props = source(origin_.prop_input_index,origin_.prop_frame);
    auto frame = factory.allocate(output.format,output.width,output.height,props);
    auto dst = frame.view();
    if (pixels) {
      auto src = pixels.view();
      if (src.format != dst.format || src.plane_count != dst.plane_count)
        throw std::runtime_error("DualSynth: staged pixel format mismatch");
      for (int p = 0; p < src.plane_count; ++p) {
        const auto& a = src.plane(p); const auto& b = dst.plane(p);
        if (a.width != b.width || a.height != b.height || !a.data || !b.data)
          throw std::runtime_error("DualSynth: staged pixel geometry mismatch");
        const auto bytes = static_cast<std::size_t>(a.width) * bytes_per_sample(src.format.sample_format);
        for (int y = 0; y < a.height; ++y)
          std::memcpy(static_cast<char*>(b.data) + static_cast<std::ptrdiff_t>(y) * b.stride_bytes,
                      static_cast<const char*>(a.data) + static_cast<std::ptrdiff_t>(y) * a.stride_bytes,bytes);
      }
    }
    VideoProcessContext context{n_,*this,dst,&state,&factory};
    const auto result = Filter::process(context,request_);
    if (!result.has_value()) throw std::runtime_error(result.error().message);
    return std::move(frame).publish();
  }
private:
  int n_;
  OutputOrigin origin_;
  bool ready_ = false;
  bool advancing_ = false;
  std::size_t frames_received_ = 0;
  std::vector<VideoFrameRequest> pending_;
  std::vector<RequestedVideoFrame> frames_;
  typename Filter::RequestState request_{};
};

template<class Filter>
void acquire_video_stages(StagedVideoRequest<Filter>& request, VideoFrameProvider& source,
                          Span<const VideoInputInfo> inputs, const VideoFilterState<Filter>& state) {
  while (!request.advance(inputs,state)) {
    for (const auto& dependency : request.pending()) {
      auto frame = source.get(dependency.input_index,dependency.frame_number);
      if (!frame.has_value()) throw std::runtime_error(frame.error().message);
      request.accept(std::move(frame.value()));
    }
  }
}
} // namespace ds
