const RESULT_KEYS = new Set(['ArrowDown', 'ArrowUp', 'Home', 'End']);

export function createHistoricalLibraryController({
  document,
  elements,
  service,
  typeLabels,
  selectGeometryVersion,
  renderMapPreview,
  createEmptyState,
  replaceSelectOptions,
  shouldShowTerritorialParentChoice,
  collator,
  closeSurface,
  focusSurfaceTrigger,
  instantiate,
  ownershipContext = () => ({ missing: [], countries: [], parents: () => [] }),
  confirm,
  setStatus,
  reportError,
  requestFrame = callback => requestAnimationFrame(callback),
}) {
  let selectedId = '';
  let selectedVersionId = '';
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
      const choice = { mode: 'subunit', countryId: item.countryId, parentId: item.countryId, name: item.name };
      ownershipChoices[item.libraryId] = choice;
      const mode = document.createElement('select');
      replaceSelectOptions(mode, [
        { value: 'subunit', label: '기존 국가의 하위단위로 추가' },
        { value: 'country', label: '독립된 국가로 추가' },
      ], 'subunit');
      field(`${item.name} · 추가 방식`, mode);
      const country = document.createElement('select');
      const countryChoice = replaceSelectOptions(country, [
        { value: '', label: '소속 국가 선택', placeholder: true },
        ...context.countries,
      ], choice.countryId, { autoSelectSingle: true }) || { single: false, value: country.value };
      choice.countryId = countryChoice.value;
      const countryRow = field('소속 국가', country);
      const parent = document.createElement('select');
      const parentRow = field('상위 단위', parent);
      const name = document.createElement('input');
      name.value = item.name;
      const nameRow = field('국가 이름', name);
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
        countryRow.hidden = mode.value === 'country' || countryChoice.single;
        parentRow.hidden = mode.value === 'country' || !shouldShowTerritorialParentChoice({
          sovereignId: choice.countryId,
          parentId: choice.parentId,
          options,
        });
        nameRow.hidden = mode.value !== 'country';
        elements.add.disabled = Object.values(ownershipChoices).some(value => value.mode === 'country' ? !value.name.trim() : !value.countryId);
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
      elements.type,
      elements.status,
      elements.year,
      elements.geographicRegion,
      elements.snapshot,
      elements.childDepth,
    ]) {
      if (element) element.disabled = loading;
    }
    if (elements.snapshotButton) elements.snapshotButton.disabled = loading || !elements.snapshot.value;
    if (elements.add) elements.add.disabled = true;
    if (elements.results) elements.results.setAttribute('aria-busy', String(loading));
  }

  function renderLoadingResults() {
    const fragment = document.createDocumentFragment();
    for (let index = 0; index < 6; index += 1) {
      const row = document.createElement('div');
      row.className = 'historical-library-result-skeleton';
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
    if (!entity.startDate && !entity.endDate) return '현존';
    return `${entity.startDate || '?'}–${entity.endDate || '현재'}`;
  }

  function hasMultipleGeometryVersions(entity) {
    return (entity?.geometryVersions || []).length > 1;
  }

  function syncFilterOptions() {
    const geographicRegions = [...new Set(service.list().map(entity => String(entity.metadata?.geographicRegion || '')).filter(Boolean))].sort(collator.compare);
    const geographicChoice = replaceSelectOptions(elements.geographicRegion, [{ value: '', label: '전체' }, ...geographicRegions.map(geographicRegion => ({ value: geographicRegion, label: geographicRegion }))], elements.geographicRegion.value, { autoSelectSingle: true });
    elements.geographicRegion.closest?.('.field-group')?.classList.toggle('hidden', geographicChoice?.single === true);
    replaceSelectOptions(elements.snapshot, [
      { value: '', label: '스냅샷 선택', placeholder: true },
      ...service.snapshots().map(snapshot => ({ value: snapshot.id, label: `${snapshot.name}${snapshot.metadata?.partial ? ' · 부분' : ''}` })),
    ], elements.snapshot.value, { autoSelectSingle: true });
    if (elements.snapshotButton) elements.snapshotButton.disabled = !elements.snapshot.value;
  }

  function searchResults() {
    return service.search({
      query: elements.search.value,
      type: elements.type.value,
      status: elements.status.value,
      referenceDate: elements.year.value,
      geographicRegion: elements.geographicRegion.value,
    });
  }

  function renderPreview() {
    const selectedRow = [...elements.results.querySelectorAll('[data-library-entity-id]')].find(row => row.dataset.libraryEntityId === selectedId);
    selectedRow?.insertAdjacentElement?.('afterend', elements.preview);
    const entity = service.get(selectedId);
    if (flagPicker) {
      const flagUrl = String(entity?.metadata?.defaultFlagDataUrl || '').trim();
      if (!entity) {
        elements.preview.hidden = true;
        elements.add.disabled = true;
        return;
      }
      elements.preview.hidden = false;
      const title = document.createElement('h3');
      title.className = 'historical-library-preview-title';
      title.textContent = entity.displayNames?.ko || entity.canonicalName;
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
    const automaticVersion = entity ? selectGeometryVersion(entity, elements.year.value) : null;
    const version = entity?.geometryVersions?.find(candidate => candidate.id === selectedVersionId) || automaticVersion;
    if (!entity || !version) {
      elements.preview.hidden = true;
      selectedVersionId = '';
      const help = document.createElement('p');
      help.className = 'editor-help';
      help.textContent = '항목을 선택하세요.';
      elements.preview.replaceChildren(help);
      elements.add.disabled = true;
      elements.addOptions?.classList.add('hidden');
      elements.optionsBack?.classList.add('hidden');
      elements.card?.classList.remove('is-detail', 'is-options');
      return;
    }
    const hasMultipleVersions = hasMultipleGeometryVersions(entity);
    if (!hasMultipleVersions) {
      // A single boundary is already represented by the result row. Keep the
      // preview surface hidden so selecting an item does not create a second,
      // mostly empty detail block beneath it.
      elements.preview.hidden = true;
      elements.card?.classList.remove('is-detail', 'is-options');
      const title = document.createElement('h3');
      title.className = 'historical-library-preview-title';
      title.textContent = entity.displayNames?.ko || entity.canonicalName;
      const heading = document.createElement('div');
      heading.className = 'historical-library-preview-heading';
      heading.append(title);
      const versionSummary = document.createElement('div');
      versionSummary.className = 'ui-field field-group historical-library-version-field';
      versionSummary.textContent = `경계 · ${version.validFrom || '?'}–${version.validTo || '현재'}`;
      const metadata = document.createElement('p');
      metadata.className = 'editor-help';
      metadata.textContent = [
        version.certainty === 'low' ? '정확도가 낮은 경계' : version.certainty === 'medium' ? '경계 일부 불확실' : '',
        entity.metadata?.approximateGeometry ? '근사 경계' : '',
      ].filter(Boolean).join(' · ');
      elements.preview.replaceChildren(heading, versionSummary, ...(metadata.textContent ? [metadata] : []));
      elements.add.disabled = false;
      const hasChildren = service.list().some(candidate => candidate.parentLibraryId === entity.libraryId);
      if (hasChildren) elements.addOptions?.classList.remove('hidden');
      else {
        elements.addOptions?.classList.add('hidden');
        elements.childDepth.value = 'none';
      }
      elements.optionsBack?.classList.add('hidden');
      elements.add.textContent = '추가';
      elements.add.setAttribute('aria-label', '선택한 항목을 현재 프로젝트에 추가');
      elements.add.dataset.tooltip = '선택한 항목을 현재 프로젝트에 추가';
      return;
    }
    elements.preview.hidden = false;
    const title = document.createElement('h3');
    title.className = 'historical-library-preview-title';
    title.textContent = entity.displayNames?.ko || entity.canonicalName;
    const versionField = document.createElement('label');
    versionField.className = 'ui-field field-group historical-library-version-field';
    const versionLabel = document.createElement('span');
    versionLabel.textContent = '경계';
    const versionSelect = document.createElement('select');
    versionSelect.id = 'historicalLibraryGeometryVersionInput';
    versionSelect.setAttribute('aria-label', `${entity.displayNames?.ko || entity.canonicalName} 경계`);
    for (const candidate of entity.geometryVersions || []) {
      const option = document.createElement('option');
      option.value = candidate.id;
      option.textContent = `${candidate.validFrom || '?'}–${candidate.validTo || '현재'}`;
      option.selected = candidate.id === version.id;
      versionSelect.appendChild(option);
    }
    versionField.append(versionLabel, versionSelect);
    versionSelect.addEventListener('change', () => {
      selectedVersionId = versionSelect.value;
      resetOwnership();
      renderPreview();
    });
    const meta = document.createElement('p');
    meta.className = 'editor-help';
    meta.textContent = [
      version.certainty === 'low' ? '정확도가 낮은 경계' : version.certainty === 'medium' ? '경계 일부 불확실' : '',
      entity.metadata?.approximateGeometry ? '근사 경계' : '',
    ].filter(Boolean).join(' · ');
    const heading = document.createElement('div');
    heading.className = 'historical-library-preview-heading';
    heading.append(title);
    elements.preview.replaceChildren(heading, versionField, renderMapPreview(entity, version),
      ...(meta.textContent ? [meta] : []));
    elements.add.disabled = false;
    const hasChildren = service.list().some(candidate => candidate.parentLibraryId === entity.libraryId);
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
    const results = searchResults().filter(entity => !flagPicker || String(entity.metadata?.defaultFlagDataUrl || '').trim());
    const fragment = document.createDocumentFragment();
    for (const entity of results) {
      const button = document.createElement('button');
      const selected = selectedId === entity.libraryId;
      button.type = 'button';
      button.className = `ui-button ui-row ui-card ui-selectable-row historical-library-result${selected ? ' is-selected' : ''}`;
      button.dataset.libraryEntityId = entity.libraryId;
      button.setAttribute('aria-expanded', String(selected));
      if (selected && hasMultipleGeometryVersions(entity)) button.setAttribute('aria-controls', elements.preview.id);
      button.tabIndex = selected ? 0 : -1;
      const strong = document.createElement('strong');
      strong.textContent = entity.displayNames?.ko || entity.canonicalName;
      const small = document.createElement('small');
      small.textContent = `${typeLabels[entity.type]} · ${period(entity)}`;
      const flagUrl = String(entity.metadata?.defaultFlagDataUrl || '').trim();
      if (flagUrl) {
        const flag = document.createElement('span');
        flag.className = 'historical-library-result-flag';
        flag.setAttribute('aria-hidden', 'true');
        const image = document.createElement('img');
        image.src = flagUrl;
        image.alt = '';
        flag.appendChild(image);
        button.className += ' historical-library-result--flagged';
        button.append(flag, strong, small);
      } else button.append(strong, small);
      fragment.appendChild(button);
    }
    if (!results.length) fragment.appendChild(createEmptyState(
      flagPicker ? '국기가 있는 항목이 없습니다.' : '조건에 맞는 항목이 없습니다.',
      flagPicker ? '검색어, 종류, 상태 또는 기준 연도를 바꿔 보세요.' : '검색어, 종류, 상태 또는 기준 연도를 바꿔 보세요.',
      { compact: true },
    ));
    elements.results.replaceChildren(fragment);
    const options = [...elements.results.querySelectorAll('[data-library-entity-id]')];
    if (options.length && !options.some(option => option.tabIndex === 0)) options[0].tabIndex = 0;
    if (selectedId && !results.some(entity => entity.libraryId === selectedId)) {
      selectedId = '';
      renderPreview();
    }
    if (selectedId) renderPreview();
  }

  function select(id) {
    if (loading) return;
    if (selectedId !== String(id || '')) {
      resetOwnership();
      selectedVersionId = '';
      elements.childDepth.value = 'none';
    }
    selectedId = String(id || '');
    const restoreFocus = document.activeElement?.hasAttribute?.('data-library-entity-id');
    renderResults();
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
    flagPicker = typeof onPickFlag === 'function' ? { onPickFlag, restoreFocus } : null;
    closeSurface('create');
    elements.modal.classList.remove('hidden');
    setLoadingState(true);
    renderLoadingResults();
    try {
      await service.load();
      setLoadingState(false);
      syncFilterOptions();
      renderResults();
      renderPreview();
      elements.search.focus();
    } catch (error) {
      loading = false;
      elements.results.setAttribute('aria-busy', 'false');
      elements.results.replaceChildren(createEmptyState('라이브러리를 불러오지 못했습니다.', '잠시 후 다시 시도해 주세요.', { compact: true }));
      reportError(error, '국가·지역 라이브러리를 불러오지 못했습니다.', 'PL-LIB-001', 4800);
    }
  }

  async function addSelected() {
    if (loading || !selectedId) return;
    const versionOverrides = selectedVersionId ? { [selectedId]: selectedVersionId } : {};
    try {
      const context = ownershipContext([selectedId], elements.year.value, elements.childDepth.value, versionOverrides);
      if (!ownershipChoices && context.missing.length) {
        showOwnership(context);
        return;
      }
    } catch (error) {
      reportError(error, '선택한 항목의 소속과 경계 버전을 확인하세요.', 'PL-LIB-002', 4800);
      return;
    }
    const generation = ++requestGeneration;
    setLoadingState(true);
    for (const control of elements.ownership?.querySelectorAll('input, select') || []) control.disabled = true;
    try {
      const result = await instantiate([selectedId], elements.year.value, elements.childDepth.value, versionOverrides, {
        ownership: ownershipChoices || {}, confirmedImpact,
        isCurrent: () => requestGeneration === generation,
      });
      if (requestGeneration !== generation) return;
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
      if (!added) setStatus('이미 현재 프로젝트에 있는 항목입니다.', 'success', 2800);
      else if (Number(result?.subtracted || 0)) {
        const deletedText = deleted ? ` 이 중 ${deleted}개는 완전히 대체되어 제거했습니다.` : '';
        setStatus(`라이브러리 항목 ${added}개를 추가하고 기존 국가 ${result.subtracted}개의 겹친 영토를 대체했습니다.${deletedText}`, 'success', 4200);
      } else {
        setStatus(`라이브러리 항목 ${added}개를 독립 프로젝트 인스턴스로 추가했습니다.`, 'success', 4200);
      }
      close();
    } catch (error) {
      if (requestGeneration !== generation) return;
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
    void addSelected();
  }

  function returnToDetail() {
    elements.addOptions?.classList.add('hidden');
    elements.optionsBack?.classList.add('hidden');
    elements.add.textContent = '추가';
    elements.card?.classList.remove('is-detail', 'is-options');
    requestFrame(() => elements.results.querySelector('[aria-selected="true"]')?.focus());
  }

  function requestSnapshot() {
    const snapshot = service.getSnapshot(elements.snapshot.value);
    if (!snapshot) return;
    confirm({
      title: `${snapshot.name} 스냅샷`,
      message: snapshot.metadata?.partial
        ? '이 스냅샷은 라이브러리 기능 시험용 부분 구성입니다. 현재 프로젝트에 없는 항목만 추가합니다.'
        : '현재 프로젝트에 없는 스냅샷 항목만 추가합니다.',
      confirmText: '없는 항목 추가',
      onConfirm: async () => {
        try {
          const result = await instantiate(snapshot.entityRefs, snapshot.referenceDate, 'all');
          setStatus(`${snapshot.name}에서 ${Number(result?.added || 0)}개 항목을 추가했습니다.`, 'success', 4200);
          close();
        } catch (error) {
          reportError(error, '세계 스냅샷을 프로젝트에 추가하지 못했습니다.', 'PL-LIB-003', 4800);
        }
      },
    });
  }

  function connect() {
    elements.childDepth?.addEventListener('change', resetOwnership);
    elements.open?.addEventListener('click', open);
    elements.close?.addEventListener('click', close);
    elements.backdrop?.addEventListener('click', close);
    for (const [element, eventName] of [
      [elements.search, 'input'],
      [elements.type, 'change'],
      [elements.status, 'change'],
      [elements.year, 'input'],
      [elements.geographicRegion, 'change'],
    ]) {
      element?.addEventListener(eventName, () => {
        resetOwnership();
        renderResults();
        if (element === elements.year) {
          selectedVersionId = '';
          renderPreview();
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
    elements.snapshot?.addEventListener('change', () => { elements.snapshotButton.disabled = !elements.snapshot.value; });
    elements.snapshotButton?.addEventListener('click', requestSnapshot);
  }

  return Object.freeze({ close, connect, isOpen: () => !elements.modal.classList.contains('hidden'), open, renderPreview, renderResults, select });
}
