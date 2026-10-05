/**
 * Scans raw nested coordinate arrays without applying projection or date-line
 * wrapping. Empty or wholly malformed input leaves the caller-owned sentinel
 * unchanged.
 */
export function coordinateBounds(value, bounds = [Infinity, Infinity, -Infinity, -Infinity]) {
  if (!Array.isArray(value)) return bounds;
  if (value.length >= 2 && !Array.isArray(value[0]) && !Array.isArray(value[1])
    && Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1]))) {
    const x = Number(value[0]);
    const y = Number(value[1]);
    bounds[0] = Math.min(bounds[0], x);
    bounds[1] = Math.min(bounds[1], y);
    bounds[2] = Math.max(bounds[2], x);
    bounds[3] = Math.max(bounds[3], y);
    return bounds;
  }
  value.forEach(item => coordinateBounds(item, bounds));
  return bounds;
}
