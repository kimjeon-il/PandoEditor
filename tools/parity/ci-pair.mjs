// Called after checkout of the repository that triggered the workflow.
// Peer main is resolved once; subsequent checkouts use only the returned SHA.
import { execFileSync } from 'node:child_process';
import { appendFileSync, writeFileSync } from 'node:fs';
const side = process.env.PARITY_SIDE;
if (!['web', 'app'].includes(side)) throw new Error('Invalid repository side');
const sha = value => {
  if (!/^[0-9a-f]{40}$/.test(value ?? '')) throw new Error('Expected a full commit SHA');
  return value;
};
const current = sha(execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim());
const ownRequested=process.env[side.toUpperCase()+'_SHA'];
if(ownRequested&&sha(ownRequested)!==current)throw new Error('Checkout does not match the requested candidate SHA');
const peer = side === 'web' ? 'app' : 'web';
const repository = peer === 'app' ? 'kimjeon-il/PandoEditor' : 'kimjeon-il/Pando';
const requested = process.env[peer.toUpperCase() + '_SHA'];
const peerSha = requested ? sha(requested) : sha(execFileSync('git', ['ls-remote', 'https://github.com/' + repository + '.git', 'refs/heads/main'],
  { encoding: 'utf8' }).trim().split(/\s+/)[0]);
appendFileSync(process.env.GITHUB_OUTPUT, `web=${side === 'web' ? current : peerSha}\napp=${side === 'app' ? current : peerSha}\n`);
if (process.env.PARITY_BASE) {
  const base = sha(process.env.PARITY_BASE);
  const files = execFileSync('git', ['diff', '--name-only', '-z', base, current], { encoding: 'utf8' }).split('\0').filter(Boolean);
  writeFileSync(process.env.PARITY_CHANGES, JSON.stringify({ web: side === 'web' ? files : [], app: side === 'app' ? files : [] }));
}
