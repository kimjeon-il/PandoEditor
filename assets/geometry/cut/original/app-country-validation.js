/** CountryValidation: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createCountryValidation() {
  let dependencies;

  function connect(ports) {
    if (dependencies) throw new Error('country-validation already connected');
    dependencies = ports;
  }

  function unwrapRingLongitudes(rawRing) {
    const ring = (0, dependencies.geometryModel.ensureClosedRing)(rawRing);
    if (!ring.length) return [];
    const out = [[ring[0][0], ring[0][1]]];
    for (let i = 1; i < ring.length; i += 1) {
      let lon = ring[i][0];
      const previous = out[i - 1][0];
      while (lon - previous > 180) lon -= 360;
      while (lon - previous < -180) lon += 360;
      out.push([lon, ring[i][1]]);
    }
    return out;
  }

  function orientation2d(a, b, c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  }

  function segmentsProperlyIntersect(a, b, c, d, epsilon = 1e-10) {
    const abC = orientation2d(a, b, c);
    const abD = orientation2d(a, b, d);
    const cdA = orientation2d(c, d, a);
    const cdB = orientation2d(c, d, b);
    return ((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon)) &&
      ((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon));
  }

  function ringHasSelfIntersection(rawRing) {
    const ring = unwrapRingLongitudes(rawRing);
    const segmentCount = Math.max(0, ring.length - 1);
    if (segmentCount < 4) return false;
    const segments = [];
    for (let index = 0; index < segmentCount; index += 1) {
      const a = ring[index], b = ring[index + 1];
      segments.push({
        index, a, b,
        minX: Math.min(a[0], b[0]), maxX: Math.max(a[0], b[0]),
        minY: Math.min(a[1], b[1]), maxY: Math.max(a[1], b[1]),
      });
    }
    segments.sort((a, b) => a.minX - b.minX || a.minY - b.minY || a.index - b.index);
    let active = [];
    for (const segment of segments) {
      active = active.filter(other => other.maxX >= segment.minX);
      for (const other of active) {
        if (Math.abs(segment.index - other.index) <= 1 ||
            (Math.min(segment.index, other.index) === 0 && Math.max(segment.index, other.index) === segmentCount - 1)) continue;
        if (other.maxY < segment.minY || other.minY > segment.maxY) continue;
        if (segmentsProperlyIntersect(segment.a, segment.b, other.a, other.b)) return true;
      }
      active.push(segment);
    }
    return false;
  }

  function snapGeometryToGrid(geometry, precision = 7) {
    if (!geometry?.coordinates) return geometry;
    const factor = 10 ** precision;
    const snap = value => {
      if (Array.isArray(value) && value.length >= 2 && Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1]))) {
        return [Math.round(Number(value[0]) * factor) / factor, Math.round(Number(value[1]) * factor) / factor];
      }
      return Array.isArray(value) ? value.map(snap) : value;
    };
    return { ...geometry, coordinates: snap(geometry.coordinates) };
  }

  function interpolateCoordinate(a, b, t) {
    let targetLon = b[0];
    while (targetLon - a[0] > 180) targetLon -= 360;
    while (targetLon - a[0] < -180) targetLon += 360;
    let lon = a[0] + (targetLon - a[0]) * t;
    while (lon > 180) lon -= 360;
    while (lon < -180) lon += 360;
    return [lon, a[1] + (b[1] - a[1]) * t];
  }

  function refreshCountryCentroids(ids = null) {
    const filter = ids ? new Set([...ids].map(String)) : null;
    (0, dependencies.countries.scheduleCountryLabelAnchors)(filter, 20);
  }

  return Object.freeze({
    connect,

    get interpolateCoordinate() { return interpolateCoordinate; },
    get refreshCountryCentroids() { return refreshCountryCentroids; },

    get ringHasSelfIntersection() { return ringHasSelfIntersection; },
    get segmentsProperlyIntersect() { return segmentsProperlyIntersect; },
    get snapGeometryToGrid() { return snapGeometryToGrid; },
  });
}
