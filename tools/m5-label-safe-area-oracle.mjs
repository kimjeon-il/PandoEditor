import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import {layoutLabels} from '../tests/fixtures/web-hydro/source/label-layout.js';
import {createMapLayoutMetricsSnapshot} from '../tests/fixtures/web-hydro/source/map-layout-metrics.js';

for (const [name, sha] of Object.entries({
  'label-layout.js': '60eecd3854a6eed925ab23b893d8c88e63636a3a',
  'map-layout-metrics.js': 'a0d3450720524bddd569e2052c27e13dda6ccff6',
})) {
  const bytes = fs.readFileSync(new URL(`../tests/fixtures/web-hydro/source/${name}`, import.meta.url));
  const blob = crypto.createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes])).digest('hex');
  assert.equal(blob, sha, `upstream blob changed: ${name}`);
}

function placed(centerY, mobile, selected = false, pinned = false) {
  // Pinned c0bd31d CSS: 2rem status bar (32px at 16px root) and 96px mobile
  // bottom inset. The source snapshot enforces a minimum 26px bottom inset.
  const snapshot = createMapLayoutMetricsSnapshot({width: 600, height: 400, mobile,
    safeInsets: {left: 0, right: 0, top: 0, bottom: mobile ? 96 : 32}});
  const label = {key: 'country:A', point: [300, centerY], width: 22, height: 19,
    priority: 100, collisionGroup: 'country', selected, pinned};
  return layoutLabels([label], {zoom: 2, padding: mobile ? 5 : 3,
    bounds: {left: snapshot.safe.left, right: snapshot.width - snapshot.safe.right,
      top: snapshot.safe.top, bottom: snapshot.height - snapshot.safe.bottom}}).length === 1;
}

for (const mobile of [false, true]) {
  assert.equal(placed(285, mobile), true);
  assert.equal(placed(355, mobile), !mobile);
  assert.equal(placed(390, mobile), false);
  assert.equal(placed(390, mobile, true), true);
  assert.equal(placed(390, mobile, false, true), true);
}
console.log('M5 pinned web label safe-area: 10 desktop/mobile ordinary, selected and pinned cases passed');
