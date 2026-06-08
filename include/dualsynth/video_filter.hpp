#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/plane_span.hpp>

#include <vector>

namespace ds {

struct VideoInputInfo {
  int width;
  int height;
  int num_frames;
};

struct RequestedVideoFrame {
  int input_index;
  int frame_number;
  PlaneSpan<const unsigned char> plane;
};

struct VideoFrameRequest {
  int input_index;
  int frame_number;
};

class VideoFrameProvider {
public:
  virtual ~VideoFrameProvider() = default;
  virtual Result<RequestedVideoFrame> get(int input_index, int frame_number) = 0;
};

struct VideoRequestResult {};

struct VideoRequestContext {
  int output_frame;
  std::vector<VideoFrameRequest>& requests;

  void request_frame(int input_index, int frame_number) {
    requests.push_back(VideoFrameRequest{input_index, frame_number});
  }
};

struct VideoProcessResult {};

struct VideoProcessContext {
  int output_frame;
  VideoFrameProvider& frames;
  PlaneSpan<unsigned char> dst;
};

} // namespace ds
