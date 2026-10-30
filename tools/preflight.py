"""Read-only local readiness checks. Never starts ROS, a game, or an HTTP request."""
import argparse,json,os,platform,subprocess,sys
from pathlib import Path
sys.dont_write_bytecode=True
from launch_bot import arguments,loopback_profile

def run(root,seat,team,referee_ip,network):
    checks=[]
    def check(name,ok):checks.append({'check':name,'passed':bool(ok)})
    check('linux',sys.platform=='linux')
    try:arguments(seat,team,'collect','greedy','immediate');metadata=True
    except (ValueError,UnicodeError):metadata=False
    check('seat_and_team',metadata)
    check('referee_address_supplied',bool(referee_ip) and not any(c.isspace() for c in referee_ip))
    domain=os.environ.get('ROS_DOMAIN_ID','')
    check('announced_ros_domain_supplied',bool(domain) and domain.isascii() and domain.isdigit() and len(domain)<=10)
    scope=os.environ.get('ROS_LOCALHOST_ONLY','0')
    profile=os.environ.get('FASTRTPS_DEFAULT_PROFILES_FILE','')
    try:trusted_loopback=profile==loopback_profile(root)
    except (OSError,ValueError):trusted_loopback=False
    check('network_scope',(scope=='1' or (scope=='0' and trusted_loopback)) if network=='local'
          else scope=='0' and not trusted_loopback)
    check('ros_humble_environment',os.environ.get('ROS_DISTRO')=='humble')
    binary=root/'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
    check('installed_executable',binary.is_file() and os.access(binary,os.X_OK))
    diagnostics=None
    if checks[-1]['passed']:
        try:
            result=subprocess.run([str(binary),'--check-config'],capture_output=True,timeout=10)
            value=json.loads(result.stdout)
            diagnostics={key:value.get(key) for key in ('stage','decision_type','ai_configured','ros_initialized','network_requests')}
            check('configuration_and_stage2_ai',result.returncode==0 and value.get('configuration_valid') is True)
        except (OSError,ValueError,subprocess.TimeoutExpired):check('configuration_and_stage2_ai',False)
    log=Path(os.environ.get('RMTC_LOG_FILE',str(root/'logs/bot.jsonl')))
    parent=log.parent
    while not parent.exists() and parent!=parent.parent:parent=parent.parent
    check('log_directory_writable',parent.is_dir() and os.access(parent,os.W_OK))
    return {'passed':all(c['passed'] for c in checks),'checks':checks,'configuration':diagnostics,
            'read_only':True,'network_requests':0,'ros_started':False,
            'not_verified':['wired DDS connectivity','official action receipt','real AI success','referee approval']}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seat',default=os.environ.get('RMTC_SEAT',''))
    parser.add_argument('--team',default=os.environ.get('RMTC_TEAM_NAME','肥鱼说的都队'))
    parser.add_argument('--referee-ip',default=os.environ.get('RMTC_REFEREE_IP',''))
    parser.add_argument('--network',choices=['local','wired'],required=True)
    args=parser.parse_args()
    report=run(Path(__file__).resolve().parent.parent,args.seat,args.team,args.referee_ip,args.network)
    print(json.dumps(report,ensure_ascii=False));raise SystemExit(0 if report['passed'] else 1)
