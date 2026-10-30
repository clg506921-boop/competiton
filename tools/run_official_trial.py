"""External test controller; never imported by the contestant or used for its decisions."""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import socket
import subprocess
import time
import traceback
import urllib.request
from verification_budget import load_match_counts,match_ledger_path,code_iteration

APP = Path('/home/ubuntu/projects/RMTC/practice-v1.1-run/rmtc_practice')
ROOT = Path(__file__).resolve().parent.parent
BASE = 'http://127.0.0.1:8080'


def request(route, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(BASE + route, data=data,
                                 headers={'Content-Type': 'application/json'} if data else {})
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.load(response)


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def rows(path):
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding='utf8').splitlines() if line]


def official_integrity():
    manifest = json.loads((APP / 'manifest.json').read_text())
    failed = [name for name, digest in manifest['files'].items()
              if hashlib.sha256((APP / name).read_bytes()).hexdigest() != digest]
    archive = APP.parent.parent / 'practice-v1.1-dist/rmtc_practice.tar.gz'
    expected = archive.with_name('rmtc_practice.sha256').read_text().split()[0]
    return dict(referee_root=str(APP),rules_version='v1.1',archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),files=len(manifest['files']), all_match=not failed, mismatches=failed,
                original_archive_matches=hashlib.sha256(archive.read_bytes()).hexdigest() == expected)


def stop(proc):
    if proc is None or proc.poll() is not None:
        return None if proc is None else proc.returncode
    os.killpg(proc.pid, signal.SIGINT)
    try:
        return proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGTERM)
        return proc.wait(timeout=5)


