"""Read one private per-match file; check locally or launch the sealed contestant.
No generated credentials file, shell eval, model call, or source modification.
"""
import argparse,json,os,re,subprocess,sys
from pathlib import Path
from urllib.parse import urlsplit
from launch_bot import publication_mode,loopback_profile
sys.dont_write_bytecode=True

ROOT=Path(__file__).resolve().parent.parent
SUPPORTED_RULES='v1.2'
SUPPORTED_RULE_VERSIONS=('v1.1','v1.2')
FIELDS={'rules_version','seat','stage','team_name','referee_host','ros_domain_id','network','gateway_url','team_token','model','max_tokens','ai_quota','max_ticks','tick_ms'}
CONNECTION_FIELDS={'RMTC_CONNECTION_CONFIG','RMTC_STAGE','RMTC_MAX_TICKS','RMTC_TICK_MS','RMTC_AI_URL','RMTC_AI_TOKEN',
    'RMTC_TEAM_TOKEN','RMTC_AI_MODEL','RMTC_AI_MAX_TOKENS','RMTC_AI_QUOTA','RMTC_AI_ALLOW_DEFAULT_MODEL'}

class SetupError(ValueError):pass
def unique(pairs):
    out={}
    for key,value in pairs:
        if key in out:raise SetupError('duplicate_json_key')
        out[key]=value
    return out
def load(path):
    try:
        if path.stat().st_size>65536:raise SetupError('settings_too_large')
        return json.loads(path.read_text(encoding='utf-8-sig'),object_pairs_hook=unique)
    except (OSError,UnicodeError,json.JSONDecodeError):raise SetupError('cannot_read_settings') from None
