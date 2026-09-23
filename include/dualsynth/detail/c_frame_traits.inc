// Included by the host bridge after native format/view helpers.
struct FrameTraits {
  using Frame = AVS_VideoFrame*;
  using Map = AVS_Map;
  AVS_ScriptEnvironment* env;
  CApi* api;
  bool same_host(const FrameTraits& other) const { return env == other.env && api == other.api; }
  void release(Frame& f) const { if (f) api->release_video_frame(f); f = nullptr; }
  Frame clone(const Frame& f) const { return api->copy_video_frame(f); }
  VideoFormat format(const Frame& f) const {
    if (!f) throw std::runtime_error("DualSynth: null property frame");
    auto result = video_format_from_pixel_type(api->video_frame_get_pixel_type(f));
    if (!result.has_value()) throw std::invalid_argument(result.error().message);
    return result.value();
  }
  VideoFrameView read(const Frame& f, VideoFormat fmt) const { return make_video_frame_view(f, fmt); }
  MutableVideoFrameView write(const Frame& f, VideoFormat fmt) const { return make_mutable_video_frame_view(f, fmt); }
  const Map* props(const Frame& f) const { return api->get_frame_props_ro(env,f); }
  Map* props_rw(const Frame& f) const { return api->get_frame_props_rw(env,f); }
  char type(const Map* m, const char* k) const { return api->prop_get_type(env,m,k); }
  Frame get_frame(const Map* m, const char* k, int i, int* e) const { return const_cast<Frame>(api->prop_get_frame(env,m,k,i,e)); }
  Frame allocate(VideoFormat fmt, int w, int h, const Frame& props) const {
    auto vi = make_video_info(VideoOutputInfo{w,h,1,fmt,{1,1}});
    if (vi.pixel_type == AVS_CS_UNKNOWN)
      throw std::invalid_argument("DualSynth: AviSynth does not support the requested auxiliary format/depth");
    // Some C runtimes wrap a null prop_src as a non-null PVideoFrame pointer.
    // Use the property-free allocator when there is no source frame.
    return props ? api->new_video_frame_p(env,&vi,props) : api->new_video_frame_a(env,&vi,AVS_FRAME_ALIGN);
  }
  int num_keys(const Map* m) const { return api->prop_num_keys(env, m); }
  const char* key(const Map* m, int i) const { return api->prop_get_key(env, m, i); }
  int count(const Map* m, const char* k) const { return api->prop_num_elements(env, m, k); }
  bool has_int_array() const { return api->prop_get_int_array != nullptr; }
  bool has_float_array() const { return api->prop_get_float_array != nullptr; }
  const std::int64_t* get_int_array(const Map* m, const char* k, int* e) const { return api->prop_get_int_array(env, m, k, e); }
  const double* get_float_array(const Map* m, const char* k, int* e) const { return api->prop_get_float_array(env, m, k, e); }
  std::int64_t get_int(const Map* m, const char* k, int i, int* e) const { return api->prop_get_int(env, m, k, i, e); }
  double get_float(const Map* m, const char* k, int i, int* e) const { return api->prop_get_float(env, m, k, i, e); }
  const char* get_data(const Map* m, const char* k, int i, int* e) const { return api->prop_get_data(env, m, k, i, e); }
  int data_size(const Map* m, const char* k, int i, int* e) const { return api->prop_get_data_size(env, m, k, i, e); }
  int data_hint(const Map* m, const char* k, int i, int* e) const { return api->prop_get_data_type_hint(env, m, k, i, e); }
  int set_ints(Map* m, const char* k, const std::int64_t* p, int n) const { return api->prop_set_int_array(env, m, k, p, n); }
  int set_floats(Map* m, const char* k, const double* p, int n) const { return api->prop_set_float_array(env, m, k, p, n); }
  int set_data(Map* m, const char* k, const char* p, int n, int hint, int append) const { return api->prop_set_data_h(env, m, k, p, n, hint, append); }
  int set_frame(Map* m, const char* k, const Frame& f, int append) const { return api->prop_set_frame(env, m, k, f, append); }
  int erase(Map* m, const char* k) const { return api->prop_delete_key(env, m, k); }
};

template<class Filter>
void check_host_requirements(AVS_ScriptEnvironment* env) {
  constexpr auto req = FilterRequirements<Filter>::value;
  auto& api = CApi::instance();
  const int version = (std::max)(req.avisynth_interface, req.frame_services ? 11 : 0);
  if (version && (!api.check_version || api.check_version(env, version)))
    throw std::runtime_error("DualSynth: filter requires AviSynth interface " + std::to_string(version));
  if (req.avisynth_bugfix && (!api.get_env_property ||
      (api.get_env_property(env, AVS_AEP_INTERFACE_VERSION) == static_cast<std::size_t>(version) &&
       api.get_env_property(env, AVS_AEP_INTERFACE_BUGFIX) < static_cast<std::size_t>(req.avisynth_bugfix))))
    throw std::runtime_error("DualSynth: AviSynth interface bugfix level is too old");
  if constexpr (req.frame_services) {
    if (!api.clip_get_error || !api.prop_num_keys ||
        !api.prop_get_key ||
        !api.prop_num_elements ||
        !api.prop_get_int ||
        !api.prop_get_float ||
        !api.prop_get_data ||
        !api.prop_get_data_size ||
        !api.prop_get_data_type_hint ||
        !api.prop_set_int_array ||
        !api.prop_set_float_array ||
        !api.prop_set_data_h ||
        !api.prop_set_frame ||
        !api.prop_delete_key ||
        !api.prop_get_type ||
        !api.prop_get_frame ||
        !api.get_frame_props_ro ||
        !api.get_frame_props_rw ||
        !api.copy_video_frame ||
        !api.video_frame_get_pixel_type ||
        !api.check_version ||
        !api.get_env_property ||
        !api.save_string ||
        !api.new_video_frame_p || !api.new_video_frame_a || !api.release_video_frame || !api.get_frame)
      throw std::runtime_error("DualSynth: required AviSynth frame service entry point is missing");
  }
}

inline const char* save_error(AVS_ScriptEnvironment* env, const char* message) noexcept {
  auto& api = CApi::instance();
  if (api.save_string) {
    if (const char* saved = api.save_string(env, message, -1)) return saved;
  }
  return "DualSynth: host cannot preserve error text";
}
