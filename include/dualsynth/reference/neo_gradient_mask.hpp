#pragma once

#include <dualsynth/video_filter.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace ds::reference {

struct NeoGradientMaskConfig {
  int width = 640;
  int height = 480;
  int color = 0x99ccff;
  int depth = 8;
};

inline Result<NeoGradientMaskConfig> read_neo_gradient_mask_config(const ParamValues* params) {
  NeoGradientMaskConfig config{};
  if (params != nullptr) {
    auto width = params->get_int("width", config.width);
    auto height = params->get_int("height", config.height);
    auto color = params->get_int("color", config.color);
    auto depth = params->get_int("depth", config.depth);
    if (!width.has_value()) {
      return Result<NeoGradientMaskConfig>::failure(width.error());
    }
    if (!height.has_value()) {
      return Result<NeoGradientMaskConfig>::failure(height.error());
    }
    if (!color.has_value()) {
      return Result<NeoGradientMaskConfig>::failure(color.error());
    }
    if (!depth.has_value()) {
      return Result<NeoGradientMaskConfig>::failure(depth.error());
    }

    config.width = width.value();
    config.height = height.value();
    config.color = color.value();
    config.depth = depth.value();
  }

  if (config.width <= 0 || config.height <= 0) {
    return Result<NeoGradientMaskConfig>::failure({
      ErrorCode::InvalidArgument,
      "NeoGradientMask width and height must be positive"
    });
  }
  if (config.depth != 8 && config.depth != 16) {
    return Result<NeoGradientMaskConfig>::failure({
      ErrorCode::UnsupportedFormat,
      "NeoGradientMask depth must be 8 or 16"
    });
  }
  return Result<NeoGradientMaskConfig>::success(config);
}

template <class T>
constexpr int gradient_sample_limit() {
  if constexpr (std::is_same_v<T, unsigned char>) {
    return 255;
  } else {
    return 65535;
  }
}

template <class T>
void fill_gradient_plane(PlaneView2D<T> dst, int component, int luma) {
  const int limit = gradient_sample_limit<T>();
  const int offset8 = std::max(0, component - luma);
  const int offset = (offset8 * limit) / 255;
  for (std::size_t y = 0; y < dst.extent(0); ++y) {
    for (std::size_t x = 0; x < dst.extent(1); ++x) {
      const int gradient = static_cast<int>((x * static_cast<std::size_t>(limit)) / dst.extent(1));
      dst[y, x] = static_cast<T>(std::min(gradient + offset, limit));
    }
  }
}

inline int rgb_component(int color, int plane) {
  switch (plane) {
  case 0:
    return (color >> 16) & 0xFF;
  case 1:
    return (color >> 8) & 0xFF;
  default:
    return color & 0xFF;
  }
}

template <class T>
void render_neo_gradient_mask(NeoGradientMaskConfig config, MutableVideoFrameView dst) {
  const int red = rgb_component(config.color, 0);
  const int green = rgb_component(config.color, 1);
  const int blue = rgb_component(config.color, 2);
  const int luma = (red + green + blue) / 3;

  for (int plane = 0; plane < dst.plane_count; ++plane) {
    fill_gradient_plane(as_plane_view<T>(dst.plane(plane)), rgb_component(config.color, plane), luma);
  }
}

struct NeoGradientMask {
  static constexpr const char* name = "NeoGradientMask";
  static constexpr int input_count = 0;
  static constexpr OutputOrigin output_origin = OutputOrigin::fresh();
  inline static const std::array<ParamSpec, 4> params{
    ParamSpec{"width", ParamType::Integer, ParamValue{640}, false},
    ParamSpec{"height", ParamType::Integer, ParamValue{480}, false},
    ParamSpec{"color", ParamType::Integer, ParamValue{0x99ccff}, false},
    ParamSpec{"depth", ParamType::Integer, ParamValue{8}, false}
  };

  static Result<VideoInitResult> init(VideoInitContext& context) {
    if (!context.inputs.empty()) {
      return Result<VideoInitResult>::failure({
        ErrorCode::InvalidArgument,
        "NeoGradientMask is a source filter and takes no video inputs"
      });
    }

    auto config = read_neo_gradient_mask_config(context.params);
    if (!config.has_value()) {
      return Result<VideoInitResult>::failure(config.error());
    }

    const auto sample_format = config.value().depth == 8 ? SampleFormat::UInt8 : SampleFormat::UInt16;
    return Result<VideoInitResult>::success(
      VideoInitResult{
        VideoOutputInfo{
          config.value().width,
          config.value().height,
          10000,
          VideoFormat{ColorFamily::Rgb, sample_format, 3, 0, 0},
          FrameRate{30000, 1001}
        }
      }
    );
  }

  static Result<VideoRequestResult> request(VideoRequestContext&) {
    return Result<VideoRequestResult>::success(VideoRequestResult{});
  }

  static Result<VideoProcessResult> process_source(
    int,
    const ParamValues& param_values,
    MutableVideoFrameView dst
  ) {
    auto config = read_neo_gradient_mask_config(&param_values);
    if (!config.has_value()) {
      return Result<VideoProcessResult>::failure(config.error());
    }

    if (dst.format.sample_format == SampleFormat::UInt8) {
      render_neo_gradient_mask<unsigned char>(config.value(), dst);
    } else if (dst.format.sample_format == SampleFormat::UInt16) {
      render_neo_gradient_mask<unsigned short>(config.value(), dst);
    } else {
      return Result<VideoProcessResult>::failure({
        ErrorCode::UnsupportedFormat,
        "NeoGradientMask output must be uint8 or uint16"
      });
    }
    return Result<VideoProcessResult>::success(VideoProcessResult{});
  }
};

struct NeoGradientMaskBridge {
  using Core = NeoGradientMask;

  static constexpr const char* vs_name = "NeoGradientMask";
  static constexpr const char* vs_signature = "width:int:opt;height:int:opt;color:int:opt;depth:int:opt;";
  static constexpr const char* avs_name = "DSNeoGradientMask";
  static constexpr const char* avs_signature = "[width]i[height]i[color]i[depth]i";
};

} // namespace ds::reference
