import vapoursynth as vs

core = vs.core
core.std.LoadPlugin(plugin)
clip = core.dsref.TestPattern(width=64, height=32)
clip.set_output()
