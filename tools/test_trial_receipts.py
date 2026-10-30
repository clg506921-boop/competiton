import json,tempfile,unittest
from pathlib import Path
import run_official_trial as trial

class RespawnReceiptAuditTests(unittest.TestCase):
    def scenario(self,variant):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);own=root/'bot.jsonl'
            state={'position_reason':'respawn','alive':True,'protected':True,'last_action':{'tick':0,'action':'wait','moved':False}}
            tick=1
            if variant=='unprotected':state['protected']=False
            if variant=='not_respawn':state.pop('position_reason')
            if variant=='delayed':tick=2
            if variant=='protocol_error':state['last_action']['error']='E_TICK'
            action={'session_id':'test','tick':0,'action':'move','direction':'right'}
            communication=[{'kind':'action','seat':'B','data':action},{'kind':'perception','seat':'B','data':{'session_id':'test','tick':tick,'self':state}}]
            (root/'communication.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in communication))
            rows=[{'kind':'decision','session_id':'test','seat':'B','tick':0,'action':'move','published':True,'application_us':100},
                  {'kind':'decision','session_id':'test','seat':'B','tick':tick,'action':'wait','published':False,'application_us':100,
                   'receipt_fresh':True,'receipt_tick':0,'receipt_action':'wait','receipt_matched':True,'expected_receipt_action':'move',
                   'receipt_status':'state_cancelled','position_reason':state.get('position_reason'),'alive':True,'protected_now':state['protected'],
                   'receipt_error':state['last_action'].get('error','')}]
            own.write_text(''.join(json.dumps(r)+'\n' for r in rows))
            return trial.analyze(root,own,'test',1,2,'B')

    def test_explicit_immediate_protected_respawn_without_error_is_associated(self):
        report=self.scenario('valid')
        self.assertEqual(report['primary_associated_receipts'],1)
        self.assertEqual(report['associated_receipts'],1)
        self.assertEqual(report['primary_mismatched_ticks'],[])
        self.assertEqual(report['mismatched_receipt_ticks'],[])

    def test_missing_reset_evidence_or_protocol_error_remains_a_failure(self):
        for variant in ['unprotected','not_respawn','delayed','protocol_error']:
            with self.subTest(variant=variant):
                report=self.scenario(variant)
                self.assertEqual(report['primary_associated_receipts'],0)
                self.assertEqual(report['primary_mismatched_ticks'],[0])
                self.assertEqual(report['associated_receipts'],0)

if __name__=='__main__':unittest.main()
