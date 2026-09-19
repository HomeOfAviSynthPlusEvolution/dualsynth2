"""Compare DS2 public frame views across hosts; no downstream plugin dependency."""
import argparse
import pathlib
import subprocess
import tempfile


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode or "Core freed but" in result.stderr:
        raise RuntimeError(f"{command!r}\n{result.stdout}\n{result.stderr}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", required=True)
    parser.add_argument("--vspipe", required=True)
    parser.add_argument("--plugin", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--cpp", action="store_true")
    args = parser.parse_args()
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix="run-", dir=work))
    print(f"Snapshot artifacts: {root}", flush=True)
    plugin = pathlib.Path(args.plugin).as_posix()
    order = (7, 0, 5, 1, 6, 2, 4, 3, 7, 0)
    formats = (("Y8", "GRAY8"), ("Y10", "GRAY10"), ("Y12", "GRAY12"),
               ("Y14", "GRAY14"), ("Y16", "GRAY16"), ("Y32", "GRAYS"),
               ("YUV420P10", "YUV420P10"), ("YUV422P12", "YUV422P12"),
               ("YUV444P16", "YUV444P16"), ("RGBP16", "RGB48"), ("RGBPS", "RGBS"))
    comparisons = 0
    for avs_format, vs_format in formats:
        directories = {}
        for backend in (["vs", "c", "cpp"] if args.cpp else ["vs", "c"]):
            directory = root / f"{avs_format}-{backend}"
            directory.mkdir()
            directories[backend] = directory
            if backend == "vs":
                script = root / f"{avs_format}.vpy"
                script.write_text(
                    "import vapoursynth as vs\n"
                    "core = vs.core\ncore.num_threads = 4\n"
                    f"core.std.LoadPlugin({plugin!r})\n"
                    f"src = core.std.BlankClip(width=16, height=8, length=4, fpsnum=24, fpsden=1, format=vs.{vs_format})\n"
                    "produced = core.dsref.FrameServices(clips=[src,src], extra=src, pattern=True)\n"
                    f"out = core.dsref.FrameServices(clips=[produced], verify=True, snapshot={directory.as_posix()!r})\n"
                    f"pending = [out.get_frame_async(n) for n in {order!r}]\n"
                    "for future in pending:\n    frame = future.result()\n    frame.close()\n"
                    "out.set_output()\n", encoding="utf-8")
                run([args.vspipe, str(script), "--"])
            else:
                script = root / f"{avs_format}-{backend}.avs"
                script.write_text(
                    f'LoadPlugin("{plugin}")\n'
                    f'src = BlankClip(width=16, height=8, length=4, fps=24, pixel_type="{avs_format}")\n'
                    'produced = DSFrameServices([src,src], extra=src, pattern=true)\n'
                    f'return DSFrameServices([produced], verify=true, snapshot="{directory.as_posix()}").Prefetch(4)\n',
                    encoding="utf-8")
                for n in order:
                    run([args.runner, "--video", str(script), "--backend", backend, "--frame", str(n)])
        expected_names = {f"{n}.bin" for n in range(8)}
        for backend, directory in directories.items():
            names = {p.name for p in directory.iterdir()}
            if names != expected_names:
                raise AssertionError(f"{directory}: missing or unexpected snapshots: {names ^ expected_names}")
            if backend == "vs":
                continue
            for name in sorted(expected_names):
                baseline = (directories["vs"] / name).read_bytes()
                actual = (directory / name).read_bytes()
                if not baseline or baseline != actual:
                    offset = next((i for i, (a, b) in enumerate(zip(baseline, actual)) if a != b),
                                  min(len(baseline), len(actual)))
                    raise AssertionError(f"{avs_format} {backend} {name}: first difference at byte {offset}; "
                                         f"sizes VS={len(baseline)}, AVS={len(actual)}")
                comparisons += 1
    print(f"{comparisons} complete frame snapshots match byte-for-byte", flush=True)


if __name__ == "__main__":
    main()
