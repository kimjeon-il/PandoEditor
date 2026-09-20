# M4 coordinate kernel

Product resource: `polygon-clipping-0.15.7.js`, separate from test fixtures.

- Origin: world-map `17c3dbe`, `assets/js/vendor/polygon-clipping.min.js`.
- Upstream: https://github.com/mfogel/polygon-clipping/tree/v0.15.7
- License: MIT; `LICENSE.polygon-clipping.txt` (upstream v0.15.7 LICENSE.md).
- LF SHA-256: `8c1ed56df8b1f97b047f82d91b910aacdaff67d8d9a55f2495eb26e8369186f7`.
- No coordinate rounding or global snapping is added by the adapter.
- Qt 6.8.3 compatibility: expand the splaytree `insert` comma-return expression
  into separate size increment, local result, `_root` assignment, and return.
  The unmodified expression produced an empty result even for one square in
  QJSEngine (queue size 8, null root); Node produced the correct square. The
  product file is unchanged; the adapter verifies its SHA-256 and exactly one
  matching expression before applying this semantics-preserving runtime rewrite.
- Each synchronous call owns its QJSEngine in the calling worker thread.
- Cancellation is checked before and after the synchronous JS call. The current
  adapter does not interrupt a running kernel; callers must retain cancellation
  and stale-session checks before accepting/applying a result. Cancellation can
  therefore delay freeing the worker, but cannot make the result applicable.
- Empty results are distinct from errors. They must never be installed as an
  empty geometry replacement; object lifetime is the territorial plan's decision.

This backend alone does not establish parity for transfer/conversion transactions.
