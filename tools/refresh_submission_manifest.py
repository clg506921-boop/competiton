"""Refresh draft source manifest; never claims a changed build has been validated."""
from pathlib import Path
import sys,json,hashlib,argparse
sys.dont_write_bytecode=True
from verification_budget import code_iteration
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--version');args=p.parse_args()
old=json.loads((root/'source-manifest.json').read_text(encoding='utf-8-sig')) if (root/'source-manifest.json').exists() else {}
files={}
for path in sorted(root.rglob('*')):
 if not path.is_file():continue
 rel=path.relative_to(root);name=rel.as_posix()
 if name=='source-manifest.json' or any(x in rel.parts for x in ['.git','__pycache__']):continue
 if name.startswith(('contestant_ws/build/','contestant_ws/install/','contestant_ws/log/')):continue
 if path.suffix=='.pyc':continue
 if path.name.startswith('.env') or path.name.endswith(('.private.json','.key','.pem')):raise SystemExit('Private file must be moved outside repository: '+name)
 files[name]=hashlib.sha256(path.read_bytes()).hexdigest()
old.update(version=args.version or old.get('version','v0.25.1-submission-draft'),iteration_id=code_iteration(root),files=files,validation='Submission draft only; final seal and runtime verification pending',submission_final=False)
(root/'source-manifest.json').write_text(json.dumps(old,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print(json.dumps({'cpp_fingerprint':old['iteration_id'],'files':len(files),'submission_final':False}))
