#pragma once

#include <dualsynth/video_filter.hpp>
#include "frame_snapshot.hpp"
#include <cstring>
#include <climits>
#include <limits>

namespace ds::acceptance {

// Synthetic SDK contract test: no downstream algorithm or data format.
struct FrameServices {
  static constexpr const char* name = "FrameServices";
  static constexpr int input_count = dynamic_video_inputs;
  static constexpr HostRequirements host_requirements{true, 11, 0};
  struct State {
    bool verify = false;
    int input_count = 0;
    FrameRef first;
    std::string snapshot;
    bool pattern = false;
    VideoOutputInfo output;
  };

  static void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(std::string("FrameServices: ") + what);
  }
  template<class T>
  static std::vector<T> get(const FrameProperties& props, const char* key) {
    auto value = props.find(key);
    check(value.has_value(), key);
    auto* array = std::get_if<std::vector<T>>(&*value);
    check(array != nullptr, "property type mismatch");
    return *array;
  }
  static void verify_numeric_snapshots(FrameFactory& factory) {
    std::vector<std::int64_t> ints(32000);
    std::vector<double> floats(32000);
    for (int i = 0; i < 32000; ++i) {
      ints[i] = 9007199254740993LL - i * 17;
      floats[i] = (i - 16000) / 8.0;
    }
    floats[0] = -0.0;
    std::optional<PropertyValue> int_snapshot, float_snapshot;
    {
      auto frame = factory.allocate({ColorFamily::Gray,SampleFormat::UInt8,1,0,0},1,1);
      auto& props = *frame.view().properties;
      props.set("Ints", ints);
      props.set("Floats", floats);
      props.set("EmptyInts", std::vector<std::int64_t>{});
      props.set("EmptyFloats", std::vector<double>{});
      int_snapshot = props.find("Ints");
      float_snapshot = props.find("Floats");
      check(int_snapshot && float_snapshot, "numeric snapshots missing");
      check(get<std::int64_t>(props,"EmptyInts").empty(), "empty integer snapshot");
      check(get<double>(props,"EmptyFloats").empty(), "empty float snapshot");
      check(!props.find("Missing"), "missing numeric property");
      // Replacing the values and releasing the only frame owner must not affect
      // PropertyValue, including on the AviSynth C++ virtual-interface path.
      props.set("Ints", std::vector<std::int64_t>{-1});
      props.set("Floats", std::vector<double>{-1.0});
    }
    check(std::get<std::vector<std::int64_t>>(*int_snapshot) == ints, "integer snapshot lifetime or order");
    const auto& actual = std::get<std::vector<double>>(*float_snapshot);
    check(actual.size() == floats.size() &&
          std::memcmp(actual.data(),floats.data(),floats.size() * sizeof(double)) == 0,
          "float snapshot lifetime or order");
  }
  static void verify(const VideoFrameView& view) {
    check(view.properties != nullptr, "missing properties");
    const auto& props = *view.properties;
    const auto ints = props.inspect("DS_Ints");
    check(ints && ints->type == PropertyType::Integer && ints->count == 3, "integer metadata");
    const auto empty = props.inspect("DS_Empty");
    check(empty && empty->type == PropertyType::Integer && empty->count == 0, "empty metadata");
    const auto frames = props.inspect("DS_Frames");
    check(frames && frames->type == PropertyType::VideoFrame && frames->count == 2, "frame metadata");
    check(!props.inspect("DS_Deleted"), "missing metadata");
    if (auto opaque = props.inspect("OpaqueNode")) {
      check(opaque->type == PropertyType::VideoNode && opaque->count == 1, "opaque node metadata");
      bool rejected = false;
      try { props.find("OpaqueNode"); } catch (const std::invalid_argument&) { rejected = true; }
      check(rejected, "opaque node should not be materialized");
    }
    check(get<std::int64_t>(props,"DS_Ints") == std::vector<std::int64_t>{INT64_MIN,INT64_MAX,9007199254740993LL}, "integer precision");
    auto floats = get<double>(props,"DS_Floats");
    const double expected[] = {0.0,-0.0,1.0 / 8.0};
    check(floats.size() == 3 && std::memcmp(floats.data(),expected,sizeof(expected)) == 0, "float bit preservation");
    auto bytes = get<PropertyData>(props,"DS_Data");
    check(bytes.size() == 2 && bytes[0].bytes == std::string("a\0b",3) && bytes[0].hint == DataHint::Binary &&
          bytes[1].bytes == "text" && bytes[1].hint == DataHint::Utf8, "data length or hint");
    check(get<std::int64_t>(props,"DS_Empty").empty(), "typed empty array");
    check(!props.find("DS_Deleted"), "property deletion");
    auto refs = get<FrameRef>(props,"DS_Frames");
    check(refs.size() == 2, "frame array count");
    for (const auto& ref : refs) {
      auto aux = ref.view();
      check(aux.format.sample_format == SampleFormat::UInt16 && aux.plane_count == 1, "auxiliary format");
      auto plane = as_plane<std::uint16_t>(aux.plane(0));
      check(plane.width() == 7 && plane.height() == 3, "auxiliary dimensions");
      for (int y = 0; y < 3; ++y) for (int x = 0; x < 7; ++x)
        check(plane.row(y)[x] == 1000 + y * 7 + x, "auxiliary pixels");
      check(get<std::int64_t>(*aux.properties,"DS_Tag") == std::vector<std::int64_t>{42}, "auxiliary properties");
    }
  }
  static Result<VideoInitStateResult<State>> init(VideoInitContext& ctx) {
    check(ctx.inputs.size() >= 1 && ctx.frames && ctx.frame_factory, "initialization services");
    verify_numeric_snapshots(*ctx.frame_factory);
    check(ctx.input_groups.size() == 2 && ctx.input_groups[0].name == "clips" &&
          ctx.input_groups[0].count >= 1 && ctx.input_groups[1].name == "extra", "input groups");
    auto verify_param = ctx.params->get_bool("verify",false);
    if (!verify_param.has_value()) return Result<VideoInitStateResult<State>>::failure(verify_param.error());
    auto first = ctx.frames->get(0,0);
    if (!first.has_value()) return Result<VideoInitStateResult<State>>::failure(first.error());
    check(bool(first.value().owner), "owning initialization frame");
    const bool checking = verify_param.value();
    if (checking) verify(first.value().frame);
    const auto& in = ctx.inputs[0];
    check(in.num_frames <= INT_MAX / 2 && in.fps.numerator <= INT64_MAX / 2, "output overflow");
    auto snapshot = ctx.params->get_string("snapshot","");
    auto pattern = ctx.params->get_bool("pattern",false);
    if (!snapshot.has_value()) return Result<VideoInitStateResult<State>>::failure(snapshot.error());
    if (!pattern.has_value()) return Result<VideoInitStateResult<State>>::failure(pattern.error());
    const VideoOutputInfo output{in.width,in.height,checking ? in.num_frames : in.num_frames * 2,in.format,
       {checking ? in.fps.numerator : in.fps.numerator * 2,in.fps.denominator}};
    return Result<VideoInitStateResult<State>>::success({output,
      {checking,static_cast<int>(ctx.inputs.size()),first.value().owner,snapshot.value(),pattern.value(),output}});
  }
  static OutputOrigin output_origin_for(int n, const State& state) {
    auto origin = OutputOrigin::fresh();
    origin.prop_frame = state.verify ? n : n / 2;
    return origin;
  }
  static Result<VideoRequestResult> request(VideoRequestContext& ctx) {
    const auto& state = ctx.state<State>();
    for (int i = 0; i < state.input_count; ++i)
      ctx.request_frame_clamped(i,state.verify ? ctx.output_frame : ctx.output_frame / 2);
    return Result<VideoRequestResult>::success({});
  }
  static Result<VideoProcessResult> process(VideoProcessContext& ctx) {
    const auto& state = ctx.state<State>();
    // Retained initialization frames remain valid across concurrent requests.
    if (state.verify) verify(state.first.view());
    auto src = ctx.frames.get(0,state.verify ? ctx.output_frame : ctx.output_frame / 2);
    if (!src.has_value()) return Result<VideoProcessResult>::failure(src.error());
    if (state.verify) {
      verify(src.value().frame);
      auto copy = ctx.frame_factory->copy(src.value().owner);
      auto view = copy.view();
      for (int p = 0; p < view.plane_count; ++p) {
        const auto& original = src.value().frame.plane(p);
        auto& copied = view.plane(p);
        const auto bytes = static_cast<std::size_t>(copied.width) * bytes_per_sample(view.format.sample_format);
        for (int y = 0; y < copied.height; ++y)
          check(std::memcmp(static_cast<const char*>(original.data) + y * original.stride_bytes,
                            static_cast<const char*>(copied.data) + y * copied.stride_bytes, bytes) == 0,
                "copy active plane bytes");
        auto* pixel = static_cast<unsigned char*>(copied.data);
        const auto before = *static_cast<const unsigned char*>(original.data);
        *pixel ^= 1;
        check(*static_cast<const unsigned char*>(original.data) == before, "copy plane isolation");
      }
      verify(std::move(copy).publish().view());
    }
    for (int p = 0; p < ctx.dst.plane_count; ++p) {
      const auto& in = src.value().frame.plane(p);
      const auto& out = ctx.dst.plane(p);
      const auto bytes = static_cast<std::size_t>(out.width) * bytes_per_sample(ctx.dst.format.sample_format);
      for (int y = 0; y < out.height; ++y)
        std::memcpy(static_cast<char*>(out.data) + y * out.stride_bytes,
                    static_cast<const char*>(in.data) + y * in.stride_bytes,bytes);
    }
    if (state.verify) {
      verify({ctx.dst.format,ctx.dst.plane_count,{},ctx.dst.properties});
      FrameSnapshot::save(state.snapshot,ctx.output_frame,state.output,ctx.dst);
      return Result<VideoProcessResult>::success({});
    }
    if (state.pattern) for (int p = 0; p < ctx.dst.plane_count; ++p) {
      const auto& plane = ctx.dst.plane(p);
      for (int y = 0; y < plane.height; ++y) for (int x = 0; x < plane.width; ++x) {
        const int value = (ctx.output_frame / 2 * 17 + p * 41 + y * 3 + x) & 255;
        if (ctx.dst.format.sample_format == SampleFormat::Float32)
          as_plane<float>(plane).row(y)[x] = static_cast<float>(value - 128) / 256.0f;
        else if (bytes_per_sample(ctx.dst.format.sample_format) == 1)
          as_plane<std::uint8_t>(plane).row(y)[x] = static_cast<std::uint8_t>(value);
        else as_plane<std::uint16_t>(plane).row(y)[x] = static_cast<std::uint16_t>((value * 257) & ((1u << bits_per_sample(ctx.dst.format.sample_format)) - 1));
      }
    }
    auto aux = ctx.frame_factory->allocate({ColorFamily::Gray,SampleFormat::UInt16,1,0,0},7,3);
    auto writable = aux.view();
    auto plane = as_plane<std::uint16_t>(writable.plane(0));
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 7; ++x) plane.row(y)[x] = static_cast<std::uint16_t>(1000 + y * 7 + x);
    writable.properties->set("DS_Tag",std::vector<std::int64_t>{42});
    auto ref = std::move(aux).publish();
    auto copied = ctx.frame_factory->copy(ref);
    {
      auto view = copied.view();
      check(get<std::int64_t>(*view.properties,"DS_Tag") == std::vector<std::int64_t>{42}, "copy inherited properties");
      auto pixels = as_plane<std::uint16_t>(view.plane(0));
      for (int y = 0; y < 3; ++y) for (int x = 0; x < 7; ++x)
        check(pixels.row(y)[x] == 1000 + y * 7 + x, "copy inherited pixels");
      pixels.row(0)[0] = 2000;
      view.properties->set("DS_Tag",std::vector<std::int64_t>{99});
      check(as_plane<std::uint16_t>(ref.view().plane(0)).row(0)[0] == 1000, "copy pixel isolation");
      check(get<std::int64_t>(*ref.view().properties,"DS_Tag") == std::vector<std::int64_t>{42}, "copy property isolation");
      pixels.row(0)[0] = 1000;
      view.properties->set("DS_Tag",std::vector<std::int64_t>{42});
    }
    auto copied_ref = std::move(copied).publish();
    auto& props = *ctx.dst.properties;
    props.set("DS_Frames",std::vector<FrameRef>{ref,copied_ref});
    props.set("DS_Ints",std::vector<std::int64_t>{INT64_MIN,INT64_MAX,9007199254740993LL});
    props.set("DS_Floats",std::vector<double>{0.0,-0.0,1.0/8.0});
    props.set("DS_Data",std::vector<PropertyData>{{std::string("a\0b",3),DataHint::Binary},{"text",DataHint::Utf8}});
    props.set("DS_Empty",std::vector<std::int64_t>{});
    props.set("DS_Deleted",std::vector<std::int64_t>{1});
    check(props.erase("DS_Deleted") && !props.erase("DS_Deleted"),"erase result");
    FrameSnapshot::save(state.snapshot,ctx.output_frame,state.output,ctx.dst);
    return Result<VideoProcessResult>::success({});
  }
};

struct FrameServicesBridge {
  using Core = FrameServices;
  static constexpr const char* vs_name = "FrameServices";
  static constexpr const char* avs_name = "DSFrameServices";
  static constexpr const char* vs_signature = "clips:vnode[];extra:vnode:opt;verify:int:opt;snapshot:data:opt;pattern:int:opt;";
  static constexpr const char* avs_signature = ".[extra]c[verify]b[snapshot]s[pattern]b";
  static constexpr const char* missing_input_error = "FrameServices requires a nonempty clip array";
  static constexpr const char* vs_format_error = "FrameServices requires planar video";
  static constexpr const char* avs_format_error = vs_format_error;
  static constexpr std::size_t parity_source_index = 0;
  static constexpr bool forward_audio = false;
  static FilterDescriptor descriptor() {
    return {"FrameServices",{{"clips",ParamType::Clip,{},true,true},
      {"extra",ParamType::Clip,{},false,false},{"verify",ParamType::Boolean,false,false,false},
      {"snapshot",ParamType::String,"",false,false},{"pattern",ParamType::Boolean,false,false,false}}};
  }
};

} // namespace ds::acceptance
