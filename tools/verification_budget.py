"""Merge immutable shipped counts with runtime counts; restoring a ZIP cannot reset them."""
import hashlib,json
from pathlib import Path

def checked_counts(value):
    if not isinstance(value,dict):raise ValueError('Invalid verification ledger')
    if any(not isinstance(k,str) or not k.startswith(('own-stage1-','own-stage2-')) or type(v) is not int or v<0 for k,v in value.items()):
        raise ValueError('Invalid verification counts')
    return value

def merge_counts(shipped,runtime):
    a=checked_counts(shipped);b=checked_counts(runtime)
    return {key:max(a.get(key,0),b.get(key,0)) for key in a.keys()|b.keys()}

def load_match_counts(root):
    root=Path(root)
    baseline=json.loads((root/'verification-budget-baseline.json').read_text(encoding='utf8'))
    if baseline.get('schema_version') not in (1,2):raise ValueError('Invalid verification baseline')
    if baseline['schema_version']==2:
        iteration=code_iteration(root)
        shipped=baseline.get('iterations',{}).get(iteration,{})
        path=match_ledger_path(root)
        runtime=json.loads(path.read_text(encoding='utf8')) if path.exists() else {}
        return merge_counts(shipped,runtime)
    path=root/'official-match-execution-ledger.json'
    runtime=json.loads(path.read_text(encoding='utf8')) if path.exists() else {}
    return merge_counts(baseline['official_match_executions'],runtime)

def code_iteration(root):
    """Production sources and build definitions, independent of labels, docs and install paths."""
    root=Path(root);source=root/'contestant_ws/src/rmtc_bot'
    files=sorted(list((source/'src').glob('*.cpp'))+list((source/'include').rglob('*.hpp'))+
        [source/'CMakeLists.txt',source/'core/CMakeLists.txt'])
    if not files or any(not p.is_file() for p in files):raise ValueError('Missing iteration sources')
    digest=hashlib.sha256()
    for path in files:
        digest.update(path.relative_to(source).as_posix().encode());digest.update(b'\0');digest.update(path.read_bytes())
    return digest.hexdigest()[:20]

def match_ledger_path(root):
    root=Path(root);baseline=json.loads((root/'verification-budget-baseline.json').read_text(encoding='utf8'))
    if baseline.get('schema_version')==1:return root/'official-match-execution-ledger.json'
    if baseline.get('schema_version')!=2:raise ValueError('Invalid verification baseline')
    return root/'experiments/iterations'/code_iteration(root)/'match-ledger.json'
