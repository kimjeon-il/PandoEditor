// Application boundary only. All geometry, keying and composition use the pinned module.
(function (row, module, clipper) {
  row = structuredClone(row); // Reject nonfinite or non-JSON data before any kernel work.
  const input = row.request || {};
  for (const index of row.liveDonorIndices || []) {
    const donor = input.donors[index];
    donor.geometryRevision = String(donor.countryId || '') + ':' + JSON.stringify(donor.geometry.coordinates || []);
  }
  const before = JSON.stringify(input);
  if (!row.omitClipper) input.clipper = clipper;
  const result = module.buildRiverTerritoryPartitions(input);
  delete input.clipper;
  const composeInput = { components: row.components || [], candidates: result.candidates, donorResults: result.donorResults };
  const composeBefore = JSON.stringify(composeInput);
  const composed = module.composeRiverBoundaryTerritoryComponents(composeInput);
  const output = {
    status: 'Completed', kernelInvoked: true,
    inputUnchanged: before === JSON.stringify(input),
    composeInputUnchanged: composeBefore === JSON.stringify(composeInput),
    result, composed,
  };
  if (row.includeIdentity) output.identity = {
    donorRevisionStrings: (input.donors || []).map(d => String(d.geometryRevision ?? d.properties?.geometryRevision ?? '')),
    editedRiverSignature: (row.signatureEdits || []).map(f => String(f.id) + ':' + JSON.stringify(f.geometry.coordinates || [])).sort().join('|'),
  };
  return output;
})
