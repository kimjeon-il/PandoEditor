import { readFileSync } from 'node:fs';
const corpus = JSON.parse(readFileSync(new URL('./timeline-storage.json', import.meta.url), 'utf8'));
export function timelineStorageCases() {
  return corpus.cases.map(row => {
    const input = structuredClone(corpus.base);
    for (const change of row.changes) {
      let parent = input;
      for (const key of change.path.slice(0, -1)) parent = parent[key];
      if (change.remove) delete parent[change.path.at(-1)];
      else parent[change.path.at(-1)] = structuredClone(change.value);
    }
    return { ...row, input, entities: structuredClone(row.entities ?? corpus.entities) };
  });
}
