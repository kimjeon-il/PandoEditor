/** Bounded faces of an already noded planar graph. Coordinates remain unchanged. */
export function tracePlanarGraphFaces(graph, edges = graph.edges) {
  const halfEdges = [];
  const outgoing = new Map();
  const add = (edge, from, to, twin) => {
    const id = halfEdges.length;
    halfEdges.push({ id, edgeId: edge.id, from, to, twin, visited: false });
    if (!outgoing.has(from)) outgoing.set(from, []);
    outgoing.get(from).push(id);
    return id;
  };
  for (const edge of edges) {
    const forward = add(edge, edge.a, edge.b, null);
    const reverse = add(edge, edge.b, edge.a, forward);
    halfEdges[forward].twin = reverse;
  }
  for (const [nodeId, ids] of outgoing) {
    const origin = graph.nodes[nodeId].point;
    const angle = id => {
      const point = graph.nodes[halfEdges[id].to].point;
      return Math.atan2(point[1] - origin[1], point[0] - origin[0]);
    };
    ids.sort((left, right) => angle(left) - angle(right));
  }
  const faces = [];
  for (const start of halfEdges) {
    if (start.visited) continue;
    const ring = [];
    const edgeIds = [];
    let current = start;
    while (!current.visited) {
      current.visited = true;
      ring.push(graph.nodes[current.from].point);
      edgeIds.push(current.edgeId);
      const choices = outgoing.get(current.to);
      const index = choices.indexOf(current.twin);
      current = halfEdges[choices[(index - 1 + choices.length) % choices.length]];
    }
    if (current.id !== start.id || ring.length < 3) continue;
    ring.push([...ring[0]]);
    // Translate before summing to avoid cancellation for small faces at high longitude.
    const origin = ring[0];
    let area = 0;
    for (let index = 1; index < ring.length; index += 1) {
      const a = ring[index - 1], b = ring[index];
      area += (a[0] - origin[0]) * (b[1] - origin[1]) - (b[0] - origin[0]) * (a[1] - origin[1]);
    }
    area /= 2;
    if (area > 0) faces.push({ ring, area, edgeIds: [...new Set(edgeIds)] });
  }
  return faces;
}
