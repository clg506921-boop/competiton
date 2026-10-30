"""Aggregate all attempts; never select only the best score or merge unlike fixtures."""
from collections import defaultdict
from pathlib import Path
import argparse,json,math,statistics

def stats(values):
    if not values:return {'n':0,'mean':None,'sample_sd':None,'min':None,'max':None}
    if any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in values):
        raise ValueError('Non-finite or nonnumeric observation')
    return {'n':len(values),'mean':statistics.mean(values),
            'sample_sd':statistics.stdev(values) if len(values)>1 else None,'min':min(values),'max':max(values)}

def summarize(results):
    groups=defaultdict(list)
    for record in results:
        key={k:record.get(k) for k in ('iteration_id','own_binary_sha256','stage','scenario','ai_mode','companions','cpu_affinity')}
        key['fixture_sha256']=record.get('external_size_fixture',{}).get('sha256')
        groups[json.dumps(key,sort_keys=True)].append(record)
    output=[]
    for key,records in sorted(groups.items()):
        # User removed the three-match cap on 2026-10-22. Keep all attempts.
        ordinals=[r.get('ordinal') for r in records]
        if len(set(ordinals))!=len(ordinals):raise ValueError('Duplicate attempt supplied')
        complete=[r for r in records if r.get('completed') and r.get('own_team_final')]
        metrics={k:stats([r['own_team_final'][k] for r in complete if k in r['own_team_final']])
                 for k in ('score','resource_value','effective_damage','mission_completed','invalid_actions')}
        output.append({'comparison_key':json.loads(key),'attempts':len(records),
          'completed':len(complete),'passed':sum(r.get('passed') is True for r in records),
          'preliminary_only':True,'metrics':metrics,
          'failed_ordinals':[r.get('ordinal') for r in records if r.get('passed') is not True],
          'note':'Failed attempts remain in reliability counts. Score statistics include completed games only; n is explicit. Three samples do not establish stable win rate.'})
    return output

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths',type=Path,nargs='+',help='Match result JSON files')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    values=summarize([json.loads(p.read_text(encoding='utf8')) for p in args.paths])
    text=json.dumps(values,ensure_ascii=False,indent=2)+'\n'
    if args.output:args.output.write_text(text,encoding='utf8',newline='\n')
    print(text,end='')

if __name__=='__main__':main()
