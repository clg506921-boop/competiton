"""Seat B process-crash/late-join check using synthetic public messages, no referee/HTTP."""
import copy
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from std_msgs.msg import String
from launch_bot import arguments
from audit_log import audit


def run():
    if os.environ.get('ROS_DOMAIN_ID') != '40' or os.environ.get('ROS_LOCALHOST_ONLY') != '1':
        raise RuntimeError('Requires isolated localhost-only ROS domain 40')
    root = Path(__file__).resolve().parent.parent
    ledger_path = root / 'recovery-execution-ledger.json'
    ledger = json.loads(ledger_path.read_text())
    if ledger['synthetic_seat_b_restart'] >= 3:
        raise RuntimeError('Three executions reached; stop automatically')
    ledger['synthetic_seat_b_restart'] += 1
    ledger_path.write_text(json.dumps(ledger))
    ordinal = ledger['synthetic_seat_b_restart']
    log = root / f'synthetic-restart-{ordinal}.jsonl'
    if log.exists():
        raise RuntimeError('Refusing to overwrite an earlier log')
    binary = root / 'contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node'
    result = {'scenario': 'seat-B-crash-late-join', 'ordinal': ordinal, 'passed': False,
              'official_referee_used': False, 'real_model_calls': 0, 'http_requests': 0,
              'restart_within_two_wallclock_ticks_verified': False}
    rclpy.init()
    node = rclpy.create_node('synthetic_restart_probe')
    qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.VOLATILE)
    transient = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.TRANSIENT_LOCAL)
    actions, registrations, callback_errors = [], [], []
    proc = None
    outputs = []

    def callback(destination, msg):
        try:
            destination.append(json.loads(msg.data))
        except Exception:
            callback_errors.append('invalid_json')

    node.create_subscription(String, '/team_B/action', lambda msg: callback(actions, msg), qos)
    node.create_subscription(String, '/team_B/registration', lambda msg: callback(registrations, msg), transient)
    pub = node.create_publisher(String, '/team_B/perception', qos)

    def spin_until(predicate, timeout=3):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            rclpy.spin_once(node, timeout_sec=.02)
            if predicate():
                return True
            if proc is not None and proc.poll() is not None:
                raise RuntimeError('Contestant exited before expected action')
        return predicate()

    def rows():
        if not log.exists():
            return []
        return [json.loads(line) for line in log.read_text().splitlines() if line]

    def start(index):
        nonlocal proc
        env = {k: v for k, v in os.environ.items() if not k.startswith('RMTC_')}
        env.update(RMTC_STAGE='1', RMTC_LOG_FILE=str(log))
        name = f'恢复测试-B-{index}'
        before = time.monotonic()
        proc = subprocess.Popen([str(binary)] + arguments('B', name, 'collect', 'greedy', 'immediate'),
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                start_new_session=True)
        assert spin_until(lambda: any(r.get('team') == 'B' and r.get('name') == name for r in registrations)
                          and pub.get_subscription_count() > 0
                          and node.count_publishers('/team_B/action') > 0
                          and sum(r['kind'] == 'startup' for r in rows()) == index, 10), 'Startup/discovery failed'
        return (time.monotonic() - before) * 1000

    def receipt(tick, action='move', moved=True, direction='right', error=None):
        r = dict(tick=tick, action=action, moved=moved)
        if moved:
            r['effective_direction'] = direction
        if error:
            r['error'] = error
        return r

    def frame(tick, position, previous, alive=True, respawn=False):
        grid = [['0'] * 9 for _ in range(9)]
        grid[4][4], grid[4][5] = '1', '2'
        own = dict(team='B', robot_id='B', position=position, hp=100 if alive else 0,
                   alive=alive, protected=respawn, last_action=previous)
        if respawn:
            own['position_reason'] = 'respawn'
        entities = [dict(type='resource_high', id=f'fixture-{tick}', value=20,
                         position='\n'.join(''.join(row) for row in grid))] if alive else []
        return dict(tick=tick, session_id='same-synthetic-session', vision_radius=4, self=own,
                    map=dict(width=20, height=20), visible_entities=entities)

    def send(f, expected='move'):
        count = len(actions)
        pub.publish(String(data=json.dumps(copy.deepcopy(f))))
        assert spin_until(lambda: len(actions) > count), 'Missing action for delivered frame'
        wanted = dict(tick=f['tick'], session_id=f['session_id'], action=expected)
        if expected == 'move':
            wanted['direction'] = 'right'
        assert actions[-1] == wanted, 'Unexpected action/session/Tick'

    try:
        result['first_startup_discovery_ms'] = start(1)
        send(frame(40, [14, 10], receipt(39)))
        send(frame(41, [15, 10], receipt(40)))
        send(frame(44, [13, 10], receipt(43)))
        assert spin_until(lambda: sum(r['kind'] == 'decision' for r in rows()) == 3)
        os.killpg(proc.pid, signal.SIGKILL)  # Only this test's own contestant process.
        output, _ = proc.communicate(timeout=5)
        outputs.append(output)
        result['crashed_process_returncode'] = proc.returncode
        assert proc.returncode == -signal.SIGKILL
        result['second_startup_discovery_ms'] = start(2)
        send(frame(48, [6, 16], receipt(47, direction='left')))
        send(frame(49, [6, 16], receipt(48, moved=False, error='E_STATE'), alive=False), 'wait')
        send(frame(50, [6, 16], receipt(49, 'wait', False), alive=False), 'wait')
        send(frame(53, [2, 1], receipt(52, 'wait', False), respawn=True))
        send(frame(54, [3, 1], receipt(53)))
        assert spin_until(lambda: sum(r['kind'] == 'decision' for r in rows()) == 8)
        assert not callback_errors
        data = rows()
        decisions = [r for r in data if r['kind'] == 'decision']
        assert len({r['run_id'] for r in data if r['kind'] == 'startup'}) == 2
        assert decisions[0]['receipt_status'] == decisions[3]['receipt_status'] == 'unconfirmed'
        assert decisions[2]['skipped_ticks'] == decisions[6]['skipped_ticks'] == 2
        assert decisions[4]['receipt_status'] == 'state_cancelled'
        assert decisions[6]['position'] == [2, 1] and decisions[6]['position_discontinuous']
        assert all(not r['ai_enabled'] and r['ai_attempts'] == 0 for r in decisions)
        report = audit(log)
        assert report['summary_complete'] and report['counts']['unique_decisions'] == 8
        assert report['counts']['missing_observation_ticks'] == 4
        result.update(passed=True, first_delivered_frame_after_restart_controlled=True,
                      action_ticks=[a['tick'] for a in actions], distinct_processes=2,
                      first_process_summary_rows_preserved=3, audit=report)
    finally:
        if proc is not None:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGINT)
            try:
                output, _ = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                output, _ = proc.communicate(timeout=3)
            outputs.append(output)
            result['final_process_returncode'] = proc.returncode
            if proc.returncode != 0:
                result['passed'] = False
        result['observed_action_ticks'] = [a.get('tick') for a in actions]
        node.destroy_node()
        rclpy.shutdown()
        (root / f'synthetic-restart-{ordinal}-console.log').write_text('\n'.join(outputs))
        (root / f'synthetic-restart-{ordinal}-result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2))
    assert result['final_process_returncode'] == 0, 'Final process did not stop cleanly'
    return result


if __name__ == '__main__':
    print(json.dumps(run(), ensure_ascii=False))
