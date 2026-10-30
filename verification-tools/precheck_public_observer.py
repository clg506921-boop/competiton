"""Separate evidence observer. Subscribes only own public topics and team registry."""
import json,os,signal,sys,time
from pathlib import Path
import rclpy
from rclpy.qos import QoSProfile,ReliabilityPolicy,DurabilityPolicy
from std_msgs.msg import String

folder=Path(sys.argv[1]);seat=sys.argv[2]
if seat not in 'ABCD' or len(seat)!=1:raise SystemExit('Invalid seat')
signal.signal(signal.SIGTERM,lambda *_:(_ for _ in ()).throw(KeyboardInterrupt()))
rclpy.init();node=rclpy.create_node('rmtc_public_precheck_evidence')
latest={'seat':seat,'actions':[],'registry':None,'perception':None,'parse_errors':0,'subscriptions':[]}
stream=(folder/'public-messages.jsonl').open('x',encoding='utf8')
def receive(kind,msg):
    try:
        data=json.loads(msg.data)
        stream.write(json.dumps({'kind':kind,'monotonic':time.monotonic(),'data':data},ensure_ascii=False)+'\n');stream.flush()
        if kind=='action':latest['actions']=(latest['actions']+[data])[-128:]
        else:latest[kind]=data
        temp=folder/'latest.tmp'
        temp.write_text(json.dumps(latest,ensure_ascii=False),encoding='utf8');os.replace(temp,folder/'latest.json')
    except Exception:latest['parse_errors']+=1
volatile=QoSProfile(depth=1,reliability=ReliabilityPolicy.RELIABLE,durability=DurabilityPolicy.VOLATILE)
retained=QoSProfile(depth=1,reliability=ReliabilityPolicy.RELIABLE,durability=DurabilityPolicy.TRANSIENT_LOCAL)
subscriptions=[]
for kind,topic,qos in [('perception',f'/team_{seat}/perception',volatile),('action',f'/team_{seat}/action',volatile),
 ('registry','/referee/team_registry',retained)]:
    latest['subscriptions'].append(topic)
    subscriptions.append(node.create_subscription(String,topic,lambda msg,kind=kind:receive(kind,msg),qos))
(folder/'observer-ready.json').write_text(json.dumps({'ready':True,'topics':latest['subscriptions']}))
try:
    while rclpy.ok():rclpy.spin_once(node,timeout_sec=.1)
except KeyboardInterrupt:pass
finally:
    stream.close();node.destroy_node()
    if rclpy.ok():rclpy.shutdown()
