"""Offline evidence audit. Test truth labels timing only; never feeds the contestant."""
from collections import Counter
from pathlib import Path
import argparse,json
from summarize_competitive_results import stats

def audit(folder):
    result=json.loads((folder/'result.json').read_text(encoding='utf8'))
    rows=[json.loads(line) for line in (folder/'contestant.jsonl').read_text(encoding='utf8').splitlines() if line]
    decisions=[r for r in rows if r.get('kind')=='decision' and r.get('session_id')==result.get('session_id')]
    seen=set();replies=[]
    for row in decisions:
        key=(row.get('ai_source_id'),row.get('ai_request_ms'))
        if not key[0] or not key[1] or key in seen:continue
        seen.add(key);replies.append(row)
    catalog=json.loads((folder/'observer-notice-catalog.json').read_text(encoding='utf8'))
    sources={r['source_id']:r for r in catalog}
    announcements=result.get('analysis',{}).get('score_file',{}).get('arena_events',{}).get('announcements',[])
    truth={f"第 {r['tick']} Tick：{r['text']}":r for r in announcements if r.get('kind')=='announcement'}
    forecast=[]
    for row in replies:
        source=sources.get(row.get('ai_source_id'))
        if not source or source['first_observed_tick']!=row.get('model_observed_tick'):continue
        label=truth.get(source['body'])
        if not label:continue
        consumed=row['model_consumed_tick'];accepted=row.get('model_event_accepted',0)>0
        forecast.append({'source_id':source['source_id'],'event':label.get('event'),
            'observed_tick':source['first_observed_tick'],'consumed_tick':consumed,
            'start_tick':label['start_tick'],'accepted':accepted,
            'accepted_before_start':accepted and consumed<label['start_tick'],
            'delivery_ticks':consumed-source['first_observed_tick']})
    latest=decisions[-1] if decisions else {}
    proxy_path=folder/'final-proxy-ai.jsonl'
    proxy=[json.loads(line) for line in proxy_path.read_text(encoding='utf8').splitlines() if line] if proxy_path.exists() else []
    team=next((t for t in result.get('analysis',{}).get('score_file',{}).get('teams',[]) if t.get('team')=='A'),{})
    return {'iteration_id':result['iteration_id'],'ordinal':result['ordinal'],'passed':result['passed'],
      'own_score':result.get('own_team_final'),'own_rank':team.get('rank'),
      'all_observed_reply_ms':stats([r['ai_request_ms'] for r in replies]),
      'own_observed_http_replies':len(replies),'own_client_timeouts':sum(r.get('ai_error')=='client_timeout' for r in replies),
      'own_model_output_states':dict(Counter(r.get('model_output_state') for r in replies)),
      'latest_counters':{k:latest.get(k) for k in ('ai_attempts','ai_successful','ai_uncertain_reserved','ai_superseded_pending',
          'ai_skipped_retries','model_rejected','model_events_validated_total','model_events_merged_total')},
      'proxy_record_counts_by_seat':dict(Counter(r.get('seat') for r in proxy)),
      'proxy_observed_successes_by_seat':dict(Counter(r.get('seat') for r in proxy if r.get('mode')=='real' and r.get('success') is True)),
      'forecast_matches':forecast,'forecast_accepted_before_start':sum(r['accepted_before_start'] for r in forecast),
      'public_forecasts_observed':sum(r['body'] in truth for r in catalog),
      'decision_reasons':dict(Counter(r.get('reason') for r in decisions)),
      'scope':'Offline observer/public-receipt audit. Referee truth labels forecast start only; never used by production. Missing observer coverage is not guessed. Proxy logs count observed records, not invoices.'}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folders',nargs='+',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();value=[audit(folder) for folder in args.folders];text=json.dumps(value,ensure_ascii=False,indent=2)+'\n'
    if args.output:args.output.write_text(text,encoding='utf8',newline='\n')
    print(text,end='')
