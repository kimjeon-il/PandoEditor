const RESULT_KEYS = new Set(['ArrowDown', 'ArrowUp', 'Home', 'End']);

export function createTerritorialLibraryController({
  document,
  elements,
  service,
  selectGeometryVersion,
  renderMapPreview,
  createEmptyState,
  replaceSelectOptions,
  shouldShowTerritorialParentChoice,
  closeSurface,
  focusSurfaceTrigger,
  instantiate,
  ownershipContext = () => ({ missing: [], countries: [], parents: () => [] }),
  setStatus,
  reportError,
  getProjectGeneration,
  requestFrame = callback => requestAnimationFrame(callback),
}) {
  let selectedId = '';
  let loading = false;
  let requestGeneration = 0;
  let ownershipChoices = null;
  let confirmedImpact = '';
  let flagPicker = null;

  function resetOwnership() {
    ownershipChoices = null;
    confirmedImpact = '';
    elements.ownership?.replaceChildren();
    elements.ownership?.classList.add('hidden');
    if (elements.add) elements.add.textContent = '추가';
  }

  function showOwnership(context) {
    const host = elements.ownership;
    if (!host) throw new Error('소속 설정 화면을 찾을 수 없습니다.');
    ownershipChoices = {};
    host.replaceChildren();
    host.classList.remove('hidden');
    const heading = document.createElement('h3');
    heading.textContent = '소속 설정';
    host.append(heading);
    function field(title, control) {
      const label = document.createElement('label');
      label.className = 'ui-field field-group';
      const text = document.createElement('span');
      text.textContent = title;
      label.append(text, control);
      host.append(label);
      return label;
    }
    for (const item of context.missing) {
      const choice = { mode: 'child', countryId: item.countryId, parentId: item.countryId, name: item.name };
      ownershipChoices[item.entityId] = choice;
      const mode = document.createElement('select');
      replaceSelectOptions(mode, [
        { value: 'child', label: '상위 객체 아래에 추가' },
        { value: 'root', label: '최상위 객체로 추가' },
      ], 'child');
      field(`${item.name} · 추가 방식`, mode);
      const country = document.createElement('select');
      const countryChoice = replaceSelectOptions(country, [
        { value: '', label: '소속 국가 선택', placeholder: true },
        ...context.countries,
      ], choice.countryId, { autoSelectSingle: true }) || { single: false, value: country.value };
      choice.countryId = countryChoice.value;
      const countryRow = field('소속 국가', country);
      const parent = document.createElement('select');
      const parentRow = field('상위 객체', parent);
      const name = document.createElement('input');
      name.value = item.name;
      const nameRow = field('객체 이름', name);
      function sync() {
        choice.mode = mode.value;
        choice.name = name.value;
        choice.countryId = country.value;
        const candidates = context.parents(country.value);
        const options = candidates.length
          ? candidates
          : [{ value: '', label: '상위 단위 선택', placeholder: true }];
        const parentChoice = replaceSelectOptions(parent, options, choice.parentId, { autoSelectSingle: true }) || { value: parent.value };
        choice.parentId = parentChoice.value;
        countryRow.hidden = mode.value === 'root' || countryChoice.single;
        parentRow.hidden = mode.value === 'root' || !shouldShowTerritorialParentChoice({
          rootId: choice.countryId,
          parentId: choice.parentId,
          options,
        });
        nameRow.hidden = mode.value !== 'root';
        elements.add.disabled = Object.values(ownershipChoices).some(value => value.mode === 'root' ? !value.name.trim() : !value.countryId);
        confirmedImpact = '';
        host.querySelector('[data-library-impact]')?.remove();
      }
      mode.addEventListener('change', sync);
      name.addEventListener('input', sync);
      country.addEventListener('change', () => { choice.parentId = country.value; sync(); });
      parent.addEventListener('change', () => { choice.parentId = parent.value; sync(); });
      sync();
    }
    host.querySelector('select')?.focus();
  }

  function setLoadingState(nextLoading) {
    loading = !!nextLoading;
    for (const element of [
      elements.search,
      elements.clearSearch,
      elements.referenceDate,
      elements.childDepth,
    ]) {
      if (element) element.disabled = loading;
    }
    if (elements.add) elements.add.disabled = true;
    if (elements.results) elements.results.setAttribute('aria-busy', String(loading));
  }

  function renderLoadingResults() {
    const fragment = document.createDocumentFragment();
    for (let index = 0; index < 6; index += 1) {
      const row = document.createElement('div');
      row.className = 'territorial-library-result-skeleton';
      row.setAttribute('aria-hidden', 'true');
      const title = document.createElement('span');
      const meta = document.createElement('span');
      title.className = 'ui-skeleton-block';
      meta.className = 'ui-skeleton-block';
      row.append(title, meta);
      fragment.appendChild(row);
    }
    elements.results.replaceChildren(fragment);
  }

  function period(entity) {
    if (!entity.lifetime.validFrom && !entity.lifetime.validTo) return '기간 미상';
    return `${entity.lifetime.validFrom || '?'}–${entity.lifetime.validTo || '현재'}`;
  }

  function searchResults() {
    return service.search({query:elements.search.value,referenceDate:elements.referenceDate.value});
  }

  function renderPreview() {
    const selectedRow = [...elements.results.querySelectorAll('[data-library-entity-id]')].find(row => row.dataset.libraryEntityId === selectedId);
    selectedRow?.insertAdjacentElement?.('afterend', elements.preview);
    const entity = flagPicker ? service.get(selectedId) : service.getLoadedEntity(selectedId);
    if (flagPicker) {
      const flagUrl = String(entity?.metadata?.defaultFlagDataUrl || '').trim();
      if (!entity) {
        elements.preview.hidden = true;
        elements.add.disabled = true;
        return;
      }
      elements.preview.hidden = false;
      const title = document.createElement('h3');
      title.className = 'territorial-library-preview-title';
      title.textContent = entity.names.ko || entity.names.en || Object.values(entity.names)[0];
      const help = document.createElement('p');
      help.className = 'editor-help';
      help.textContent = flagUrl ? '이 항목의 기본 국기를 적용합니다.' : '이 항목에는 기본 국기가 없습니다.';
      elements.preview.replaceChildren(title, help);
      elements.add.disabled = !flagUrl;
      elements.addOptions?.classList.add('hidden');
      elements.optionsBack?.classList.add('hidden');
      elements.card?.classList.remove('is-detail', 'is-options');
      elements.add.textContent = '적용';
      elements.add.setAttribute('aria-label', '선택한 라이브러리 국기 적용');
      elements.add.dataset.tooltip = '선택한 라이브러리 국기 적용';
      return;
    }
    const version = entity ? selectGeometryVersion(entity, elements.referenceDate.value) : null;
    if (!entity || !version) {
      elements.preview.hidden = !selectedId;
      const help = document.createElement('p');
      help.className = 'editor-help';
      help.textContent = selectedId ? (entity ? '선택한 시점의 국토 자료가 없습니다.' : '국토 자료를 불러오는 중입니다.') : '항목을 선택하세요.';
      elements.preview.replaceChildren(help);
      elements.add.disabled = true;
      elements.addOptions?.classList.add('hidden');
      elements.optionsBack?.classList.add('hidden');
      return;
    }
    elements.preview.hidden = false;
    const title = document.createElement('h3');
    title.className = 'territorial-library-preview-title';
    title.textContent = entity.names.ko || entity.names.en || Object.values(entity.names)[0];
    const versionField = document.createElement('div');
    versionField.className = 'territorial-library-version-field';
    versionField.textContent = `${version.validFrom || '?'}–${version.validTo || '현재'}`;
    versionField.dataset.geometryVersionId = version.versionId;
    const meta = document.createElement('p');
    meta.className = 'editor-help';
    meta.textContent = [
      version.certainty === 'low' ? '정확도가 낮은 경계' : version.certainty === 'medium' ? '경계 일부 불확실' : '',
      entity.metadata?.approximateGeometry ? '근사 경계' : '',
    ].filter(Boolean).join(' · ');
    const heading = document.createElement('div');
    heading.className = 'territorial-library-preview-heading';
    heading.append(title);
    elements.preview.replaceChildren(heading, versionField, renderMapPreview(entity, version),
      ...(meta.textContent ? [meta] : []));
    elements.add.disabled = false;
    const hasChildren = service.list().some(candidate => candidate.parentEntityId === entity.entityId);
    if (hasChildren) elements.addOptions?.classList.remove('hidden');
    else {
      elements.addOptions?.classList.add('hidden');
      elements.childDepth.value = 'none';
    }
    elements.optionsBack?.classList.add('hidden');
    elements.add.textContent = '추가';
    elements.add.setAttribute('aria-label', '선택한 항목을 현재 프로젝트에 추가');
    elements.add.dataset.tooltip = '선택한 항목을 현재 프로젝트에 추가';
  }

  function renderResults() {
    const groups = searchResults().map(group=>({...group,entities:group.entities.filter(entity=>!flagPicker || String(entity.metadata?.defaultFlagDataUrl || '').trim())})).filter(group=>group.entities.length);
    const results = groups.flatMap(group=>group.entities);
    const fragment = document.createDocumentFragment();
    for (const group of groups) {
      const heading=document.createElement('h3');
      heading.className='territorial-library-lineage-title';
      heading.textContent=group.names.ko || group.names.en || Object.values(group.names)[0];
      fragment.appendChild(heading);
      for (const entity of group.entities) {
      const button = document.createElement('button');
      const selected = selectedId === entity.entityId;
      button.type = 'button';
      button.className = `ui-button ui-row ui-card ui-selectable-row territorial-library-result${selected ? ' is-selected' : ''}`;
      button.dataset.libraryEntityId = entity.entityId;
      button.setAttribute('aria-expanded', String(selected));
      if (selected) button.setAttribute('aria-controls', elements.preview.id);
      button.tabIndex = selected ? 0 : -1;
      const strong = document.createElement('strong');
      strong.textContent = entity.names.ko || entity.names.en || Object.values(entity.names)[0];
      const small = document.createElement('small');
      small.textContent = period(entity);
      const flagUrl = String(entity.metadata?.defaultFlagDataUrl || '').trim();
      if (flagUrl) {
        const flag = document.createElement('span');
        flag.className = 'territorial-library-result-flag';
        flag.setAttribute('aria-hidden', 'true');
        const image = document.createElement('img');
        image.src = flagUrl;
        image.alt = '';
        flag.appendChild(image);
        button.className += ' territorial-library-result--flagged';
        button.append(flag, strong, small);
      } else button.append(strong, small);
      fragment.appendChild(button);
      }
    }
    if (!results.length) fragment.appendChild(createEmptyState(
      flagPicker ? '국기가 있는 항목이 없습니다.' : '조건에 맞는 항목이 없습니다.',
      flagPicker ? '검색어 또는 시점을 바꿔 보세요.' : '검색어 또는 시점을 바꿔 보세요.',
      { compact: true },
    ));
    elements.results.replaceChildren(fragment);
    const options = [...elements.results.querySelectorAll('[data-library-entity-id]')];
    if (options.length && !options.some(option => option.tabIndex === 0)) options[0].tabIndex = 0;
    if (selectedId && !results.some(entity => entity.entityId === selectedId)) {
      selectedId = '';
      renderPreview();
    }
    if (selectedId) renderPreview();
  }

  async function select(id) {
    if (loading) return;
    if (selectedId !== String(id || '')) {
      resetOwnership();
      elements.childDepth.value = 'none';
    }
    selectedId = String(id || '');
    const generation = ++requestGeneration;
    const project = getProjectGeneration();
    const restoreFocus = document.activeElement?.hasAttribute?.('data-library-entity-id');
    try {
      renderResults();
      if (!flagPicker && selectedId) await service.loadEntity(selectedId);
      if (generation === requestGeneration && project !== getProjectGeneration()) {
        selectedId='';resetOwnership();renderResults();renderPreview();return;
      }
      if (generation !== requestGeneration || project !== getProjectGeneration() || elements.modal.classList.contains('hidden')) return;
      renderPreview();
    } catch (error) {
      if (generation === requestGeneration && project === getProjectGeneration()) reportError(error, '선택한 경계를 불러오지 못했습니다.', 'PL-LIB-004', 4800);
      return;
    }
    if (restoreFocus) requestFrame(() => elements.results.querySelector('[aria-expanded="true"]')?.focus({ preventScroll: true }));
  }

  function close() {
    requestGeneration += 1;
    resetOwnership();
    elements.modal.classList.add('hidden');
    elements.card?.classList.remove('is-detail', 'is-options');
    const restoreFocus = flagPicker?.restoreFocus;
    flagPicker = null;
    if (restoreFocus?.focus) restoreFocus.focus({ preventScroll: true });
    else focusSurfaceTrigger('create');
  }

  async function open({ onPickFlag = null, restoreFocus = null } = {}) {
    const generation = ++requestGeneration;
    const project = getProjectGeneration();
    flagPicker = typeof onPickFlag === 'function' ? { onPickFlag, restoreFocus } : null;
    closeSurface('create');
    elements.modal.classList.remove('hidden');
    setLoadingState(true);
    renderLoadingResults();
    try {
      await service.load();
      if (generation !== requestGeneration || project !== getProjectGeneration()) return;
      setLoadingState(false);
      if (!elements.referenceDate.value) elements.referenceDate.value=service.today();
      renderResults();
      renderPreview();
      elements.search.focus();
    } catch (error) {
      if (generation !== requestGeneration || project !== getProjectGeneration()) return;
      loading = false;
      elements.results.setAttribute('aria-busy', 'false');
      elements.results.replaceChildren(createEmptyState('라이브러리를 불러오지 못했습니다.', '잠시 후 다시 시도해 주세요.', { compact: true }));
      reportError(error, '국가·지역 라이브러리를 불러오지 못했습니다.', 'PL-LIB-001', 4800);
    }
  }

  async function addSelected() {
    if (loading || !selectedId) return;
    const generation = ++requestGeneration;
    const project = getProjectGeneration();
    function isCurrent() {
      if (generation !== requestGeneration) return false;
      if (project === getProjectGeneration()) return true;
      selectedId = '';
      resetOwnership();
      setLoadingState(false);
      renderResults();
      renderPreview();
      return false;
    }
    setLoadingState(true);
    try {
      const context = await ownershipContext([selectedId], elements.referenceDate.value, elements.childDepth.value);
      if (!isCurrent()) return;
      if (!ownershipChoices && context.missing.length) {
        setLoadingState(false);
        showOwnership(context);
        return;
      }
    } catch (error) {
      if (!isCurrent()) return;
      setLoadingState(false);
      renderPreview();
      reportError(error, '선택한 항목의 소속과 경계 버전을 확인하세요.', 'PL-LIB-002', 4800);
      return;
    }
    setLoadingState(true);
    for (const control of elements.ownership?.querySelectorAll('input, select') || []) control.disabled = true;
    try {
      const result = await instantiate([selectedId], elements.referenceDate.value, elements.childDepth.value, {
        ownership: ownershipChoices || {}, confirmedImpact,
        isCurrent,
      });
      if (!isCurrent()) return;
      if (result?.confirmationRequired) {
        const host = elements.ownership;
        host.classList.remove('hidden');
        host.querySelector('[data-library-impact]')?.remove();
        const impact = document.createElement('div');
        impact.dataset.libraryImpact = '';
        const title = document.createElement('h3');
        title.textContent = '영토 변경 확인';
        const list = document.createElement('ul');
        for (const message of result.impacts) {
          const item = document.createElement('li');
          item.textContent = message;
          list.append(item);
        }
        impact.append(title, list);
        host.append(impact);
        confirmedImpact = result.impactKey;
        setLoadingState(false);
        elements.add.disabled = false;
        elements.add.textContent = '확인 후 추가';
        return;
      }
      const added = Number(result?.added || 0);
      const deleted = Number(result?.deleted || 0);
      if (!added) throw new Error('추가 결과에 새 프로젝트 객체가 없습니다.');
      else if (Number(result?.subtracted || 0)) {
        const deletedText = deleted ? ` 이 중 ${deleted}개는 완전히 대체되어 제거했습니다.` : '';
        setStatus(`라이브러리 항목 ${added}개를 추가하고 기존 국가 ${result.subtracted}개의 겹친 영토를 대체했습니다.${deletedText}`, 'success', 4200);
      } else {
        setStatus(`라이브러리 항목 ${added}개를 독립 프로젝트 인스턴스로 추가했습니다.`, 'success', 4200);
      }
      close();
    } catch (error) {
      if (!isCurrent()) return;
      setLoadingState(false);
      renderPreview();
      reportError(error, '라이브러리 항목을 프로젝트에 추가하지 못했습니다.', 'PL-LIB-002', 4800);
    } finally {
      if (requestGeneration === generation) {
        for (const control of elements.ownership?.querySelectorAll('input, select') || []) control.disabled = false;
      }
    }
  }

  function applySelectedFlag() {
    const selected = flagPicker;
    const flagUrl = String(service.get(selectedId)?.metadata?.defaultFlagDataUrl || '').trim();
    if (!selected?.onPickFlag || !flagUrl) return;
    close();
    selected.onPickFlag(flagUrl);
  }

  function advanceAdd() {
    if (!selectedId) return;
    if (flagPicker) {
      applySelectedFlag();
      return;
    }
    return addSelected();
  }

  function returnToDetail() {
    elements.addOptions?.classList.add('hidden');
    elements.optionsBack?.classList.add('hidden');
    elements.add.textContent = '추가';
    elements.card?.classList.remove('is-detail', 'is-options');
    requestFrame(() => elements.results.querySelector('[aria-selected="true"]')?.focus());
  }

  function connect() {
    elements.childDepth?.addEventListener('change', resetOwnership);
    elements.open?.addEventListener('click', open);
    elements.close?.addEventListener('click', close);
    elements.backdrop?.addEventListener('click', close);
    for (const [element, eventName] of [
      [elements.search, 'input'],
      [elements.referenceDate, 'input'],
    ]) {
      element?.addEventListener(eventName, () => {
        resetOwnership();
        try {
          renderResults();
          renderPreview();
        } catch (error) {
          elements.add.disabled = true;
          elements.preview.replaceChildren();
          reportError(error, '검색 조건의 시점을 확인해 주세요.', 'PL-LIB-005', 4800);
        }
      });
    }
    elements.clearSearch?.addEventListener('click', () => {
      elements.search.value = '';
      elements.search.dispatchEvent(new elements.search.ownerDocument.defaultView.Event('input', { bubbles: true }));
      elements.search.focus({ preventScroll: true });
    });
    elements.results?.addEventListener('click', event => {
      const button = event.target.closest('[data-library-entity-id]');
      if (button) select(button.dataset.libraryEntityId);
    });
    elements.results?.addEventListener('keydown', event => {
      if (!RESULT_KEYS.has(event.key) || !event.target.closest('[data-library-entity-id]')) return;
      const options = [...elements.results.querySelectorAll('[data-library-entity-id]')];
      if (!options.length) return;
      const current = event.target.closest('[data-library-entity-id]');
      const currentIndex = Math.max(0, options.indexOf(current));
      const nextIndex = event.key === 'Home' ? 0
        : event.key === 'End' ? options.length - 1
          : event.key === 'ArrowDown' ? Math.min(options.length - 1, currentIndex + 1)
            : Math.max(0, currentIndex - 1);
      const next = options[nextIndex];
      if (!(next instanceof HTMLElement)) return;
      event.preventDefault();
      options.forEach(option => { option.tabIndex = option === next ? 0 : -1; });
      next.focus();
    });
    elements.add?.addEventListener('click', advanceAdd);
    elements.optionsBack?.addEventListener('click', returnToDetail);
  }

  return Object.freeze({ close, connect, isOpen: () => !elements.modal.classList.contains('hidden'), open, renderPreview, renderResults, select });
}
