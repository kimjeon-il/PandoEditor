# Terrain integration validation

## Remaining-plan official pixel execution (2026-10-07)

`official-pixels-original-02`: actual native PID 20004, OS exit 0,
declared/processed 16/16 (DEM 8, original raster 8), 18 Qt pass, 0 fail/skip.
The original 1,282-file DEM inventory and two raster base tiles were verified
again against original SHA256/size entries before launch (1,284 verified files).
Actual Windows Qt 6.8.3 D3D11 / GTX 1650 pixels were read back: four samples
per phase, 64 comparisons, mismatch 0, exact alpha and existing 1-unit RGB
quantization bound. Color, gray, dark color/gray, pan, replacement mask,
absent mask and None transitions use the production provider/material/mask.
Sixteen original-data captures, logs, verified asset list and binary hash are
preserved in `D:/Codex/evidence/p1-p8-m98-20261006/official-pixels-original-02/`.

The oracle is independent pinned-Web arithmetic applied to original decoded
tile/tint channels, not native display helpers or regenerated expected files.
This is an original-data sampled pixel gate; it does not claim full-screen
browser image equality, every DEM texel, derived-shade blend coverage, measured
GPU fences or driver VRAM. Earlier synthetic mechanism tests remain separate.
The first execution (`official-pixels-original-01`, PID 15188/exit 4) had
12/16 complete phases and four harness failures: physical raster color never
requests a land mask. Its raw failure is retained. Only that incorrect wait
was removed; production terrain code and comparison tolerance are unchanged.

Reproduce with `tools/run-official-terrain-pixels.ps1`, passing the ordinary
`terrain_official_pixel_tests.exe`, original DEM/raster roots and fresh evidence
directory. It verifies all source files and requires exact native case counts
and exit code. Hardware CTest registration follows the existing explicit
`PANDOEDITOR_REGISTER_TERRAIN_GPU_DISPLAY` option; missing data fails the gate.

## P1 data and display checkpoint (2026-10-07)

App base: `2e65cf9f4a1d29e01d406029de60c5c9ff0b6eb5`.
Work branch: `codex/m97-web-editing-parity`.
P1 Web shader: `5649c307da24d0965d63bc8c00206aed9b9d3438`.
Overall fixed Web candidate: `ebcfae4d27b29cbbea6416a7045a4806930204be`.
Published DEM data: `c3c18d167dae2dd9639844e5174dd68f745c6832`.

The immutable data acquisition verified all 1,282 files, 854,965,132 compressed
bytes, individual Git blob/size/SHA-256 identities and the independently checked
builder aggregate `622a6d272bdda6b507a1f4ba49794fe9ef41aca06f29d8b38e24114caa50d023`.
Tint SHA-256 is `1ae4c70e05494917c11d3c6b32869db61bf4f9e7e4b0239f9b4bd2b9bec582f3`.
The tracked inventory/provenance keeps the 1,280 DEM paths at `terrain/v0.13.0`;
only manifest and tint are `terrain/v0.13.3`. Large binaries remain outside Git.

Windows Release checks use Qt 6.8.3, MinGW 13.1 and the separate development
build `D:/build/Pandoeditor-p1-p8-m98`. Evidence is under
`D:/Codex/evidence/p1-p8-m98-20261006`. These are development/test executables,
not a portable package, installer, deployment or performance acceptance.

### Actual executions

| Check | Actual result | Raw evidence |
| --- | --- | --- |
| Focused registered provider/decoder/mask/cache/store/source/acquisition/scene checks | 11/11 test groups, exit 0, no failed or skipped groups | `p1-current-focused-native.xml` |
| Physical-store suite after review correction | 8/8 Qt cases (6 functional + setup/cleanup), exit 0, skip 0 | `p1-metadata-native-green.txt` |
| Terrain-cache suite after source ownership correction | 5/5 Qt cases (3 functional + setup/cleanup), exit 0, skip 0 | `p1-source-protection-native-green.txt` |
| Configured-root controller lifecycle | 3/3 Qt cases (1 functional + setup/cleanup), exit 0, skip 0 | `p1-ui-source-configured-root.txt` |
| Canonical world shell regression | 3/3 Qt cases (1 functional + setup/cleanup), exit 0, skip 0 | `p1-ui-canonical-world-current.txt` |
| Actual DEM and raster RHI pixels after review changes | required 16 phases, processed 16, failed/skip 0; 4/4 Qt cases, native PID 9164, exit 0 | `p1-rhi-review-fixed-04.{txt,stdout,receipt.json}` |

The actual RHI log identifies the NVIDIA GeForce GTX 1650 adapter with D3D11,
non-MSAA rendering and real `afterFrameEnd`/`frameSwapped` signals. Eight DEM
phases verify hidden General land, exclusion of Regional geometry, asymmetric
mask orientation, pan/style retention, replacement, stale mask refusal and
missing mask refusal. Eight raster phases verify original RGB and original
Gray Earth A, opaque output, current land mask, dark style and source/off release.
Pixel inputs are explicitly synthetic lossless WebP decoded by the production
provider and displayed through the production Qt materials. Independent fixed
Web arithmetic supplies the pixel expectations. RGB tolerance is one output
quantization unit; alpha must match exactly. This is not an official pixel corpus,
measured GPU fence, measured VRAM or a device performance result.

### Reproduced defects and corrections

