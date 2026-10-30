"""Pure ledger tests; cannot start ROS, a referee or an HTTP request."""
import json,tempfile,unittest
from pathlib import Path
from verification_budget import load_match_counts,merge_counts,code_iteration,match_ledger_path

class VerificationBudgetTests(unittest.TestCase):
    def test_restored_empty_runtime_retains_all_spent_counts(self):
        counts={'own-stage1-default':1,'own-stage1-size20':1,'own-stage1-precheck-B-v09':1}
        self.assertEqual(merge_counts(counts,{}),counts)

    def test_runtime_larger_values_and_new_attempts_are_retained(self):
        result=merge_counts({'own-stage1-default':1},{'own-stage1-default':2,'own-stage2-real-solo':1})
        self.assertEqual(result,{'own-stage1-default':2,'own-stage2-real-solo':1})

    def test_counts_do_not_add_the_same_attempt_twice(self):
        self.assertEqual(merge_counts({'own-stage2-real-solo':1},{'own-stage2-real-solo':1}),{'own-stage2-real-solo':1})

    def test_invalid_values_fail_closed(self):
        for value in (True,-1,1.5,'3'):
            with self.assertRaises(ValueError):merge_counts({'own-stage1-default':value},{})
        with self.assertRaises(ValueError):merge_counts({'new-name':3},{})

    def test_missing_runtime_file_uses_shipped_baseline(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'verification-budget-baseline.json').write_text(json.dumps({'schema_version':1,'official_match_executions':{'own-stage1-default':3}}))
            self.assertEqual(load_match_counts(root),{'own-stage1-default':3})

    def test_missing_or_corrupt_baseline_never_allows_fresh_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            with self.assertRaises(FileNotFoundError):load_match_counts(root)
            (root/'verification-budget-baseline.json').write_text('{')
            with self.assertRaises(ValueError):load_match_counts(root)

    def make_v2(self, root):
        source=root/'contestant_ws/src/rmtc_bot'
        for name in ('src/core.cpp','include/core.hpp','CMakeLists.txt','core/CMakeLists.txt'):
            path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('baseline')
        (root/'verification-budget-baseline.json').write_text(json.dumps({'schema_version':2,'iterations':{}}))
        return source

    def test_actual_source_change_starts_new_iteration_and_preserves_old_ledger(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=self.make_v2(root);before=code_iteration(root)
            path=match_ledger_path(root);path.parent.mkdir(parents=True);path.write_text('{"own-stage2-real-solo":3}')
            self.assertEqual(load_match_counts(root),{'own-stage2-real-solo':3})
            (source/'src/core.cpp').write_text('revised planner')
            self.assertNotEqual(before,code_iteration(root));self.assertEqual(load_match_counts(root),{})
            (source/'src/core.cpp').write_text('baseline')
            self.assertEqual(code_iteration(root),before)
            self.assertEqual(load_match_counts(root),{'own-stage2-real-solo':3})

    def test_docs_labels_and_relocation_do_not_reset_iteration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.make_v2(root);before=code_iteration(root)
            (root/'README.md').write_text('new version label')
            self.assertEqual(code_iteration(root),before)
            import shutil
            restored=root/'restored';shutil.copytree(root/'contestant_ws',restored/'contestant_ws')
            self.assertEqual(code_iteration(restored),before)

    def test_v2_shipped_counts_are_merged_only_for_matching_iteration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=self.make_v2(root);iteration=code_iteration(root)
            (root/'verification-budget-baseline.json').write_text(json.dumps({'schema_version':2,'iterations':{iteration:{'own-stage1-default':3}}}))
            self.assertEqual(load_match_counts(root),{'own-stage1-default':3})
            (source/'include/core.hpp').write_text('changed contract')
            self.assertEqual(load_match_counts(root),{})

if __name__=='__main__':unittest.main(verbosity=2)
