# Official Schleswig-Holstein WMS period-map coast review — October 9, 2026

**Working branch:** `work/gis`  
**Reference state:** German Empire 1914-07-31, provisional modern-coast working geometry.  
**Provenance:** [Official Preußische Landesaufnahme (Chronologen) WMS](https://www.govdata.de/suche/daten/preussische-landesaufnahme-bis-1950-chronologen), WMS layers `3` (1878–1880) and `2` (1902–1930), **© GeoBasis-DE/LVermGeo SH/CC BY 4.0**. Layer intervals **are not per-sheet survey dates**.  
**Method:** Historic WMS raster rendered into known EPSG:4326 map bounds, overlain with the project's **temporary modern** main-polygon coast (red). Human-picked coastline-symbol *example points* were projected at web max flat zoom 64 and 2560 CSS px width, then compared by nearest modern polygon segment. This is a sampled **diagnostic, not historical 1914 geometry verification**.

## Verified source acquisition

The GitHub Actions source probe `37921081699` successfully returned **8 nonempty original WMS raster images** over 4 coastal windows: Eider mouth, Husum, Hauke-Haien-Koog and Beltringharder Koog, each with 1878–1880 and 1902–1930 layer variants. Exact input BBOX, layer IDs, SHA-256 raster hashes and temporary review imagery filenames are recorded in [WMS acquisition report](sh-wms-period-overlay-probe.json). An earlier wide-area fetch returned empty transparent PNGs; the native WMS images appear with compact windows around each coastal segment. The resulting JPEG overlays are retained as GitHub Actions artifacts for only 7 days; the hashes and reproducible requests are retained in JSON.

The two WMS epochs present visibly different cartography but broad coincident pre-1950 coast shapes for the selected Eider and North Frisia cases. **The visible historical map image is not by itself a traced 1914 coastline.**

## Visual-example differences against project's temporary modern-coast line

Selected pixels from the source overlay thumbnails were converted back to EPSG:4326 and projected with the *actual web flat-zoom CSS pixel scale*. Distances were then calculated to the nearest line segment of the working polygon's main exterior.

| Location | Raster period group | Examined example points | Largest sample discrepancy |
|---|---|---:|---:|
| hauke-haien-koog | 1902–1930 | 5 | 약 6.4px |
| hauke-haien-koog | 1878–1880 | 5 | 약 6.2px |
| beltringharder | 1902–1930 | 5 | 약 5.3px |
| beltringharder | 1878–1880 | 4 | 약 5.5px |
| eider-mouth | 1902–1930 | 5 | 약 4.4px |
| eiderstedt-husum | 1902–1930 | 4 | 약 0.6px |

Machine-readable sample coordinates, screen distances, geographic conversion formula, provenance and uncertainties: [`sh-pre1914-era-screen-point-samples.json`](sh-pre1914-era-screen-point-samples.json).

**How to read these numbers:** For example, `Hauke-Haien-Koog 6.4px` means *one manually identified apparent coast-symbol position on the 1902–1930 group overlay* is approximately 6.4 CSS px from the modern working polygon. This is **NOT** the maximum 1914-to-2026 coastline deviation. The maximum along the untraced full coast may differ, and visually selected points can be dyke/foreshore rather than the appropriate legal land boundary. Accordingly, these readings are rounded to approximately 0.1 CSS px for communication, without claiming 0.1px historical accuracy.

### What is now independently supported

- **Hauke-Haien-Koog (1958/59 closure):** 1878–1880 and 1902–1930 official historical map groups show pre-inclosure water/embankment shapes; the modern working shore is visibly displaced at selected northern sample points (6.2–6.4px). The 1958/59 construction chronology is separately documented by [Jordsand](https://www.jordsand.de/hauke-haien-koog). It includes storage water bodies and cannot be made uniformly dry land.
- **Beltringharder Koog (1987 closure):** clear old coast/seaward dike and modern red line mismatch, about 5.3–5.5px at selected northern points. The [Beltringharder Koog history](https://www.beltringharderkoog.de/der-beltringharder-koog/entstehung/) confirms the pre-1987 Nordstrander Bucht and Nordstrand island-to-peninsula connectivity change; **topological chronology alone makes this a mandatory follow-up independent of 0.5px**. The enclosed koog still contains water, tidal lagoon and wetland.
- **Eider mouth (1973 barrage):** the period map shows the pre-barrage estuary banks; one illustrated southern-bank sample is about 4.4px from modern working geometry. Since [the barrier was opened in March 1973](https://www.outdooractive.com/en/poi/nordfriesland/eidersperrwerk/1283954/), the 1914 map must not show that structure.
- **Husum (control):** the four selected coast-symbol samples have maximum about 0.6px, but this does **not** clear Husum's full coastline. These samples merely flag less dramatic displacements than the three other cases. Do **not** mark the wider Husum coast `0.5px passed`.

## Blocking issues / next independent geometry pass

1. Establish exact survey/revision/edition year for each `Chronologen_2_1902-1930` coastline raster sheet. A publication interval covering 1930 cannot simply certify 1914.
2. On complete-resolution WMS original images, identify the **actual land/sea edge**, separately from historical dyke, levee, salt marsh, exposed foreshore, enclosed water and tidal lagoon. Validate georeferencing and map symbol interpretation using independent control points.
3. Digitize a complete 1914 coastline candidate for prioritized small windows, including old island/component connectivity around Nordstrand, without changing the original modern coastline.
4. Measure **bilateral projected-line Hausdorff distance** between genuine 1914 and modern polygons in the web's exact 2560 CSS px / 64 zoom display. Do not treat these sampled point differences as completed `screen-delta-checked`.
5. Apply only verified 1914 corrections to an isolated temporal working version after consulting the chronology. **Do not alter `main`, current-world coast, or 1914 master just from these overlays.**

**Status:** `source-overlay-acquired` + `manual visual sample preliminary`. Historical country geometry stays `provisional`. **1914 screen-delta validation has not passed and has not failed**, because an independently dated and fully interpreted period shoreline is still missing.

Use the project's [historical reconstruction policy](../../../../docs/historical-border-reconstruction-policy.md).
