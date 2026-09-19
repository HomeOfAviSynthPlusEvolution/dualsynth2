#pragma once

#include <dualsynth/video_filter.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace ds::acceptance {

// Test-only serialization of the public DS2 view. No host pointers, row padding
// or automatic host properties enter the comparison. Float payloads retain bits.
class FrameSnapshot {
public:
  void number(std::uint64_t value) {
    for (int i = 0; i < 8; ++i) { bytes_.push_back(static_cast<char>(value & 255)); value >>= 8; }
  }
  void data(const void* data, std::size_t size) {
    if (size) bytes_.append(static_cast<const char*>(data), size);
  }
  void string(const std::string& value) { number(value.size()); data(value.data(),value.size()); }
  void frame(const VideoFrameView& view, int depth = 0) {
    if (depth > 16) throw std::runtime_error("FrameSnapshot: property nesting exceeds test limit");
    number(static_cast<unsigned>(view.format.color_family));
    number(static_cast<unsigned>(view.format.sample_format));
    number(view.format.subsampling_w); number(view.format.subsampling_h);
    number(view.plane_count);
    for (int p = 0; p < view.plane_count; ++p) {
      const auto& plane = view.plane(p);
      number(plane.width); number(plane.height);
      const auto row = static_cast<std::size_t>(plane.width) * bytes_per_sample(view.format.sample_format);
      for (int y = 0; y < plane.height; ++y)
        data(static_cast<const char*>(plane.data) + static_cast<std::ptrdiff_t>(y) * plane.stride_bytes, row);
    }
    std::vector<std::string> keys;
    if (view.properties) for (const auto& key : view.properties->keys())
      if (key.compare(0,3,"DS_") == 0) keys.push_back(key);
    std::sort(keys.begin(),keys.end());
    number(keys.size());
    for (const auto& key : keys) {
      string(key);
      const auto value = view.properties->find(key);
      if (!value) throw std::runtime_error("FrameSnapshot: enumerated property disappeared");
      number(value->index());
      std::visit([&](const auto& array) {
        number(array.size());
        using T = typename std::decay_t<decltype(array)>::value_type;
        for (const auto& item : array) {
          if constexpr (std::is_same_v<T,std::int64_t>) number(static_cast<std::uint64_t>(item));
          else if constexpr (std::is_same_v<T,double>) {
            std::uint64_t bits; static_assert(sizeof(bits) == sizeof(item));
            std::memcpy(&bits,&item,sizeof(bits)); number(bits);
          } else if constexpr (std::is_same_v<T,PropertyData>) {
            number(static_cast<std::uint64_t>(item.hint)); string(item.bytes);
          } else frame(item.view(),depth+1);
        }
      },*value);
    }
  }
  static void save(const std::string& directory, int n, const VideoOutputInfo& info,
                   const MutableVideoFrameView& output) {
    if (directory.empty()) return;
    FrameSnapshot snapshot;
    snapshot.string("DS2 frame snapshot v1");
    snapshot.number(n); snapshot.number(info.width); snapshot.number(info.height);
    snapshot.number(info.num_frames); snapshot.number(info.fps.numerator); snapshot.number(info.fps.denominator);
    VideoFrameView view{output.format,output.plane_count,{},output.properties};
    for (int p = 0; p < output.plane_count; ++p) {
      const auto& plane = output.plane(p);
      view.planes[p] = {plane.data,plane.stride_bytes,plane.width,plane.height};
    }
    snapshot.frame(view);
    // A host may process the same frame concurrently. Serialize duplicate writes
    // and reject nondeterminism instead of letting the last writer hide it.
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const auto path = std::filesystem::path(directory) / (std::to_string(n)+".bin");
    if (std::filesystem::exists(path)) {
      std::ifstream existing(path,std::ios::binary);
      const std::string previous((std::istreambuf_iterator<char>(existing)),{});
      if (previous != snapshot.bytes_) throw std::runtime_error("FrameSnapshot: repeated frame differs");
    } else {
      std::ofstream file(path,std::ios::binary);
      file.write(snapshot.bytes_.data(),static_cast<std::streamsize>(snapshot.bytes_.size()));
      file.close();
      if (!file) throw std::runtime_error("FrameSnapshot: cannot write snapshot");
    }
  }
private:
  std::string bytes_;
};

} // namespace ds::acceptance
