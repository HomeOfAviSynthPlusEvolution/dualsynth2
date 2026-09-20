import vapoursynth as vs
core = vs.core
core.num_threads = 4
core.std.LoadPlugin(plugin)

def exercise():
    base = core.std.BlankClip(width=16, height=8, length=8, format=vs.GRAY8, color=[7])
    def stamp(n, f):
        out = f.copy()
        out.props['Stamp'] = n
        out.props['Select'] = n % 2
        return out
    source = core.std.ModifyFrame(base, base, stamp)
    seen = [[], []]
    def reference(which, fail=False):
        clip = core.std.BlankClip(base, color=[17 + which * 14])
        def select(n, f):
            seen[which].append(n)
            if n % 2 != which:
                raise vs.Error('excluded reference fetched')
            if fail and n == 2:
                raise vs.Error('upstream original detail')
            return f.copy()
        return core.std.ModifyFrame(clip, clip, select)
    a, b = reference(0), reference(1)
    outputs = [core.dsref.StageProbe(clips=[source, source, a, b]) for _ in range(2)]
    requests = [(i,n) for n in (7,0,5,2,6,1,4,3) for i in (0,1)]
    futures = [outputs[i].get_frame_async(n) for i,n in requests]
    for (i,n), future in zip(requests,futures):
        with future.result() as f:
            assert f.props['Stage'] == n and f.props['Stamp'] == n
            assert bytes(f[0]) == bytes([17+(n%2)*14])*128
    assert all(n % 2 == 0 for n in seen[0])
    assert all(n % 2 == 1 for n in seen[1])
    def forbidden(n, f):
        raise vs.Error('early-finish reference fetched')
    bad = core.std.ModifyFrame(base,base,forbidden)
    early = core.dsref.StageProbe(clips=[source,source,bad,bad],mode=1)
    with early.get_frame(3) as f:
        assert bytes(f[0]) == bytes([7])*128
    for mode, message in [(2,'stage exception: original detail'),(3,'stage result: original detail'),
                          (4,'process exception: original detail'),(5,'stage made no new frame requests')]:
        output = core.dsref.StageProbe(clips=[source,source,a,b],mode=mode)
        try:
            output.get_frame(0)
            raise AssertionError('expected stage error')
        except vs.Error as error:
            assert message in str(error), str(error)
    failing = core.dsref.StageProbe(clips=[source,source,reference(0,True),b])
    try:
        failing.get_frame(2)
        raise AssertionError('expected upstream error')
    except vs.Error as error:
        assert 'upstream original detail' in str(error), str(error)
    with failing.get_frame(3) as f:
        assert f.props['Stage'] == 3
    return outputs[0]

exercise().set_output()
import gc
gc.collect()
