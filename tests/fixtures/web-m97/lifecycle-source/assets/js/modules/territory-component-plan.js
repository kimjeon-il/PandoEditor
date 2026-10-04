/** Read-only geometry preparation shared by all territory selection tools. */
export function createTerritoryComponentPlan({ clipper, normalize, checkpoint = async () => {} }) {
  const polygons = geometry => geometry?.type === 'Polygon' ? [geometry.coordinates]
    : geometry?.type === 'MultiPolygon' ? geometry.coordinates : [];
  const from = coordinates => coordinates?.length ? normalize({ type: 'MultiPolygon', coordinates }) : null;
  const union = geometries => {
    const values = geometries.filter(Boolean);
    if (!values.length) return null;
    if (values.length === 1) return values[0];
    return from(clipper.union(...values.map(polygons)));
  };
  const difference = (base, removed) => !base ? null : !removed ? base
    : from(clipper.difference(polygons(base), polygons(removed)));

  async function prepare({ features = [], baseGeometry = null, parts = [] }) {
    const archivedGeometry = union(parts);
    const baseSourceGeometry = baseGeometry || union(features.map(feature => feature.geometry));
    const workingSourceGeometry = difference(baseSourceGeometry, archivedGeometry);
    const items = [];
    const componentFeatures = [];
    for (const feature of features) {
      const countryId = String(feature.id);
      const coordinates = [];
      const origins = [];
      const source = polygons(feature.geometry);
      for (let sourcePolygonIndex = 0; sourcePolygonIndex < source.length; sourcePolygonIndex += 1) {
        await checkpoint();
        const original = { type: 'Polygon', coordinates: source[sourcePolygonIndex] };
        // Unedited polygons retain their coordinates and provenance without clipping.
        const remainder = difference(original, archivedGeometry);
        let fragment = 0;
        for (const polygon of polygons(remainder)) {
          const polygonIndex = coordinates.length;
          const geometry = { type: 'Polygon', coordinates: polygon };
          coordinates.push(polygon);
          origins.push(sourcePolygonIndex);
          items.push({
            key: `component:${countryId}:${sourcePolygonIndex}:${fragment++}`,
            countryId, polygonIndex, sourcePolygonIndex,
            componentKey: `${countryId}:${polygonIndex}`, geometry,
          });
        }
      }
      if (coordinates.length) componentFeatures.push({ ...feature,
        properties: { ...feature.properties, __territorySourceIndices: origins },
        geometry: { type: 'MultiPolygon', coordinates },
      });
    }
    return { items, componentFeatures, baseSourceGeometry, workingSourceGeometry, archivedGeometry };
  }

  async function selection({ selected = [], currentGeometry = null, archivedGeometry = null,
    workingSourceGeometry = null, components = false, candidates = false }) {
    await checkpoint();
    const current = components || candidates ? union(selected) : currentGeometry;
    await checkpoint();
    const combinedGeometry = union([archivedGeometry, current]);
    await checkpoint();
    return { currentGeometry: current, combinedGeometry,
      remainingGeometry: components ? workingSourceGeometry : difference(workingSourceGeometry, current) };
  }

  async function slivers({ groups = [], combinedGeometry }) {
    const result = [];
    for (const group of groups) {
      const unselectedGeometries = [];
      for (const geometry of group.geometries) {
        await checkpoint();
        const remaining = difference(geometry, combinedGeometry);
        if (remaining) unselectedGeometries.push(remaining);
      }
      result.push({ donorId: group.donorId, polygonIndex: group.polygonIndex, unselectedGeometries });
    }
    return result;
  }
  return { prepare, selection, slivers };
}
