# M7.1 world rendering fixture and baseline

Status: **fixture and Node verifier prepared; C++ validation and CPU measurements NOT RUN** in the current environment. This document is the M7.2 handoff draft, not an M7.1 completion claim.

## Pinned provenance

PandoEditor main implementation reference: `168fb7cf7ecde29544e9f65e10fca6a2530d1de0`. The M7 working tree started at the exact `codex/integration` tree `844cb945f76bb2ec76583eb6484c643820e744a3`. The upstream `world-map` commit is `c0bd31d13dc8495593d78cf51f7cc195de7c9469`.

| Source | Git blob |
|---|---|
| `assets/data/countries-ne-5.1.1.geojson` | `d79abcb4a47d49f188e0601c3721d9e10e6c7f52` |
| `assets/data/hydro/rivers_base.geojson` | `6ec119337f9c38b33bb498edbe351573b9e6aa60` |
| `assets/data/hydro/lakes_base.geojson` | `bf088f97a5362842635369656c57314f4521f398` |
| Canonical packet reference `countries-canonical-v0.33.0.pcg.gz` | `54146d9eeb28e4af08e094f5061bf64689e6bdf3` |
| GPU mesh reference `world-mesh-v0.12.6.bin.gz` | `8c73420b92e89ab64cbe75dcc5016efe2c0a22b6` |

The original Natural Earth 5.1.1 Admin 0 1:10m country source has **258 features and 548,454 positions**. The three source files above were obtained at the pinned commit and locally checked against their Git blob identities. The local source-only snapshot has a pinned commit marker; it is not a full `world-map` checkout. The generator requires matching raw blobs and either the exact Git HEAD or this explicit marker before writing any output. CI reads only the committed corpus and does not fetch `world-map`.

## Committed corpus

The 13 real countries contain **1,507 polygons, 1,509 rings, 2 holes, and 168,920 positions**. Geometry coordinates, order, type, winding, and ring hierarchy are copied from parsed upstream JSON without simplification, wrapping, snapping, or repair. The generator stores SHA-256 per geometry, a file hash per fixture, bounds, counts, and maximum consecutive longitude jump in `manifest.json`.

| ID | Why selected |
|---|---|
| DEU | Ordinary control |
| RUS | Very large multipart extent across ±180° |
| FJI | Multipart dateline extent |
| KIR | Many islands across the dateline |
| USA | Large multipart and Aleutian extent |
| FRA | Overseas multipart geometry |
| IDN | Many islands and multipart geometry |
| PHL | Many islands and multipart geometry |
| ATA | Polar, very large multipart geometry |
| ZAF | Polygon containing an enclave hole |
| LSO | Independent enclave country |
| CHL | Long, narrow country with distant parts |
| NOR | High latitude multipart geometry |

The deterministic river is `rivers_base:1159113097` (791 positions). The deterministic lake is `lakes_base:1159113191` (21 holes, 2,225 positions). The selection rules rank every eligible real source feature, with stable ID/name and source index ties.

`sentinels.geojson` contains **synthetic** DATELINE with a hole and a two-part north/south POLAR shape. They are separate from the real country and hydro files and are never described as Natural Earth or Hydro source features. `composition.json` defines a DEU subunit, nested region, overlapping language/ethnicity/religion geometry, generic polygon/line/point, manual and automatic labels, and references to both real hydro fixtures. The test-only loader constructs an in-memory `ProjectDocument` rather than a version-specific `.pando.json` file. Native distribution entries allow either a geometry or a territorial reference; the fixture uses explicit geometry for overlap and retains the composition territory ID in entry metadata.

## Verification evidence and environment

| Check | Result |
|---|---|
| Three pinned upstream Git blob hashes | PASS, local raw-byte check |
| 258-feature / 548,454-position source envelope | PASS, generator |
| 13 geometry objects equal parsed upstream geometry | PASS, direct comparison |
| Two regenerations versus committed real fixtures and manifest | PASS, four byte-identical files |
| Real river and lake selection | PASS, pinned feature IDs and geometry hashes |
| DATELINE and POLAR exact synthetic geometry | PASS, offline verifier |
| ZAF hole and independent LSO | PASS, committed geometry stats; C++ check NOT RUN |
| Mixed in-memory `ProjectDocument` validation | NOT RUN — CMake/Qt unavailable locally |
| Node corpus/verifier tests | PASS, 25 tests at latest local run |
| Offline verifier, five fixture files | PASS |
| C++ structural tests, projection diagnostics | NOT RUN — CMake/Qt unavailable locally |
| Five CPU baseline scenarios | NOT RUN — CMake/Qt unavailable locally |
| Fresh full CTest | NOT RUN — CMake/Qt unavailable locally |
| Windows physical platform | NOT RUN |
| Android physical platform | NOT RUN |

The baseline executable is designed for **1440×900 offscreen QImage CPU painting** with the current `MapRenderItem::paint()`, first and second paint at the same viewport. Its JSON reports structure, source and path sizes, load/rebuild/paint milliseconds, and risk diagnostics. It is neither a GPU benchmark nor visible Windows/Android performance. Hardware-dependent times are diagnostic only; **no FPS, memory, or elapsed-time pass/fail threshold exists yet**. Actual timing numbers remain unrecorded until the executable runs.

`coordinateSourceBytes` is the compact JSON byte count of coordinate arrays for geometry selected into that scenario, reserialized from parsed numeric values. `committedFixtureBytes` counts the complete six committed corpus files, independently of scenario. The offscreen paint uses a common diagnostic color for all domains, so its output does not establish visual separation or label/symbol correctness. Those visual checks remain in later M7 work.

The standalone M7.1 CI workflow builds and runs the full available CTest suite without an upstream checkout. Existing `hydro_full_dataset_tests` skips its upstream dataset gate in that workflow; zero failures there does not establish that this separate real-data gate ran. Record skips from the CTest log when CI executes.

## Current renderer risks and M7.2 contract

The current `MapProjection` computes a single flat longitude extent and serializes projected shapes as `QVariant` maps containing path strings. The current `MapRenderItem` parses those strings during CPU painting. Source RUS/FJI/KIR have nearly global longitude spans even though their individual source segments need not jump 180°; DATELINE deliberately has a raw segment over 180°. This is a risk diagnosis from source geometry and code paths, **not** a claim that M7.1 fixed the display.

The existing bounds-only `CountryView` built in `MapProjection::rebuild(ProjectDocument)` holds references initialized from temporary values. The mixed-content baseline exercises this path; its behavior needs an actual compiled run, and this lifetime risk must be resolved in M7.2 rather than patched into the M7.1 production renderer. A diagnostic or paint crash is a failed M7.1 gate, not a measured pass.

M7.2 receives canonical EPSG:4326 geometry, immutable `ProjectDocument`, manifest risk tags and these diagnostics. It must define antimeridian segmentation/wrapping, flat ±180° behavior, globe-ready geographic representation, both poles, holes and enclaves, huge multipolygons and many islands, typed render transport replacing bulk `QVariant`/path strings, spatial candidate filtering, and projection/view revisioning. The fixture coordinates must not be changed to hide a rendering defect.

Roadmap boundary: M7.1 fixes corpus and evidence; M7.2 defines projection/scene/spatial contracts; M7.3 implements the GPU renderer; M7.4 loads the complete 258-country world at runtime; M7.5 addresses culling, LOD and cache optimization. M7.1 does **not** mean the complete world renders correctly in the app.
