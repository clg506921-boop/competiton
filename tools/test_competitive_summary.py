import unittest
from summarize_competitive_results import stats,summarize

def record(ordinal,score,passed=True,fixture='map1',completed=True):
    return dict(ordinal=ordinal,iteration_id='code1',own_binary_sha256='bin1',stage=2,
                scenario='own-stage2-size24-real-three-companions',ai_mode='real',companions=['B','C','D'],
                cpu_affinity={'referee':[0,1],'contestant':[2,3]},external_size_fixture={'sha256':fixture},
                completed=completed,passed=passed,own_team_final={'score':score} if completed else None)

class SummaryTests(unittest.TestCase):
    def test_all_three_scores_and_variability_are_used(self):
        result=summarize([record(1,30),record(2,45),record(3,60)])[0]
        self.assertEqual(result['metrics']['score'],{'n':3,'mean':45,'sample_sd':15.0,'min':30,'max':60})

    def test_failure_is_retained_without_fabricating_zero_score(self):
        result=summarize([record(1,40),record(2,None,False,completed=False)])[0]
        self.assertEqual((result['attempts'],result['passed'],result['completed']),(2,1,1))
        self.assertEqual(result['failed_ordinals'],[2]);self.assertEqual(result['metrics']['score']['n'],1)

    def test_different_maps_never_share_mean(self):
        self.assertEqual(len(summarize([record(1,40),record(1,80,fixture='map2')])),2)

    def test_bad_observations_and_duplicate_attempts_fail(self):
        for values in ([float('nan')],[float('inf')],[True]):
            with self.assertRaises(ValueError):stats(values)
        with self.assertRaises(ValueError):summarize([record(1,40),record(1,40)])

    def test_fourth_attempt_is_retained_after_user_removed_match_cap(self):
        result=summarize([record(i,40) for i in range(1,5)])[0]
        self.assertEqual(result['attempts'],4)
        self.assertEqual(result['metrics']['score']['n'],4)

if __name__=='__main__':unittest.main(verbosity=2)