def analyze(run, own_log, session, max_ticks, stage, seat='A'):
    communication = rows(run / 'communication.jsonl')
    perceptions = [r['data'] for r in communication if r.get('seat') == seat and r.get('kind') == 'perception'
                   and r['data'].get('session_id') == session]
    actions = [r['data'] for r in communication if r.get('seat') == seat and r.get('kind') == 'action'
               and r['data'].get('session_id') == session]
    submitted = {a['tick']: a for a in actions if type(a.get('tick')) is int and 0 <= a['tick'] < max_ticks}
    receipts = {}
    receipt_observations = {}
    for p in perceptions:
        receipt = p.get('self', {}).get('last_action')
        if isinstance(receipt, dict) and type(receipt.get('tick')) is int:
            receipts[receipt['tick']] = receipt
            receipt_observations[receipt['tick']] = p
    associated, mismatched, protocol_errors, execution_errors = [], [], [], Counter()
    for tick, receipt in receipts.items():
        action = submitted.get(tick)
        if action is None:
            continue
        error = receipt.get('error')
        if error:
            execution_errors[error if error in {'E_PARSE','E_SCHEMA','E_TICK','E_SESSION','E_STATE','E_RATE','E_OOB','E_BLOCKED'} else 'UNKNOWN'] += 1
        if error in {'E_PARSE', 'E_SCHEMA', 'E_TICK', 'E_SESSION', 'E_RATE'}:
            protocol_errors.append(tick)
        if receipt.get('action') == action.get('action'):
            associated.append(tick)
        elif error == 'E_STATE' or (
            not error and receipt.get('action') == 'wait' and receipt.get('moved') is False
            and receipt_observations[tick].get('self', {}).get('position_reason') == 'respawn'
            and receipt_observations[tick].get('self', {}).get('protected') is True
            and receipt_observations[tick].get('self', {}).get('alive') is True
            and receipt_observations[tick].get('tick') == tick + 1):
            associated.append(tick)  # A cancellation is still an associated execution receipt.
        else:
            mismatched.append(tick)
    bot_rows = rows(own_log)
    decisions = [r for r in bot_rows if r.get('kind') == 'decision' and r.get('session_id') == session and r.get('seat') == seat]
    unique = {r['tick']: r for r in decisions if not r.get('duplicate') and 0 <= r.get('tick', -1) < max_ticks}
    primary_receipts = {r['receipt_tick']: r for r in decisions if r.get('receipt_fresh') and 'receipt_tick' in r}
    primary_associated, primary_mismatched, primary_protocol_errors = [], [], []
    for tick, row in primary_receipts.items():
        action = unique.get(tick)
        if action is None:
            continue
        error = row.get('receipt_error', '')
        if error in {'E_PARSE','E_SCHEMA','E_TICK','E_SESSION','E_RATE'}:
            primary_protocol_errors.append(tick)
        cancelled = row.get('receipt_status') == 'state_cancelled' and (error == 'E_STATE' or (
            not error and row.get('position_reason') == 'respawn'
            and row.get('protected_now') is True and row.get('alive') is True
            and row.get('receipt_action') == 'wait' and row.get('tick') == tick + 1))
        if row.get('receipt_matched') and row.get('expected_receipt_action') == action.get('action') and (
                row.get('receipt_action') == action.get('action') or cancelled):
            primary_associated.append(tick)
        else:
            primary_mismatched.append(tick)
    times = sorted(r['application_us'] / 1000 for r in unique.values())
    score_path = run / 'score.json'
    score = json.loads(score_path.read_text()) if score_path.exists() else None
    report = dict(seat=seat,perceptions=len(perceptions), unique_perception_ticks=len({p['tick'] for p in perceptions}),
        logged_actions=len(actions), unique_submitted_window_ticks=len(submitted),
        missing_submission_ticks=[t for t in range(max_ticks) if t not in submitted],
        all_action_sessions_match=all(a.get('session_id') == session for a in actions),
        terminal_candidate_actions=sum(type(a.get('tick')) is int and a['tick'] >= max_ticks for a in actions),
        associated_receipts=len(associated), receipt_association_rate=len(associated) / len(submitted) if submitted else None,
        mismatched_receipt_ticks=mismatched, protocol_error_ticks=protocol_errors, receipt_errors=dict(execution_errors),
        moved_receipts=sum(r.get('moved') is True for r in receipts.values()),
        action_types=dict(Counter(a.get('action') for a in submitted.values())),
        own_unique_decisions=len(unique), own_parse_errors=sum(r.get('kind') == 'parse_error' for r in bot_rows),
        primary_receipt_source='contestant subscriber summaries of official self.last_action; independent observer coverage is reported separately',
        primary_associated_receipts=len(primary_associated),
        primary_receipt_association_rate=len(primary_associated)/len(unique) if unique else None,
        primary_mismatched_ticks=primary_mismatched, primary_protocol_error_ticks=primary_protocol_errors,
        primary_missing_receipt_ticks=[tick for tick in unique if tick not in primary_receipts],
        primary_receipt_statuses=dict(Counter(r.get('receipt_status') for r in primary_receipts.values())),
        observer_missing_actions_with_executed_move_receipts=[tick for tick in unique if tick not in submitted
            and primary_receipts.get(tick,{}).get('moved') is True
            and primary_receipts[tick].get('receipt_action') == 'move'],
        own_log_dropped=max((r.get('log_dropped', 0) for r in decisions), default=0),
        application_timing=dict(samples=len(times), p99_ms=times[math.ceil(.99 * len(times)) - 1] if times else None,
            max_ms=max(times) if times else None, scope='callback-entry through publish; excludes upstream DDS and referee settlement'),
        score_file=score, final_public_self=perceptions[-1].get('self') if perceptions else None,
        ai_log=rows(run / 'ai.jsonl'), stage=stage)
    save(own_log.parent / 'communication-analysis.json', report)
    return report


