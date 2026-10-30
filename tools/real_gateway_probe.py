"""One paid request through the unmodified practice gateway. Secret arrives on stdin only."""
from pathlib import Path
import json,os,signal,socket,subprocess,sys,time,urllib.request,urllib.error
ROOT=Path(__file__).resolve().parent.parent
APP=Path('/home/ubuntu/projects/RMTC/practice-run/rmtc_practice')
BASE='http://127.0.0.1:8080'


def run():
    incoming=json.load(sys.stdin)
    key=incoming.pop('key','');incoming.clear()
    if not key or '\n' in key or '\r' in key:raise ValueError('Invalid credential input')
    ledger_path=ROOT/'real-probe-execution-ledger.json'
    ledger=json.loads(ledger_path.read_text()) if ledger_path.exists() else {'executions':0}
    if ledger['executions']>=3:raise RuntimeError('Three executions reached; stop')
    with socket.socket() as s:
        s.settimeout(1)
        if s.connect_ex(('127.0.0.1',8080))==0:raise RuntimeError('Port occupied; refusing to affect existing controller')
    ordinal=ledger['executions']+1
    folder=ROOT/'matches'/f'real-gateway-probe-attempt{ordinal}'
    folder.mkdir(parents=True,exist_ok=False)
    ledger['executions']=ordinal;ledger_path.write_text(json.dumps(ledger))
    result={'scenario':'real-gateway-probe','ordinal':ordinal,'paid_request_limit':1,'paid_requests_attempted':0,
      'model':'deepseek-flash','upstream_url':'https://api.deepseek.com/chat/completions','passed':False,
      'official_referee_modified':False,'match_ticks_resumed':0,'production_runtime_tested':False}
    proc=None
    env={k:v for k,v in os.environ.items() if not k.startswith(('RMTC_','PRACTICE_'))}
    env.update(ROS_DOMAIN_ID='27',ROS_LOCALHOST_ONLY='1',RMW_IMPLEMENTATION='rmw_fastrtps_cpp',
      PRACTICE_LLM_URL=result['upstream_url'],PRACTICE_LLM_KEY=key,PRACTICE_MODEL=result['model'])
    try:
        with (folder/'controller.log').open('xb') as output:
            proc=subprocess.Popen(['taskset','--cpu-list','0,1','bash',str(APP/'start_referee.sh'),
              '--stage','2','--ai-mode','real','--data-dir',str(folder/'referee-data')],cwd=APP,env=env,
              stdin=subprocess.DEVNULL,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
        env.pop('PRACTICE_LLM_KEY',None);key=None
        deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            if proc.poll() is not None:raise RuntimeError('Controller startup failed')
            try:
                with urllib.request.urlopen(BASE+'/api/state',timeout=2) as response:state=json.load(response)
                if state['status'].get('ready') and not state['starting']:break
            except OSError:pass
            time.sleep(.2)
        else:raise RuntimeError('Controller readiness deadline')
        assert state['status']['paused'] and state['status']['tick']==0
        result['session_id']=state['session_id'];result['run_dir']=state['run_dir']
        body={'model':result['model'],'messages':[{'role':'system','content':'Summarize public robot missions briefly; do not add new goals.'},
          {'role':'user','content':'Collect two normal resources, one high-value resource, and reach the announced target.'}],
          'max_tokens':256,'stream':False,'thinking':{'type':'disabled'}}
        result['practice_only_provider_option']='thinking disabled; not added to production or formal requests'
        req=urllib.request.Request(BASE+'/v1/chat/completions',data=json.dumps(body).encode(),method='POST',
          headers={'Content-Type':'application/json','Authorization':'Bearer practice-A'})
        result['paid_requests_attempted']=1;begin=time.monotonic()
        try:
            with urllib.request.urlopen(req,timeout=35) as response:
                result['http_status']=response.status;data=json.load(response)
            choice=data.get('choices',[{}])[0];message=choice.get('message',{})
            content=message.get('content','')
            result['content_nonempty']=isinstance(content,str) and bool(content)
            result['finish_reason']=choice.get('finish_reason')
            result['usage']=data.get('usage')
            result['passed']=result['http_status']==200 and result['content_nonempty']
        except urllib.error.HTTPError as error:
            result['http_status']=error.code  # Do not record untrusted provider error bodies.
        result['roundtrip_seconds']=time.monotonic()-begin
        with urllib.request.urlopen(BASE+'/api/state',timeout=5) as response:state=json.load(response)
        result['gateway_ai_record']=state.get('ai',{}).get('A')
        result['real_success_count']=state.get('ai',{}).get('A',{}).get('success_count',0)
    except Exception as error:
        result['local_error_type']=type(error).__name__  # No traceback with secret input.
    finally:
        env.pop('PRACTICE_LLM_KEY',None);key=None
        if proc is not None and proc.poll() is None:
            os.killpg(proc.pid,signal.SIGINT)
            try:proc.wait(timeout=8)
            except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGTERM);proc.wait(timeout=5)
        result['controller_returncode']=None if proc is None else proc.returncode
        (folder/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(result,ensure_ascii=False))
    return 0 if result['passed'] else 1


if __name__=='__main__':
    try:raise SystemExit(run())
    except Exception:print(json.dumps({'error':'probe_setup_failed_without_logging_credentials'}));raise SystemExit(2)
