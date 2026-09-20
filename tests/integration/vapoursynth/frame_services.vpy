import struct
import vapoursynth as vs

core = vs.core
core.num_threads = 4
core.std.LoadPlugin(plugin)

def stamp(n, f):
    out = f.copy()
    out.props["Origin"] = n
    return out

outputs = []
formats = [vs.GRAY8, vs.GRAY16, vs.GRAYS, vs.YUV420P10, vs.RGBS]
formats += [core.query_video_format(vs.GRAY, vs.INTEGER, bits, 0, 0).id for bits in (9, 11, 13, 15)]
for fmt in formats:
    src = core.std.BlankClip(width=16, height=8, length=4, format=fmt)
    src = core.std.ModifyFrame(src, src, stamp)
    for failing, index in ((core.dsref.FrameServicesFailedFirst, 0), (core.dsref.FrameServicesFailedPair, 1)):
        try:
            failing(clips=[src])
            raise AssertionError("failed bundle returned a partial result")
        except vs.Error as error:
            assert f"bundle member[{index}]: FrameServices requires a nonempty clip array" in str(error), str(error)
    del failing
    pair = core.dsref.FrameServicesPair(clips=[src, src], extra=src)
    assert len(pair) == 2
    out = core.dsref.FrameServices(clips=[pair[0]], verify=True)
    assert out.num_frames == 8 and out.fps == src.fps * 2
    # Fetch out of order and retain auxiliary frames beyond their parent lifetime.
    for n in (7, 0, 5, 1, 6, 2, 4, 3):
        with out.get_frame(n) as f, src.get_frame(n // 2) as ref, pair[1].get_frame(n) as sibling:
            assert f.props["Origin"] == n // 2
            assert list(f.props["DS_Ints"]) == [-(1 << 63), (1 << 63) - 1, (1 << 53) + 1]
            assert struct.pack("3d", *f.props["DS_Floats"]) == struct.pack("3d", 0.0, -0.0, 0.125)
            assert list(f.props["DS_Data"]) == [b"a\0b", "text"]
            assert list(f.props["DS_Empty"]) == []
            assert "DS_Deleted" not in f.props
            for p in range(f.format.num_planes):
                assert bytes(f[p]) == bytes(ref[p]) == bytes(sibling[p])
            aux = f.props["DS_Frames"]
        assert len(aux) == 2
        for frame in aux:
            assert frame.width == 7 and frame.height == 3 and frame.format.id == vs.GRAY16
            assert frame.props["DS_Tag"] == 42
            assert list(struct.unpack("21H", bytes(frame[0]))) == list(range(1000, 1021))
            frame.close()
    outputs.append(out)

# Also exercise asynchronous scheduling with several outstanding requests.
for f in outputs[0].frames(prefetch=4):
    assert "DS_Frames" in f.props
    f.close()
outputs[0].set_output()
# Release test-owned nodes before the script environment tears down the core.
del src, pair, out, outputs, f, ref, sibling, frame, aux
import gc
gc.collect()
