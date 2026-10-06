const TOOL_DEFINITIONS = Object.freeze({
  select: Object.freeze({ label: '국가 선택', task: '지도 편집', stage: '작업 진행', cursor: 'select', icon: 'focus' }),
  move: Object.freeze({ label: '이동', task: '지도 이동', stage: '탐색', cursor: 'select', icon: 'map' }),
  'new-country': Object.freeze({ label: '국가 추가', task: '국가 추가', cursor: 'phased', special: true, icon: 'country' }),
  'annex-territory': Object.freeze({ label: '영토 편입', task: '영토 편입', cursor: 'phased', special: true, icon: 'transfer' }),
  'merge-country': Object.freeze({ label: '국가 합병', task: '국가 합병', stage: '합칠 국가 선택', cursor: 'country', special: true, icon: 'merge' }),
  'merge-generic-feature': Object.freeze({ label: '영역 합치기', task: '영역 합치기', stage: '대상 영역 선택', cursor: 'country', special: true, icon: 'merge' }),
  'split-generic-feature': Object.freeze({ label: '영역 나누기', task: '영역 나누기', stage: '경계 그리기', cursor: 'generic', special: true, icon: 'split', draft: Object.freeze({ shape: 'line', profile: 'boundary' }) }),
  'merge-territorial-unit': Object.freeze({ label: '영역 합치기', task: '영역 합치기', stage: '인접 영역 선택', cursor: 'country', special: true, icon: 'merge' }),
  'split-territorial-unit': Object.freeze({ label: '영역 나누기', task: '영역 나누기', stage: '경계 그리기', cursor: 'generic', special: true, icon: 'split', draft: Object.freeze({ shape: 'line', profile: 'boundary' }) }),
  'redraw-territorial-unit': Object.freeze({ label: '영역 다시 지정', task: '영역 다시 지정', stage: '영역 그리기', cursor: 'generic', special: true, icon: 'boundary', draft: Object.freeze({ shape: 'polygon', profile: 'area' }) }),
  'draw-territorial-unit': Object.freeze({ label: '영역 추가', task: '영역 추가', cursor: 'phased', special: true, icon: 'boundary' }),
  'territorial-border': Object.freeze({ label: '국경 조정', task: '국경 조정', stage: '공유국경 편집', cursor: 'phased', special: true, icon: 'boundary' }),
  'country-coast': Object.freeze({ label: '해안선 조정', task: '해안선 조정', stage: '해안선 편집', cursor: 'select', special: true, icon: 'coastline' }),
  label: Object.freeze({ label: '지명 배치', task: '지명 추가', stage: '위치 선택', cursor: 'generic', special: true, icon: 'place' }),
  river: Object.freeze({ label: '강 추가', task: '강 추가', stage: '경로 그리기', cursor: 'generic', special: true, icon: 'river', draft: Object.freeze({ shape: 'line', profile: 'river' }) }),
  lake: Object.freeze({ label: '호수 추가', task: '호수 추가', stage: '영역 그리기', cursor: 'generic', special: true, icon: 'lake', draft: Object.freeze({ shape: 'polygon', profile: 'area' }) }),
  polygon: Object.freeze({ label: '영역 그리기', task: '영역 그리기', stage: '경계 그리기', cursor: 'generic', icon: 'boundary', draft: Object.freeze({ shape: 'polygon', profile: 'area' }) }),
  line: Object.freeze({ label: '선 그리기', task: '선 그리기', stage: '경로 그리기', cursor: 'generic', icon: 'boundary', draft: Object.freeze({ shape: 'line', profile: 'river' }) }),
  point: Object.freeze({ label: '점 찍기', task: '점 찍기', stage: '위치 선택', cursor: 'generic', icon: 'place' }),
});

const territorySelectionForTool = (tool, state) => {
  const session = state.territorySelectionSession;
  return session?.tool === tool ? session : null;
};

export function describeTool(tool, state, { labelPlacement = false } = {}) {
  if (labelPlacement || tool === 'label') return { name: '지명 추가', stage: '위치 선택', icon: TOOL_DEFINITIONS.label.icon };
  const definition = TOOL_DEFINITIONS[tool] || TOOL_DEFINITIONS.select;
  const territorySelection = territorySelectionForTool(tool, state);
  if (territorySelection) {
    const stage = territorySelection.stage === 'setup'
      ? territorySelection.setupStageLabel
      : territorySelection.stage === 'review' ? '결과 확인' : '영역 선택';
    return { name: territorySelection.taskLabel, stage, icon: definition.icon };
  }
  if (tool === 'territorial-border') return { name: definition.task, stage: state.boundaryEditPhase === 'selecting' ? '맞닿은 국가 선택' : '공유국경 편집', icon: definition.icon };
  return { name: definition.task, stage: definition.stage || '작업 진행', icon: definition.icon };
}

export function toolCursorMode(tool, state, { labelPlacement = false } = {}) {
  const territorySelection = territorySelectionForTool(tool, state);
  const country = (territorySelection?.stage === 'setup' && territorySelection.setupCountryPicking)
    || (territorySelection?.stage === 'selection' && territorySelection.activePhase === 'source'
      && territorySelection.methodsRequiringSources?.includes(territorySelection.requestedMethod))
    || (tool === 'merge-country' && !!state.mergeSourceCountryId)
    || (tool === 'territorial-border' && state.boundaryEditPhase === 'selecting')
    || tool === 'merge-generic-feature'
    || tool === 'merge-territorial-unit';
  const generic = labelPlacement
    || ['polygon', 'line', 'river', 'lake', 'split-generic-feature', 'split-territorial-unit', 'redraw-territorial-unit'].includes(tool)
    || (territorySelection?.stage === 'selection' && territorySelection.activePhase === 'drawing'
      && ['line', 'polygon'].includes(territorySelection.activeMethod));
  const candidate = territorySelection?.stage === 'selection'
    && ['candidate', 'components'].includes(territorySelection.activePhase);
  return { country, generic, candidate, select: !country && !generic && !candidate };
}

export const toolLabel = tool => TOOL_DEFINITIONS[tool]?.label || String(tool || '');
export const isSpecialTool = tool => !!TOOL_DEFINITIONS[tool]?.special;

export function toolDraftDefinition(tool, state = {}) {
  const territorySelection = territorySelectionForTool(tool, state);
  if (territorySelection) {
    if (territorySelection.stage !== 'selection') return null;
    if (territorySelection.activePhase !== 'drawing') return null;
    if (territorySelection.activeMethod === 'line') return Object.freeze({ shape: 'line', profile: 'boundary' });
    if (territorySelection.activeMethod === 'polygon') return Object.freeze({ shape: 'polygon', profile: 'area' });
    return null;
  }
  const definition = TOOL_DEFINITIONS[tool];
  if (!definition?.draft) return null;
  return definition.draft;
}

export function dispatchTool(tool, handlers, fallback) {
  return (handlers[tool] || fallback)?.();
}
