"""Read-only comparison with the bundled manifest; no ROS, HTTP or test execution."""
import hashlib,json,sys
from pathlib import Path

def verify(root):
    root=root.resolve();manifest=json.loads((root/'source-manifest.json').read_text(encoding='utf8'))
    bad=[]
    for name,digest in manifest['files'].items():
        path=(root/name).resolve()
        if not path.is_relative_to(root) or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            bad.append(name)
    return {'version':manifest['version'],'checked_files':len(manifest['files']),'all_match':not bad,
        'mismatches':bad,'operation':'read-only integrity comparison; not a signature or formal acceptance'}

if __name__=='__main__':
    try:
        value=verify(Path(__file__).resolve().parent.parent)
        print(json.dumps(value,ensure_ascii=False));raise SystemExit(0 if value['all_match'] else 1)
    except (OSError,ValueError,KeyError,TypeError):raise SystemExit('Delivery manifest could not be checked')
