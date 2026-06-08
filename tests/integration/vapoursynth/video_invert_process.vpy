import vapoursynth as vs

core = vs.core
core.std.LoadPlugin(plugin)
clip = core.dsref.TestPattern(width=16, height=16)
clip = core.dsref.VideoInvert(clip=clip)
clip.set_output()
