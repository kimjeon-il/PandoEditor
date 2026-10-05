import { geometryRevision } from './geometry-versions.js';
const results = new WeakMap();
export function rememberCutPreparation(source, line, result) {
  results.set(source, { revision: geometryRevision(source), line: JSON.stringify(line), result });
}
export function preparedCut(source, line) {
  const entry = results.get(source);
  return entry?.revision === geometryRevision(source) && entry.line === JSON.stringify(line) ? entry.result : null;
}
