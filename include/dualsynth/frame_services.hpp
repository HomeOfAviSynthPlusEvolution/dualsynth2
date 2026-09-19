#pragma once

#include <dualsynth/frame.hpp>

#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ds {

class FrameStorage;

// An owning, immutable host frame. Views remain valid while this reference lives.
class FrameRef {
public:
  FrameRef() = default;
  explicit FrameRef(std::shared_ptr<FrameStorage> storage) : storage_(std::move(storage)) {}
  explicit operator bool() const noexcept { return bool(storage_); }
  VideoFrameView view() const;
  const FrameStorage& storage() const;
private:
  std::shared_ptr<FrameStorage> storage_;
};

enum class DataHint { Unknown = -1, Binary = 0, Utf8 = 1 };
struct PropertyData {
  std::string bytes; // Length-delimited; embedded NUL bytes are significant.
  DataHint hint = DataHint::Unknown;
};
using PropertyValue = std::variant<std::vector<std::int64_t>, std::vector<double>,
                                  std::vector<PropertyData>, std::vector<FrameRef>>;

// Reads return owning snapshots. No numeric conversion, string truncation or
// normalization is performed. Missing keys return nullopt; invalid types throw.
class FrameProperties {
public:
  virtual ~FrameProperties() = default;
  virtual std::vector<std::string> keys() const = 0;
  virtual std::optional<PropertyValue> find(const std::string& key) const = 0;
  virtual void set(const std::string& key, const PropertyValue& value) = 0;
  virtual bool erase(const std::string& key) = 0;
};

class FrameStorage {
public:
  virtual ~FrameStorage() = default;
  virtual VideoFrameView read() const = 0;
  virtual MutableVideoFrameView write() = 0;
};

inline const FrameStorage& FrameRef::storage() const {
  if (!storage_) throw std::logic_error("DualSynth: empty frame reference");
  return *storage_;
}
inline VideoFrameView FrameRef::view() const { return storage().read(); }

// Only newly allocated frames are writable. Publishing consumes ownership;
// callers must discard all borrowed writable views before publish().
class WritableFrame {
public:
  explicit WritableFrame(std::unique_ptr<FrameStorage> storage) : storage_(std::move(storage)) {}
  WritableFrame(WritableFrame&&) noexcept = default;
  WritableFrame& operator=(WritableFrame&&) noexcept = default;
  MutableVideoFrameView view() {
    if (!storage_) throw std::logic_error("DualSynth: frame already published");
    return storage_->write();
  }
  FrameRef publish() && {
    if (!storage_) throw std::logic_error("DualSynth: frame already published");
    return FrameRef(std::shared_ptr<FrameStorage>(std::move(storage_)));
  }
private:
  std::unique_ptr<FrameStorage> storage_;
};

class FrameFactory {
public:
  virtual ~FrameFactory() = default;
  WritableFrame copy(const FrameRef& source);
  virtual WritableFrame allocate(VideoFormat format, int width, int height,
                                 const FrameRef& property_source = {}) = 0;
};

inline void validate_frame_dimensions(VideoFormat format, int width, int height) {
  auto supported = is_supported_video_format(format);
  if (!supported.has_value() || !supported.value() || width <= 0 || height <= 0 ||
      format.subsampling_w < 0 || format.subsampling_w > 2 ||
      format.subsampling_h < 0 || format.subsampling_h > 2 ||
      width % (1 << format.subsampling_w) || height % (1 << format.subsampling_h) ||
      width > (std::numeric_limits<int>::max)() / bytes_per_sample(format.sample_format)) {
    throw std::invalid_argument("DualSynth: invalid auxiliary frame dimensions or format");
  }
}

// Copy active pixels and inherit the native property map. Property-held frame
// references remain shared and immutable; the returned frame/map is writable.
inline WritableFrame FrameFactory::copy(const FrameRef& source) {
  const auto input = source.view();
  const auto& first = input.plane(0);
  validate_frame_dimensions(input.format, first.width, first.height);
  auto result = allocate(input.format, first.width, first.height, source);
  auto output = result.view();
  if (input.plane_count != output.plane_count || input.format != output.format)
    throw std::runtime_error("DualSynth: copied frame format mismatch");
  for (int p = 0; p < input.plane_count; ++p) {
    const auto& src = input.plane(p);
    const auto& dst = output.plane(p);
    if (src.width != dst.width || src.height != dst.height || !src.data || !dst.data)
      throw std::runtime_error("DualSynth: copied plane geometry mismatch");
    const auto bytes = static_cast<std::size_t>(src.width) * bytes_per_sample(input.format.sample_format);
    for (int y = 0; y < src.height; ++y)
      std::memcpy(static_cast<char*>(dst.data) + static_cast<std::ptrdiff_t>(y) * dst.stride_bytes,
                  static_cast<const char*>(src.data) + static_cast<std::ptrdiff_t>(y) * src.stride_bytes, bytes);
  }
  return result;
}

} // namespace ds
