// Included by the host bridge after native format/view helpers.
struct FrameTraits {
  using Frame = const VSFrame*;
  using Map = VSMap;
  const VSAPI* api;
  VSCore* core;
  bool same_host(const FrameTraits& other) const { return api == other.api && core == other.core; }
  void release(Frame& f) const { if (f) api->freeFrame(f); f = nullptr; }
  Frame clone(const Frame& f) const { return api->addFrameRef(f); }
  VideoFormat format(const Frame& f) const {
    if (!f) throw std::runtime_error("DualSynth: null property frame");
    auto result = make_video_format(*api->getVideoFrameFormat(f));
    if (!result.has_value()) throw std::invalid_argument(result.error().message);
    return result.value();
  }
  VideoFrameView read(const Frame& f, VideoFormat fmt) const { return make_video_frame_view(f, fmt, api); }
  MutableVideoFrameView write(const Frame& f, VideoFormat fmt) const { return make_mutable_video_frame_view(const_cast<VSFrame*>(f), fmt, api); }
  const Map* props(const Frame& f) const { return api->getFramePropertiesRO(f); }
  Map* props_rw(const Frame& f) const { return api->getFramePropertiesRW(const_cast<VSFrame*>(f)); }
  char type(const Map* m, const char* k) const {
    switch (api->mapGetType(m,k)) {
      case ptInt: return 'i'; case ptFloat: return 'f'; case ptData: return 's';
      case ptVideoFrame: return 'v'; case ptAudioFrame: return 'a';
      case ptVideoNode: return 'c'; case ptAudioNode: return 'n';
      case ptFunction: return 'm'; default: return '?';
    }
  }
  Frame get_frame(const Map* m, const char* k, int i, int* e) const { return api->mapGetFrame(m,k,i,e); }
  Frame allocate(VideoFormat fmt, int w, int h, const Frame& props) const {
    VSVideoFormat native{};
    if (!query_video_format(fmt, native, core, api)) throw std::invalid_argument("DualSynth: unsupported auxiliary format");
    return api->newVideoFrame(&native,w,h,props,core);
  }
  int num_keys(const Map* m) const { return api->mapNumKeys(m); }
  const char* key(const Map* m, int i) const { return api->mapGetKey(m, i); }
  int count(const Map* m, const char* k) const { return api->mapNumElements(m, k); }
  std::int64_t get_int(const Map* m, const char* k, int i, int* e) const { return api->mapGetInt(m, k, i, e); }
  double get_float(const Map* m, const char* k, int i, int* e) const { return api->mapGetFloat(m, k, i, e); }
  const char* get_data(const Map* m, const char* k, int i, int* e) const { return api->mapGetData(m, k, i, e); }
  int data_size(const Map* m, const char* k, int i, int* e) const { return api->mapGetDataSize(m, k, i, e); }
  int data_hint(const Map* m, const char* k, int i, int* e) const { return api->mapGetDataTypeHint(m, k, i, e); }
  int set_ints(Map* m, const char* k, const std::int64_t* p, int n) const { return api->mapSetIntArray(m, k, p, n); }
  int set_floats(Map* m, const char* k, const double* p, int n) const { return api->mapSetFloatArray(m, k, p, n); }
  int set_data(Map* m, const char* k, const char* p, int n, int hint, int append) const { return api->mapSetData(m, k, p, n, hint, append); }
  int set_frame(Map* m, const char* k, const Frame& f, int append) const { return api->mapSetFrame(m, k, f, append); }
  int erase(Map* m, const char* k) const { return api->mapDeleteKey(m, k); }
};

template<class Filter>
void check_host_requirements(const VSAPI* api) {
  if constexpr (FilterRequirements<Filter>::value.frame_services) {
    if (!api->getFramePropertiesRO || !api->getFramePropertiesRW || !api->getVideoFrameFormat ||
        !api->mapGetFrame || !api->mapSetFrame || !api->mapGetInt || !api->mapSetIntArray ||
        !api->mapGetFloat || !api->mapSetFloatArray || !api->mapGetData || !api->mapSetData ||
        !api->mapGetDataSize || !api->mapGetDataTypeHint || !api->mapGetType ||
        !api->mapNumKeys || !api->mapGetKey || !api->mapNumElements || !api->mapDeleteKey ||
        !api->getFrame || !api->addFrameRef || !api->freeFrame || !api->newVideoFrame)
      throw std::runtime_error("DualSynth: filter requires complete VapourSynth 4 frame services");
  }
}
