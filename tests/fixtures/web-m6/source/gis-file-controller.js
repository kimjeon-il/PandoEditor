export function createGisFileController({
  elements,
  onFiles,
  setTarget = () => {},
  requireCanonicalData = () => true,
  projectDomain = null,
  setActionStatus = () => {},
  window = globalThis,
  document = globalThis.document,
} = {}) {
  let returnFocus = null;
  let saving = false;

  const openPicker = ({ target = '', trigger = null } = {}) => {
    setTarget(target);
    returnFocus = trigger || elements.open;
    elements.input.dataset.returnFocusId = returnFocus?.id || '';
    elements.input.click();
  };

  const handleChange = async event => {
    const files = [...(event.target.files || [])];
    event.target.value = '';
    try { return await onFiles(files, { sourceKind: event.target === elements.projectInput ? 'project' : 'vector' }); }
    finally {
      const target = returnFocus;
      returnFocus = null;
      target?.focus?.({ preventScroll: true });
    }
  };

  const bind = () => {
    elements.input?.addEventListener('change', handleChange);
    elements.projectInput?.addEventListener('change', handleChange);
    return api;
  };

  const openProjectPicker = () => {
    setTarget('');
    returnFocus = elements.projectOpen;
    elements.projectInput.dataset.returnFocusId = returnFocus?.id || '';
    elements.projectInput.click();
  };

  const downloadBlob = (filename, blob) => {
    const url = window.URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = filename;
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    window.setTimeout(() => window.URL.revokeObjectURL(url), 1000);
  };

  const saveProject = async () => {
    if (saving || !requireCanonicalData()) return false;
    saving = true;
    const button = elements.save;
    if (button) button.disabled = true;
    try {
      const filename = '판도연구소-프로젝트.gpkg';
      // Request the destination while the click still has user activation.
      const handle = typeof window.showSaveFilePicker === 'function'
        ? await window.showSaveFilePicker({
          suggestedName: filename,
          types: [{ description: '판도연구소 프로젝트', accept: { 'application/geopackage+sqlite3': ['.gpkg'] } }],
        }) : null;
      return await projectDomain.save(async blob => {
        if (handle) {
          const writable = await handle.createWritable();
          await writable.write(blob);
          await writable.close();
          setActionStatus('프로젝트 저장 완료', 'success', 3200);
          return { downloaded: false };
        } else {
          downloadBlob(filename, blob);
          setActionStatus('브라우저 다운로드 폴더에 저장 완료', 'success', 4800);
          return { downloaded: true };
        }
      });
    } catch (error) {
      if (error?.name === 'AbortError') {
        setActionStatus('저장 취소', 'ready', 2200);
        return false;
      }
      console.error('[PL-GPKG-001]', error);
      setActionStatus('프로젝트 저장 실패', 'error', 0);
      return false;
    } finally {
      saving = false;
      if (button) button.disabled = false;
    }
  };

  const api = Object.freeze({ bind, openPicker, openProjectPicker, handleChange, saveProject });
  return api;
}
