# Flag output contract — 2026-10-04

Scope: territorial flag assets, upload preservation, default selection, conversion
and map flag decoration. This is not a claim that every editor mechanism, name
placement policy, Qt renderer or physical GPU output is identical to the web.

## Reference and implementation

Web asset/algorithm comparison was run against commit
`c6d081472b70ef8f02a436767a27cce3a2eecf22`. Native changes start from
`b606b907e14b35443a3fac0e90fdf28bfaa36836`.

- All 241 bundled SVGs (235 native, 4 legacy, 2 political) match both repositories'
  Git blobs and Windows checkouts byte-for-byte. Checkout CRLF and Git LF must not
  be confused when reporting file sizes. No flag asset was replaced or resized.
- Map flags occupy 18×12 logical pixels, preserve aspect ratio, sit beside the
  centered name with a 5px gap, and appear only at active flat/globe zoom ≥1.8.
- Accepted names are decorated afterwards. Expanded flag boxes use the web's
  12px extension and 3px spacing, check all collision groups in placement order,
  and suppress only the colliding flag. A flag-only rejected item is omitted.
  A screen grid implements the same decisions without a quadratic scan.
- Camera reprojection reevaluates flag visibility without rebuilding source
  geometry or rerunning the full name query/layout. Below threshold/no available
  flags, collision-grid construction is avoided.
- Image decode errors hide the image and recenter the retained name.
- Selection-card previews use 92×68 in a 108×86 box; below 800px window width,
  80×60 in a 94×76 box. Editor-toolbar previews retain their separate layout.
- Uploads preserve original SVG/raster bytes and image MIME instead of converting
  every file into PNG. Original raster metadata survives save/reopen. Existing
  16MiB file / 24MiB data-URL safety caps remain; SVG texture size stays bounded
  to displayed size × device pixel ratio.
- One resolver serves map, selection and editing defaults. Preserved web
  `flagDataUrl`, `defaultFlagDataUrl`, `convertedFromCountry.override` and
  `builtinSubunit.sourceCountryId` obey explicit removal/default precedence.
  Native edits supersede preserved metadata.
- Country→subunit conversion captures the original country's default and its
  explicit uploaded/none override in `TerritorialSymbolStyle`. These defaults
  survive edits, reset, Undo/Redo and native save/reopen, independently of the
  current override. New optional codec fields are `defaultCountryId` and
  `defaultFlagDataUrl`; absent fields in existing projects mean no captured
  conversion default.
- The picker now includes the same legacy `cd/sm/ga/pg` and political `cyn/sol`
  files used by automatic defaults.

## Reproducible checks

```powershell
node tools/flag-assets-parity.mjs '../map editor'
cmake --build <ascii-build-directory> --parallel 4
ctest --test-dir <ascii-build-directory> --output-on-failure
```

`flag_layout_web_parity` runs the pinned, executable web algorithm against
48 native ordered/cross-group/flag-only layouts. The asset audit additionally
checks that the current territorial web module still matches that pinned
algorithm (country/territorial naming is the only normalization).

`presentation_editor_tests` covers original SVG/PNG bytes, metadata precedence,
conversion defaults/removal/overrides, Undo/Redo, save/reopen and recovery.
`ui_tests` covers flat/globe geometry, decode failure, responsive previews,
flag-only visibility and delegate survival during camera motion. Flag-only
isolation tests use a single-country fixture: unrelated place labels are allowed
to suppress a flag by the new, web-correct collision rule.

Windows Release build: `D:/Pandoeditor-flags-20261004/build-ascii`, Qt 6.8.3,
MinGW 13.1, `-O2 -UNDEBUG` (assertions active). Functional QML checks use
`QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`; they are not physical
GPU or portable acceptance evidence. No portable replacement or release was
performed in this change.

## Verification result

- Fresh full CTest: **115/115 passed**, 114.40 seconds.
- Presentation suite: **20 passed, 0 failed** (includes setup/cleanup).
- Focused flag QML run: **9 passed, 0 failed** (includes setup/cleanup).
- Executable native/web flag-decoration oracle: **48 cases passed**.
- Asset audit: **241/241 identical** in Git and local checkouts.

The initial full run failed the flag-only placement assumptions at zoom 1 and
camera-motion tests using a colliding multi-label sample. Tests now explicitly
use an eligible zoom and isolate delegate-retention from legitimate collision
removal. The final full run has no outstanding test failures. Native GPU,
browser-vs-Qt pixel identity and portable acceptance remain unverified.
