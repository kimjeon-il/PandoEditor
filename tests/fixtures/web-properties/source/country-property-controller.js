import { countrySelectionStatus } from './country-display.js';
import { createSemanticIcon } from './icon-utils.js';

export function createCountryPropertyController({
  window,
  document,
  elements = {},
  getCountryView,
  getPrimaryRef = () => null,
  showPropertyForm,
  resolveColor,
  defaultColor,
  syncColorPicker,
  resolveFlagUrl,
  calculateAreaKm2,
  formatArea,
  syncActions = () => {},
  syncStatus = () => {},
  commitField = () => false,
  metrics = {},
} = {}) {
  const areaCache = new WeakMap();
  const pendingAreas = new WeakSet();
  let presentationToken = 0;
  let disposed = false;

  const renderFlag = (dataUrl, displayName = '') => {
    const preview = elements.flagPreview;
    if (!preview) return;
    preview.replaceChildren();
    if (!dataUrl) {
      preview.appendChild(createSemanticIcon(document, 'country'));
      if (elements.flagUpload) elements.flagUpload.textContent = '국기 추가';
      elements.flagRemove?.classList.add('hidden');
      return;
    }
    elements.flagRemove?.classList.remove('hidden');
    if (elements.flagUpload) elements.flagUpload.textContent = '국기 변경';
    const image = document.createElement('img');
    image.src = dataUrl;
    image.alt = `${displayName || '선택한 국가'} 국기`;
    image.addEventListener('error', () => {
      if (!preview.contains(image)) return;
      renderFlag('', displayName);
    }, { once: true });
    preview.appendChild(image);
  };

  const scheduleArea = (view, token) => {
    const geometry = view?.feature?.geometry;
    if (!geometry || pendingAreas.has(geometry)) return;
    pendingAreas.add(geometry);
    const calculate = () => {
      const value = calculateAreaKm2(geometry);
      areaCache.set(geometry, value);
      pendingAreas.delete(geometry);
      const primary = getPrimaryRef();
      if (disposed || token !== presentationToken || primary?.key !== view.ref.key) return;
      const current = getCountryView(view.ref);
      if (current?.feature?.geometry !== geometry) return;
      if (elements.area) elements.area.textContent = formatArea(value);
      if (elements.selectionStatus) elements.selectionStatus.textContent = countrySelectionStatus(view, formatArea(value));
      syncStatus();
    };
    if (typeof window?.requestIdleCallback === 'function') window.requestIdleCallback(calculate, { timeout: 800 });
    else window?.setTimeout?.(calculate, 0);
  };

  const present = (countryRef, { refreshOnly = false } = {}) => {
    if (disposed) return false;
    const view = getCountryView(countryRef);
    if (!view?.feature) return false;
    elements.flagMenu?.hidePopover();
    const token = ++presentationToken;
    const startedAt = globalThis.performance?.now?.() || Date.now();
    showPropertyForm('country', view.displayName, { resetScroll: !refreshOnly });
    const fieldsStartedAt = globalThis.performance?.now?.() || Date.now();
    if (elements.name) elements.name.value = view.displayName;
    const color = resolveColor(view);
    if (elements.color) elements.color.value = color.value;
    syncColorPicker('country', { value: color.value, defaultColor: defaultColor(), isDefault: color.isDefault });
    if (elements.notes) elements.notes.value = view.override.notes || '';
    renderFlag(resolveFlagUrl(view), view.displayName);
    const geometry = view.feature.geometry;
    const cached = geometry && areaCache.has(geometry);
    const area = cached ? areaCache.get(geometry) : null;
    if (elements.area) {
      elements.area.textContent = cached ? formatArea(area) : '면적 계산 중…';
      elements.area.dataset.tooltip = '구면 근사 면적이며 고정밀 GIS 측정값과 차이가 날 수 있습니다.';
    }
    if (elements.selectionStatus) elements.selectionStatus.textContent = cached
      ? countrySelectionStatus(view, formatArea(area))
      : countrySelectionStatus(view);
    if (!cached) scheduleArea(view, token);
    syncStatus();
    syncActions(view);
    metrics.propertyPanelMs = fieldsStartedAt - startedAt;
    metrics.propertyFieldsMs = (globalThis.performance?.now?.() || Date.now()) - fieldsStartedAt;
    metrics.transactionMs = (globalThis.performance?.now?.() || Date.now()) - startedAt;
    return true;
  };

  const refresh = countryRef => present(countryRef, { refreshOnly: true });
  const clear = () => { presentationToken += 1; elements.flagMenu?.hidePopover(); };

  const bind = () => {
    const bindField = (element, field, transform = value => value) => element?.addEventListener('change', event => {
      commitField(field, transform(event.target.value));
    });
    bindField(elements.name, 'name', value => value.trim());
    bindField(elements.notes, 'notes');
    const closeMenu = () => { elements.flagMenu?.hidePopover(); };
    elements.flagTrigger?.addEventListener('click', () => {
      const menu = elements.flagMenu, trigger = elements.flagTrigger;
      if (!menu || trigger.disabled) return;
      if (menu.matches(':popover-open')) { closeMenu(); return; }
      menu.showPopover();
      const rect = trigger.getBoundingClientRect();
      const edge = parseFloat(window.getComputedStyle(document.documentElement).fontSize) / 2;
      menu.style.left = `${Math.max(edge, Math.min(rect.left, window.innerWidth - menu.offsetWidth - edge))}px`;
      menu.style.top = `${Math.max(edge, Math.min(rect.bottom + edge, window.innerHeight - menu.offsetHeight - edge))}px`;
      menu.querySelector('button:not(:disabled):not(.hidden)')?.focus();
    });
    elements.flagMenu?.addEventListener('toggle', () => {
      elements.flagTrigger?.setAttribute('aria-expanded', String(elements.flagMenu.matches(':popover-open')));
    });
    elements.flagMenu?.addEventListener('keydown', event => {
      const items = [...elements.flagMenu.querySelectorAll('button:not(:disabled):not(.hidden)')];
      const index = items.indexOf(document.activeElement);
      if (['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
        event.preventDefault();
        const next = event.key === 'Home' ? 0 : event.key === 'End' ? items.length - 1 : (index + (event.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
        items[next]?.focus();
      } else if (event.key === 'Escape') {
        event.preventDefault(); event.stopPropagation(); closeMenu(); elements.flagTrigger?.focus();
      } else if (event.key === 'Tab') { closeMenu(); }
    });
    elements.flagUpload?.addEventListener('click', () => { closeMenu(); elements.flagTrigger?.focus(); elements.flagFile?.click(); });
    elements.flagFile?.addEventListener('change', event => {
      const file = event.target.files?.[0];
      const primary = getPrimaryRef();
      if (!file || primary?.domain !== 'territorial' || primary?.type !== 'country') return;
      const Reader = event.target.ownerDocument?.defaultView?.FileReader;
      if (typeof Reader !== 'function') return;
      const reader = new Reader();
      reader.addEventListener('load', () => commitField('flagDataUrl', reader.result), { once: true });
      reader.readAsDataURL(file);
      event.target.value = '';
    });
    elements.flagRemove?.addEventListener('click', () => { closeMenu(); commitField('flagDataUrl', null); elements.flagTrigger?.focus(); });
    return api;
  };

  const dispose = () => {
    disposed = true;
    presentationToken += 1;
    elements.flagMenu?.hidePopover();
  };

  const api = Object.freeze({ bind, present, refresh, clear, dispose });
  return api;
}
