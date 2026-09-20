import argparse
import pathlib
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--runner', required=True)
p.add_argument('--plugin', required=True)
p.add_argument('--backend', required=True, choices=['c', 'cpp'])
p.add_argument('--work', required=True)
a = p.parse_args()
work = pathlib.Path(a.work)
work.mkdir(parents=True, exist_ok=True)
loader = 'LoadCPlugin' if a.backend == 'c' else 'LoadPlugin'
base = f'''{loader}("{a.plugin.replace(chr(92), '/')}")
base = BlankClip(width=16,height=8,length=8,pixel_type="Y8")
src = DSStageProbe([base,base,base,base],mode=10)
back = DSStageProbe([base,base,base,base],mode=11)
forward = DSStageProbe([base,base,base,base],mode=12)
bad = DSStageProbe([base,base,base,base],mode=8)
'''
order = [7, 0, 5, 2, 6, 1, 4, 3]

def expected(n, delta):
    target = n + delta
    if delta == 0 or target < 0 or target >= 8:
        return 40 + n
    return (80 if delta < 0 else 120) + target

def run(name, expression, backward, forward, frames, deltas):
    script = base + f'''
vectors = src.ScriptClip("""propSet(last,"Delta",{expression})""")
output = DSTemporalStageProbe([src,vectors,{backward},{forward}])
'''
    # Every requested output is consumed; Prefetch cannot hide untested frames.
    clips = [f'output.Trim({n},-1)' for n in frames]
    combined = clips[0] if len(clips) == 1 else 'StackHorizontal(' + ','.join(clips) + ')'
    script += f'return {combined}.Prefetch(4)\n'
    path = work / (name + '.avs')
    path.write_text(script)
    checksum = 128 * sum(expected(n, d) for n, d in zip(frames, deltas))
    result = subprocess.run([a.runner, '--video', str(path), '--backend', a.backend,
                             '--frame', '0', '--expect-y8-sum', str(checksum)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (name, result.stdout, result.stderr)

for delta in (-1, 0, 1):
    run(f'delta-{delta}', str(delta), 'back' if delta < 0 else 'bad',
        'forward' if delta > 0 else 'bad', order, [delta] * len(order))
run('first-exclusion', '-1', 'bad', 'bad', [0], [-1])
run('last-exclusion', '1', 'bad', 'bad', [7], [1])
run('varying-vectors', 'current_frame % 3 - 1', 'back', 'forward', order,
    [n % 3 - 1 for n in order])
print('temporal stages: neighbor frames, boundary exclusion and zero-delta reuse passed')
