"""Explicit isolated O3/LTO and ASan/UBSan validation; never runs during a match."""
from pathlib import Path
import argparse,hashlib,json,os,socket,subprocess,time
from verification_budget import code_iteration

def save(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--resume',action='store_true');args=parser.parse_args()
    root=Path(__file__).resolve().parent.parent
    with socket.socket() as sock:
        if sock.connect_ex(('127.0.0.1',8080))==0:raise RuntimeError('Stop the practice match before profile validation')
    iteration=code_iteration(root);folder=root/'verification-builds'/iteration
    if args.resume:
        if not folder.is_dir():raise RuntimeError('No preserved profile validation to resume')
    else:folder.mkdir(parents=True,exist_ok=False)
    ledger_path=root/'experiments/iterations'/iteration/'native-validation-ledger.json'
    ledger=json.loads(ledger_path.read_text(encoding='utf8'))
    source=root/'contestant_ws/src/rmtc_bot'
    hashes=lambda:{p.relative_to(source).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file()}
    before=hashes()
    if args.resume:
        result=json.loads((folder/'result.json').read_text(encoding='utf8'))
        initial=folder/'initial-result.json'
        if not initial.exists():save(initial,result)
        result['repair_source_hashes']=before;result['initial_failure_retained']=True
        result['repair_changed_source_files']=[name for name,digest in result['source_hashes'].items() if before.get(name)!=digest]
    else:result={'iteration_id':iteration,'passed':False,'steps':[],'source_hashes':before}
    settings=[('debug-sanitize',['-DCMAKE_BUILD_TYPE=Debug','-DRMTC_SANITIZE=ON','-DRMTC_LTO=OFF']),
              ('release-o3-lto',['-DCMAKE_BUILD_TYPE=Release','-DRMTC_OPTIMIZATION=O3','-DRMTC_LTO=ON','-DRMTC_SANITIZE=OFF'])]
    for profile,flags in settings:
        prior=[s for s in result['steps'] if s['profile']==profile and s['step']=='test']
        if prior and prior[-1]['returncode']==0:continue
        build=folder/profile
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        commands=[('configure',['cmake','-S',str(source/'core'),'-B',str(build),'-DBUILD_TESTING=ON']+flags),
                  ('compile',['cmake','--build',str(build),'-j2']),
                  ('test',['ctest','--test-dir',str(build),'--output-on-failure'])]
        for step,cmd in commands:
            if step=='test':
                if ledger.get(profile,0)>=3:raise RuntimeError('Three suite executions reached for this code iteration and build profile')
                ledger[profile]=ledger.get(profile,0)+1;save(ledger_path,ledger)
            start=time.monotonic()
            ordinal=ledger.get(profile,0) if step=='test' else ledger.get(profile,0)+1
            log_path=folder/(profile+'-attempt'+str(ordinal)+'-'+step+'.log')
            with log_path.open('xb') as output:
                process=subprocess.run(cmd,cwd=root,env=env,stdout=output,stderr=subprocess.STDOUT)
            result['steps'].append({'profile':profile,'step':step,'returncode':process.returncode,'seconds':time.monotonic()-start,'log':log_path.name})
            result['source_unchanged_after_repair' if args.resume else 'source_unchanged']=before==hashes();save(folder/'result.json',result)
            if process.returncode:return 1
    result['passed']=before==hashes();result['test_ledger']=ledger
    save(folder/'result.json',result)
    print(json.dumps({'folder':str(folder),'passed':result['passed']}))
    return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
