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