def prepare(value,base=None):
    if not isinstance(value,dict):raise SetupError('settings_must_be_object')
    if set(value)-FIELDS:raise SetupError('unknown_settings_field')
    # This is a compatibility check against the frozen implementation, never a
    # runtime selector for changing scoring constants after sealing.
    if value.get('rules_version',SUPPORTED_RULES) not in SUPPORTED_RULE_VERSIONS:raise SetupError('unsupported_rules_version')
    errors=[]
    def require(ok,name):
        if not ok:errors.append(name)
    seat=value.get('seat');stage=value.get('stage','auto');team=value.get('team_name','肥鱼说的都队')
    require(seat in ('A','B','C','D'),'seat');require(stage in ('0','1','2','auto'),'stage')
    require(isinstance(team,str) and 1<=len(team.encode('utf8'))<=96 and not any(c in team for c in '\r\n\0'),'team_name')
    host=value.get('referee_host');domain=value.get('ros_domain_id');network=value.get('network','wired')
    require(isinstance(host,str) and bool(re.fullmatch(r'[A-Za-z0-9_.:\[\]-]+',host)),'referee_host')
    require(type(domain) is int and 0<=domain<=232,'ros_domain_id')
    require(network in ('wired','local'),'network')
    if stage=='1':require(seat=='A','stage1_requires_seat_A')
    env=dict(os.environ if base is None else base)
    for name in CONNECTION_FIELDS:env.pop(name,None)
    # Preflight and launch must inspect the same registered decision file.
    # Match run_contestant.sh's default, including an explicitly empty variable.
    env['RMTC_DECISION_CONFIG']=env.get('RMTC_DECISION_CONFIG') or str(ROOT/'decision_config.json')
    # Production always uses the frozen default dispatch/policy. Approved
    # decision_config remains the existing registered file, not this input file.
    for name in ('RMTC_RESEARCH_POLICY','RMTC_RESEARCH_DISPATCH','RMTC_RUN_MODE'):env.pop(name,None)
    env.update(RMTC_STAGE=str(stage),RMTC_SEAT=str(seat),RMTC_TEAM_NAME=str(team),RMTC_REFEREE_IP=str(host),
        ROS_DOMAIN_ID=str(domain),ROS_LOCALHOST_ONLY='0',RMW_FASTRTPS_USE_QOS_FROM_XML='0')
    if network=='local':
        try:env['FASTRTPS_DEFAULT_PROFILES_FILE']=loopback_profile(ROOT)
        except (OSError,ValueError):errors.append('local_dds_profile')
        env.pop('FASTDDS_DEFAULT_PROFILES_FILE',None)
    elif env.get('FASTRTPS_DEFAULT_PROFILES_FILE')==str((ROOT/'transport/loopback-udp.xml').resolve()):
        env.pop('FASTRTPS_DEFAULT_PROFILES_FILE',None)
    for key,maximum in [('max_ticks',9223372036854775807),('tick_ms',60000)]:
        if key in value:
            require(type(value[key]) is int and 0<value[key]<=maximum,key)
            env['RMTC_'+key.upper()]=str(value[key])
    if stage not in ('0','1'):
        gateway=value.get('gateway_url');token=value.get('team_token');model=value.get('model')
        if model is None or model=='':model='deepseek-flash'
        limit=value.get('max_tokens');quota=value.get('ai_quota')
        require(isinstance(gateway,str) and bool(gateway),'gateway_url')
        require(isinstance(token,str) and bool(token) and not any(c in token for c in '\r\n\0'),'team_token')
        require(isinstance(model,str) and model in ('deepseek-flash','practice-mock'),'model')
        require(type(limit) is int and 0<limit<=2147483647,'max_tokens')
        require(type(quota) is int and 0<=quota<=18446744073709551615,'ai_quota')
        if isinstance(gateway,str) and gateway:
            endpoint=gateway.rstrip('/')
            if not endpoint.endswith('/v1/chat/completions'):endpoint+='/v1/chat/completions'
            try:
                parsed=urlsplit(endpoint)
                require(parsed.scheme in ('http','https') and bool(parsed.hostname) and '@' not in parsed.netloc
                    and not parsed.query and not parsed.fragment and not any(c.isspace() for c in endpoint),'gateway_url')
                _=parsed.port
            except ValueError:errors.append('gateway_url')
            env['RMTC_AI_URL']=endpoint
        env.update(RMTC_TEAM_TOKEN=str(token or ''),RMTC_AI_MODEL=model or '',RMTC_AI_MAX_TOKENS=str(limit),
            RMTC_AI_QUOTA=str(quota),RMTC_AI_ALLOW_DEFAULT_MODEL='0')
    try:env['RMW_FASTRTPS_PUBLICATION_MODE']=publication_mode(env)
    except ValueError:errors.append('dds_publication_mode')
    if errors:raise SetupError('missing_or_invalid: '+','.join(dict.fromkeys(errors)))
    return env

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group();mode.add_argument('--check',action='store_true');mode.add_argument('--launch',action='store_true')
    parser.add_argument('settings',type=Path);args=parser.parse_args()
    try:
        value=load(args.settings);env=prepare(value)
        binary=ROOT/'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
        checked=subprocess.run([str(binary),'--check-config'],env=env,capture_output=True,timeout=10)
        config=json.loads(checked.stdout)
        if checked.returncode or config.get('configuration_valid') is not True:
            raise SetupError('node_configuration_not_ready')
        command=[sys.executable,'-B',str(ROOT/'tools/preflight.py'),'--seat',env['RMTC_SEAT'],
            '--team',env['RMTC_TEAM_NAME'],'--referee-ip',env['RMTC_REFEREE_IP'],'--network',value.get('network','wired')]
        preflight=subprocess.run(command,env=env,capture_output=True,timeout=15)
        report=json.loads(preflight.stdout)
        summary={'passed':preflight.returncode==0,'seat':env['RMTC_SEAT'],'stage':env['RMTC_STAGE'],
            'rules_version':value.get('rules_version',SUPPORTED_RULES),
            'implementation_rules_default':SUPPORTED_RULES,
            'dds_publication_mode':env['RMW_FASTRTPS_PUBLICATION_MODE'],
            'dds_transport':'loopback_udp' if value.get('network','wired')=='local' else 'middleware_default',
            'ros_domain_id':int(env['ROS_DOMAIN_ID']),'network':value.get('network','wired'),
            'ai_configured':config.get('ai_configured'),'model':env.get('RMTC_AI_MODEL'),
            'ai_request_profile':'official_flash_no_thinking_json' if env.get('RMTC_AI_MODEL')=='deepseek-flash' else 'practice_mock' if env.get('RMTC_AI_MODEL')=='practice-mock' else 'not_used',
            'gateway_model_default':env.get('RMTC_AI_ALLOW_DEFAULT_MODEL')=='1',
            'failed_checks':[c['check'] for c in report['checks'] if not c['passed']],
            'network_requests':0,'not_verified':report['not_verified']}
        print(json.dumps(summary,ensure_ascii=False),flush=True)
        if not summary['passed']:return 3
        if args.launch:
            os.environ.update(env)
            # Remove inherited stale variables too: os.environ.update alone
            # would retain a conflicting old token/config that prepare removed.
            for name in CONNECTION_FIELDS:
                if name not in env:os.environ.pop(name,None)
            for name in ('RMTC_RESEARCH_POLICY','RMTC_RESEARCH_DISPATCH','RMTC_RUN_MODE'):os.environ.pop(name,None)
            launcher=ROOT/'run_contestant.sh'
            os.execv('/bin/bash',['bash',str(launcher),env['RMTC_SEAT'],env['RMTC_TEAM_NAME'],env['RMTC_REFEREE_IP']])
        return 0
    except SetupError as e:
        print(json.dumps({'passed':False,'error':str(e),'network_requests':0},ensure_ascii=False));return 2
    except (OSError,ValueError,subprocess.TimeoutExpired):
        print(json.dumps({'passed':False,'error':'local_setup_check_failed','network_requests':0}));return 2

if __name__=='__main__':raise SystemExit(main())
