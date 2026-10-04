import { readFileSync } from 'node:fs';

const corpus = JSON.parse(readFileSync(new URL('./timeline-records.json', import.meta.url), 'utf8'));
function applyChanges(target, changes = []) {
  for (const change of changes) {
    let parent = target;
    for (const key of change.path.slice(0, -1)) parent = parent[key];
    const key = change.path.at(-1);
    if (change.remove) delete parent[key];
    else parent[key] = structuredClone(change.value);
  }
  return target;
}
export function timelineCases() {
  return corpus.cases.map(row => ({
    ...row,
    input: applyChanges(structuredClone(corpus.base), row.changes),
    context: applyChanges(structuredClone(corpus.context), row.contextChanges),
  }));
}
export function timelineContext(context) {
  return {
    entities: context.entities,
    geometryExists: ref => context.geometryRefs.some(value => value.id === ref.id && value.version === ref.version),
  };
}
