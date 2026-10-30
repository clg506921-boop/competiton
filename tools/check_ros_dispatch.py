"""Synthetic public-message transport check in an isolated domain, NOT a referee game."""
from pathlib import Path
import json, os, signal, subprocess, time
import rclpy
from rclpy.qos import QoSProfile,ReliabilityPolicy,DurabilityPolicy
from std_msgs.msg import String
from launch_bot import arguments

def run():
    if os.environ.get('ROS_LOCALHOST_ONLY')!='1' or os.environ.get('ROS_DOMAIN_ID')!='38':
        raise RuntimeError('This test requires localhost-only isolated domain 38')
    root=Path(__file__).resolve().parent.parent
    binary=root/'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
    results=[]
    rclpy.init()
    try:
        for dispatch in ['immediate','mailbox']:
            node=rclpy.create_node('synthetic_transport_probe')
            qos=QoSProfile(depth=1,reliability=ReliabilityPolicy.RELIABLE,durability=DurabilityPolicy.VOLATILE)
            received=[];callback_errors=[]
            def callback(msg):
                try:received.append(json.loads(msg.data))
                except Exception as e:callback_errors.append(type(e).__name__)
            sub=node.create_subscription(String,'/team_A/action',callback,qos)
            pub=node.create_publisher(String,'/team_A/perception',qos)
            env=dict(os.environ,RMTC_STAGE='1',RMTC_LOG_FILE=str(root/f'synthetic-{dispatch}.jsonl'))
            proc=subprocess.Popen([str(binary)]+arguments('A',"001 '测试'",'wait','greedy',dispatch),env=env,
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,start_new_session=True)
            def spin_until(predicate,seconds=3):
                until=time.monotonic()+seconds
                while time.monotonic()<until:
                    rclpy.spin_once(node,timeout_sec=.02)
                    if predicate():return True
                    if proc.poll() is not None:raise RuntimeError('Bot exited before transport completed')
                return predicate()
            try:
                assert spin_until(lambda:pub.get_subscription_count()>0 and node.count_publishers('/team_A/action')>0,8),'DDS discovery timeout'
                base={'tick':0,'session_id':'synthetic-'+dispatch,'vision_radius':4,
                    'self':{'team':'A','robot_id':'A','position':[1,1],'hp':100,'alive':True,'protected':False,'last_action':None},
                    'map':{'width':20,'height':20},'visible_entities':[]}
                def send(frame):pub.publish(String(data=json.dumps(frame)))
                for tick in (0,1,2):
                    frame=dict(base,tick=tick);before=len(received);send(frame)
                    assert spin_until(lambda:len(received)>before),'Missing action'
                    action=received[-1]
                    assert action=={'tick':tick,'session_id':base['session_id'],'action':'wait'}
                before=len(received);send(dict(base,tick=1));spin_until(lambda:len(received)>before,.2)
                assert len(received)==before,'Old Tick was republished'
                send(dict(base,tick=True));spin_until(lambda:len(received)>before,.2)
                assert len(received)==before,'Invalid Tick was published'
                send(dict(base,tick=2));assert spin_until(lambda:len(received)>before),'Duplicate Tick did not resend cache'
                assert received[-1]['tick']==2 and not callback_errors
                results.append({'dispatch':dispatch,'passed':True,'current_tick_actions':3,'stale_and_invalid_ignored':True,'duplicate_cached':True})
            finally:
                if proc.poll() is None:os.killpg(proc.pid,signal.SIGINT)
                try:out,_=proc.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid,signal.SIGKILL);out,_=proc.communicate(timeout=3)
                node.destroy_node()
                if proc.returncode!=0:raise RuntimeError('Bot did not exit cleanly: '+out[-1500:])
    finally:rclpy.shutdown()
    return {'synthetic_ros_cases':results,'official_referee_used':False,'real_model_calls':0,'timing_conclusion':None}

if __name__=='__main__':print(json.dumps(run(),ensure_ascii=False))
