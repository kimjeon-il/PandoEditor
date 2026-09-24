# M5 web evidence

Pinned source: world-map `bc46720`. The following files are byte-for-byte copies
under `tests/fixtures/web-content/source`, with LF enforced by .gitattributes.
They are unchanged relative to the original `17c3dbe` baseline.

| Source | Git blob |
| --- | --- |
| distribution-model.js | 7fa381ac8ddf67daeb66df027017d83a98fcf845 |
| temporal.js | 31faf94894e302b3eadf61d79a32efd0490affe3 |

`tools/m5-content-oracle.mjs <content_probe.exe>` verifies blob identities, calls
the actual exported web `dominantDistributionEntries`, and compares its output
with the native probe. The probe receives cases and never generates expected
answers. Covered cases: independent shares, ties preserving input order, hidden
layers, and geometry entries appended after territorial winners.

Correction to the proposed plan: the web function preserves the first entry on
equal shares; it does not break ties by sorted IDs. Native behavior matches the
web. Layer input order and entry order remain meaningful.

This is the distribution-dominant subset only. Label layout, normalization,
deletion, hydro, symbols and generic behaviors are not yet Oracle-complete.

The connected-path pass also inspected `bc46720`'s `app-hydro-settings.js` and
`label-layout.js`: hydro category follows line versus polygon geometry; label
layout prioritizes selection, pinned state, priority and stable key within its
collision group. The hydro category rule is used during promotion and the typed
layout now implements that ordering. These source inspections remain evidence,
not a new executable JavaScript Oracle pass.

The exact pinned `country-flags.js` mapping and its existing fixed-version SVG
tree are copied into the app resources with source notices. The native resolver
implements the same default/none/embedded precedence and reports a missing
asset rather than substituting one. Distribution display uses the pinned web
alpha formula and first-input winner for equal shares. Executable Oracle
coverage is still limited to the previously recorded dominant-distribution
fixture; label pixels, symbol pixels and hydro-provider behavior are covered by
focused native tests rather than claimed as new web-Oracle passes.

## M5 hydro format pin for the completion pass

The hydro worker, shard store, tile-window module and their small runtime
dependencies are pinned from world-map `c0bd31d` in
`tests/fixtures/web-hydro/source`. Source blob IDs and fixture provenance are
in that fixture's README. `tools/m5-hydro-oracle.mjs --fixture-only` executes
the real web decoder on a six-record v4 pack/v5 metadata dataset. It checks
index, metadata, geometry, widths, mesh, viewport tile enumeration and logical
fragment merge against golden output produced by that same web decoder.
`m5_hydro_index_parity`, `m5_hydro_pack_parity` and
`m5_hydro_logical_merge_parity` compare native probes with the pinned worker;
`m5_hydro_viewport_parity` checks native tile selection against the pinned
tile-window module. `m5_render_pick_order_parity` calls the current web scene
pass and picker rank function. The actual full dataset is a separate gate.

From `bc46720..c0bd31d`, label layout gained viewport bounds: ordinary labels
whose boxes cross the safe area are dropped before collision placement;
selected and pinned labels bypass the bounds filter. Qt now applies viewport
bounds. Web theme-specific safe-area insets and the new `colorVisible` display
channel have not been asserted as M5 pixel parity.
