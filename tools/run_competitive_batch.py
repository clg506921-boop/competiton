"""Fixed three-game functional batch. Uses only public test-controller APIs.
Never imported by the contestant. A failed game stops the batch; no silent retries.
"""
from pathlib import Path
import json,sys
from verification_budget import code_iteration
from run_official_trial import ROOT,run,save

def main():
    incoming=json.load(sys.stdin);key=incoming.pop('key');incoming.clear()
    folder=ROOT/'experiments/iterations'/code_iteration(ROOT)/'size24-real-batch'
    folder.mkdir(parents=True,exist_ok=False)
    result={'iteration_id':code_iteration(ROOT),'stage':2,'ai_mode':'real',
            'map':'match-configs/stage2-size24.json','companions':['B','C','D'],
            'planned_games':3,'completed_games':0,'passed':False,'returncodes':[]}
    try:
        for _ in range(3):
            code=run(2,'real',False,key,ROOT/'match-configs/stage2-size24.json')
            result['returncodes'].append(code)
            result['completed_games']+=1
            save(folder/'result.json',result)
            if code:break
        result['passed']=result['returncodes']==[0,0,0]
    finally:
        key=None;save(folder/'result.json',result)
    return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
