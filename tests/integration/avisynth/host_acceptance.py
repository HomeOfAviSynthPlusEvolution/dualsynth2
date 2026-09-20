import argparse
import pathlib
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--runner', required=True)
p.add_argument('--plugin', required=True)
p.add_argument('--backend', required=True)
p.add_argument('--work', required=True)
a = p.parse_args()
work = pathlib.Path(a.work)
work.mkdir(parents=True, exist_ok=True)
base = pathlib.Path(__file__).with_name('staged_video.avs.in').read_text()
base = base.replace('@PLUGIN@', a.plugin.replace('\\', '/'))
base = base[:base.index('return DSStageProbe')]
# Force the plugin entry point as well as the runner ABI for C coverage.
if a.backend == 'c':
    base = base.replace('LoadPlugin(', 'LoadCPlugin(')

def run(name, body, mode='--video', extra=(), error=None):
    path = work / (name + '.avs')
    path.write_text(base + body)
    result = subprocess.run([a.runner, mode, str(path), '--backend', a.backend, *extra],
                            capture_output=True, text=True, timeout=30)
    text = result.stdout + result.stderr
    if error:
        assert result.returncode != 0 and error in text, (name, text)
    else:
        assert result.returncode == 0, (name, text)
    return result.stdout

for index in (0, 1):
    run(f'failed-member-{index}', f'return DSStagePair([src,vectors,a,b],fail={index})',
        error=f'member[{index}]: stage init: original detail')

# Cropping only the second member makes order observable in the pixel checksum.
run('interleaved-bundles', '''
p = DSStagePair([src,vectors,a,b])
q = DSStagePair([src,vectors,a,b])
return StackHorizontal(p[0],q[1].Crop(0,0,8,8),q[0],p[1].Crop(0,0,8,8)).Prefetch(4)
''', extra=('--frame', '7', '--expect-y8-sum', '8832'))

audio = '''
src = AudioDub(src,Tone(length=1,frequency=100,samplerate=48000,channels=2)).AssumeTFF()
vectors = AudioDub(vectors,Tone(length=1,frequency=300,samplerate=44100,channels=1)).AssumeBFF()
output = DSStageForward([src,vectors,a,b])
Assert(GetParity(src,7) != GetParity(vectors,7), "parity fixture must differ")
Assert(GetParity(output,7) == GetParity(vectors,7), "wrong parity source")
Assert(AudioRate(output) == AudioRate(src), "wrong audio rate")
Assert(AudioChannels(output) == AudioChannels(src), "wrong audio channel count")
Assert(AudioLength(output) == AudioLength(src), "wrong audio length")
'''
original = run('original-audio', audio + 'return src', mode='--audio')
forwarded = run('forwarded-audio', audio + 'return output.Prefetch(4)', mode='--audio')
other = run('other-audio', audio + 'return vectors', mode='--audio')
assert original == forwarded and original != other, (original, forwarded, other)
run('forwarded-video', audio + 'return output.Prefetch(4)',
    extra=('--frame', '7', '--expect-y8-sum', '3968'))
print('bundle rollback/order, interleaving, audio and parity passed')
