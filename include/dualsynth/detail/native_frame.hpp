#pragma once

#include <dualsynth/frame_services.hpp>
#include <climits>

namespace ds::detail {

template<class Traits> class NativeFrame;

template<class Traits>
class NativeProperties final : public FrameProperties {
public:
  using Frame = typename Traits::Frame;
  NativeProperties(const Traits& traits, Frame& frame, bool writable)
    : traits_(traits), frame_(frame), writable_(writable) {}

  std::vector<std::string> keys() const override {
    const auto* map = traits_.props(frame_);
    std::vector<std::string> result;
    const int count = traits_.num_keys(map);
    for (int i = 0; i < count; ++i) result.emplace_back(traits_.key(map, i));
    return result;
  }
  std::optional<PropertyInfo> inspect(const std::string& name) const override {
    validate_key(name);
    const auto* map = traits_.props(frame_);
    const int count = traits_.count(map, name.c_str());
    if (count < 0) return std::nullopt;
    PropertyType type = PropertyType::Unknown;
    switch (traits_.type(map, name.c_str())) {
      case 'i': type = PropertyType::Integer; break;
      case 'f': type = PropertyType::Float; break;
      case 's': type = PropertyType::Data; break;
      case 'v': type = PropertyType::VideoFrame; break;
      case 'a': type = PropertyType::AudioFrame; break;
      case 'c': type = PropertyType::VideoNode; break;
      case 'n': type = PropertyType::AudioNode; break;
      case 'm': type = PropertyType::Function; break;
    }
    return PropertyInfo{type, static_cast<std::size_t>(count)};
  }
  std::optional<PropertyValue> find(const std::string& name) const override {
    validate_key(name);
    const auto* map = traits_.props(frame_);
    const char* key = name.c_str();
    const int count = traits_.count(map, key);
    if (count < 0) return std::nullopt;
    const auto type = traits_.type(map, key);
    if (type == 'i') {
      std::vector<std::int64_t> values;
      values.reserve(count);
      for (int i = 0; i < count; ++i) { int error = 0; auto v = traits_.get_int(map, key, i, &error); check(error); values.push_back(v); }
      return PropertyValue(std::move(values));
    }
    if (type == 'f') {
      std::vector<double> values;
      values.reserve(count);
      for (int i = 0; i < count; ++i) { int error = 0; auto v = traits_.get_float(map, key, i, &error); check(error); values.push_back(v); }
      return PropertyValue(std::move(values));
    }
    if (type == 's') {
      std::vector<PropertyData> values;
      values.reserve(count);
      for (int i = 0; i < count; ++i) {
        int error = 0;
        int size = traits_.data_size(map, key, i, &error); check(error);
        const char* data = traits_.get_data(map, key, i, &error); check(error);
        const int hint = traits_.data_hint(map, key, i, &error); check(error);
        if (size < 0 || (!data && size)) throw std::runtime_error("DualSynth: invalid property data");
        values.push_back({std::string(data ? data : "", static_cast<std::size_t>(size)), static_cast<DataHint>(hint)});
      }
      return PropertyValue(std::move(values));
    }
    if (type == 'v') {
      std::vector<FrameRef> values;
      values.reserve(count);
      for (int i = 0; i < count; ++i) {
        int error = 0;
        auto frame = traits_.get_frame(map, key, i, &error);
        check(error);
        values.push_back(NativeFrame<Traits>::adopt(traits_, std::move(frame)));
      }
      return PropertyValue(std::move(values));
    }
    throw std::invalid_argument("DualSynth: unsupported frame property type for '" + name + "'");
  }
  void set(const std::string& name, const PropertyValue& value) override {
    validate_key(name);
    if (!writable_) throw std::logic_error("DualSynth: read-only frame properties");
    // Validate all lengths/foreign frame references before mutating the map.
    std::visit([&](const auto& values) {
      if (values.size() > INT_MAX) throw std::length_error("DualSynth: property array too large");
      using V = typename std::decay_t<decltype(values)>::value_type;
      if constexpr (std::is_same_v<V, PropertyData>) {
        for (const auto& data : values) {
          if (data.bytes.size() > INT_MAX) throw std::length_error("DualSynth: property data too large");
          if (data.hint != DataHint::Unknown && data.hint != DataHint::Binary && data.hint != DataHint::Utf8)
            throw std::invalid_argument("DualSynth: invalid data hint");
        }
      } else if constexpr (std::is_same_v<V, FrameRef>) {
        for (const auto& frame : values) (void)native(frame);
      }
    }, value);
    auto* map = traits_.props_rw(frame_);
    const char* key = name.c_str();
    std::visit([&](const auto& values) {
      using V = typename std::decay_t<decltype(values)>::value_type;
      if constexpr (std::is_same_v<V, std::int64_t>) {
        check(traits_.set_ints(map, key, values.data(), static_cast<int>(values.size())));
      } else if constexpr (std::is_same_v<V, double>) {
        check(traits_.set_floats(map, key, values.data(), static_cast<int>(values.size())));
      } else {
        // Both hosts can retain typed empty numeric arrays. There is no common
        // API for empty data/frame arrays; reject rather than silently lose type.
        if (values.empty()) throw std::invalid_argument("DualSynth: empty data/frame arrays are not supported");
        for (std::size_t i = 0; i < values.size(); ++i) {
          const int append = i == 0 ? 0 : 1;
          if constexpr (std::is_same_v<V, PropertyData>) {
            const auto& v = values[i];
            check(traits_.set_data(map, key, v.bytes.data(), static_cast<int>(v.bytes.size()), static_cast<int>(v.hint), append));
          } else {
            check(traits_.set_frame(map, key, native(values[i]), append));
          }
        }
      }
    }, value);
  }
  bool erase(const std::string& name) override {
    validate_key(name);
    if (!writable_) throw std::logic_error("DualSynth: read-only frame properties");
    auto* map = traits_.props_rw(frame_);
    const bool existed = traits_.count(map, name.c_str()) >= 0;
    traits_.erase(map, name.c_str());
    return existed;
  }
  const Frame& native(const FrameRef& ref) const {
    const auto* storage = dynamic_cast<const NativeFrame<Traits>*>(&ref.storage());
    if (!storage || !traits_.same_host(storage->traits()))
      throw std::invalid_argument("DualSynth: foreign host frame reference");
    return storage->frame();
  }
private:
  static void validate_key(const std::string& key) {
    if (key.empty() || key.find('\0') != std::string::npos)
      throw std::invalid_argument("DualSynth: invalid property key");
  }
  static void check(int error) {
    if (error) throw std::runtime_error("DualSynth: host frame property operation failed (" + std::to_string(error) + ")");
  }
  Traits traits_;
  Frame& frame_;
  bool writable_;
};

template<class Traits>
class NativeFrame final : public FrameStorage {
public:
  using Frame = typename Traits::Frame;
  NativeFrame(Traits traits, Frame frame, VideoFormat format)
    : traits_(traits), frame_(std::move(frame)), format_(format), props_(traits_, frame_, true) {}
  NativeFrame(const NativeFrame&) = delete;
  NativeFrame& operator=(const NativeFrame&) = delete;
  ~NativeFrame() override { traits_.release(frame_); }
  static FrameRef adopt(Traits traits, Frame frame) {
    std::unique_ptr<NativeFrame> owned;
    try {
      auto format = traits.format(frame);
      owned = std::make_unique<NativeFrame>(traits, std::move(frame), format);
    } catch (...) { traits.release(frame); throw; }
    return FrameRef(std::shared_ptr<FrameStorage>(std::move(owned)));
  }
  VideoFrameView read() const override {
    auto view = traits_.read(frame_, format_);
    view.properties = &props_;
    return view;
  }
  MutableVideoFrameView write() override {
    auto view = traits_.write(frame_, format_);
    view.properties = &props_;
    return view;
  }
  const Frame& frame() const { return frame_; }
  const Traits& traits() const { return traits_; }
private:
  Traits traits_;
  Frame frame_;
  VideoFormat format_;
  NativeProperties<Traits> props_;
};

template<class Traits>
class NativeFrameFactory final : public FrameFactory {
public:
  explicit NativeFrameFactory(Traits traits) : traits_(traits) {}
  WritableFrame allocate(VideoFormat format, int width, int height, const FrameRef& source = {}) override {
    validate_frame_dimensions(format, width, height);
    typename Traits::Frame property_source{};
    if (source) {
      const auto* storage = dynamic_cast<const NativeFrame<Traits>*>(&source.storage());
      if (!storage || !traits_.same_host(storage->traits()))
        throw std::invalid_argument("DualSynth: foreign property source");
      property_source = storage->frame();
    }
    auto frame = traits_.allocate(format, width, height, property_source);
    if (!frame) throw std::runtime_error("DualSynth: auxiliary frame allocation failed");
    try { return WritableFrame(std::make_unique<NativeFrame<Traits>>(traits_, std::move(frame), format)); }
    catch (...) { traits_.release(frame); throw; }
  }
private:
  Traits traits_;
};

} // namespace ds::detail
