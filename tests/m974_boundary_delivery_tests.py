"""Require real cancellation and selection ownership at completed-worker delivery."""
import argparse
import base64
import hashlib
import json
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--probe')
parser.add_argument('--observations')
parser.add_argument('--cases')
args = parser.parse_args()
if args.observations:
    with open(args.observations, encoding='utf-8') as stream:
        receipt = json.load(stream)
else:
    with open(args.cases, encoding='utf-8') as stream:
        inputs = json.load(stream)
    inputs = inputs if isinstance(inputs, list) else inputs['cases']
    inputs = [row for row in inputs if row.get('scenario')]
    result = subprocess.run([args.probe], input=json.dumps(inputs), text=True,
                            capture_output=True, timeout=180, check=True)
    receipt = json.loads(result.stdout)
rows = {row['input'].get('scenario'): row for row in receipt['rows']}


def assert_unchanged(row, stage):
    state = stage['state']
    before = row['stages']['before']['state']
    raw = base64.b64decode(state['canonicalBytesBase64'], validate=True)
    assert hashlib.sha256(raw).hexdigest() == state['documentSha256']
    assert json.loads(raw) == state['nativeCanonicalDocument']
    assert state['canonicalBytesBase64'] == before['canonicalBytesBase64']
    assert state['unchangedFromBefore'] is True
    assert state['history'] == before['history'] == {'canUndo': False, 'canRedo': False}


def assert_selection_owned(row, scenario):
    # The legacy web stimulus changes generation/revision, while native performs
    # real selectCountry(C). They are deliberately not equivalent stale-project tests.
    settled = row['stages']['settled']
    edit = settled['edit']
    assert edit['active'] is True
    assert edit['calculating'] is False
    assert edit['boundaryStatus'] == 'ready'
    assert edit['previewReady'] is (scenario != 'stale-preparation')
    assert settled['outcome']['ok'] is True
    expected_trigger = ('selection revision change before owner delivery'
                        if scenario == 'stale-preparation'
                        else 'selection revision change before preview delivery')
    assert settled['outcome']['trigger'] == expected_trigger
    assert [ref['id'] for ref in settled['selectionItems']] == [row['input']['features'][-1]['id']]
    assert settled['selection']['id'] == row['stages']['pending']['selection']['id']
    assert edit['targets'] == row['stages']['pending']['edit']['targets']
    assert_unchanged(row, settled)

for scenario in ('pending-cancel', 'stale-preparation', 'pending-preview-cancel', 'stale-preview'):
    row = rows[scenario]
    assert 'error' not in row, row.get('error')
    barrier = row.get('deliveryBarrier')
    assert barrier is not None, f'{scenario}: completed-worker delivery barrier was not observed'
    assert barrier == {'mechanism': 'native-worker-completed-owner-delivery-withheld',
                       'timeoutMs': 30000, 'completed': True, 'ownerEventsProcessed': False}, barrier
    delayed = row['stages']['delayed']
    assert delayed['observed'] is True
    assert delayed['edit']['calculating'] is True
    assert_unchanged(row, delayed)
    settled = row['stages']['settled']
    assert settled['observed'] is True
    if scenario in ('pending-cancel', 'pending-preview-cancel'):
        assert settled['edit']['active'] is False
        assert not settled['edit'].get('previewReady', False)
        assert settled['outcome']['ok'] is False
    else:
        assert_selection_owned(row, scenario)
    assert_unchanged(row, settled)
    assert row['observationLimits']['completedWorkerOwnerDeliveryWithheld'] is True
    assert row['observationLimits']['workerResultInterception'] is False
row = rows['stale-move']
assert row['stages']['delayed']['observed'] is False
assert 'synchronous' in row['stages']['delayed']['reason']
assert row['observationLimits']['completedWorkerOwnerDeliveryWithheld'] is False
assert_selection_owned(row, 'stale-move')
print('2 real cancellation paths, 3 selection-owned outcomes and 1 explicit synchronous move limitation verified; legacy web generation/revision stimuli remain non-equivalent')
