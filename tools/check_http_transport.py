"""Local fake HTTP server only. Does not start or modify the referee or call a real model."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, HTTPServer
import json, subprocess, sys, threading

def run(probe):
    results=[]
    cases=[(200,{'choices':[{'message':{'content':'{"ok":true}'}}]},True,''),
           (429,{'error':{'code':'quota_exceeded'}},False,'quota_exceeded'),
           (503,'not JSON',False,'non_json_body'),
           (200,'not JSON',True,'non_json_body')]
    for status,body,success,error in cases:
        captured={}
        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                count=int(self.headers.get('Content-Length','0'))
                request=json.loads(self.rfile.read(count))
                captured.update(path=self.path,request=request,
                    content_length_present=count>0,
                    transfer_encoding_absent=self.headers.get('Transfer-Encoding') is None,
                    expect_absent=not self.headers.get('Expect'),
                    bearer_matches_fake=self.headers.get('Authorization')=='Bearer fake-token-for-local-test-only')
                data=(json.dumps(body) if isinstance(body,dict) else body).encode()
                self.send_response(status);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
            def log_message(self,*args):pass
        server=HTTPServer(('127.0.0.1',0),Handler)
        worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
        try:
            out=subprocess.run([str(probe),f'http://127.0.0.1:{server.server_port}/v1/chat/completions'],capture_output=True,text=True,check=True,timeout=15)
            reply=json.loads(out.stdout)
            assert reply['status']==status and reply['forwarded_success']==success and reply['error_code']==error
            assert captured['path']=='/v1/chat/completions'
            assert all(captured[k] for k in ['content_length_present','transfer_encoding_absent','expect_absent','bearer_matches_fake'])
            assert captured['request']['max_tokens']==256 and captured['request']['stream'] is False
            results.append({'status':status,'body_kind':'json' if isinstance(body,dict) else 'non-json','passed':True})
        finally:server.shutdown();server.server_close();worker.join(timeout=2)
    return results

if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit('Usage: python3 check_http_transport.py /path/to/llm_probe')
    print(json.dumps({'local_http_cases':run(Path(sys.argv[1])),'real_model_calls':0,'referee_used':False},ensure_ascii=False))
