#pragma once

#include <dualsynth/frame.hpp>

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

} // namespace ds
