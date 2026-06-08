import vapoursynth as vs

core = vs.core
core.std.LoadPlugin(plugin)

a = core.dsref.TestPattern(width=16, height=16)
b = core.dsref.TestPattern(width=16, height=16)
c = core.dsref.TestPattern(width=16, height=16)

clip = core.dsref.AcceptanceTemporalAverage3(a=a, b=b, c=c)
actual = core.std.Trim(clip, first=1, last=1)
expected = core.std.Trim(b, first=1, last=1)
diff = core.std.Expr([actual, expected], "x y - abs")
stats = core.std.PlaneStats(diff)

def check_frame(n, f):
    if f.props["PlaneStatsMax"] != 0:
        raise vs.Error("AcceptanceTemporalAverage3 changed the expected average pixels")
    return actual.get_frame(n)

core.std.ModifyFrame(actual, stats, check_frame).set_output()
