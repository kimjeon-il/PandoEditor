# Historical German-coast shoreline change inventory

This directory is an **evidence index**, not a substitute for historical coastline geometry.
Its geographic scope is the coastline of the 1914 German Empire and related
subsequent German-state history, **regardless of the sovereign of each site
when the change happened**. The same event can inform German Empire, German
Reich (1919–1945), postwar and modern maps without duplicating it under each
political owner.

- [`change-events.json`](change-events.json) records dated coastal works and
  an initial map-sheet inventory. Each event cites an independently accessible
  source key from the top-level `sources` object.
- `datedWorks` records the **evidence date**, not an exact time-series polygon
  switch. Construction, dyke closure, port opening and formal completion can
  be different events. Unknown exact switch dates remain `null`.
- An enclosed koog, lagoon, salt marsh, tidal basin or flood-protected lowland
  is **not automatically dry land**. The GIS digitizer must identify the
  relevant land, foreshore, water and engineered connections individually.
- Every currently indexed event is `source-indexed` with
  `geometry.status = "not-digitized"`. The catalogue alone **must not**
  change a country polygon, coastline source, country owner or timeline.
- Map-sheet catalogue years may be edition or publication years, not survey
  years. Geological editions are marked as unsuitable for direct standalone
  shoreline geometry control; none of the listed rasters is georeferenced
  or quantitatively overlaid by this catalogue.

## How to advance a sector

1. Locate the appropriate dated event and its source. Confirm survey,
   revision, construction and closure dates separately.
2. Check one independent period map against the relevant existing working
   coastline. Preserve original source references and original raster/vector.
3. Digitize the **period-specific** coastal geometry in the established
   historical-library workflow, not into this research-only JSON file.
4. Compare at the web flat-map max-zoom 64, width 2560 CSS px, aiming for
   at most 0.5 CSS px against an **independent digitized period boundary**.
   Historical ownership and topological/date errors take precedence over pixels.
5. Once a geometry and time interval are actually validated, update evidence
   and apply only that independently verified patch to the working polygon.
   Do not overwrite the contemporary coastline or another temporal variant.

See [the mandatory GIS reconstruction policy](../../../../docs/historical-border-reconstruction-policy.md).
The current 1914 working polygon continues to contain **provisional modern
coastline sections**. This inventory neither fixes nor conceals that gap.

Focused validation:
`node --test tests/unit/historical-german-coastline-events.test.mjs`

## October 2026 Schleswig-Holstein WMS period-map overlays

The official LVermGeo SH chronological Prussian Landesaufnahme WMS returned
eight dated-*group* (not exact sheet-year) historical raster overlays from a
GitHub Actions source-probe run `37921081699`, for the Eider mouth, Husum,
Hauke-Haien-Koog and Beltringharder Koog (1878–1880 versus 1902–1930 layers).
The request metadata, original raster SHA-256 values and temporary artifact
filenames are in [`sh-wms-period-overlay-probe.json`](sh-wms-period-overlay-probe.json).
Reproduce with [the source-only script](../../../../tools/probe_german_empire_1914_coast_period_rasters.py).
The map attribution is **© GeoBasis-DE/LVermGeo SH/CC BY 4.0**.

Small, manually selected *visual-example point* offsets against the website's
temporary modern coast total about 6.4 CSS px at Hauke-Haien-Koog,
5.3 CSS px at Beltringharder Koog and 4.4 CSS px at the Eider mouth, at the
project's 2560 CSS px / flat zoom 64 settings.
**These are neither the verified 1914 displacement nor full coast maxima.**
The 1902–1930 layer is a mixed period, exact sheet years/georeferencing have
not been individually checked, the shoreline-versus-dike symbol needs independent
interpretation, and the hand-selected sample set is not a continuous historical
coastline.

See [short overview](1914-sh-period-map-overlay-review.md) and
[point-level reproducibility/provenance](sh-pre1914-era-screen-point-samples.json).
No geometry or timeline has been changed by this source work. Period-map vector
digitization and actual screen Hausdorff validation remain outstanding.

## North Friesland chronology gap closed in the event catalogue (2026-10-09)

