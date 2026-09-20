import argparse
import pathlib
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--runner', required=True)
p.add_argument('--plugin', required=True)
p.add_argument('--backend', required=True)
p.add_argument('--work', required=True)
a = p.parse_args()
base = pathlib.Path(__file__).with_name('staged_video.avs.in').read_text().replace('@PLUGIN@', a.plugin.replace('\\','/'))
work = pathlib.Path(a.work)
work.mkdir(parents=True, exist_ok=True)
cases = [(1, None), (2, 'stage exception: original detail'), (3, 'stage result: original detail'),
         (4, 'process exception: original detail'), (5, 'stage made no new frame requests'),
         (0, 'upstream selected reference detail')]
for mode, expected in cases:
    script = base.replace('DSStageProbe([src,vectors,a,b])', f'DSStageProbe([src,vectors,a,b], mode={mode})')
    if mode == 1:
        script = script.replace('mode=6', 'mode=8').replace('mode=7', 'mode=8')
    if mode == 0:
        script = script.replace('mode=7', 'mode=8')
    path = work / f'fault-{mode}.avs'
    path.write_text(script)
    cmd = [a.runner, '--video', str(path), '--backend', a.backend, '--frame', '7']
    if mode == 1: cmd += ['--expect-y8-sum', '896']
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    message = result.stdout + result.stderr
    if expected:
        assert result.returncode != 0 and expected in message, f'mode={mode}: {message}'
    else:
        assert result.returncode == 0, message
print('stage faults and early completion passed')
