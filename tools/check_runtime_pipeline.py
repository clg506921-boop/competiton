"""Integrated own-node check: synthetic public ROS + local fake AI, NEVER a referee match."""
from pathlib import Path
import copy, json, os, signal, subprocess, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from std_msgs.msg import String
from launch_bot import arguments

def run():
    if os.environ.get('ROS_DOMAIN_ID') != '39' or os.environ.get('ROS_LOCALHOST_ONLY') != '1':
        raise RuntimeError('Requires isolated localhost ROS domain 39')
    root = Path(__file__).resolve().parent.parent
    scratch = root / 'runtime-check'
    scratch.mkdir(exist_ok=True)
    fixtures = json.loads((root/'contestant_ws/src/rmtc_bot/test/fixtures/event-output.examples.json').read_text())
    calls = []
    request_started = threading.Event()
    release_response = threading.Event()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass
        def do_POST(self):
            try:
                data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                context = json.loads(data['messages'][1]['content'])
                output = copy.deepcopy(fixtures['cases'][0]['output'])
                event = output['events'][0]
                event.update(source_ids=[context['source_id']], announce_tick=0, start_tick=7)
                request_started.set()
                if not release_response.wait(15): raise TimeoutError('Test never released local response')
                body = json.dumps({'choices':[{'message':{'content':json.dumps(output)}}]}).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                calls.append({'source_id':context['source_id'], 'max_tokens':data['max_tokens'], 'stream':data['stream']})
            except Exception:
                self.send_error(500)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    connection = {'stage':'2', 'ai_url':f'http://127.0.0.1:{server.server_port}/v1/chat/completions',
                  'ai_model':'local-fake-only', 'max_tokens':256, 'ai_quota':3}
    connection_path = scratch/'connection.json'
    connection_path.write_text(json.dumps(connection))
    env = dict(os.environ, RMTC_CONNECTION_CONFIG=str(connection_path), RMTC_TEAM_TOKEN='fixed-fake-token',
               RMTC_DECISION_CONFIG=str(root/'decision_config.json'), RMTC_LOG_FILE=str(scratch/'bot.jsonl'))
    for key in ('RMTC_STAGE','RMTC_MAX_TICKS','RMTC_AI_URL','RMTC_AI_TOKEN','RMTC_AI_MODEL','RMTC_AI_MAX_TOKENS','RMTC_AI_QUOTA','RMTC_AI_ALLOW_DEFAULT_MODEL'):
        env.pop(key, None)
    log_path = scratch/'bot.jsonl'
    if log_path.exists():
        log_path.rename(scratch/('bot-'+str(time.time_ns())+'.jsonl'))
    rclpy.init()
    node = rclpy.create_node('runtime_pipeline_probe')
    qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.VOLATILE)
    received = []
    callback_errors = []
    def on_action(msg):
        try: received.append(json.loads(msg.data))
        except Exception as error: callback_errors.append(type(error).__name__)
    sub = node.create_subscription(String, '/team_A/action', on_action, qos)
    pub = node.create_publisher(String, '/team_A/perception', qos)
    binary = root/'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
    proc = None
    checked = []
    try:
        proc = subprocess.Popen([str(binary)]+arguments('A','runtime-check','collect','greedy','immediate'),
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
        def spin_until(predicate, seconds=5):
            end = time.monotonic()+seconds
            while time.monotonic()<end:
                rclpy.spin_once(node, timeout_sec=.02)
                if predicate(): return True
                if proc.poll() is not None: raise RuntimeError('Own node exited before check completed')
            return predicate()
        assert spin_until(lambda:pub.get_subscription_count()>0 and node.count_publishers('/team_A/action')>0, 10), 'DDS discovery'
        base = {'tick':0, 'session_id':'integrated-runtime-check', 'vision_radius':4,
            'self':{'team':'A','robot_id':'A','position':[4,4],'hp':100,'alive':True,'protected':False,
                    'defending':False,'respawn_in':0,'last_action':None},
            'map':{'width':20,'height':20},'visible_entities':[],
            'score':{'resource_value':0,'effective_damage':0,'mission_completed':0,'mission_completed_ids':[],'invalid_actions':0},
            'missions':[{'id':'reach','type':'reach','position':[4,2]}], 'event_text':'opaque announcement for model only'}
        def send(frame, action, direction=None):
            before=len(received)
            pub.publish(String(data=json.dumps(frame)))
            assert spin_until(lambda:len(received)>before), 'No action'
            expected={'tick':frame['tick'],'session_id':base['session_id'],'action':action}
            if direction: expected['direction']=direction
            assert received[-1]==expected, 'Action differs: '+json.dumps(received[-1])
            checked.append(expected)
        send(base,'move','up')
        assert spin_until(request_started.is_set), 'Local request did not start'
        for tick in range(1,5):
            send(dict(base,tick=tick),'move','up')
        assert len(calls)==0, 'Model response arrived before the pending-request check'
        pending_request_actions=len(checked)
        release_response.set()
        assert spin_until(lambda:len(calls)==1), 'No local model request'
        spin_until(lambda:False, .10)  # Let this same HTTP result reach the worker queue.
        send(dict(base,tick=5),'move','up')  # Leave before the announced pre-move blast at T=7.
        third=copy.deepcopy(base)
        third['tick']=6
        third['self']['position']=[4,3]
        third['self']['last_action']={'tick':5,'action':'move','direction':'up','effective_direction':'up','moved':True}
        send(third,'move','up')
        fourth=copy.deepcopy(third)
        fourth['tick']=7
        fourth['self'].update(hp=15,last_action=None)
        fourth['missions']=[{'id':'reach','type':'reach','position':[4,1]}]
        relative='\n'.join(''.join('1' if (x,y)==(4,4) else '2' if (x,y)==(5,4) else '0' for x in range(9)) for y in range(9))
        fourth['visible_entities']=[{'type':'robot','robot_id':'B','position':relative,'hp':100,'protected':False,'defending':False}]
        send(fourth,'defend')
        assert len(calls)==1, 'Same opaque notice was sent twice'
        assert not callback_errors, 'Invalid action JSON'
    finally:
        release_response.set()
        if proc is not None:
            if proc.poll() is None: os.killpg(proc.pid,signal.SIGINT)
            try: output,_=proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid,signal.SIGKILL)
                output,_=proc.communicate(timeout=3)
            if proc.returncode!=0: raise RuntimeError('Own node failed clean shutdown: '+output[-1200:])
        node.destroy_node()
        rclpy.shutdown()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    rows=[json.loads(line) for line in log_path.read_text().splitlines()]
    decisions=[r for r in rows if r.get('kind')=='decision']
    assert len(decisions)==8 and all(r['seat']=='A' and r['stage']=='2' for r in decisions)
    assert decisions[5]['reason']=='P1_event_deadline_escape'
    assert decisions[7]['reason']=='P3_defend'
    assert decisions[5]['ai_local_success'] is True
    assert decisions[5]['ai_http_status']==200 and decisions[5]['ai_error']==''
    assert all(r['invalid_actions']==0 and r['respawn_in']==0 for r in decisions)
    assert 'fixed-fake-token' not in log_path.read_text()
    return {'passed':True,'actions':checked,'http_requests':calls,'log_fields_verified':True,
            'pending_request_actions':pending_request_actions,'model_response_released_after_control_check':True,
            'formal_qualification_claimed':False,'referee_used':False,'real_model_calls':0,
            'latency_conclusion':None,'clean_shutdown':True}

if __name__=='__main__':
    print(json.dumps(run(),ensure_ascii=False))
