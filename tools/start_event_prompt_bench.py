"""Short-lived stdin launcher; never logs the upstream credential."""
from pathlib import Path
import json,subprocess,sys
from verification_budget import code_iteration
root=Path(__file__).resolve().parent.parent
incoming=json.load(sys.stdin)
if not isinstance(incoming.get('key'),str):raise SystemExit(2)
output=root/'experiments/event-prompts'/('worker-'+code_iteration(root)+'.log')
output.parent.mkdir(parents=True,exist_ok=True)
if output.exists():raise SystemExit('Event benchmark already launched; inspect it before any retry')
with output.open('xb') as log:
    child=subprocess.Popen(['python3','-B',str(root/'tools/run_event_prompt_bench.py')],stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    child.stdin.write(json.dumps(incoming).encode());child.stdin.close()
incoming.clear()
print(json.dumps({'started':True,'pid':child.pid,'maximum_real_requests':48,'game_clock_resumed':False}))
