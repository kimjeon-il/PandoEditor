// Immutable, serializable display lookup prepared off the main thread.
function buildIndex(rows, boundsFor) {
  const cells = Object.create(null), large = [], bounds = [];
  rows.forEach((row, index) => {
    const box = boundsFor(row); bounds.push(box);
    const [x0, y0, x1, y1] = box.map(Math.floor);
    if ((x1 - x0 + 1) * (y1 - y0 + 1) > 4096) { large.push(index); return; }
    for (let x = x0; x <= x1; x++) for (let y = y0; y <= y1; y++) (cells[`${x}:${y}`] ||= []).push(index);
  });
  return { cells, large, bounds };
}
export function prepareBoundaryDisplay(handles, segments) {
  const nodeKeys = Object.create(null);
  handles.forEach((handle, index) => { if (handle.nodeKey) nodeKeys[handle.nodeKey] = index; });
  return {
    nodeKeys,
    handles: buildIndex(handles, row => [...row.coordinate, ...row.coordinate]),
    segments: buildIndex(segments, row => {
      const [a, b] = [row.start, row.end];
      const radians = Math.PI / 180;
      const vector = ([lon, lat]) => [Math.cos(lat * radians) * Math.cos(lon * radians), Math.cos(lat * radians) * Math.sin(lon * radians), Math.sin(lat * radians)];
      const u = vector(a), v = vector(b);
      const dot = Math.max(-1, Math.min(1, u.reduce((sum, value, i) => sum + value * v[i], 0)));
      const angle = Math.acos(dot), sine = Math.sin(angle);
      const latitudes = [a[1], b[1]];
      let west = Math.min(a[0], b[0]), east = Math.max(a[0], b[0]);
      if (east - west > 180) { west = -180; east = 180; }
      if (angle > 1e-10 && Math.abs(sine) > 1e-10) {
        const z = (v[2] - u[2] * dot) / sine;
        const extremum = Math.atan2(z, u[2]);
        for (const t of [extremum - Math.PI, extremum, extremum + Math.PI]) if (t > 0 && t < angle) {
          latitudes.push(Math.asin(Math.max(-1, Math.min(1, u[2] * Math.cos(t) + z * Math.sin(t)))) / radians);
        }
      } else if (angle > 1) return [-180, -90, 180, 90];
      if (latitudes.some(lat => Math.abs(lat) > 89.999999)) { west = -180; east = 180; }
      return [west, Math.max(-90, Math.min(...latitudes) - 1e-7), east, Math.min(90, Math.max(...latitudes) + 1e-7)];
    }),
  };
}
export function boundaryViewBounds(frame, margin = 64) {
  if (!frame?.cssTranslate || !frame?.cssViewport || !frame?.cssScale) return null;
  if (frame.projectionKind === 'globe') {
    if (!frame.rowZ) return null;
    const [tx, ty] = frame.cssTranslate, [width, height] = frame.cssViewport;
    const radius = Math.max(...[-margin, width + margin].flatMap(x => [-margin, height + margin].map(y => Math.hypot(x - tx, y - ty)))) / frame.cssScale;
    const angle = Math.asin(Math.min(1, radius)) + 0.006; // Includes the renderer's small back-face allowance.
    const latitude = Math.asin(Math.max(-1, Math.min(1, frame.rowZ[2])));
    const longitude = Math.atan2(frame.rowZ[1], frame.rowZ[0]);
    const degrees = 180 / Math.PI;
    const south = Math.max(-90, (latitude - angle) * degrees), north = Math.min(90, (latitude + angle) * degrees);
    if (south <= -90 || north >= 90 || angle >= Math.PI / 2) return [[-180, south, 180, north]];
    const delta = Math.asin(Math.min(1, Math.sin(angle) / Math.cos(latitude)));
    const west = (longitude - delta) * degrees, east = (longitude + delta) * degrees;
    if (west < -180) return [[west + 360, south, 180, north], [-180, south, east, north]];
    if (east > 180) return [[west, south, 180, north], [-180, south, east - 360, north]];
    return [[west, south, east, north]];
  }
  if (frame.projectionKind !== 'flat') return null;
  const scale = frame.cssScale, [tx, ty] = frame.cssTranslate, [width, height] = frame.cssViewport;
  const [cx, cy] = frame.projectionCenter || frame.viewState?.projectionCenter || [0, 0];
  const degrees = 180 / Math.PI;
  const south = Math.max(-90, cy - (height + margin - ty) / scale * degrees);
  const north = Math.min(90, cy - (-margin - ty) / scale * degrees);
  if (south > north) return [];
  return (frame.worldOffsets || [0]).map(offset => [
    Math.max(-180, cx + (-margin - tx) / scale * degrees - offset * degrees), south,
    Math.min(180, cx + (width + margin - tx) / scale * degrees - offset * degrees), north,
  ]).filter(box => box[0] <= box[2]);
}
export function queryBoundaryDisplay(index, bounds, rows) {
  if (!index || bounds === null) return rows;
  const found = new Set();
  for (const box of bounds) {
    const [x0, y0, x1, y1] = box.map(Math.floor);
    const ids = new Set(index.large);
    if ((x1 - x0 + 1) * (y1 - y0 + 1) > 4096) rows.forEach((_, i) => ids.add(i));
    else for (let x = x0; x <= x1; x++) for (let y = y0; y <= y1; y++) for (const id of index.cells[`${x}:${y}`] || []) ids.add(id);
    for (const id of ids) {
      const b = index.bounds[id];
      if (b[0] <= box[2] && b[2] >= box[0] && b[1] <= box[3] && b[3] >= box[1]) found.add(id);
    }
  }
  return [...found].sort((a, b) => a - b).map(index => rows[index]);
}
