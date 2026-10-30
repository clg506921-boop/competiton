"""Paid offline event-Prompt experiment through unmodified practice gateway.
No match clock is resumed; contestant parsing/validation code evaluates replies.
Credential is supplied on stdin, passed in child environment, never persisted.
"""
from pathlib import Path
from collections import defaultdict
import hashlib,json,os,signal,socket,statistics,subprocess,sys,time,urllib.error,urllib.request
from verification_budget import code_iteration
from run_official_trial import APP,request,save,stop,official_integrity,rows
ROOT=Path(__file__).resolve().parent.parent

def run():
    incoming=json.load(sys.stdin);key=incoming.pop('key');incoming.clear()
    if not isinstance(key,str) or not key or '\n' in key or '\r' in key:raise ValueError('Invalid credential')
    iteration=code_iteration(ROOT);folder=ROOT/'experiments/event-prompts'/iteration
    folder.mkdir(parents=True,exist_ok=False)
    bank=json.loads((ROOT/'experiment-fixtures/event-bench-v010.json').read_text())
    executable=ROOT/'build-core/event_eval_tool'
    integrity=official_integrity();assert integrity['all_match'] and integrity['original_archive_matches']
    with socket.socket() as sock:assert sock.connect_ex(('127.0.0.1',8080))!=0,'Port occupied'
    def evaluate(case,**data):
        payload={k:case[k] for k in ('source_id','current_tick','width','height')};payload.update(data)
        p=subprocess.run([str(executable)],input=json.dumps(payload,ensure_ascii=False)+'\n',capture_output=True,text=True,timeout=10)
        if p.returncode:raise RuntimeError('Offline evaluator rejected input')
        return json.loads(p.stdout)
    gold={}
    for case in bank['cases']+bank.get('blind_cases',[]):
        response={'choices':[{'finish_reason':'stop','message':{'content':json.dumps(case['expected'])}}]}
        value=evaluate(case,operation='evaluate',response=response)
        assert value['state']=='validated' and len(value['accepted'])==1,'Invalid experiment gold'
        gold[case['id']]=value['accepted'][0]
    ledger_path=folder/'request-ledger.json';ledger={};records=[]
    result={'iteration_id':iteration,'passed':False,'model':'deepseek-flash','request_profile':'strict OpenAI-compatible fields; no thinking/format extensions',
      'max_tokens':bank['strict_request_max_tokens'],'trials_per_training_case_and_prompt':3,'paid_request_limit':48,
      'before_integrity':integrity,'match_ticks_resumed':0,'official_referee_modified':False,'records':records}
    result['evaluator_sha256']=hashlib.sha256(executable.read_bytes()).hexdigest()
    result['source_hashes']={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted((ROOT/'contestant_ws/src/rmtc_bot').rglob('*')) if p.is_file()}
    save(folder/'result.json',result)
    env={k:v for k,v in os.environ.items() if not k.startswith(('RMTC_','PRACTICE_'))}
    env.update(ROS_DOMAIN_ID='27',ROS_LOCALHOST_ONLY='1',RMW_IMPLEMENTATION='rmw_fastrtps_cpp',
        ROS_LOG_DIR=str(folder/'ros-logs'),PRACTICE_LLM_URL='https://api.deepseek.com/chat/completions',PRACTICE_MODEL=result['model'],PRACTICE_LLM_KEY=key)
    controller=None;output=None
    def trial(case,style,repeat,held_out=False):
        name=case['id']+'/'+style
        if ledger.get(name,0)>=3:raise RuntimeError('Three executions reached for this fixed case and code variant')
        messages=evaluate(case,operation='prompt',style=style,announcement=case['announcement'],known_events=case['known_events'])
        payload={'model':result['model'],'messages':messages,'max_tokens':result['max_tokens'],'stream':False}
        ledger[name]=ledger.get(name,0)+1;save(ledger_path,ledger)
        row={'case':case['id'],'style':style,'repeat':repeat,'held_out':held_out,'prompt_bytes':len(json.dumps(messages,ensure_ascii=False).encode()),
            'status':None,'exact':False,'within_production_timeout':False}
        save(folder/'progress.json',{'case':case['id'],'style':style,'repeat':repeat,'requests_attempted':sum(ledger.values())})
        req=urllib.request.Request('http://127.0.0.1:8080/v1/chat/completions',data=json.dumps(payload).encode(),
            headers={'Content-Type':'application/json','Authorization':'Bearer practice-A'})
        start=time.monotonic()
        try:
            with urllib.request.urlopen(req,timeout=40) as response:data=json.load(response);row['status']=response.status
            checked=evaluate(case,operation='evaluate',response=data,http_status=row['status'])
            row['validation']=checked
            if len(checked['accepted'])==1:
                actual=checked['accepted'][0];expected=gold[case['id']]
                keys=('type','phase','announce_tick','start_tick','end_tick','trigger_end_tick','effect_end_tick','area','trigger','params','subject_ids','related_event_id')
                row['different_fields']=[k for k in keys if actual.get(k)!=expected.get(k)]
                row['exact']=not row['different_fields']
            usage=data.get('usage',{})
            row['usage']={k:usage.get(k) for k in ('prompt_tokens','completion_tokens','total_tokens','completion_tokens_details')}
            data=None
        except urllib.error.HTTPError as error:row['status']=error.code
        except (OSError,ValueError,RuntimeError) as error:row['error_type']=type(error).__name__
        row['elapsed_ms']=(time.monotonic()-start)*1000
        row['within_production_timeout']=row['elapsed_ms']<10000 and row['exact']
        records.append(row);save(folder/'result.json',result)
        with (folder/'trials.jsonl').open('a') as stream:stream.write(json.dumps(row,ensure_ascii=False)+'\n')
        if row['status']==401:raise RuntimeError('Upstream authentication failed; stop paid requests')
        time.sleep(.25)
    try:
        output=(folder/'controller.log').open('xb')
        controller=subprocess.Popen(['taskset','-c','0,1','bash',str(APP/'start_referee.sh'),'--stage','2','--ai-mode','real','--data-dir',str(folder/'referee-data')],
            cwd=APP,env=env,stdout=output,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
        env.pop('PRACTICE_LLM_KEY',None);key=None
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            if controller.poll() is not None:raise RuntimeError('Proxy exited')
            try:
                state=request('/api/state')
                if state['status'].get('ready') and not state['starting']:break
            except OSError:pass
            time.sleep(.2)
        else:raise RuntimeError('Proxy readiness timeout')
        assert state['status']['paused'] and state['status']['tick']==0
        result['session_id']=state['session_id'];result['run_dir']=state['run_dir']
        training=[c for c in bank['cases'] if c['id'] in bank['training_cases']]
        styles=['baseline','compact','example']
        for repeat in range(3):
            for case in training:
                for style in styles[repeat:]+styles[:repeat]:trial(case,style,repeat+1)
        summary={}
        for style in styles:
            selected=[r for r in records if r['style']==style and not r['held_out']]
            times=[r['elapsed_ms'] for r in selected]
            summary[style]={'samples':len(selected),'exact_rate':sum(r['exact'] for r in selected)/len(selected),
                'within_10s_rate':sum(r['within_production_timeout'] for r in selected)/len(selected),
                'mean_ms':statistics.mean(times),'sd_ms':statistics.stdev(times),'min_ms':min(times),'max_ms':max(times)}
        result['per_case_summary']={}
        for case in training:
            result['per_case_summary'][case['id']]={}
            for style in styles:
                selected=[r for r in records if r['style']==style and r['case']==case['id']]
                times=[r['elapsed_ms'] for r in selected]
                result['per_case_summary'][case['id']][style]={'samples':len(selected),'exact_count':sum(r['exact'] for r in selected),
                    'mean_ms':statistics.mean(times),'sd_ms':statistics.stdev(times),'min_ms':min(times),'max_ms':max(times)}
        winner=max(styles,key=lambda s:(summary[s]['exact_rate'],summary[s]['within_10s_rate'],-summary[s]['mean_ms']))
        result['training_summary']=summary;result['selected_style']=winner
        for case in bank['cases']:
            if case['id'] in bank['held_out_cases']:
                for repeat in range(1,4):trial(case,winner,repeat,True)
        for case in bank.get('blind_cases',[]):
            for repeat in range(1,4):trial(case,winner,repeat,True)
        held=[r for r in records if r['held_out']]
        result['held_out_exact_rate']=sum(r['exact'] for r in held)/len(held)
        result['passed']=result['held_out_exact_rate']==1 and summary[winner]['exact_rate']==1
    except Exception as error:result['local_error_type']=type(error).__name__
    finally:
        env.pop('PRACTICE_LLM_KEY',None);key=None
        if controller is not None and controller.poll() is None:
            try:
                state=request('/api/state');result['final_tick']=state['status']['tick'];request('/api/stop',{})
            except Exception:result['proxy_stop_error']=True
        result['controller_returncode']=stop(controller)
        if output:output.close()
        result['paid_requests_attempted']=sum(ledger.values());result['after_integrity']=official_integrity()
        if result.get('run_dir'):
            audit=rows(Path(result['run_dir'])/'ai.jsonl');result['proxy_successes']=sum(r.get('success') is True for r in audit)
        save(folder/'result.json',result);save(folder/'progress.json',{'stopped':True,'paid_requests_attempted':result['paid_requests_attempted'],'passed':result['passed']})
    print(json.dumps({'folder':str(folder),'paid_requests_attempted':result['paid_requests_attempted'],'passed':result['passed']}))

if __name__=='__main__':
    try:run()
    except Exception as error:print(json.dumps({'startup_error_type':type(error).__name__}));raise SystemExit(2)
