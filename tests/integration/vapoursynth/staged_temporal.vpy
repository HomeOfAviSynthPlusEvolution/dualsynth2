import gc
import vapoursynth as vs

core = vs.core
core.num_threads = 4
core.std.LoadPlugin(plugin)

def exercise():
    base = core.std.BlankClip(width=16, height=8, length=8, format=vs.GRAY8)
    source = core.dsref.StageProbe(clips=[base] * 4, mode=10)
    back = core.dsref.StageProbe(clips=[base] * 4, mode=11)
    forward = core.dsref.StageProbe(clips=[base] * 4, mode=12)
    bad = core.dsref.StageProbe(clips=[base] * 4, mode=8)
    outputs = []
    for delta in (-1, 0, 1):
        vectors = core.std.SetFrameProps(source, Delta=delta)
        outputs.append(core.dsref.TemporalStageProbe(clips=[source, vectors,
            back if delta < 0 else bad, forward if delta > 0 else bad]))
    def vectors_for_frame(n, f):
        result = f.copy()
        result.props['Delta'] = n % 3 - 1
        return result
    vectors = core.std.ModifyFrame(source, source, vectors_for_frame)
    outputs.append(core.dsref.TemporalStageProbe(clips=[source, vectors, back, forward]))
    requests = [(i, n) for n in (7, 0, 5, 2, 6, 1, 4, 3) for i in range(4)]
    futures = [outputs[i].get_frame_async(n) for i, n in requests]
    for (i, n), future in zip(requests, futures):
        delta = i - 1 if i < 3 else n % 3 - 1
        target = n + delta
        selected = 2 if delta < 0 else 3
        value = (80 if delta < 0 else 120) + target
        if delta == 0 or target < 0 or target >= 8:
            selected, target, value = 0, n, 40 + n
        with future.result() as frame:
            assert frame.props['Stamp'] == n
            assert frame.props['TemporalTarget'] == target
            assert frame.props['TemporalInput'] == selected
            assert bytes(frame[0]) == bytes([value]) * 128
    for n, delta in ((0, -1), (7, 1)):
        vectors = core.std.SetFrameProps(source, Delta=delta)
        output = core.dsref.TemporalStageProbe(clips=[source, vectors, bad, bad])
        with output.get_frame(n) as frame:
            assert bytes(frame[0]) == bytes([40 + n]) * 128
    return outputs[0]

exercise().set_output()
gc.collect()
