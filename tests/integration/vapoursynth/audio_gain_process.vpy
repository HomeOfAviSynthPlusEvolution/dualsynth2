import vapoursynth as vs

core = vs.core
core.std.LoadPlugin(plugin)
clip = core.dsref.AudioTestTone(samples=4096)
clip = core.dsref.AudioGain(clip=clip, gain=0.5)
clip.set_output()