def run(stage, ai_mode='mock', solo=False, credential=None, external_config=None, seat='A',prepared_launch=False,wait_only=False):
    if stage not in (1, 2):
        raise ValueError('Unsupported stage')
    if seat not in ('A','B','C','D'):raise ValueError('Unsupported own seat')
    companions=[s for s in ('A','B','C','D') if s!=seat] if stage==2 and not solo else []
    if ai_mode not in ('mock','real') or (ai_mode=='real' and (stage!=2 or not credential)):
        raise ValueError('Real mode requires stage 2 and an stdin credential')
    scenario = ('own-stage1-default' if stage == 1 else
                'own-stage2-'+ai_mode+('-solo' if solo else '-three-companions'))
    config_data = None
    if external_config:
        external_config = Path(external_config).resolve(strict=True)
        if not external_config.is_relative_to((ROOT/'match-configs').resolve()):
            raise ValueError('Only own external size-coverage configurations are allowed')
        config_data = json.loads(external_config.read_text())
        size = 20 if stage == 1 else 24
        if (config_data.get('stage') != stage or config_data['map']['width'] != size
                or config_data['map']['height'] != size or len(config_data['map']['resources']) != 16
                or sum(r['value'] for r in config_data['map']['resources']) != 200):
            raise ValueError('Size-coverage fixture must have standard dimensions, 16 resources, total 200')
        scenario = ('own-stage1-size20' if stage == 1 else
                    'own-stage2-size24-'+ai_mode+('-solo' if solo else '-three-companions'))
        scenario += '-map-'+hashlib.sha256(external_config.read_bytes()).hexdigest()[:12]
    scenario += '-seat-'+seat
    ledger_path = match_ledger_path(ROOT)
    ledger = load_match_counts(ROOT)
    # 2026-10-22: user explicitly removed the per-scenario three-match cap.
    # Retain cumulative attempt numbers and all failures; never reset history.
    with socket.socket() as s:
        s.settimeout(1)
        if s.connect_ex(('127.0.0.1', 8080)) == 0:
            raise RuntimeError('Port 8080 is occupied; inspect the existing controller first')
    manifest = json.loads((ROOT / 'source-manifest.json').read_text())
    assert all(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest for name, digest in manifest['files'].items())
    integrity = official_integrity()
    assert integrity['all_match'] and integrity['original_archive_matches']
    ordinal = ledger.get(scenario, 0) + 1
    iteration = code_iteration(ROOT)
    folder = ledger_path.parent / 'matches' / f'{scenario}-attempt{ordinal}'
    folder.mkdir(parents=True, exist_ok=False)
    (folder/'test-controller.py').write_bytes(Path(__file__).read_bytes())
    ledger[scenario] = ordinal
    save(ledger_path, ledger)  # Count the attempt before starting any processes.
    result = dict(scenario=scenario, ordinal=ordinal, passed=False, stage=stage,iteration_id=iteration,
                  official_referee_modified=False, real_model_calls=0, ai_mode=ai_mode,
                  tested_version=manifest['version'], own_seat=seat, companions=companions,
                  launcher='match-settings' if prepared_launch else 'legacy-env',
                  cpu_affinity=dict(referee=[0,1], contestant=[2,3]), before_integrity=integrity)
    if config_data:
        save(folder/'external-test-config.json', config_data)
        result['external_size_fixture'] = dict(width=size, height=size, resources=16, total_value=200,
            sha256=hashlib.sha256(external_config.read_bytes()).hexdigest(), official_map=False)
    save(folder / 'result.json', result)
    controller = bot = None
    own_log = folder / 'contestant.jsonl'
    output_handles = []
    env = {k: v for k, v in os.environ.items() if not k.startswith('RMTC_')}
    env.update(ROS_DOMAIN_ID='27', ROS_LOCALHOST_ONLY='1', RMW_IMPLEMENTATION='rmw_fastrtps_cpp')
    if ai_mode=='real':
        env.update(PRACTICE_LLM_URL='https://api.deepseek.com/chat/completions',PRACTICE_LLM_KEY=credential,PRACTICE_MODEL='deepseek-flash')
    binary = ROOT / 'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
    result['own_binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
    result['source_manifest_sha256'] = hashlib.sha256((ROOT / 'source-manifest.json').read_bytes()).hexdigest()
    max_ticks = 100 if stage == 1 else 500
    try:
        output = (folder / 'controller.log').open('xb'); output_handles.append(output)
        args = ['taskset', '--cpu-list', '0,1', 'bash', str(APP / 'start_referee.sh'),
                '--stage', str(stage), '--ai-mode', ai_mode, '--data-dir', str(folder / 'referee-data')]
        if external_config:
            args += ['--config', str(external_config)]
        if stage == 2 and not solo:
            args += ['--bots', ','.join(companions)]
        controller = subprocess.Popen(args, cwd=APP, env=env, stdout=output, stderr=subprocess.STDOUT,
                                      start_new_session=True, stdin=subprocess.DEVNULL)
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            if controller.poll() is not None:
                raise RuntimeError('Controller exited before readiness')
            try:
                state = request('/api/state')
                if state['status'].get('ready') and not state['starting']:
                    break
            except OSError:
                pass
            time.sleep(.2)
        else:
            raise RuntimeError('Controller readiness deadline exceeded')
        assert state['status']['paused'] and state['status']['tick'] == 0 and state['ai_mode'] == ai_mode
        assert state['settings']['tick_ms'] == (100 if stage == 1 else 500)
        assert state['settings']['max_ticks'] == max_ticks
        save(folder / 'before.json', state)
        session = state['session_id']; result['session_id'] = session; result['run_dir'] = state['run_dir']
        own_env = dict({k:v for k,v in env.items() if not k.startswith('PRACTICE_')}, RMTC_STAGE=str(stage), RMTC_MAX_TICKS=str(max_ticks), RMTC_TICK_MS=str(state['settings']['tick_ms']), RMTC_LOG_FILE=str(own_log))
        if wait_only:own_env['RMTC_RUN_MODE']='wait'
        if stage == 2:
            own_env.update(RMTC_TEAM_TOKEN='practice-'+seat, RMTC_AI_URL=BASE + '/v1/chat/completions',
                           RMTC_AI_MODEL='deepseek-flash' if ai_mode=='real' else 'practice-mock',
                           RMTC_AI_MAX_TOKENS='2048' if ai_mode=='real' else '256')
            # Practice proxy default is 100. This is a test connection setting,
            # not a prediction of the formal announced quota.
            if ai_mode=='real':own_env['RMTC_AI_QUOTA']='100'
        output = (folder / 'contestant-console.log').open('xb'); output_handles.append(output)
        launch=['bash',str(ROOT/'run_contestant.sh'),seat,'RMTC-Own-'+manifest['version'],'127.0.0.1']
        if prepared_launch:
            settings={'rules_version':'v1.1','seat':seat,'stage':str(stage),'team_name':'肥鱼说的都队','referee_host':'127.0.0.1',
                'ros_domain_id':27,'network':'local','gateway_url':BASE,'team_token':'practice-'+seat,
                'model':'deepseek-flash' if ai_mode=='real' else 'practice-mock',
                'max_tokens':2048 if ai_mode=='real' else 256,'ai_quota':100}
            settings_path=folder/'launch-settings.private.json'
            save(settings_path,settings);settings_path.chmod(0o600)
            launch=['bash',str(ROOT/'prepare_match.sh'),'--launch',str(settings_path)]
        bot = subprocess.Popen(['taskset','--cpu-list','2,3']+launch, cwd=ROOT, env=own_env,
                               stdout=output, stderr=subprocess.STDOUT, start_new_session=True, stdin=subprocess.DEVNULL)
        result['bot_pid'] = bot.pid
        ready_deadline = time.monotonic() + 10
        while time.monotonic() < ready_deadline:
            if bot.poll() is not None:
                raise RuntimeError('Contestant exited during startup')
            if own_log.exists() and any(r.get('kind') == 'startup' for r in rows(own_log)):
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Contestant startup log absent')
        time.sleep(2)  # One DDS discovery interval before opening the first full window.
        request('/api/resume', {})
        begin = time.monotonic(); deadline = begin + (120 if stage == 1 else 720)
        while time.monotonic() < deadline:
            state = request('/api/state')
            sample = dict(elapsed=time.monotonic()-begin, tick=state['status'].get('tick'),
                          board_tick=state.get('board', {}).get('tick'), checks=state.get('checks', {}),
                          finished=state['status'].get('finished'), fault=state.get('fault'))
            with (folder / 'samples.jsonl').open('a') as stream:
                stream.write(json.dumps(sample, ensure_ascii=False) + '\n')
            save(folder / 'progress.json', sample)
            if state.get('fault') or state['status'].get('fault'):
                raise RuntimeError('Referee fault; preserve evidence and stop')
            if bot.poll() is not None:
                raise RuntimeError('Contestant exited during match')
            if state['status'].get('finished'):
                break
            time.sleep(.5 if stage == 1 else 2)
        else:
            raise RuntimeError('Match deadline exceeded')
        result['observed_elapsed_seconds'] = time.monotonic()-begin
        save(folder / 'final-state.json', state)
        result['completed'] = state['status'].get('tick') == max_ticks and state.get('board', {}).get('tick') == max_ticks
        result['bot_returncode'] = stop(bot)
        time.sleep(.5)
        report = analyze(Path(state['run_dir']), own_log, session, max_ticks, stage,seat)
        result['analysis'] = report
        result['real_model_calls']=sum(r.get('mode')=='real' for r in report['ai_log'])
        result['real_successes']=sum(r.get('mode')=='real' and r.get('success') is True for r in report['ai_log'])
        own_ai=[r for r in report['ai_log'] if r.get('seat')==seat and r.get('session_id')==session]
        result['own_real_model_calls']=sum(r.get('mode')=='real' for r in own_ai)
        result['own_real_successes']=sum(r.get('mode')=='real' and r.get('success') is True for r in own_ai)
        result['real_model_calls_scope']='all seats; own_* fields are filtered by own seat and session'
        team = next((team for team in state['board'].get('teams', []) if team.get('team') == seat), {})
        result['own_team_final'] = team
        result['passed'] = bool(result['completed'] and report['primary_associated_receipts'] > 0
            and report['primary_receipt_association_rate'] >= .99 and not report['primary_mismatched_ticks']
            and not report['primary_protocol_error_ticks'] and report['all_action_sessions_match']
            and report['own_unique_decisions'] >= .99 * max_ticks and report['own_log_dropped'] == 0
            and team.get('invalid_actions') == 0 and result['bot_returncode'] == 0)
        if ai_mode=='real':result['passed']=bool(result['passed'] and result['own_real_successes']>0
            and any(row.get('ai_local_success') for row in rows(own_log)))
    except Exception:
        result['exception'] = traceback.format_exc()
    finally:
        env.pop('PRACTICE_LLM_KEY',None);credential=None
        result['bot_final_returncode'] = stop(bot)
        if controller is not None and controller.poll() is None:
            try:
                request('/api/stop', {})
            except Exception:
                result['controller_stop_api_failed'] = True
        result['controller_returncode'] = stop(controller)
        for handle in output_handles:
            handle.close()
        result['after_integrity'] = official_integrity()
        result['passed'] = bool(result['passed'] and result['after_integrity']['all_match'] and result['after_integrity']['original_archive_matches'])
        save(folder / 'result.json', result)
    print(json.dumps(dict(folder=str(folder), passed=result['passed'], ordinal=ordinal,
                          exception=result.get('exception'), own_team_final=result.get('own_team_final')),
                     ensure_ascii=False))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=int, choices=[1, 2], required=True)
    parser.add_argument('--ai-mode',choices=['mock','real'],default='mock')
    parser.add_argument('--solo',action='store_true')
    parser.add_argument('--seat',choices=['A','B','C','D'],default='A')
    parser.add_argument('--wait-only',action='store_true')
    parser.add_argument('--prepared-launch',action='store_true')
    parser.add_argument('--config',help='Own external 20x20 stage1 or 24x24 stage2 fixture')
    args=parser.parse_args()
    if args.ai_mode=='real':
        import sys
        secret=json.load(sys.stdin).get('key')
    else:secret=None
    raise SystemExit(run(args.stage,args.ai_mode,args.solo,secret,args.config,args.seat,args.prepared_launch,args.wait_only))
