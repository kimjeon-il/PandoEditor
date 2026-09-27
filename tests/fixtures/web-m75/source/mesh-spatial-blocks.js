// Worker-side index reordering only: vertex values and primitive winding stay exact.
export function prepareMeshSpatialBlocks(mesh) {
  const vectors = new Float64Array(mesh.positions.length / 2 * 3);
  for (let i = 0; i < mesh.positions.length / 2; i++) {
    const lon = mesh.positions[i * 2] * Math.PI / 180e6, lat = mesh.positions[i * 2 + 1] * Math.PI / 180e6;
    vectors[i * 3] = Math.cos(lat) * Math.cos(lon); vectors[i * 3 + 1] = Math.cos(lat) * Math.sin(lon); vectors[i * 3 + 2] = Math.sin(lat);
  }
  const build = (indices, ranges, stride, limit) => {
    const centroids = new Float64Array(indices.length / stride * 3);
    for (let p = 0; p < indices.length / stride; p++) for (let k = 0; k < stride; k++) for (let axis = 0; axis < 3; axis++) centroids[p * 3 + axis] += vectors[indices[p * stride + k] * 3 + axis] / stride;
    const output = new Uint32Array(indices.length), nodes = [], roots = [];
    function partition(order, first) {
      const id = nodes.length, box = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity], geo = [Infinity, Infinity, -Infinity, -Infinity];
      for (const p of order) for (let k = 0; k < stride; k++) {
        const vertex = indices[p * stride + k];
        for (let axis = 0; axis < 3; axis++) { const v = vectors[vertex * 3 + axis]; box[axis] = Math.min(box[axis], v); box[axis + 3] = Math.max(box[axis + 3], v); }
        const x = mesh.positions[vertex * 2] / 1e6, y = mesh.positions[vertex * 2 + 1] / 1e6;
        geo[0] = Math.min(geo[0], x); geo[1] = Math.min(geo[1], y); geo[2] = Math.max(geo[2], x); geo[3] = Math.max(geo[3], y);
      }
      const node = { first, count: order.length * stride, left: -1, right: -1, box, geo }; nodes.push(node);
      if (order.length > limit) {
        let axis = 0; for (let a = 1; a < 3; a++) if (box[a + 3] - box[a] > box[axis + 3] - box[axis]) axis = a;
        order.sort((a, b) => centroids[a * 3 + axis] - centroids[b * 3 + axis] || a - b);
        const middle = Math.floor(order.length / 2);
        node.left = partition(order.subarray(0, middle), first); node.right = partition(order.subarray(middle), first + middle * stride);
      } else {
        let target = first;
        for (const p of order) for (let k = 0; k < stride; k++) output[target++] = indices[p * stride + k];
      }
      return id;
    }
    for (let country = 0; country < ranges.length / 2; country++) {
      const first = ranges[country * 2], count = ranges[country * 2 + 1];
      if (!count) { roots.push(-1); continue; }
      roots.push(partition(Uint32Array.from({ length: count / stride }, (_, i) => first / stride + i), first));
    }
    return { indices: output, tree: { roots: Int32Array.from(roots), ranges: Uint32Array.from(nodes.flatMap(n => [n.first, n.count])), children: Int32Array.from(nodes.flatMap(n => [n.left, n.right])), bounds: Float64Array.from(nodes.flatMap(n => n.box)), geographicBounds: Float64Array.from(nodes.flatMap(n => n.geo)) } };
  };
  const triangles = build(mesh.triangleIndices, mesh.countryTriangleRanges, 3, 2048);
  const boundaries = build(mesh.lineIndices, mesh.countryBoundaryRanges, 2, 4096);
  mesh.triangleIndices = triangles.indices; mesh.lineIndices = boundaries.indices;
  mesh.spatialBlocks = { version: 1, triangle: triangles.tree, boundary: boundaries.tree };
  return mesh;
}

