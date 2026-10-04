// Same application boundary as app-river-candidates: normalize AFTER raw keys,
// filter unusable candidates, then call the original composition function again.
(function (row, raw, module, normalize) {
  const candidates=raw.result.candidates.map(function(candidate) {
    const copy=structuredClone(candidate);
    copy.geometry=normalize(candidate.geometry);
    return copy;
  }).filter(function(candidate) { return candidate.geometry && candidate.donorCountryId; });
  const composed=module.composeRiverBoundaryTerritoryComponents({components:row.components || [],candidates:candidates,donorResults:raw.result.donorResults});
  return {candidates:candidates,composed:composed};
})
