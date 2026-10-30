"""One final stage1 campaign execution, with restored release, delayed referee and pause.
External verification only. Never imported by contestant decisions. No real AI calls.
"""
from pathlib import Path
from collections import Counter
import hashlib,json,os,shlex,socket,subprocess,sys,time,traceback
ROOT=Path('/home/ubuntu/projects/RMTC/contestant-v0.1')
sys.path.insert(0,str(ROOT/'tools'))
from run_official_trial import APP,request,save,stop,rows,official_integrity

def latest(folder):
    path=folder/'latest.json'
    return json.loads(path.read_text()) if path.exists() else {}

def wait_until(predicate,seconds,message):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        value=predicate()
        if value:return value
        time.sleep(.15)
    raise RuntimeError(message)

def run():
    ledger_path=ROOT/'official-match-execution-ledger.json';ledger=json.loads(ledger_path.read_text())
    stage1_before=sum(v for k,v in ledger.items() if k.startswith('own-stage1-'))
    authorization_path=ROOT/'precheck-extra-authorization.json'
    authorization=None
    if stage1_before>=3:
        assert stage1_before==3 and authorization_path.exists(),'Stage1 campaign exhausted; stop'
        authorization=json.loads(authorization_path.read_text())
        assert authorization.get('scope')=='one corrected stage1 B precheck' and authorization.get('allowed_additional')==1 and authorization.get('consumed') is False,'No unused explicit authorization'
    config_path=ROOT/'verification-tools/stage1-B-size20.json'
    config=json.loads(config_path.read_text())
    # Check routing before spending an execution; the first attempt omitted this.
    assert config['stage']==1 and [r['team'] for r in config['robots']]==['B']
    assert config['map']['width']==config['map']['height']==20
    assert len(config['map']['resources'])==16 and sum(r['value'] for r in config['map']['resources'])==200
    validation=subprocess.run([str(APP/'bin/validate_config')],input=json.dumps(config),capture_output=True,text=True,timeout=8)
    assert validation.returncode==0 and [r['team'] for r in json.loads(validation.stdout)['robots']]==['B']
    restored=ROOT/'restore-validation/v0.9/rmtc-bot'
    restored_result=json.loads((restored.parent/'result.json').read_text());assert restored_result['passed']
    manifest=json.loads((restored/'source-manifest.json').read_text())
    assert all(hashlib.sha256((restored/n).read_bytes()).hexdigest()==h for n,h in manifest['files'].items())
    integrity=official_integrity();assert integrity['all_match'] and integrity['original_archive_matches']
    with socket.socket() as sock:assert sock.connect_ex(('127.0.0.1',8080))!=0,'8080 already in use'
    scenario='own-stage1-precheck-B-v09-corrected';assert not ledger.get(scenario,0)
    folder=ROOT/'matches'/scenario;folder.mkdir(parents=True,exist_ok=False)
    for name in ('run_v09_precheck.py','precheck_public_observer.py','capture_precheck_window.py'):
        (folder/name).write_bytes((ROOT/'verification-tools'/name).read_bytes())
    if authorization is not None:
        authorization['consumed']=True;save(authorization_path,authorization)
    ledger[scenario]=1;save(ledger_path,ledger)
    result={'passed':False,'scenario':scenario,'stage':1,'seat':'B','tested_version':'v0.9',
      'team_name':'肥鱼说的都队','stage1_campaign_total':stage1_before+1,'stage2_campaign_total_unchanged':3,
      'archive_sha256':restored_result['archive_sha256'],'own_binary_sha256':restored_result['binary_sha256'],
      'restored_source_manifest_sha256':hashlib.sha256((restored/'source-manifest.json').read_bytes()).hexdigest(),
      'one_off_extra_execution_authorized':authorization is not None,'config_seats':['B'],
      'external_config_sha256':hashlib.sha256(config_path.read_bytes()).hexdigest(),
      'before_integrity':integrity,'real_model_calls':0,'cpu_affinity':{'referee':[0,1],'contestant':[2,3],'observer':[4]},
      'timing_conclusion':'functional only; paused UI capture and known VM timing limitations'}
    save(folder/'result.json',result);save(folder/'capture-metadata.json',{'archive_sha256':result['archive_sha256'],'binary_sha256':result['own_binary_sha256']})
    observer=bot=controller=None;handles=[];own_log=folder/'contestant.jsonl'
    env={k:v for k,v in os.environ.items() if not k.startswith(('RMTC_','PRACTICE_'))}
    env.update(ROS_DOMAIN_ID='27',ROS_LOCALHOST_ONLY='1',RMW_IMPLEMENTATION='rmw_fastrtps_cpp',ROS_LOG_DIR=str(folder/'ros-logs'))
    own_env=dict(env,RMTC_STAGE='1',RMTC_LOG_FILE=str(own_log))
    def progress(phase,**kw):save(folder/'progress.json',{'phase':phase,**kw})
    def launch(args,filename,**kw):
        handle=(folder/filename).open('xb');handles.append(handle)
        return subprocess.Popen(args,stdout=handle,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True,**kw)
    def screenshot(phase):
        snap=latest(folder);assert snap.get('perception') and snap.get('actions')
        save(folder/(phase+'-public-snapshot.json'),snap)
        capture_env=dict(env,DISPLAY=':0')
        p=subprocess.run(['python3','-B',str(folder/'capture_precheck_window.py'),str(folder),phase],env=capture_env,capture_output=True,text=True,timeout=25)
        result[phase+'_capture_returncode']=p.returncode
        if p.returncode:save(folder/(phase+'-capture-error.json'),{'error':p.stderr[-1200:]})
    try:
        progress('preflight')
        p=subprocess.run(['bash',str(restored/'tools/preflight.sh'),'--seat','B','--referee-ip','127.0.0.1','--network','local'],env=own_env,capture_output=True,text=True,timeout=15)
        save(folder/'preflight.json',json.loads(p.stdout));assert p.returncode==0
        progress('contestant-before-referee')
        bot=launch(['taskset','-c','2,3','bash',str(restored/'run_team.sh'),'B','127.0.0.1'],'contestant-console.log',cwd=restored,env=own_env)
        wait_until(lambda:own_log.exists() and any(r.get('kind')=='startup' for r in rows(own_log)),12,'Contestant startup missing')
        result['contestant_ready_before_referee_started']=True
        observer_cmd='source /opt/ros/humble/setup.bash && exec '+shlex.join(['python3','-B',str(folder/'precheck_public_observer.py'),str(folder),'B'])
        observer=launch(['taskset','-c','4','bash','-c',observer_cmd],'observer-console.log',cwd=ROOT,env=env)
        wait_until(lambda:(folder/'observer-ready.json').exists(),10,'Observer startup missing')
        controller=launch(['taskset','-c','0,1','bash',str(APP/'start_referee.sh'),'--stage','1','--ai-mode','mock','--config',str(config_path),'--data-dir',str(folder/'referee-data')],
            'controller.log',cwd=APP,env=env)
        def ready_state():
            if controller.poll() is not None:raise RuntimeError('Controller exited')
            try:
                state=request('/api/state')
                return state if state['status'].get('ready') and not state['starting'] else None
            except OSError:return None
        state=wait_until(ready_state,30,'Controller readiness timeout')
        assert state['status']['paused'] and state['status']['tick']==0 and state['settings']['max_ticks']==100
        result['session_id']=state['session_id'];result['run_dir']=state['run_dir'];save(folder/'before.json',state)
        def registered():
            registry=latest(folder).get('registry') or {}
            return any(r.get('team')=='B' and r.get('name')=='肥鱼说的都队' for r in registry.get('teams',[]))
        wait_until(registered,12,'Retained actual team registration not received')
        result['delayed_referee_registration_passed']=True
        time.sleep(1)
        request('/api/resume',{});begin=time.monotonic();did_pause=False
        deadline=begin+180
        while time.monotonic()<deadline:
            state=request('/api/state');tick=state['status'].get('tick',-1)
            progress('running',tick=tick,paused=state['status'].get('paused'),pause_verified=did_pause)
            if state.get('fault') or state['status'].get('fault'):raise RuntimeError('Official referee fault')
            if bot.poll() is not None or observer.poll() is not None:raise RuntimeError('Contestant or observer exited')
            if not did_pause and tick>=20 and not state['status'].get('finished'):
                request('/api/pause',{})
                paused=wait_until(lambda:(s if (s:=request('/api/state'))['status'].get('paused') else None),10,'Pause acknowledgement timeout')
                result['pause_tick']=paused['status']['tick'];save(folder/'paused-state.json',paused)
                progress('paused-capture',tick=result['pause_tick'])
                time.sleep(1);screenshot('paused');time.sleep(2)
                still=request('/api/state')
                result['pause_tick_stable']=still['status']['tick']==result['pause_tick']
                result['pause_seconds_minimum']=3
                request('/api/resume',{});did_pause=True
            if state['status'].get('finished'):break
            time.sleep(.2)
        else:raise RuntimeError('Stage1 deadline exceeded')
        result['elapsed_seconds_including_pause']=time.monotonic()-begin
        result['completed']=state['status']['tick']==100 and state['board']['tick']==100
        save(folder/'final-state.json',state)
        wait_until(lambda:(latest(folder).get('perception') or {}).get('tick')==100,6,'Final public state missing')
        screenshot('final')
        result['contestant_returncode']=stop(bot);result['observer_returncode']=stop(observer)
        decisions=[r for r in rows(own_log) if r.get('kind')=='decision' and r.get('session_id')==result['session_id']]
        unique={r['tick']:r for r in decisions if not r.get('duplicate') and type(r.get('tick')) is int and 0<=r['tick']<100}
        receipts={r['receipt_tick']:r for r in decisions if r.get('receipt_fresh') and 'receipt_tick' in r}
        matched=[];mismatched=[];protocol=[]
        for tick,action in unique.items():
            row=receipts.get(tick)
            if not row:continue
            if row.get('receipt_error') in ('E_PARSE','E_SCHEMA','E_TICK','E_SESSION','E_RATE'):protocol.append(tick)
            if row.get('receipt_matched') and row.get('expected_receipt_action')==action['action'] and row.get('receipt_action')==action['action']:matched.append(tick)
            else:mismatched.append(tick)
        team=next(t for t in state['board']['teams'] if t.get('team')=='B');result['own_team_final']=team
        result['analysis']={'unique_decisions':len(unique),'primary_associated_receipts':len(matched),
            'receipt_rate':len(matched)/len(unique) if unique else 0,'missing_decision_ticks':[t for t in range(100) if t not in unique],
            'mismatched_receipts':mismatched,'protocol_errors':protocol,'parse_errors':sum(r.get('kind')=='parse_error' for r in rows(own_log)),
            'log_dropped':max((r.get('log_dropped',0) for r in decisions),default=0),
            'duplicate_rows':sum(r.get('duplicate',False) for r in decisions),
            'resumed_after_silence_rows':sum(r.get('resumed_after_silence',False) for r in decisions),
            'actions':dict(Counter(r['action'] for r in unique.values())),
            'ai_attempts':max((r.get('ai_attempts',0) for r in decisions),default=0),
            'own_log_sha256':hashlib.sha256(own_log.read_bytes()).hexdigest()}
        result['real_model_calls']=sum(r.get('mode')=='real' for r in rows(Path(state['run_dir'])/'ai.jsonl'))
        a=result['analysis']
        result['communication_passed']=bool(result['completed'] and len(unique)==100 and a['receipt_rate']>=.99 and not mismatched and not protocol
            and not a['parse_errors'] and a['log_dropped']==0 and team.get('invalid_actions')==0 and result['contestant_returncode']==0)
        result['passed']=bool(result['communication_passed'] and result['delayed_referee_registration_passed'] and result['pause_tick_stable']
            and result.get('paused_capture_returncode')==result.get('final_capture_returncode')==0 and result['real_model_calls']==0)
    except Exception:result['exception']=traceback.format_exc()
    finally:
        result['bot_final_returncode']=stop(bot);result['observer_final_returncode']=stop(observer)
        if controller is not None and controller.poll() is None:
            try:request('/api/stop',{})
            except Exception:result['stop_api_failed']=True
        result['controller_returncode']=stop(controller)
        for handle in handles:handle.close()
        result['after_integrity']=official_integrity()
        result['passed']=bool(result['passed'] and result['after_integrity']['all_match'] and result['after_integrity']['original_archive_matches'])
        save(folder/'result.json',result);progress('stopped',passed=result['passed'])
    print(json.dumps({'passed':result['passed'],'folder':str(folder),'exception':result.get('exception'),'analysis':result.get('analysis')},ensure_ascii=False))
    return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(run())
