"""Require real completed-worker cancellation observations from the native probe."""
import argparse
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
    assert delayed['state']['unchangedFromBefore'] is True
    assert delayed['state']['documentSha256'] == row['stages']['before']['state']['documentSha256']
    settled = row['stages']['settled']
    assert settled['observed'] is True
    if scenario in ('pending-cancel', 'pending-preview-cancel'):
        assert settled['edit']['active'] is False
    else:
        assert settled['edit']['calculating'] is False
        assert settled['edit']['previewReady'] is False
        assert settled['edit']['boundaryStatus'] == 'error'
    assert settled['state']['unchangedFromBefore'] is True
    assert settled['state']['documentSha256'] == row['stages']['before']['state']['documentSha256']
    assert row['observationLimits']['completedWorkerOwnerDeliveryWithheld'] is True
    assert row['observationLimits']['workerResultInterception'] is False
row = rows['stale-move']
assert row['stages']['delayed']['observed'] is False
assert 'synchronous' in row['stages']['delayed']['reason']
assert row['observationLimits']['completedWorkerOwnerDeliveryWithheld'] is False
print('4 completed-worker cancellation paths and 1 explicit synchronous limitation verified')
