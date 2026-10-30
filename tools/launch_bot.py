"""Preserve arbitrary legal UTF-8 team names as string-valued ROS parameters."""
import json, os, stat, sys, tempfile, hashlib
from pathlib import Path

def acquire_seat_lock(seat, domain):
    """Keep the lock descriptor across exec so the actual node owns its lifetime."""
    import fcntl
    if seat not in ('A','B','C','D') or not domain.isascii() or not domain.isdigit() or len(domain)>10:
        raise ValueError('Invalid seat or ROS domain')
    folder=Path(tempfile.gettempdir())/('rmtc-bot-'+str(os.getuid()))
    folder.mkdir(mode=0o700,exist_ok=True)
    info=folder.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o077:
        raise ValueError('Unsafe instance-lock directory')
    fd=os.open(folder/('domain-'+str(int(domain))+'-seat-'+seat+'.lock'),os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW,0o600)
    try:
        info=os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o077:
            raise ValueError('Unsafe instance-lock file')
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        os.set_inheritable(fd,True)
        return fd
    except BaseException:
        os.close(fd)
        raise

LOOPBACK_PROFILE_SHA256='4dc817a7c49d34467810b362e2d01127f4f2d1185d1f11abf847682010f303fe'

def loopback_profile(root):
    path=Path(root)/'transport/loopback-udp.xml'
    if hashlib.sha256(path.read_bytes()).hexdigest()!=LOOPBACK_PROFILE_SHA256:
        raise ValueError('Invalid loopback DDS profile')
    return str(path.resolve())

def publication_mode(environment):
    """Keep announced stage-1 actions out of the asynchronous sender queue.

    This is a transport choice, not an action/tick/scoring policy. Explicit
    supported deployment overrides remain available for network validation.
    """
    override=environment.get('RMW_FASTRTPS_PUBLICATION_MODE')
    if override:
        if override not in ('SYNCHRONOUS','ASYNCHRONOUS','AUTO'):
            raise ValueError('Invalid DDS publication mode')
        return override
    return 'SYNCHRONOUS' if environment.get('RMTC_STAGE')=='1' else 'ASYNCHRONOUS'

def arguments(seat,team,mode,policy,dispatch):
    if seat not in ('A','B','C','D') or not 1<=len(team.encode('utf-8'))<=96 or '\n' in team or '\r' in team:
        raise ValueError('Invalid seat or team name')
    return ['--ros-args','-p','seat:='+seat,'-p','team_name:='+json.dumps(team,ensure_ascii=False),
            '-p','mode:='+mode,'-p','policy:='+policy,'-p','dispatch:='+dispatch]

if __name__=='__main__':
    if len(sys.argv)!=4:raise SystemExit('Usage: launch_bot.py seat team referee_ip')
    try:
        os.environ['RMW_FASTRTPS_PUBLICATION_MODE']=publication_mode(os.environ)
        args=arguments(sys.argv[1],sys.argv[2],os.environ.get('RMTC_RUN_MODE','collect'),
            os.environ.get('RMTC_RESEARCH_POLICY','greedy'),os.environ.get('RMTC_RESEARCH_DISPATCH','immediate'))
        lock=acquire_seat_lock(sys.argv[1],os.environ.get('ROS_DOMAIN_ID','0'))
        binary=Path(__file__).resolve().parent.parent/'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
        if not binary.is_file() or not os.access(binary,os.X_OK):raise ValueError('Installed contestant executable missing')
        os.execv(str(binary),[str(binary)]+args)
    except BlockingIOError:raise SystemExit('This ROS domain and seat already have a running contestant')
    except (ValueError,OSError):raise SystemExit('Contestant launch validation failed')
