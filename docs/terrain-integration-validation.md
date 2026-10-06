# Terrain integration validation

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
`terrain-p2-validation.md`. P3 progressive displayed coverage, resource admission
and upload scheduling remain pending. The current CPU-ready bootstrap/old handoff
does not prove them.
P4 and M9.8 functional/structural/physical-device acceptance remain pending.
Future source changes require affected checks to run again; P1's result cannot
certify a later candidate or replace final same-commit/fixture evidence.
