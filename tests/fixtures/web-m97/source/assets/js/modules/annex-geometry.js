const clone = value => value == null ? value : structuredClone(value);

function polygonCoordinates(geometry) {
  if (geometry?.type === 'Polygon') return [geometry.coordinates || []];
  if (geometry?.type === 'MultiPolygon') return geometry.coordinates || [];
  return [];
}

function geometryFromCoordinates(coordinates) {
  if (!Array.isArray(coordinates) || !coordinates.length) return null;
  return { type: 'MultiPolygon', coordinates: clone(coordinates) };
}

export function planDrawnTerritoryAnnex({ drawnGeometry, donorFeatures = [], targetFeature = null, clipper } = {}) {
  if (!drawnGeometry || !clipper?.intersection || !clipper?.difference || !clipper?.union) return null;
  const donors = donorFeatures.filter(feature => polygonCoordinates(feature?.geometry).length);
  if (!donors.length || !targetFeature?.geometry) return null;
  const donorUnionCoordinates = clipper.union(...donors.map(feature => polygonCoordinates(feature.geometry)));
  let transferCoordinates = clipper.intersection(polygonCoordinates(drawnGeometry), donorUnionCoordinates);
  if (!transferCoordinates?.length) return null;
  transferCoordinates = clipper.difference(transferCoordinates, polygonCoordinates(targetFeature.geometry));
  const transferGeometry = geometryFromCoordinates(transferCoordinates);
  if (!transferGeometry) return null;
  const donorChanges = donors.map(feature => {
    const overlap = clipper.intersection(polygonCoordinates(feature.geometry), transferCoordinates);
    return { countryId: String(feature?.id || ''), geometry: geometryFromCoordinates(overlap) };
  }).filter(item => item.geometry);
  return {
    transferGeometry,
    donorChanges,
    targetCountryId: String(targetFeature?.id || ''),
  };
}
