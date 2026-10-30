"""Start one bounded production trial; credential arrives only on stdin."""
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
payload = sys.stdin.buffer.read(4096)
value = json.loads(payload)
if not isinstance(value, dict) or not isinstance(value.get('key'), str) or not value['key'].startswith('sk-'):
    raise SystemExit('Missing stdin credential')
ledger = root / 'official-match-execution-ledger.json'
counts = json.loads(ledger.read_text()) if ledger.exists() else {}
if sum(v for k, v in counts.items() if k.startswith('own-stage2-')) >= 3:
    raise SystemExit('Three stage2 executions reached; stop')
if counts.get('own-stage2-real-solo', 0):
    raise SystemExit('Inspect existing real trial before another attempt')
with (root / 'real-production-worker.log').open('xb') as log:
    worker = subprocess.Popen(
        [sys.executable, '-B', str(root / 'tools/run_official_trial.py'),
         '--stage', '2', '--ai-mode', 'real', '--solo'],
        cwd=root, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT,
        start_new_session=True)
    worker.stdin.write(payload)
    worker.stdin.close()
payload = value = None
print(json.dumps({'started': True, 'pid': worker.pid, 'quota': 3,
                  'companions': [], 'credential_saved': False}))
