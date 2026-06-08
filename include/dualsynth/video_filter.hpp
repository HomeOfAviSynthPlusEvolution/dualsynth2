#pragma once

#include <dualsynth/error.hpp>
#include <dualsynth/plane_span.hpp>

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

class VideoFrameProvider {
public:
  virtual ~VideoFrameProvider() = default;
  virtual Result<RequestedVideoFrame> get(int input_index, int frame_number) = 0;
};

struct VideoProcessResult {};

struct VideoProcessContext {
  int output_frame;
  VideoFrameProvider& frames;
  PlaneSpan<unsigned char> dst;
};

} // namespace ds
