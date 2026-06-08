import vapoursynth as vs

core = vs.core
core.std.LoadPlugin(plugin)

clip = core.dsref.NeoGradientMask(width=8, height=4, color=0x99CCFF, depth=16)
clip.set_output()
