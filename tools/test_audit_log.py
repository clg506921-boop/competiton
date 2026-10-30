"""New audit-only fixtures; not a replay of prior ROS/HTTP scenarios."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from audit_log import audit


def decision(tick, run='first', **fields):
    return dict(kind='decision', run_id=run, seat='B', session_id='one-session', tick=tick,
                action='wait', duplicate=False, application_us=100 + tick, **fields)


class AuditTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / 'test.jsonl'

    def tearDown(self):
        self.directory.cleanup()

    def write(self, rows):
        self.path.write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8', newline='\n')

    def test_restart_gap_and_execution_failures_are_distinct(self):
        a = decision(40)
        b = decision(43, receipt_fresh=True, receipt_matched=True, receipt_status='state_cancelled',
                     receipt_error='E_STATE', receipt_expected=True, receipt_missing=False)
        duplicate = dict(b, duplicate=True)
        c = decision(48, run='second', receipt_fresh=True, receipt_matched=False, receipt_status='unconfirmed')
        self.write([a, b, duplicate, c])
        report = audit(self.path)
        self.assertTrue(report['summary_complete'])
        self.assertEqual(report['counts']['missing_observation_ticks'], 2)
        self.assertEqual(report['counts']['duplicate_resends'], 1)
        self.assertEqual(report['receipt_statuses'], {'state_cancelled': 1, 'unconfirmed': 1})
        self.assertEqual(report['receipt_association_rate'], .5)
        self.assertEqual(report['timing']['unique_samples'], 3)
        self.assertEqual(report['timing']['p99_us'], 148)

    def test_cli_reports_corruption_without_echoing_input_and_fails(self):
        self.write([decision(1), dict(decision(2), tick=True),
                    decision(3, receipt_error='Bearer SECRET-fixture')])
        with self.path.open('ab') as f:
            f.write(b'{"kind":"SECRET-fixture","kind":"decision"}\n')
        p = subprocess.run([sys.executable, str(Path(__file__).with_name('audit_log.py')), str(self.path)],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 1)
        self.assertNotIn('SECRET-fixture', p.stdout + p.stderr)
        report = json.loads(p.stdout)
        self.assertEqual(report['counts']['integrity_errors'], 3)

    def test_cli_creates_lf_report_without_overwriting_input(self):
        self.write([decision(10)])
        original = self.path.read_bytes()
        tool = str(Path(__file__).with_name('audit_log.py'))
        p = subprocess.run([sys.executable, tool, str(self.path), '--output', str(self.path)], capture_output=True)
        self.assertEqual(p.returncode, 2)
        self.assertEqual(self.path.read_bytes(), original)
        out = self.path.with_suffix('.report.json')
        p = subprocess.run([sys.executable, tool, str(self.path), '--output', str(out)], capture_output=True)
        self.assertEqual(p.returncode, 0)
        self.assertNotIn(b'\r\n', out.read_bytes())


if __name__ == '__main__':
    unittest.main()