A high-resolution official WMS microtile comparison showed a **new seaward
dyke west of Klein Königs Pieck / Meedhallig** in the 1902–1930 layer that is
not present on the same 1878–1880 raster. The **independent municipality of
Reußenköge** identifies it with the Cecilienkoog construction **1903–1905**
and 1906 land allocation. This is decisive historical chronology: the
1905-protected polder was *already present* by the German Empire reference
date 1914-07-31, unlike Sönke-Nissen-Koog (1924–1926), later Beltringharder
Koog (1987), or a 1939 permanent winter dyke built atop a 1913
Galmsbüller Sommerkoog. **A summer dyke does not necessarily prove
permanently dry land.**

The event inventory records four newly sourced changes:

- `cecilienkoog-1903-1905` — protected polder should be represented in
  1914; whether mapped tidal foreshore is legal land remains unverified.
- `galmsbuell-summer-koog-1913` — preserve 1913 seasonal dyke regime,
  rather than applying 1939 winter protection.
- `soenke-nissen-koog-1924-1926` — post-1914, exclude from 1914 dry land.
- `galmsbuell-winter-dyke-1933-1939` — not a 1914 polygon switch.

**Provisional independent period-map vector:** 25 source image points were
read from the 1902–1930 WMS raster for the seaward Cecilienkoog **dyke**
and converted into a ~2.18 km `LineString`. At current web flat zoom 64
(2560 CSS px width) its **one-way directed distance** to the *temporary
generalized modern working coast* peaks at **~7.59 CSS px** with 169
line-interpolated samples. This is **not** the 1914-to-present historic
shoreline Hausdorff distance; the source WMS epoch group spans 1902–1930,
sheet revision year and georeferencing residual remain unchecked, and a
dyke crest/foot cannot automatically be used as the high-tide line.

- [Manual image-to-coordinate trace source](cecilienkoog-1905-dyke-manual-source.json)
- [High-resolution two-epoch WMS microtile metadata](sh-coast-microtile-source-probe.json)
- [Separated provisional dyke vector](../../working/german-empire-1914-cecilienkoog-1905-seaward-dyke-candidate.geojson)
- [Derived directed pixel QA](../../working/german-empire-1914-cecilienkoog-1905-seaward-dyke-candidate.qa.json)
- Reproducible build: `python tools/build_german_cecilienkoog_1905_provisional.py`, followed by `--check`.
- GitHub Actions renders a blue historical dyke versus red current
  working coast overlay for human review; only the derived line,
  source metadata and QA are committed, **not the third-party map PNGs**.

**Do not substitute the traced dyke for a historical coastline.** Confirm
the 1902–1930 layer's exact local sheet year, track the seaward dyke toe and
tidal wetland, check old/new component topology, and reconstruct the 1914
land/water edge independently before promoting any of it into a country polygon.

## Helgoland 1914: working geometry blocker

The existing German Empire 1914 **working polygon** contains the present-day
Helgoland and Düne OSM island rings. They establish that both island components
exist, but **not** their historical shapes. This is explicitly marked in the
working GeoJSON's `properties.heligoland1914GeometryStatus` and independently
recorded in [Helgoland temporal QA](../../working/german-empire-1914-helgoland-temporal-qa.json).

- The 1908–1916 southern harbor works were still underway in July 1914.
- The 1938–1941 Nordostland and Düne additions are **later** than the German
  Empire; proposed but unbuilt Hummerschere outlines must not be digitized.
- The public-domain UK Admiralty No. 126 chart (published 22 Sep 1914) is a
  1:15,000 control source. A Wikimaps Warper project reports six GCPs; neither
  georeferencing residuals nor the chart's original survey/revision date have
  been checked. The 2009 *Die Küste* figure compares 1890, 1903, 1916, 1928,
  1940 and 1970 main-island configurations; this is a comparison aid, **not**
  a georeferenced period coastline.

**Next geometric step:** independently rectify the 1914 chart, digitize the
period coastline for the main island and Düne **separately**, and check the
screen-space difference at the production web baseline (flat zoom 64, 2560 CSS
pixels). Resolve the harbor-versus-dry-land and island topology before replacing
one of the modern working components. No guessed buffer or area-preserving
shrink operation is allowed.

Validate the provisional geometry evidence contract with
`node --test tests/unit/historical-german-coastline-helgoland.test.mjs`.
