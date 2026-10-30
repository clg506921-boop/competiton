"""Render a frozen REAL public-message snapshot, capture the actual X11 window as PNG.
This is an evidence viewer, explicitly not an official referee screen or live full map.
No perception/action is published and no model is called.
"""
import hashlib,json,os,struct,subprocess,sys,time,tkinter as tk,zlib
from pathlib import Path
folder=Path(sys.argv[1]);phase=sys.argv[2]
snapshot=folder/(phase+'-public-snapshot.json')
value=json.loads(snapshot.read_text());p=value['perception'];seat=value['seat'];self_state=p['self']
score=p.get('score',{})
receipt=self_state.get('last_action');actions=value.get('actions',[])
matching=next((a for a in reversed(actions) if receipt and a.get('tick')==receipt.get('tick')),None)
registry=value.get('registry')
meta=json.loads((folder/'capture-metadata.json').read_text())
lines=[
 'RMTC 前置检录：公开消息证据查看器',
 '真实 ROS 观测的冻结快照；本窗口不是裁判界面，不包含观赛全图。',
 '队名：肥鱼说的都队   席位：'+seat+'   阶段：1   状态：'+phase,
 '系统：Ubuntu 22.04 / ROS 2 Humble / C++17',
 '恢复来源：rmtc-bot-v0.9-2026-10-22.zip',
 'ZIP SHA256: '+meta['archive_sha256'],
 'Node SHA256: '+meta['binary_sha256'],
 '',
 '订阅：'+', '.join(value['subscriptions']),
 'session_id: '+p['session_id'],
 '当前感知 tick: '+str(p['tick'])+'    self.position: '+json.dumps(self_state.get('position')),
 'score.invalid_actions: '+str(score.get('invalid_actions','本帧未提供')),
 'score.resource_value: '+str(score.get('resource_value','本帧未提供')),
 '',
 '实际观察的动作（按回执 Tick 关联）：',json.dumps(matching,ensure_ascii=False),
 '',
 '下一帧 self.last_action（官方执行回执）：',json.dumps(receipt,ensure_ascii=False),
 '',
 '公开 /referee/team_registry：',json.dumps(registry,ensure_ascii=False),
 '',
 '只读证据文件：'+snapshot.name,
 'Snapshot SHA256: '+hashlib.sha256(snapshot.read_bytes()).hexdigest(),
 '不输入人工动作；不解析公告语义；本次真实 AI 调用 0。',
]
window=tk.Tk();window.title('RMTC public ROS evidence - '+phase)
window.geometry('1400x920+30+40');window.configure(background='#101820');window.attributes('-topmost',True)
text=tk.Text(window,font=('Noto Sans CJK SC',12),background='#101820',foreground='#e4edf6',
    wrap='word',borderwidth=0,padx=22,pady=18)
text.pack(fill='both',expand=True);text.insert('1.0','\n'.join(lines));text.configure(state='disabled')
error=[]
def capture():
    try:
        window.update_idletasks()
        rawpath=folder/(phase+'-window.xwd')
        subprocess.run(['xwd','-silent','-id',str(window.winfo_id()),'-out',str(rawpath)],check=True,timeout=10)
        data=rawpath.read_bytes();h=struct.unpack('>25I',data[:100])
        header,version,fmt,depth,width,height=h[:6];order=h[7];bits=h[11];stride=h[12]
        red,green,blue=h[14:17];ncolors=h[19]
        assert version==7 and fmt==2 and bits in (24,32) and width<=4096 and height<=4096
        offset=header+ncolors*12;bytespp=bits//8
        def component(pixel,mask):
            shift=(mask&-mask).bit_length()-1;return ((pixel&mask)>>shift)*255//(mask>>shift)
        rows=[]
        for y in range(height):
            row=bytearray(b'\0')
            start=offset+y*stride
            for x in range(width):
                p=int.from_bytes(data[start+x*bytespp:start+(x+1)*bytespp],'little' if order==0 else 'big')
                row.extend((component(p,red),component(p,green),component(p,blue)))
            rows.append(row)
        def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')
        output=folder/(phase+'-ros-evidence.png');output.write_bytes(png)
        (folder/(phase+'-capture.json')).write_text(json.dumps({'passed':True,'kind':'actual X11 window capture of a frozen public ROS evidence viewer',
            'snapshot_sha256':hashlib.sha256(snapshot.read_bytes()).hexdigest(),'png_sha256':hashlib.sha256(png).hexdigest(),
            'width':width,'height':height,'image_edited':False,'not_official_referee_UI':True}))
    except Exception as e:error.append(type(e).__name__)
    finally:window.destroy()
window.after(900,capture);window.mainloop()
if error:raise SystemExit('Window capture failed: '+error[0])
