const LABELS = Object.freeze({
  countries: '국가', subunits: '하위단위', regions: '지방',
  genericFeatures: '기타 객체', distributions: '분포', labels: '지명',
});

export function createGisExportController({
  window,
  document,
  elements,
  ensureRuntime,
  requireReady,
  getProject,
  getCounts,
  download,
  setStatus,
  reportError,
} = {}) {
  let busy = false;
  let returnFocus = null;

  const selectedLayers = () => [...elements.form.querySelectorAll('.gis-export-layers input:checked')]
    .filter(input => !input.disabled).map(input => input.value);
  const updateSummary = () => {
    const counts = getCounts();
    const selected = selectedLayers();
    const nonEmpty = selected.filter(layer => counts[layer] > 0);
    const summary = elements.summary?.querySelector('p');
    if (!summary) return;
    summary.textContent = nonEmpty.length ? '' : '내보낼 데이터를 선택하세요.';
    elements.summary.hidden = nonEmpty.length > 0;
    elements.confirm.disabled = busy || !nonEmpty.length;
  };

  const refreshLayers = () => {
    const counts = getCounts();
    for (const input of elements.form.querySelectorAll('.gis-export-layers input')) {
      const count = Number(counts[input.value]) || 0;
      input.disabled = count === 0;
      input.checked = count > 0;
      const row = input.closest('label');
      if (row) row.hidden = count === 0;
      const label = row?.querySelector('span');
      if (label) label.textContent = `${LABELS[input.value]} · ${count.toLocaleString()}개`;
    }
    updateSummary();
  };

  const open = async () => {
    await ensureRuntime();
    if (!requireReady()) return false;
    const HTMLElementCtor = document.defaultView?.HTMLElement;
    returnFocus = typeof HTMLElementCtor === 'function' && document.activeElement instanceof HTMLElementCtor ? document.activeElement : elements.trigger;
    elements.error.textContent = '';
    elements.error.classList.add('hidden');
    elements.modal.classList.remove('hidden');
    document.body.classList.add('modal-open');
    refreshLayers();
    window.requestAnimationFrame(() => elements.format?.focus());
    return true;
  };

  const close = () => {
    if (elements.modal?.classList.contains('hidden')) return false;
    elements.modal.classList.add('hidden');
    document.body.classList.remove('modal-open');
    const target = returnFocus?.isConnected ? returnFocus : elements.trigger;
    returnFocus = null;
    target?.focus({ preventScroll: true });
    return true;
  };

  const confirm = async () => {
    if (busy) return false;
    const selected = selectedLayers();
    const counts = getCounts();
    if (!selected.some(layer => counts[layer] > 0)) {
      elements.error.textContent = '내보낼 데이터가 있는 범주를 하나 이상 선택하세요.';
      elements.error.classList.remove('hidden');
      return false;
    }
    busy = true;
    elements.confirm.disabled = true;
    elements.error.classList.add('hidden');
    try {
      setStatus('GIS 데이터를 내보내는 중입니다.', 'working', 0);
      const project = getProject();
      if (elements.format.value === 'geojson-zip') {
        const result = await window.PandoLabGIS.exportGeoJsonBundle(project, selected, (_message, percent) => setStatus(`GeoJSON 묶음 생성 중 · ${Math.round(percent || 0)}%`, 'working', 0));
        download('판도연구소-GIS-데이터.zip', result.blob);
        close();
        setStatus(`GeoJSON 레이어 ${result.manifest.layers.length}개를 만들었습니다.`, 'success', 3600);
      } else {
        const blob = await window.PandoLabGIS.exportGeoPackage(project, (_message, percent) => setStatus(`GIS용 GeoPackage 생성 중 · ${Math.round(percent || 0)}%`, 'working', 0), { mode: 'gis', layers: selected });
        download('판도연구소-GIS-데이터.gpkg', blob);
        close();
        setStatus('GIS용 GeoPackage를 만들었습니다.', 'success', 3600);
      }
      return true;
    } catch (error) {
      elements.error.textContent = error?.message || String(error);
      elements.error.classList.remove('hidden');
      reportError(error);
      return false;
    } finally {
      busy = false;
      updateSummary();
    }
  };

  const bind = () => {
    elements.close?.addEventListener('click', close);
    elements.cancel?.addEventListener('click', close);
    elements.backdrop?.addEventListener('click', close);
    elements.confirm?.addEventListener('click', confirm);
    elements.format?.addEventListener('change', updateSummary);
    elements.form.querySelector('.gis-export-layers')?.addEventListener('change', updateSummary);
    return api;
  };

  const api = Object.freeze({ bind, open, close, confirm, updateSummary });
  return api;
}