export function spatialBlockTransferables(mesh) {
  return Object.values(mesh.spatialBlocks || {}).filter(value => value && typeof value === 'object').flatMap(tree => Object.values(tree).map(array => array.buffer));
}

const validation = new WeakMap();
export function visibleSpatialBlockRanges(mesh, frame, kind = 'triangle', padding = 64, maxRanges = 96) {
  const tree = mesh.spatialBlocks?.[kind];
  if (!tree || mesh.spatialBlocks.version !== 1) return null;
  const fullCount = (kind === 'triangle' ? mesh.triangleIndices : mesh.lineIndices).length;
  if (!validation.has(tree)) {
    const count = tree.ranges?.length / 2;
    const valid = tree.roots instanceof Int32Array && tree.ranges instanceof Uint32Array && tree.children instanceof Int32Array && tree.bounds instanceof Float64Array && tree.geographicBounds instanceof Float64Array
      && Number.isInteger(count) && tree.bounds.length === count * 6 && tree.geographicBounds.length === count * 4 && tree.children.length === count * 2
      && [...tree.roots].every(n => n >= -1 && n < count)
      && Array.from({ length: count }, (_, n) => n).every(n => tree.ranges[n * 2] + tree.ranges[n * 2 + 1] <= fullCount && [tree.children[n * 2], tree.children[n * 2 + 1]].every(c => c === -1 || (c > n && c < count)))
      && [...tree.bounds, ...tree.geographicBounds].every(Number.isFinite);
    validation.set(tree, valid);
  }
  if (!validation.get(tree)) return null;
  const ranges = [], [width, height] = frame.cssViewport, [tx, ty] = frame.cssTranslate, scale = frame.cssScale;
  const interval = (node, row) => {
    let low = 0, high = 0;
    for (let a = 0; a < 3; a++) { const x = tree.bounds[node * 6 + a] * row[a], y = tree.bounds[node * 6 + a + 3] * row[a]; low += Math.min(x, y); high += Math.max(x, y); }
    return [low, high];
  };
  const visible = node => {
    if (frame.mode === 0) {
      if (interval(node, frame.rowZ)[1] < -padding / Math.max(1, scale)) return false;
      const x = interval(node, frame.rowX), y = interval(node, frame.rowY);
      return tx + scale * x[1] >= -padding && tx + scale * x[0] <= width + padding && ty + scale * y[1] >= -padding && ty + scale * y[0] <= height + padding;
    }
    const [west, south, east, north] = tree.geographicBounds.subarray(node * 4, node * 4 + 4), rad = Math.PI / 180;
    if (east - west > 180 || west < -180 || east > 180) return true;
    const center = frame.flatCenter || [0, 0];
    if (ty - scale * (south * rad - center[1]) < -padding || ty - scale * (north * rad - center[1]) > height + padding) return false;
    return (frame.worldOffsets || [0]).some(offset => tx + scale * (east * rad + offset - center[0]) >= -padding && tx + scale * (west * rad + offset - center[0]) <= width + padding);
  };
  const visit = node => {
    if (node < 0 || !visible(node)) return;
    if (tree.children[node * 2] >= 0) { visit(tree.children[node * 2]); visit(tree.children[node * 2 + 1]); }
    else { const first = tree.ranges[node * 2], count = tree.ranges[node * 2 + 1], previous = ranges.at(-1); if (previous && previous.first + previous.count === first) previous.count += count; else ranges.push({ first, count }); }
  };
  for (const root of tree.roots) visit(root);
  while (ranges.length > maxRanges) {
    let best = 0; for (let i = 1; i < ranges.length - 1; i++) if (ranges[i + 1].first - ranges[i].first - ranges[i].count < ranges[best + 1].first - ranges[best].first - ranges[best].count) best = i;
    ranges[best].count = ranges[best + 1].first + ranges[best + 1].count - ranges[best].first; ranges.splice(best + 1, 1);
  }
  return ranges;
}
