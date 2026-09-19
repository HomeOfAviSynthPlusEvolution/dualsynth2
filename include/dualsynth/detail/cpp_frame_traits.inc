// Included by the host bridge after native format/view helpers.
struct FrameTraits {
  using Frame = PVideoFrame;
  using Map = AVSMap;
  IScriptEnvironment* env;
  bool same_host(const FrameTraits& other) const { return env == other.env; }
  void release(Frame& f) const { f = PVideoFrame{}; }
  Frame clone(const Frame& f) const { return f; }
  VideoFormat format(const Frame& f) const {
    if (!f) throw std::runtime_error("DualSynth: null property frame");
    auto result = video_format_from_pixel_type(f->GetPixelType());
    if (!result.has_value()) throw std::invalid_argument(result.error().message);
    return result.value();
  }
  VideoFrameView read(const Frame& f, VideoFormat fmt) const { return make_video_frame_view(f, fmt); }
  MutableVideoFrameView write(const Frame& f, VideoFormat fmt) const { return make_mutable_video_frame_view(f, fmt); }
  const Map* props(const Frame& f) const { return env->getFramePropsRO(f); }
  Map* props_rw(Frame& f) const { return env->getFramePropsRW(f); }
  char type(const Map* m, const char* k) const { return env->propGetType(m,k); }
  Frame get_frame(const Map* m, const char* k, int i, int* e) const { return env->propGetFrame(m,k,i,e); }
  Frame allocate(VideoFormat fmt, int w, int h, const Frame& props) const {
    auto vi = make_video_info(VideoOutputInfo{w,h,1,fmt,{1,1}});
    if (vi.pixel_type == VideoInfo::CS_UNKNOWN)
      throw std::invalid_argument("DualSynth: AviSynth does not support the requested auxiliary format/depth");
    return env->NewVideoFrameP(vi, props ? &props : nullptr);
  }
  int num_keys(const Map* m) const { return env->propNumKeys(m); }
  const char* key(const Map* m, int i) const { return env->propGetKey(m, i); }
  int count(const Map* m, const char* k) const { return env->propNumElements(m, k); }
  std::int64_t get_int(const Map* m, const char* k, int i, int* e) const { return env->propGetInt(m, k, i, e); }
  double get_float(const Map* m, const char* k, int i, int* e) const { return env->propGetFloat(m, k, i, e); }
  const char* get_data(const Map* m, const char* k, int i, int* e) const { return env->propGetData(m, k, i, e); }
  int data_size(const Map* m, const char* k, int i, int* e) const { return env->propGetDataSize(m, k, i, e); }
  int data_hint(const Map* m, const char* k, int i, int* e) const { return env->propGetDataTypeHint(m, k, i, e); }
  int set_ints(Map* m, const char* k, const std::int64_t* p, int n) const { return env->propSetIntArray(m, k, p, n); }
  int set_floats(Map* m, const char* k, const double* p, int n) const { return env->propSetFloatArray(m, k, p, n); }
  int set_data(Map* m, const char* k, const char* p, int n, int hint, int append) const { return env->propSetDataH(m, k, p, n, hint, append); }
  int set_frame(Map* m, const char* k, const Frame& f, int append) const { return env->propSetFrame(m, k, f, append); }
  int erase(Map* m, const char* k) const { return env->propDeleteKey(m, k); }
};

template<class Filter>
void check_host_requirements(IScriptEnvironment* env) {
  constexpr auto req = FilterRequirements<Filter>::value;
  const int version = (std::max)(req.avisynth_interface, req.frame_services ? 11 : 0);
  if (version) env->CheckVersion(version);
  if (req.avisynth_bugfix && env->GetEnvProperty(AEP_INTERFACE_VERSION) == static_cast<std::size_t>(version) &&
      env->GetEnvProperty(AEP_INTERFACE_BUGFIX) < static_cast<std::size_t>(req.avisynth_bugfix))
    throw std::runtime_error("DualSynth: AviSynth interface bugfix level is too old");
}
