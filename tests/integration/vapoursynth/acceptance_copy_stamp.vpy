import vapoursynth as vs

core = vs.core
core.std.LoadPlugin(plugin)

src = core.dsref.TestPattern(width=4, height=4)
clip = core.dsref.AcceptanceCopyStamp(clip=src)
actual = core.std.Trim(clip, first=1, last=1)
stats = core.std.PlaneStats(actual)

expected_average = 318 / (255 * 16)

def check_frame(n, f):
    if abs(f.props["PlaneStatsAverage"] - expected_average) > 1e-9:
        raise vs.Error("AcceptanceCopyStamp did not preserve copied source pixels")
    return actual.get_frame(n)

core.std.ModifyFrame(actual, stats, check_frame).set_output()
