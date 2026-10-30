"""Inspect a fresh local install without starting ROS/DDS or an HTTP client."""
import hashlib,json,os,platform,subprocess,sys
from pathlib import Path

root=Path(__file__).resolve().parent.parent
target=Path(sys.argv[1]).resolve()
if not target.is_relative_to(root) or target==root:raise SystemExit('Invalid isolated build directory')
binary=target/'install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
def capture(args):
    r=subprocess.run(args,capture_output=True,text=True,timeout=20)
    if r.returncode:raise RuntimeError('Build inspection command failed')
    return r.stdout.strip()
prefix=capture(['ros2','pkg','prefix','rmtc_bot'])
ldd=capture(['ldd',str(binary)])
if Path(prefix).resolve()!=(target/'install/rmtc_bot').resolve() or 'not found' in ldd:
    raise SystemExit('Isolated install or shared-library verification failed')
env={k:v for k,v in os.environ.items() if not k.startswith('RMTC_')}
env['RMTC_STAGE']='1'
check=subprocess.run([str(binary),'--check-config'],env=env,capture_output=True,text=True,timeout=10)
value=json.loads(check.stdout)
if check.returncode or value.get('ros_initialized') is not False or value.get('network_requests')!=0:
    raise SystemExit('Configuration-only executable check failed')
source=root/'contestant_ws/src/rmtc_bot'
report={'passed':True,'kind':'fresh build/install directories on existing Ubuntu; not a fresh OS or image',
 'ros_distro':os.environ.get('ROS_DISTRO'),'architecture':platform.machine(),'package_prefix':prefix,
 'compiler':capture(['c++','--version']).splitlines()[0],'cmake':capture(['cmake','--version']).splitlines()[0],
 'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'file':capture(['file',str(binary)]),
 'ldd':ldd.splitlines(),'configuration_check':value,'tests_executed':0,'real_model_calls':0,
 'source_hashes':{p.relative_to(source).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
     for p in sorted(source.rglob('*')) if p.is_file()}}
(target/'inspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
print(json.dumps({key:report[key] for key in ('passed','kind','architecture','package_prefix','binary_sha256','tests_executed','real_model_calls')}))
