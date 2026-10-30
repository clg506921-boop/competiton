"""Stdin-only credential launcher for the three-game comparison batch."""
from pathlib import Path
import json,subprocess,sys
from verification_budget import code_iteration
root=Path(__file__).resolve().parent.parent
data=json.load(sys.stdin)
if not isinstance(data.get('key'),str) or not data['key']:raise SystemExit(2)
path=root/'experiments/iterations'/code_iteration(root)/'size24-real-worker.log'
path.parent.mkdir(parents=True,exist_ok=True)
with path.open('xb') as output:
    proc=subprocess.Popen(['python3','-B',str(root/'tools/run_competitive_batch.py')],stdin=subprocess.PIPE,
        stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
    proc.stdin.write(json.dumps(data).encode());proc.stdin.close()
data.clear()
print(json.dumps({'started':True,'pid':proc.pid,'maximum_games':3,'mode':'real, four vehicles, 24x24'}))
