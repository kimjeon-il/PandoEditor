// Preserve dash distance across topology segments, stopping at junctions.
export function connectBoundarySegments(segments) {
  const key = point => point.map(value => Number(value).toFixed(7)).join(',');
  const edges = segments.map(([a, b]) => ({ a, b, start: key(a), end: key(b) }));
  const nodes = new Map();
  edges.forEach((edge, index) => {
    for (const endpoint of [edge.start, edge.end]) {
      if (!nodes.has(endpoint)) nodes.set(endpoint, []);
      nodes.get(endpoint).push(index);
    }
  });
  const used = new Set(), lines = [];
  function walk(index, start) {
    const points = [];
    let cursor = start;
    while (!used.has(index)) {
      used.add(index);
      const edge = edges[index], forward = edge.start === cursor;
      if (!points.length) points.push(forward ? edge.a : edge.b);
      points.push(forward ? edge.b : edge.a);
      cursor = forward ? edge.end : edge.start;
      const incident = nodes.get(cursor);
      if (incident.length !== 2) break;
      const next = incident.find(candidate => !used.has(candidate));
      if (next === undefined) break;
      index = next;
    }
    lines.push(points);
  }
  for (const [node, incident] of nodes) if (incident.length !== 2) {
    for (const index of incident) if (!used.has(index)) walk(index, node);
  }
  edges.forEach((edge, index) => { if (!used.has(index)) walk(index, edge.start); });
  return lines;
}
