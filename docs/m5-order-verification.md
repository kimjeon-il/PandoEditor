# M5 map draw and chooser order verification

Web source pin: `kimjeon-il/world-map` `c0bd31d13dc8495593d78cf51f7cc195de7c9469`.
The executable Oracle checks the blob IDs of the pinned scene pass, picker,
overlay-group and country-flag modules before running them. It records the raw
web base-pass calls and compares the **visible** opaque overlap stack with the
native `mapRenderOrder` probe. The web territorial stencil reserves a child's
pixel before the later country call; the native painter therefore paints the
country first. The chooser comparison calls the web `selectableVisualRank`
function and compares its sorted candidates separately from the draw result.

| Overlap | Visible top | Chooser first |
| --- | --- | --- |
| country / river | river | river |
| country / lake | lake | lake |
| country / religion | religion | religion |
| country / ethnicity | ethnicity | ethnicity |
| country / language | language | language |
| country / subunit | subunit | subunit |
| country / region | region | region |
| country / generic | generic | generic |
| country / place | place | place |
| country / label | label | label |
| river / lake | river | lake |
| river / religion | river | religion |
| river / ethnicity | river | ethnicity |
| river / language | river | language |
| river / subunit | river | subunit |
| river / region | river | region |
| river / generic | river | generic |
| river / place | place | place |
| river / label | label | label |
| lake / religion | lake | religion |
| lake / ethnicity | lake | ethnicity |
| lake / language | lake | language |
| lake / subunit | lake | subunit |
| lake / region | lake | region |
| lake / generic | lake | generic |
| lake / place | place | place |
| lake / label | label | label |
| religion / ethnicity | ethnicity | religion |
| religion / language | language | religion |
| religion / subunit | religion | religion |
| religion / region | religion | religion |
| religion / generic | generic | religion |
| religion / place | place | place |
| religion / label | label | label |
| ethnicity / language | language | ethnicity |
| ethnicity / subunit | ethnicity | ethnicity |
| ethnicity / region | ethnicity | ethnicity |
| ethnicity / generic | generic | ethnicity |
| ethnicity / place | place | place |
| ethnicity / label | label | label |
| language / subunit | language | language |
| language / region | language | language |
| language / generic | generic | language |
| language / place | place | place |
| language / label | label | label |
| subunit / region | region | subunit |
| subunit / generic | generic | subunit |
| subunit / place | place | place |
| subunit / label | label | label |
| region / generic | generic | region |
| region / place | place | place |
| region / label | label | label |
| generic / place | place | place |
| generic / label | label | label |
| place / label | label | same-ref |
| lake / lake-boundary | lake-boundary | same-ref |
| lake-boundary / river | river | lake-boundary |
| river / country-boundary | country-boundary | river |
| country-boundary / generic-line | generic-line | generic-line |
| generic-line / place | place | place |

The fixed native country/child paint order is asserted in the core probe and
by offscreen Qt pixels for nested country, subunit and region fills. Other
offscreen renderer tests cover the overlay groups, water, points, selected
outlines and holes. A 1100/360 px QML test checks that selected geometry is
present in the painter and the label/flag delegate is above that painter, with
the flag before the text in the delegate. The pinned flag function confirms
that adding a flag decorates an already placed label without evicting its name.

A live browser inspection of `https://kimjeon-il.github.io/world-map/` at
zoom above 1.8 showed country flags next to name labels above the map canvas
and river strokes. DOM computed z-index put the GPU canvas at 1 and the
interaction SVG containing labels at 4. That deployed page is not the pinned
commit, so this is a visual cross-check, not a pinned pixel golden. Clicking a
country in that deployment produced `PL-RUNTIME-001` (`self.d3.geo.contains is
not a function`); a selected-emphasis web screenshot could not be compared.
The 55 domain/role combinations and five extra primitive overlaps use the real pinned pass and picker policy but do
not claim browser screenshots for every pair. Translucent territorial stencil
ownership, actual label glyph pixels, flag images and all-domain hover/render
cache remain separate visual acceptance evidence.
