"""Audit contestant decision summaries offline. Never opens a socket or runs a game."""
import argparse
from collections import Counter, OrderedDict
import json
import math
from pathlib import Path

MAX_LINE = 2 * 1024 * 1024
MAX_CONTEXTS = 128
MAX_SAMPLES = 200000
STATUSES = ('confirmed', 'rejected', 'mismatched', 'unconfirmed', 'terminal_candidate',
            'move_failed', 'state_cancelled')
ERRORS = {'', 'E_PARSE', 'E_SCHEMA', 'E_TICK', 'E_SESSION', 'E_STATE', 'E_RATE', 'E_OOB', 'E_BLOCKED', 'UNKNOWN'}
ACTIONS = {'move', 'attack', 'defend', 'wait'}


def integer(value):
    return type(value) is int and 0 <= value <= 9223372036854775807


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key')
        result[key] = value
    return result


def reject_constant(_):
    raise ValueError('non-finite JSON constant')


def audit(path):
    counts, statuses, errors = Counter(), Counter(), Counter()
    contexts = OrderedDict()
    samples, timing_sum, timing_max, timing_count = [], 0.0, 0.0, 0
    issues = []

    def bad(line, code):
        counts['integrity_errors'] += 1
        if len(issues) < 32:
            issues.append({'line': line, 'code': code})  # No input content is echoed.

    with Path(path).open('rb') as stream:
        line_number = 0
        while True:
            raw = stream.readline(MAX_LINE + 1)
            if not raw:
                break
            line_number += 1
            if len(raw) > MAX_LINE:
                while raw and not raw.endswith(b'\n'):
                    raw = stream.readline(MAX_LINE + 1)
                bad(line_number, 'oversized_line')
                continue
            try:
                row = json.loads(raw.decode('utf-8'), object_pairs_hook=unique_object,
                                 parse_constant=reject_constant)
            except (ValueError, UnicodeError, RecursionError):
                bad(line_number, 'invalid_json')
                continue
            if not isinstance(row, dict) or not isinstance(row.get('kind'), str):
                bad(line_number, 'invalid_record')
                continue
            counts['records'] += 1
            if row['kind'] == 'startup':
                counts['startups'] += 1
                continue
            if row['kind'] == 'parse_error':
                counts['perception_parse_errors'] += 1
                continue
            if row['kind'] != 'decision':
                counts['other_records'] += 1
                continue
            tick, seat, session = row.get('tick'), row.get('seat'), row.get('session_id')
            run_id = row.get('run_id', 'legacy')
            if (not integer(tick) or not isinstance(seat, str) or seat not in {'A', 'B', 'C', 'D'}
                    or not isinstance(session, str) or not 0 < len(session) <= 256
                    or not isinstance(run_id, str) or not 0 < len(run_id) <= 128
                    or not isinstance(row.get('action'), str) or row['action'] not in ACTIONS
                    or type(row.get('duplicate')) is not bool):
                bad(line_number, 'invalid_decision_fields')
                continue
            boolean_fields = ('receipt_fresh', 'receipt_matched', 'receipt_expected', 'receipt_missing',
                              'receipt_direction_applied', 'resumed_after_silence')
            if any(key in row and type(row[key]) is not bool for key in boolean_fields):
                bad(line_number, 'invalid_diagnostic_fields')
                continue
            receipt_status = row.get('receipt_status')
            if receipt_status is None and integer(row.get('receipt_kind')) and row['receipt_kind'] < len(STATUSES):
                receipt_status = STATUSES[row['receipt_kind']]
            if receipt_status is not None and (not isinstance(receipt_status, str) or receipt_status not in STATUSES):
                bad(line_number, 'invalid_receipt_status')
                continue
            error = row.get('receipt_error', '')
            if not isinstance(error, str) or error not in ERRORS:
                bad(line_number, 'unsafe_receipt_error')
                continue
            elapsed = row.get('application_us')
            if elapsed is not None and (type(elapsed) not in (int, float) or not 0 <= elapsed <= 1e15 or not math.isfinite(elapsed)):
                bad(line_number, 'invalid_timing')
                continue
            dropped = row.get('log_dropped', 0)
            if not integer(dropped):
                bad(line_number, 'invalid_log_counter')
                continue
            counts['decisions'] += 1
            counts['max_log_dropped'] = max(counts['max_log_dropped'], dropped)
            if 'run_id' not in row:
                counts['legacy_decisions_without_restart_identity'] += 1
            key = (run_id, seat, session)
            previous = contexts.get(key)
            if row['duplicate']:
                counts['duplicate_resends'] += 1
                if previous is not None and tick != previous:
                    bad(line_number, 'duplicate_tick_differs_from_cached_tick')
                continue
            counts['unique_decisions'] += 1
            if previous is not None:
                if tick <= previous:
                    bad(line_number, 'nonincreasing_fresh_tick')
                else:
                    counts['missing_observation_ticks'] += tick - previous - 1
            contexts[key] = tick
            contexts.move_to_end(key)
            if len(contexts) > MAX_CONTEXTS:
                contexts.popitem(last=False)
                counts['context_evictions'] += 1
            counts['resumed_after_silence'] += bool(row.get('resumed_after_silence'))
            counts['receipt_expected_frames'] += bool(row.get('receipt_expected'))
            counts['previous_submission_receipt_missing_frames'] += bool(row.get('receipt_missing'))
            if row.get('receipt_fresh', receipt_status is not None):
                counts['fresh_receipts'] += 1
                counts['associated_receipts'] += bool(row.get('receipt_matched', receipt_status != 'unconfirmed'))
                if receipt_status is not None:
                    statuses[receipt_status] += 1
                if error:
                    errors[error] += 1
            if elapsed is not None:
                timing_count += 1
                timing_sum += elapsed
                timing_max = max(timing_max, elapsed)
                if len(samples) < MAX_SAMPLES:
                    samples.append(elapsed)
    samples.sort()
    counts = dict(counts)
    for name in ('decisions', 'unique_decisions', 'duplicate_resends', 'integrity_errors',
                 'fresh_receipts', 'associated_receipts', 'missing_observation_ticks', 'max_log_dropped'):
        counts.setdefault(name, 0)
    fresh = counts['fresh_receipts']
    complete_timing = timing_count <= MAX_SAMPLES
    return {
        'report_version': 1,
        'scope': 'contestant summary logs only; no official receipt authenticity, score, qualification or end-to-end latency verdict',
        'counts': counts, 'receipt_statuses': dict(statuses), 'receipt_errors': dict(errors),
        'receipt_association_rate': counts['associated_receipts'] / fresh if fresh else None,
        'timing': {'scope': 'logged callback-entry through publish; excludes upstream DDS and referee settlement',
                   'unique_samples': timing_count, 'mean_us': timing_sum / timing_count if timing_count else None,
                   'max_us': timing_max if timing_count else None,
                   'p99_us': samples[math.ceil(len(samples) * .99) - 1] if samples and complete_timing else None,
                   'p99_sample_limit_exceeded': not complete_timing},
        'issues': issues,
        'summary_complete': not (counts['integrity_errors'] or counts['max_log_dropped'] or counts.get('context_evictions')),
        'official_referee_used_by_tool': False, 'network_requests': 0,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        if args.output is not None and args.output.resolve() == args.log.resolve():
            raise ValueError('input/output overlap')
        report = audit(args.log)
        text = json.dumps(report, ensure_ascii=False, indent=2) + '\n'
        if args.output:
            args.output.write_text(text, encoding='utf-8', newline='\n')
        print(text, end='')
        return 0 if report['summary_complete'] else 1
    except (OSError, ValueError):
        print(json.dumps({'error': 'cannot_audit_input_or_write_report'}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
