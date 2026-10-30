import json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest import mock
import match_setup as setup

def valid():
    return {'seat':'B','stage':'2','team_name':'肥鱼说的都队','referee_host':'127.0.0.1',
        'ros_domain_id':27,'network':'local','gateway_url':'http://127.0.0.1:8080',
        'team_token':'setup-fake-private-marker','model':'practice-mock','max_tokens':256,'ai_quota':100}

class MatchSetupTests(unittest.TestCase):
    def test_announced_stage_selects_transport_in_both_preflight_and_launch(self):
        from launch_bot import publication_mode
        for stage,expected in [('1','SYNCHRONOUS'),('2','ASYNCHRONOUS'),('auto','ASYNCHRONOUS')]:
            value=valid();value.update(stage=stage,seat='A')
            env=setup.prepare(value,{})
            self.assertEqual(env['RMW_FASTRTPS_PUBLICATION_MODE'],expected)
            self.assertEqual(publication_mode(env),expected)

    def test_explicit_deployment_override_is_validated_without_echoing_it(self):
        for mode in ('SYNCHRONOUS','ASYNCHRONOUS','AUTO'):
            self.assertEqual(setup.prepare(valid(),{'RMW_FASTRTPS_PUBLICATION_MODE':mode})['RMW_FASTRTPS_PUBLICATION_MODE'],mode)
        with self.assertRaisesRegex(setup.SetupError,'dds_publication_mode'):
            setup.prepare(valid(),{'RMW_FASTRTPS_PUBLICATION_MODE':'unsupported-private-marker'})

    def test_local_transport_uses_sealed_loopback_profile_and_ros_qos(self):
        env=setup.prepare(valid(),{'FASTRTPS_DEFAULT_PROFILES_FILE':'stale.xml','FASTDDS_DEFAULT_PROFILES_FILE':'stale-new.xml','RMW_FASTRTPS_USE_QOS_FROM_XML':'1'})
        self.assertEqual(env['FASTRTPS_DEFAULT_PROFILES_FILE'],setup.loopback_profile(setup.ROOT))
        self.assertNotIn('FASTDDS_DEFAULT_PROFILES_FILE',env)
        self.assertEqual(env['ROS_LOCALHOST_ONLY'],'0')
        self.assertEqual(env['RMW_FASTRTPS_USE_QOS_FROM_XML'],'0')

    def test_wired_launch_cannot_accidentally_retain_own_loopback_profile(self):
        value=valid();value['network']='wired'
        env=setup.prepare(value,{'FASTRTPS_DEFAULT_PROFILES_FILE':setup.loopback_profile(setup.ROOT)})
        self.assertEqual(env['ROS_LOCALHOST_ONLY'],'0')
        self.assertNotIn('FASTRTPS_DEFAULT_PROFILES_FILE',env)

    def test_preflight_defaults_to_the_file_used_by_launch(self):
        for base in ({}, {'RMTC_DECISION_CONFIG':''}):
            with self.subTest(base=base):
                self.assertEqual(setup.prepare(valid(),base)['RMTC_DECISION_CONFIG'],str(setup.ROOT/'decision_config.json'))

    def test_explicit_registered_decision_file_is_preserved(self):
        self.assertEqual(setup.prepare(valid(),{'RMTC_DECISION_CONFIG':'/run/rmtc/registered.json'})['RMTC_DECISION_CONFIG'],
            '/run/rmtc/registered.json')

    def test_reviewed_v12_and_legacy_practice_rules_are_accepted(self):
        self.assertEqual(setup.SUPPORTED_RULES,'v1.2')
        for version in ('v1.1','v1.2'):
            value=valid();value['rules_version']=version
            self.assertEqual(setup.prepare(value,{})['RMTC_STAGE'],'2')
    def test_unknown_rules_are_still_rejected(self):
        for version in ('v1.3','v2.0','',None,True,[]):
            value=valid();value['rules_version']=version
            with self.subTest(version=version),self.assertRaisesRegex(setup.SetupError,'unsupported_rules_version'):setup.prepare(value,{})
    def test_current_match_replaces_stale_connection_without_shell_interpolation(self):
        env=setup.prepare(valid(),{'RMTC_CONNECTION_CONFIG':'old.json','RMTC_AI_TOKEN':'old-private',
            'RMTC_TEAM_TOKEN':'old-private','RMTC_AI_URL':'old','RMTC_RESEARCH_POLICY':'frontier','ROS_DISTRO':'humble'})
        self.assertEqual(env['RMTC_TEAM_TOKEN'],'setup-fake-private-marker')
        self.assertEqual(env['RMTC_AI_URL'],'http://127.0.0.1:8080/v1/chat/completions')
        self.assertEqual(env['ROS_LOCALHOST_ONLY'],'0');self.assertEqual(env['FASTRTPS_DEFAULT_PROFILES_FILE'],setup.loopback_profile(setup.ROOT));self.assertEqual(env['ROS_DOMAIN_ID'],'27')
        self.assertNotIn('RMTC_CONNECTION_CONFIG',env);self.assertNotIn('RMTC_AI_TOKEN',env);self.assertNotIn('RMTC_RESEARCH_POLICY',env)

    def test_unlimited_quota_and_announced_timing_and_diagnostic(self):
        value=valid();value.update(ai_quota=0,max_ticks=600,tick_ms=200)
        env=setup.prepare(value,{})
        self.assertEqual(env['RMTC_AI_QUOTA'],'0');self.assertEqual(env['RMTC_MAX_TICKS'],'600')
        self.assertEqual(env['RMTC_TICK_MS'],'200')
        value.update(stage='0',seat='D');value.pop('max_tokens');value.pop('team_token')
        self.assertNotIn('RMTC_TEAM_TOKEN',setup.prepare(value,{}))

    def test_unknown_limit_is_a_named_gap_never_an_invented_default(self):
        value=valid();value['max_tokens']=None
        with self.assertRaisesRegex(setup.SetupError,'max_tokens'):setup.prepare(value,{})

    def test_missing_model_explicitly_uses_official_flash_but_does_not_invent_limit(self):
        value=valid();value['model']=None;env=setup.prepare(value,{})
        self.assertEqual(env['RMTC_AI_MODEL'],'deepseek-flash');self.assertEqual(env['RMTC_AI_ALLOW_DEFAULT_MODEL'],'0')
        value['max_tokens']=None
        with self.assertRaisesRegex(setup.SetupError,'max_tokens'):setup.prepare(value,{})

    def test_unsupported_formal_models_fail_before_any_request(self):
        for model in ('deepseek-v4-pro','deepseek-chat','custom-model',True,[]):
            value=valid();value['model']=model
            with self.subTest(model=model),self.assertRaisesRegex(setup.SetupError,'model'):setup.prepare(value,{})

    def test_official_flash_is_preserved_and_endpoint_is_chat_completions(self):
        value=valid();value['model']='deepseek-flash';env=setup.prepare(value,{})
        self.assertEqual(env['RMTC_AI_MODEL'],'deepseek-flash')
        self.assertEqual(env['RMTC_AI_URL'],'http://127.0.0.1:8080/v1/chat/completions')
        self.assertEqual(env['RMTC_AI_ALLOW_DEFAULT_MODEL'],'0')

    def test_first_stage_requires_no_ai_and_uses_official_seat_A(self):
        value={k:v for k,v in valid().items() if k not in ('gateway_url','team_token','model','max_tokens','ai_quota')}
        value.update(stage='1',seat='A');env=setup.prepare(value,{'RMTC_TEAM_TOKEN':'stale'})
        self.assertNotIn('RMTC_TEAM_TOKEN',env)
        value['seat']='B'
        with self.assertRaisesRegex(setup.SetupError,'stage1_requires_seat_A'):setup.prepare(value,{})

    def test_types_unknown_fields_and_unsafe_authority_are_rejected(self):
        for key,bad in [('max_tokens',True),('ai_quota',-1),('ros_domain_id',True),('ros_domain_id',233),
            ('gateway_url','http://private-marker@localhost:8080'),('gateway_url','http://:8080'),('referee_host','host;echo marker'),('decision_type','collect')]:
            value=valid();value[key]=bad
            with self.subTest(field=key),self.assertRaises(setup.SetupError):setup.prepare(value,{})

    def test_duplicate_json_keys_and_parser_errors_do_not_echo_values(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'settings.json';p.write_text('{"seat":"private-marker","seat":"B"}')
            with self.assertRaisesRegex(setup.SetupError,'duplicate_json_key'):setup.load(p)
            p.write_text('{"private-marker":')
            with self.assertRaisesRegex(setup.SetupError,'cannot_read_settings'):setup.load(p)

    def test_real_cli_check_does_not_launch_ros_write_files_or_expose_token(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'settings.json';p.write_text(json.dumps(valid()),encoding='utf8')
            env=dict(os.environ,ROS_DISTRO='humble',RMTC_LOG_FILE=str(Path(folder)/'must-not-exist.jsonl'))
            before={x.name for x in Path(folder).iterdir()}
            result=subprocess.run([sys.executable,'-B',str(setup.ROOT/'tools/match_setup.py'),'--check',str(p)],
                env=env,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertTrue(json.loads(result.stdout)['passed']);self.assertNotIn('setup-fake-private-marker',result.stdout+result.stderr)
            self.assertEqual(before,{x.name for x in Path(folder).iterdir()})

if __name__=='__main__':unittest.main()
