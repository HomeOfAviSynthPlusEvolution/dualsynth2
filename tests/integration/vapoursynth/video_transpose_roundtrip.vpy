import vapoursynth as vs

core = vs.core
core.std.LoadPlugin(plugin)

src = core.dsref.TestPattern(width=13, height=7)
transposed = core.dsref.VideoTranspose(clip=src)
assert transposed.width == 7
assert transposed.height == 13

roundtrip = core.dsref.VideoTranspose(clip=transposed)
assert roundtrip.width == src.width
assert roundtrip.height == src.height

diff = core.std.Expr([src, roundtrip], "x y - abs")
stats = core.std.PlaneStats(diff)

def check_frame(n, f):
    if f.props["PlaneStatsMax"] != 0:
        raise vs.Error("DualSynth transpose roundtrip changed pixel data")
    return roundtrip.get_frame(n)

core.std.ModifyFrame(roundtrip, stats, check_frame).set_output()