- The configured world root was used for world geometry but ignored by physical
  inventory/raster metadata bootstrap. The actual diagnostic reached canonical
  world with no terrain. Root selection now agrees; the failing wait is retained
  in `p1-ui-source-bootstrap-diagnostic.txt`.
- Source replacement retained inactive-provider visible/fallback/pending pins.
  The production bridge now releases old viewport ownership before replacement;
  same-source updates preserve ownership. `p1-source-protection-native-red.txt`
  reproduces the leak; the green suite proves pending/protection and zero-budget
  cache reclamation through the actual source owner.
- One existing native inventory row declared incorrect terrain manifest bytes
  and SHA. The original c0bd31d Git blob is 2024 bytes, SHA
  `093ae0f088622e2867cad5f318749c47e28776a870e6467d00c31ef810d8690f`, blob
  `6821c49315ffd381758f81dfe4d83b6b574f0ad4`. Only those two row values changed;
  all 342 paths, dataset/version/source pin and hydro rows remain unchanged.
  This mismatch is not a simple CRLF conversion. Authentic metadata previously
  failed verification (`p1-metadata-native-red.txt`); it now seeds/resolves while
  altered bytes reject without replacing the good file. Web/expected files were
  not changed.
- Initial RHI failures are preserved: the fixture omitted the independent
  Regional visibility group, then hidden-console Windows startup hid its first
  window. Input visibility and actual window exposure were corrected without
  changing the shader expectations or relaxing exposure checks.

Read-only independent review checked both resource and immutable-byte fixes and
reported no remaining important findings in the reviewed P1 changes. The listed
checks were then rerun after corrections. Receipt hashes bind actual executable
and raw log bytes; this checkpoint ran with uncommitted P1 sources at the base
HEAD and is not final-commit aggregate acceptance.

## Remaining stages

P2 LOD/demand mechanisms have a separate executed checkpoint in
`terrain-p2-validation.md`. The original P1 CPU-ready bootstrap/old handoff did
not prove P3 progressive displayed coverage, resource admission or upload
scheduling. Later P3/P4 implementation and execution is recorded separately.
The subsequent dirty-tree checkpoint is recorded in
`p1-p8-m98-execution-followup.md`: actual D3D11 controller execution processed
12 prepared-DEM transitions with native exit 0; pure coverage/state and
mutation results remain separate from pixel or GPU completion claims.
`retained-terrain-owner-prefetch-full-05` additionally executed eight retained
owner pressure/source/delayed-mask/window checks in actual Windows D3D11,
PID 5848, native exit 0, 8 Qt pass and zero failure/skip. The new checks use
explicit synthetic pixels, include pre-adoption readback while old backing is
still resident, zero settled overflow/staging, context re-upload and stale
receipt refusal. `retained-terrain-device-mutations-03` caught two actual
compiled device regressions (negative native exits 1; named pixel assertions),
without changing shared build/source bytes. The earlier surviving suppression
mutant and failed harness executions remain recorded as failures in the
follow-up ledger. Actual driver memory and full-data M9.8 acceptance remain
unobserved/blocked respectively. These results do not enlarge the earlier P1
corpus gate or relabel synthetic inputs as authentic DEM parity.
Future source changes require affected checks to run again; P1's result cannot
certify a later candidate or replace final same-commit/fixture evidence.

## Remaining-work retirement and display-state correction

Actual Windows D3D11 controller runs47/52/53/54/57/58 retained failures.
Run58 (PID10320/exit1) proved that reverse-zoom candidate26/27 selected
`1/0/0/raw` and `1/1/0/raw`, although the actual retained render owner had
already destroyed those backings. Their older retirement acknowledgements were
queued until a new unsubmitted candidate protected the stale GUI metadata.
The current physical mask was ready; logs/Actions success did not establish
current display. Raw native diagnostics remain outside the repository.

RED55 (PID19688/exit1) separately catches current display evidence erased by
unrelated readiness. Acceptance now persists within its demand; new demand,
cancel and reset invalidate it. RED59 failed the authored domain setup before
the intended assertion; corrected RED60 (PID7412/exit1) catches the actual older
retirement/soft-lease race. An older request's same-owner acknowledgement can
remove only an unsubmitted candidate/target lease. Actual displayed/submitted/
base and current/future-request leases remain protected. Successful retirement
rebuilds a resident-only candidate and schedules missing current requests even
when deferred is0. That zero-deferred controller edge was reviewed but has no
separate executed controller regression result at this checkpoint.

State61 (PID11576/exit0) runs22/22 independent cases, failure/skip0. Controller62
(PID17904/exit0) actually runs12/12 transitions, Qt3/3, with8 Windows captures;
same original DEM, D3D11, unchanged deadlines/assertions. Its final scope-guard
diagnostic occurs after window close and includes the intended teardown
invalidation; it is not the receipt used by the12 transition assertions.
The same reviewer checked the fixes and current/future/submitted protection.
Temporary render-owner logging was removed. New isolated negative execution
and final-source device results are recorded separately when actually complete.

`terrain-state-final-mutants-64/terrain-display-mutations-20261007T100629Z-ea55065d/results.json`
actually completes the unmodified22-case baseline and all6 compiled negative
variants. Each negative exits1 at its unique original assertion, including the
receipt pulse and older retirement race; baseline exits0. Original production
header and tests stay hash-identical throughout; no shared build is modified.
Runner exits0 and originalSourcesUnchanged=true. This is a pure-state negative
gate, separate from actual controller62 and original-data pixel evidence.
