"""New CLI/launcher boundaries only; does not run a ROS scenario or a match."""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools'))
import launch_bot
import preflight


class SeatLockTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='rmtc-new-startup-')
        self.base=Path(self.temp.name)
        self.patch=mock.patch.object(launch_bot.tempfile,'gettempdir',return_value=str(self.base))
        self.patch.start()
        self.fds=[]

    def tearDown(self):
        for fd in self.fds:os.close(fd)
        self.patch.stop()
        self.temp.cleanup()

    def lock(self,seat,domain):
        fd=launch_bot.acquire_seat_lock(seat,domain);self.fds.append(fd);return fd

    def test_same_seat_domain_is_exclusive(self):
        self.lock('A','27')
        with self.assertRaises(BlockingIOError):self.lock('A','27')

    def test_other_seats_and_domains_are_independent(self):
        self.lock('A','27');self.lock('B','27');self.lock('A','28')

    def test_leading_zero_domain_is_canonical(self):
        self.lock('A','027')
        with self.assertRaises(BlockingIOError):self.lock('A','27')

    def test_invalid_domain_and_seat_cannot_form_paths(self):
        for seat,domain in [('A','../27'),('../A','27'),('A','２７'),('A',''),('A','9'*11)]:
            with self.assertRaises(ValueError):self.lock(seat,domain)

    def test_directory_symlink_and_public_mode_rejected(self):
        folder=self.base/('rmtc-bot-'+str(os.getuid()))
        actual=self.base/'actual';actual.mkdir(mode=0o700);folder.symlink_to(actual)
        with self.assertRaises(ValueError):self.lock('A','27')
        folder.unlink();folder.mkdir(mode=0o755);folder.chmod(0o755)
        with self.assertRaises(ValueError):self.lock('A','27')

    def test_lock_symlink_cannot_be_followed(self):
        folder=self.base/('rmtc-bot-'+str(os.getuid()));folder.mkdir(mode=0o700)
        target=self.base/'sentinel';target.write_text('keep')
        (folder/'domain-27-seat-A.lock').symlink_to(target)
        with self.assertRaises(OSError):self.lock('A','27')
        self.assertEqual(target.read_text(),'keep')

    def test_exec_child_owns_lock_until_exit(self):
        # Exercise the real launcher exec path against a harmless fake executable.
        fixture=self.base/'fixture';(fixture/'tools').mkdir(parents=True)
        shutil.copyfile(ROOT/'tools/launch_bot.py',fixture/'tools/launch_bot.py')
        binary=fixture/'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
        binary.parent.mkdir(parents=True)
        binary.write_text('#!/usr/bin/python3\nimport json,sys\nprint(json.dumps(sys.argv[1:]),flush=True)\nsys.stdin.read(1)\n')
        binary.chmod(0o700)
        env=dict(os.environ,TMPDIR=str(self.base),ROS_DOMAIN_ID='27')
        p=subprocess.Popen([sys.executable,str(fixture/'tools/launch_bot.py'),'A','肥鱼说的都队','127.0.0.1'],
            env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            import select
            self.assertTrue(select.select([p.stdout],[],[],5)[0],'fake node did not start')
            args=json.loads(p.stdout.readline())
            self.assertIn('team_name:='+json.dumps('肥鱼说的都队',ensure_ascii=False),args)
            with self.assertRaises(BlockingIOError):self.lock('A','27')
            p.communicate('x',timeout=5);self.assertEqual(p.returncode,0)
            self.lock('A','27')
        finally:
            if p.poll() is None:p.kill();p.communicate(timeout=5)


class ConfigCheckCliTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='rmtc-config-cli-')
        self.folder=Path(self.temp.name)
        self.binary=ROOT/'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
        self.env={k:v for k,v in os.environ.items() if not k.startswith('RMTC_')}
        self.env.pop('ROS_DOMAIN_ID',None)
        self.env.update(RMTC_STAGE='1',RMTC_LOG_FILE=str(self.folder/'must-not-exist.jsonl'))

    def tearDown(self):self.temp.cleanup()

    def invoke(self,extra=None,code=0):
        env=dict(self.env);env.update(extra or {})
        p=subprocess.run([str(self.binary),'--check-config'],env=env,capture_output=True,text=True,timeout=5)
        self.assertEqual(p.returncode,code,p.stdout+p.stderr)
        self.assertFalse(p.stderr)
        data=json.loads(p.stdout)
        self.assertIs(data['ros_initialized'],False);self.assertEqual(data['network_requests'],0)
        self.assertFalse((self.folder/'must-not-exist.jsonl').exists())
        return data,p.stdout

    def test_stage1_works_without_ai(self):
        value,_=self.invoke();self.assertTrue(value['configuration_valid']);self.assertFalse(value['ai_configured'])

    def test_stage2_missing_ai_is_not_ready(self):
        value,_=self.invoke({'RMTC_STAGE':'2'},3);self.assertFalse(value['stage2_ready'])

    def test_valid_ai_checks_never_connect_or_print_credentials(self):
        with socket.socket() as listener:
            listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(.05)
            value,raw=self.invoke({'RMTC_STAGE':'2','RMTC_AI_URL':'http://127.0.0.1:'+str(listener.getsockname()[1])+'/v1/chat/completions',
                'RMTC_TEAM_TOKEN':'test-only-private-marker','RMTC_AI_MODEL':'test-model','RMTC_AI_MAX_TOKENS':'256'})
            self.assertTrue(value['ai_configured']);self.assertTrue(value['stage2_ready'])
            self.assertNotIn('test-only-private-marker',raw)
            with self.assertRaises(socket.timeout):listener.accept()

    def test_auto_missing_ai_and_missing_limit_are_not_ready(self):
        value,_=self.invoke({'RMTC_STAGE':'auto'},3)
        self.assertTrue(value['ai_required']);self.assertFalse(value['ready_for_selected_stage'])
        value,_=self.invoke({'RMTC_STAGE':'auto','RMTC_AI_URL':'http://127.0.0.1:8080/v1/chat/completions',
            'RMTC_TEAM_TOKEN':'fake-only-marker','RMTC_AI_MODEL':'test-model'},3)
        self.assertFalse(value['ai_configured'])

    def test_bad_gateway_host_is_not_ready_and_is_not_echoed(self):
        value,raw=self.invoke({'RMTC_STAGE':'2','RMTC_AI_URL':'http://:8080/v1/chat/completions',
            'RMTC_TEAM_TOKEN':'fake-only-marker','RMTC_AI_MODEL':'test-model','RMTC_AI_MAX_TOKENS':'256'},3)
        self.assertFalse(value['ai_configured']);self.assertNotIn('fake-only-marker',raw)

    def test_actual_auto_startup_without_ai_exits_before_ros_or_log(self):
        env=dict(self.env,RMTC_STAGE='auto')
        result=subprocess.run([str(self.binary)],env=env,capture_output=True,text=True,timeout=5)
        self.assertEqual(result.returncode,3);self.assertFalse(result.stdout)
        self.assertIn('AI connection is required',result.stderr)
        self.assertFalse((self.folder/'must-not-exist.jsonl').exists())

    def test_invalid_config_is_sanitized(self):
        path=self.folder/'bad.json';path.write_text('{"secret-error-marker":true}')
        value,raw=self.invoke({'RMTC_CONNECTION_CONFIG':str(path)},2)
        self.assertFalse(value['configuration_valid']);self.assertNotIn('secret-error-marker',raw)

    def test_duplicate_keys_are_rejected_before_ros(self):
        path=self.folder/'duplicate.json';path.write_text('{"stage":"1","stage":"2"}')
        self.invoke({'RMTC_CONNECTION_CONFIG':str(path)},2)

    def test_launch_only_flags_are_validated_before_ros(self):
        for name,value in [('RMTC_RUN_MODE','bad'),('RMTC_RESEARCH_POLICY','bad'),('RMTC_RESEARCH_DISPATCH','bad')]:
            self.invoke({name:value},2)

    def test_preflight_reports_required_connection_gaps(self):
        with mock.patch.dict(os.environ,self.env,clear=True):
            value=preflight.run(ROOT,'A','肥鱼说的都队','127.0.0.1','wired')
        self.assertFalse(value['passed']);self.assertTrue(value['read_only'])
        failed={c['check'] for c in value['checks'] if not c['passed']}
        self.assertIn('announced_ros_domain_supplied',failed)

    def test_preflight_accepts_local_metadata_without_starting_ros(self):
        env=dict(self.env,ROS_DOMAIN_ID='27',ROS_LOCALHOST_ONLY='1',ROS_DISTRO='humble')
        with mock.patch.dict(os.environ,env,clear=True):
            value=preflight.run(ROOT,'A','肥鱼说的都队','127.0.0.1','local')
        self.assertTrue(value['passed'],value);self.assertFalse(value['ros_started'])
        self.assertIn('official action receipt',value['not_verified'])

    def test_direct_preflight_creates_no_python_cache_or_logs(self):
        fixture=self.folder/'readonly-fixture';(fixture/'tools').mkdir(parents=True)
        for name in ('preflight.py','launch_bot.py'):shutil.copyfile(ROOT/'tools'/name,fixture/'tools'/name)
        binary=fixture/'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
        binary.parent.mkdir(parents=True);binary.symlink_to(self.binary)
        before={p.relative_to(fixture) for p in fixture.rglob('*')}
        env=dict(self.env,ROS_DOMAIN_ID='27',ROS_LOCALHOST_ONLY='1',ROS_DISTRO='humble')
        p=subprocess.run([sys.executable,str(fixture/'tools/preflight.py'),'--seat','A','--network','local',
            '--referee-ip','127.0.0.1'],env=env,capture_output=True,text=True,timeout=5)
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertTrue(json.loads(p.stdout)['passed'])
        self.assertEqual(before,{p.relative_to(fixture) for p in fixture.rglob('*')})


if __name__=='__main__':unittest.main(verbosity=2)
