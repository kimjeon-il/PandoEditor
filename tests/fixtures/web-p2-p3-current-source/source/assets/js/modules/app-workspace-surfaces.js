import { clearMenuPosition, createMenuPositionScheduler, positionRootMenu } from './menu-presentation.js';
import { createSearchToolbarPresentation } from './search-toolbar-presentation.js';

/** WorkspaceSurfaces: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createWorkspaceSurfaces() {
  let dependencies;
  let isPolygonDraftTool;
  let isGenericFeatureDraftTool;
  let clampViewZooms;
  let uid;
  let detectLayoutMode;
  let layoutMode;
  let isMobile;
  let lastOverlayTrigger;
  let fileMenuTrigger;
  let pendingLayoutChange = false;
  let surfaceController;
  let surfaceState;
  let editorWorkspacePresentation;
  let editorSurfaceTabs;
  let MOBILE_SHEET_SNAP_COLLAPSED_PX;
  let MOBILE_SHEET_EDITOR_RATIOS;
  let MOBILE_SHEET_AUXILIARY_RATIOS;
  let SHEET_SNAP_RATIOS;
  let SHEET_SNAP_LABELS;
  let SHEET_SNAP_DEFAULTS;
  let MOBILE_SHEET_DEFAULT_SNAP;
  let MOBILE_SHEET_IDS;
  let sheetSnapIndex;
  let sheetSnapTouched;
  let activeSheetDrag;
  let mapModeContextWasActive;
  let MOBILE_SHEET_HISTORY_KEY;
  let ignoreNextMobileSheetPopstate;
  let mobileViewportHeight;
  let mobileSheetSettlement;
  let menuPositionScheduler;
  let searchToolbarPresentation;
  let searchFocusFrame = 0;
  let searchComposing = false;
  let referenceSurface = null;
  let referenceGestureActive = false;
  function connect(ports) {
    if (dependencies) throw new Error('workspace-surfaces already connected');
    dependencies = ports;
  }

  function mobileSheetNavHeight() {
    const nav = document.querySelector('.adaptive-nav');
    const height = nav?.getBoundingClientRect().height || 0;
    return height > 0 ? height : 64;
  }

  function mobileSheetAvailableHeight() {
    const topbarHeight = document.querySelector('.topbar')?.getBoundingClientRect().height || (isMobile() ? 0 : 60);
    return Math.max(180, mobileViewportHeight() - topbarHeight - mobileSheetNavHeight());
  }

  function mobileSheetKind(panelOrKind) {
    if (typeof panelOrKind === 'string') return panelOrKind;
    const panelId = panelOrKind?.id;
    return Object.entries(MOBILE_SHEET_IDS).find(([, id]) => id === panelId)?.[0] || 'edit';
  }

  function mobileSheetSnapHeight(index, panelOrKind = 'edit') {
    const safeIndex = (0, dependencies.platform.clamp)(Number(index) || 0, 0, SHEET_SNAP_RATIOS.length - 1);
    const kind = mobileSheetKind(panelOrKind);
    if (safeIndex === 0) {
      const panel = typeof panelOrKind === 'string' ? mobileSheetPanel(kind) : panelOrKind;
      const headerHeight = panel?.querySelector('.surface-header')?.getBoundingClientRect().height || 0;
      const rootScale = (parseFloat(getComputedStyle(document.documentElement).fontSize) || 16) / 16;
      return Math.min(mobileSheetAvailableHeight(), Math.max(headerHeight, MOBILE_SHEET_SNAP_COLLAPSED_PX * rootScale));
    }
    const ratios = kind === 'edit' ? MOBILE_SHEET_EDITOR_RATIOS : MOBILE_SHEET_AUXILIARY_RATIOS;
    const ratio = safeIndex === 1 ? ratios.half : ratios.expanded;
    return Math.min(mobileSheetAvailableHeight(), mobileViewportHeight() * ratio);
  }

  function mobileSheetPanel(kind) {
    return (0, dependencies.platform.$)(MOBILE_SHEET_IDS[kind]);
  }

  function mobileSheetHistoryKind() {
    return window.history.state?.[MOBILE_SHEET_HISTORY_KEY] || null;
  }

  function trackMobileSheetHistory(kind) {
    if (!isMobile() || !kind) return;
    const currentState = window.history.state && typeof window.history.state === 'object' ? window.history.state : {};
    const nextState = { ...currentState, [MOBILE_SHEET_HISTORY_KEY]: kind };
    if (mobileSheetHistoryKind()) window.history.replaceState(nextState, '', window.location.href);
    else window.history.pushState(nextState, '', window.location.href);
  }

  function releaseMobileSheetHistory() {
    if (!mobileSheetHistoryKind()) return;
    ignoreNextMobileSheetPopstate = true;
    window.history.back();
  }

  function setMobileSheetHeight(panel, index, temporaryHeight = null) {
    if (!panel) return 0;
    const safeIndex = (0, dependencies.platform.clamp)(Number(index) || 0, 0, SHEET_SNAP_RATIOS.length - 1);
    const maxHeight = mobileSheetSnapHeight(SHEET_SNAP_RATIOS.length - 1, panel);
    const minHeight = mobileSheetSnapHeight(0, panel);
    const height = temporaryHeight == null
      ? mobileSheetSnapHeight(safeIndex, panel)
      : (0, dependencies.platform.clamp)(temporaryHeight, Math.max(128, minHeight * 0.62), maxHeight);
    panel.style.setProperty('--sheet-height', `${Math.round(height)}px`);
    if (temporaryHeight == null) {
      sheetSnapIndex.set(panel.id, safeIndex);
      panel.dataset.sheetSnap = String(safeIndex);
      const handle = panel.querySelector('[data-sheet-handle]');
      handle?.setAttribute('aria-valuenow', String(safeIndex));
      handle?.setAttribute('aria-valuetext', SHEET_SNAP_LABELS[safeIndex]);
    }
    return height;
  }

  function resetMobileSheetSession(panel, { applyHeight = true } = {}) {
    if (!panel) return;
    sheetSnapTouched.delete(panel.id);
    const defaultSnap = SHEET_SNAP_DEFAULTS[mobileSheetKind(panel)] ?? MOBILE_SHEET_DEFAULT_SNAP;
    sheetSnapIndex.set(panel.id, defaultSnap);
    if (applyHeight) setMobileSheetHeight(panel, defaultSnap);
  }

  function refreshMapSheetMetrics() {
    const panels = Object.values(MOBILE_SHEET_IDS).map(id => (0, dependencies.platform.$)(id)).filter(Boolean);
    if (!isMobile()) {
      panels.forEach(panel => panel.style.removeProperty('--sheet-height'));
      document.body.classList.remove('map-sheet-dragging');
      return;
    }
    panels.forEach(panel => {
      if (activeSheetDrag?.panel !== panel) setMobileSheetHeight(panel, sheetSnapIndex.get(panel.id) ?? MOBILE_SHEET_DEFAULT_SNAP);
    });
  }

  function syncEditorPanelControls() {
    const headerToggle = (0, dependencies.platform.$)('mobileCloseRightBtn');
    if (!headerToggle) return;
    const label = layoutMode === 'wide' ? '편집창 접기' : '편집창 닫기';
    headerToggle.setAttribute('aria-label', label);
    headerToggle.dataset.tooltip = label;
  }

  function applyLayoutMode({ initial = false } = {}) {
    const nextLayout = detectLayoutMode();
    if (nextLayout !== layoutMode && (activeSheetDrag || mobileSheetSettlement.size || searchComposing || referenceGestureActive)) {
      pendingLayoutChange = true;
      return false;
    }
    const previous = layoutMode;
    if (previous !== nextLayout) {
      cancelAnimationFrame(searchFocusFrame);
      searchFocusFrame = 0;
    }
    pendingLayoutChange = false;
    layoutMode = nextLayout;
    const app = (0, dependencies.platform.$)('app');
    if (app) app.dataset.layout = layoutMode;
    document.body.dataset.layout = layoutMode;
    surfaceController.syncLayout(previous);
    if (previous === 'mobile' && layoutMode !== 'mobile') releaseMobileSheetHistory();
    else if (layoutMode === 'mobile' && previous !== 'mobile' && surfaceController.activeMobileSheet) {
      trackMobileSheetHistory(surfaceController.activeMobileSheet);
    }
    const fileOpen = !!document.querySelector('.top-actions')?.classList.contains('mobile-open');
    surfaceController.render({ fileOpen });
    referenceSurface?.sync();
    searchToolbarPresentation.sync();
    editorWorkspacePresentation.sync();
    dependencies.domains.renderingDomain?.setCountryPropertyEditingActive?.(
      surfaceController.isOpen('editor') && !!dependencies.projectState.state.selected,
    );
    dependencies.domainControllers.syncSelectionToolbarInteraction();
    if (fileOpen) requestAnimationFrame(syncFileMenuNotificationOffset);
    refreshMapSheetMetrics();
    syncEditorPanelControls();
    requestAnimationFrame(dependencies.taskPresentation.syncMapHudBounds);
    syncCreateSurface();
    menuPositionScheduler.schedule();
    if (!initial && previous !== layoutMode) (0, dependencies.mapHostViewB.queueMapResize)('layout-mode-change');
    return previous !== layoutMode;
  }

  function syncOverlayState() {
    const fileOpen = !!document.querySelector('.top-actions')?.classList.contains('mobile-open');
    const view = surfaceController.render({ fileOpen });
    referenceSurface?.sync();
    if (!surfaceController.isOpen('search')) {
      dependencies.domains.layerTreeController?.cancelSearch();
      cancelAnimationFrame(searchFocusFrame);
      searchFocusFrame = 0;
    } else {
      dependencies.domains.layerTreeController?.render(true);
    }
    searchToolbarPresentation.sync();
    editorWorkspacePresentation.sync();
    dependencies.domains.renderingDomain?.setCountryPropertyEditingActive?.(
      surfaceController.isOpen('editor') && !!dependencies.projectState.state.selected,
    );
    dependencies.domainControllers.syncSelectionToolbarInteraction();
    syncEditorPanelControls();
    refreshMapSheetMetrics();
    requestAnimationFrame(dependencies.taskPresentation.syncMapHudBounds);
    syncCreateSurface();
    menuPositionScheduler.schedule();
    if (fileOpen) requestAnimationFrame(syncFileMenuNotificationOffset);
    else (0, dependencies.platform.$)('app')?.style.removeProperty('--file-menu-notification-top');
    if (layoutMode !== 'wide') (0, dependencies.mapHostViewB.queueMapResize)('panel-layout');
    return view;
  }

  function syncFileMenuNotificationOffset() {
    const app = (0, dependencies.platform.$)('app');
    const menu = document.querySelector('.top-actions.mobile-open');
    if (!app || layoutMode !== 'mobile' || !menu) {
      app?.style.removeProperty('--file-menu-notification-top');
      return;
    }
    app.style.setProperty('--file-menu-notification-top', `${Math.ceil(menu.getBoundingClientRect().bottom)}px`);
  }

  function closeFileMenu({ restoreFocus = false } = {}) {
    const menu = document.querySelector('.top-actions');
    if (!menu?.classList.contains('mobile-open')) return;
    const trigger = fileMenuTrigger;
    fileMenuTrigger = null;
    menu.classList.remove('mobile-open');
    syncOverlayState();
    if (restoreFocus) {
      const focusTarget = isMobile()
        ? ((0, dependencies.platform.$)('mobileMenuFileBtn') || (0, dependencies.platform.$)('mobileMenuBtn'))
        : trigger;
      if (focusTarget?.isConnected) focusTarget.focus({ preventScroll: true });
    }
  }

  function activeCreateMenuItems() {
    const panel = (0, dependencies.platform.$)('createMenu');
    return panel ? [...panel.querySelectorAll('.ui-menu-item:not([disabled])')]
      .filter(item => !item.closest('.hidden, [hidden]') && item.getClientRects().length > 0) : [];
  }

  function focusSurfaceTrigger(surface) {
    const ids = isMobile()
      ? { create: 'mobileCreateBtn', search: 'mobileSearchBtn', display: 'mobileDisplayBtn', editor: 'mobileEditBtn', reference: 'referenceImageBtn' }
      : { create: 'createMenuBtn', search: 'objectSearchBtn', display: 'mapDisplayBtn', reference: 'referenceImageBtn' };
    const panelId = { create: 'createMenu', search: 'objectSearchSurface', display: 'mapDisplaySurface', editor: 'editorSurface', reference: 'referenceImageSurface' }[surface];
    const rememberedTrigger = lastOverlayTrigger?.isConnected
      && lastOverlayTrigger.getClientRects().length > 0
      && lastOverlayTrigger.getAttribute('aria-controls') === panelId
      ? lastOverlayTrigger
      : null;
    const trigger = rememberedTrigger || (0, dependencies.platform.$)(ids[surface])
      || (lastOverlayTrigger?.getClientRects().length ? lastOverlayTrigger : (0, dependencies.platform.$)('map'));
    trigger?.focus({ preventScroll: true });
  }

  function syncCreateSurface() {
    const panel = (0, dependencies.platform.$)('createMenu');
    if (!panel) return;
    const mobile = isMobile();
    panel.classList.toggle('ui-menu-surface', !mobile);
    panel.classList.toggle('ui-command-menu', !mobile);
    for (const button of panel.querySelectorAll('.ui-menu-item')) {
      button.tabIndex = mobile ? 0 : -1;
      if (mobile) button.removeAttribute('role');
      else button.setAttribute('role', 'menuitem');
    }
    (0, dependencies.platform.$)('createMenuBtn')?.setAttribute('aria-haspopup', 'menu');
    (0, dependencies.platform.$)('mobileCreateBtn')?.setAttribute('aria-haspopup', 'dialog');
    if (mobile) {
      clearMenuPosition(panel);
    } else if (surfaceController.isOpen('create')) {
      menuPositionScheduler.schedule();
    }
  }

  function positionWorkspaceMenus() {
    searchToolbarPresentation.position();
    const fileMenu = document.querySelector('.top-actions');
    const createMenu = (0, dependencies.platform.$)('createMenu');
    if (isMobile()) {
      clearMenuPosition(fileMenu);
      clearMenuPosition(createMenu);
      return;
    }
    if (fileMenu?.classList.contains('mobile-open')) {
      positionRootMenu({ menu: fileMenu, trigger: (0, dependencies.platform.$)('mobileFileBtn') });
    }
    if (surfaceController.isOpen('create')) {
      positionRootMenu({ menu: createMenu, trigger: (0, dependencies.platform.$)('createMenuBtn') });
    }
  }

  function closeActiveMobileSheet({ restoreFocus = false, syncHistory = true } = {}) {
    if (!isMobile() || !surfaceController.activeMobileSheet) return;
    const kind = surfaceController.activeMobileSheet;
    const surface = { create: 'create', search: 'search', display: 'display', edit: 'editor', reference: 'reference' }[kind];
    const panel = mobileSheetPanel(kind);
    surfaceController.close(surface);
    resetMobileSheetSession(panel);
    syncOverlayState();
    if (syncHistory) releaseMobileSheetHistory();
    if (restoreFocus) focusSurfaceTrigger(surface);
    if (restoreFocus) lastOverlayTrigger = null;
  }

  function closeMobileSheets(except = null, { restoreFocus = false } = {}) {
    if (isMobile()) {
      const exceptKind = { create: 'create', search: 'search', display: 'display', editor: 'edit', reference: 'reference' }[except] || null;
      if (surfaceController.activeMobileSheet && surfaceController.activeMobileSheet !== exceptKind) closeActiveMobileSheet({ restoreFocus });
      return;
    }
    for (const surface of ['create', 'search', 'display', 'editor', 'reference']) if (surface !== except) surfaceController.close(surface);
    syncOverlayState();
    if (restoreFocus && lastOverlayTrigger?.isConnected) lastOverlayTrigger.focus({ preventScroll: true });
    if (restoreFocus) lastOverlayTrigger = null;
  }

  function toggleFileMenu() {
    closeSurface('create');
    const menu = document.querySelector('.top-actions');
    if (!menu) return;
    const willOpen = !menu.classList.contains('mobile-open');
    if (willOpen) fileMenuTrigger = (0, dependencies.platform.$)('mobileFileBtn');
    menu.classList.toggle('mobile-open', willOpen);
    syncOverlayState();
    if (willOpen) requestAnimationFrame(() => menu.querySelector('[role="menuitem"]:not(:disabled)')?.focus({ preventScroll: true }));
  }

  function nearestSheetSnapIndex(height, panel) {
    let nearest = 0;
    let distance = Infinity;
    SHEET_SNAP_RATIOS.forEach((_, index) => {
      const nextDistance = Math.abs(height - mobileSheetSnapHeight(index, panel));
      if (nextDistance < distance) {
        distance = nextDistance;
        nearest = index;
      }
    });
    return nearest;
  }

  function clearMobileSheetDragPresentation(panel) {
    if (!panel) return;
    const pending = mobileSheetSettlement.get(panel);
    if (pending?.timer) clearTimeout(pending.timer);
    mobileSheetSettlement.delete(panel);
    panel.classList.remove('is-sheet-dragging', 'is-sheet-settling');
    panel.style.removeProperty('--sheet-drag-height');
    panel.style.removeProperty('--sheet-drag-offset');
  }

  function applyMobileSheetDragPreview(panel, height) {
    const maxHeight = mobileSheetSnapHeight(SHEET_SNAP_RATIOS.length - 1, panel);
    const minHeight = mobileSheetSnapHeight(0, panel);
    const visibleHeight = (0, dependencies.platform.clamp)(Number(height) || 0, Math.max(128, minHeight * 0.62), maxHeight);
    panel.style.setProperty('--sheet-drag-height', `${Math.round(maxHeight)}px`);
    panel.style.setProperty('--sheet-drag-offset', `${Math.round(maxHeight - visibleHeight)}px`);
    return { visibleHeight, maxHeight };
  }

  function finalizeMobileSheetSettlement(panel) {
    const settlement = mobileSheetSettlement.get(panel);
    if (!settlement) return false;
    if (settlement.timer) clearTimeout(settlement.timer);
    mobileSheetSettlement.delete(panel);
    const { dismiss, restoreFocus } = settlement;
    const resumeLayout = () => {
      if (!pendingLayoutChange) return;
      pendingLayoutChange = false;
      applyLayoutMode();
    };
    panel.classList.remove('is-sheet-dragging', 'is-sheet-settling');
    panel.style.removeProperty('--sheet-drag-height');
    panel.style.removeProperty('--sheet-drag-offset');
    if (dismiss) {
      closeActiveMobileSheet({ restoreFocus });
      resumeLayout();
      return true;
    }
    resumeLayout();
    refreshMapSheetMetrics();
    requestAnimationFrame(dependencies.taskPresentation.syncMapHudBounds);
    (0, dependencies.mapHostViewB.queueMapResize)('panel-layout');
    return true;
  }

  function settleMobileSheetDrag(panel, { targetIndex = null, dismiss = false, restoreFocus = false } = {}) {
    if (!panel) return;
    const previous = mobileSheetSettlement.get(panel);
    if (previous?.timer) clearTimeout(previous.timer);
    const maxHeight = mobileSheetSnapHeight(SHEET_SNAP_RATIOS.length - 1, panel);
    let targetOffset = maxHeight;
    if (!dismiss) {
      const safeTarget = (0, dependencies.platform.clamp)(Number(targetIndex) || 0, 0, SHEET_SNAP_RATIOS.length - 1);
      const targetHeight = setMobileSheetHeight(panel, safeTarget);
      targetOffset = Math.max(0, maxHeight - targetHeight);
    }
    const settlement = { dismiss, restoreFocus, timer: 0 };
    mobileSheetSettlement.set(panel, settlement);
    panel.classList.remove('is-sheet-dragging');
    panel.classList.add('is-sheet-settling');
    panel.style.setProperty('--sheet-drag-height', `${Math.round(maxHeight)}px`);
    requestAnimationFrame(() => {
      if (mobileSheetSettlement.get(panel) !== settlement) return;
      panel.style.setProperty('--sheet-drag-offset', `${Math.round(targetOffset)}px`);
    });
    settlement.timer = setTimeout(() => finalizeMobileSheetSettlement(panel), 280);
  }

  function beginMobileSheetDrag(panel, source, pointerId, clientY) {
    if (!isMobile() || !panel || activeSheetDrag) return false;
    clearMobileSheetDragPresentation(panel);
    const currentHeight = Number.parseFloat(getComputedStyle(panel).height)
      || setMobileSheetHeight(panel, sheetSnapIndex.get(panel.id) ?? MOBILE_SHEET_DEFAULT_SNAP);
    const preview = applyMobileSheetDragPreview(panel, currentHeight);
    panel.classList.add('is-sheet-dragging');
    activeSheetDrag = {
      panel,
      source,
      pointerId,
      startY: clientY,
      startHeight: currentHeight,
      previewHeight: preview.visibleHeight,
      maxHeight: preview.maxHeight,
      startIndex: sheetSnapIndex.get(panel.id) ?? MOBILE_SHEET_DEFAULT_SNAP,
      startTime: performance.now(),
      moved: false,
    };
    document.body.classList.add('map-sheet-dragging');
    return true;
  }

  function moveMobileSheetDrag(panel, source, pointerId, clientY) {
    const drag = activeSheetDrag;
    if (!drag || drag.panel !== panel || drag.source !== source || drag.pointerId !== pointerId) return false;
    const deltaY = clientY - drag.startY;
    if (Math.abs(deltaY) > 6) drag.moved = true;
    const preview = applyMobileSheetDragPreview(panel, drag.startHeight - deltaY);
    drag.previewHeight = preview.visibleHeight;
    drag.maxHeight = preview.maxHeight;
    return true;
  }

  function finishMobileSheetDrag(panel, source, pointerId, clientY, { cancelled = false } = {}) {
    const drag = activeSheetDrag;
    if (!drag || drag.panel !== panel || drag.source !== source || drag.pointerId !== pointerId) return null;
    const deltaY = clientY - drag.startY;
    const currentHeight = Number(drag.previewHeight || drag.startHeight);
    const elapsed = Math.max(1, performance.now() - drag.startTime);
    const velocity = deltaY / elapsed;
    activeSheetDrag = null;
    document.body.classList.remove('map-sheet-dragging');
    const dismissDistance = Math.min(180, drag.startHeight * 0.3);
    if (!cancelled && deltaY > 64 && (deltaY >= dismissDistance || velocity > 0.65)) {
      settleMobileSheetDrag(panel, { dismiss: true, restoreFocus: true });
    } else {
      let targetIndex = cancelled ? drag.startIndex : nearestSheetSnapIndex(currentHeight, panel);
      if (!cancelled && drag.moved && Math.abs(deltaY) > 24 && Math.abs(velocity) > 0.45) {
        targetIndex = (0, dependencies.platform.clamp)(drag.startIndex + (deltaY < 0 ? 1 : -1), 0, SHEET_SNAP_RATIOS.length - 1);
      }
      if (!cancelled) sheetSnapTouched.add(panel.id);
      settleMobileSheetDrag(panel, { targetIndex });
    }
    return drag;
  }

  function bindSheetDragHandle(handle) {
    const panel = (0, dependencies.platform.$)(handle?.dataset?.sheetHandle);
    if (!handle || !panel) return;
    handle.addEventListener('click', event => {
      if (!isMobile()) return;
      if (handle.dataset.dragged === 'true') {
        handle.dataset.dragged = 'false';
        event.preventDefault();
        return;
      }
      const current = sheetSnapIndex.get(panel.id) ?? MOBILE_SHEET_DEFAULT_SNAP;
      const target = current === SHEET_SNAP_RATIOS.length - 1 ? 0 : SHEET_SNAP_RATIOS.length - 1;
      sheetSnapTouched.add(panel.id);
      setMobileSheetHeight(panel, target);
      syncOverlayState();
    });
    handle.addEventListener('keydown', event => {
      if (!isMobile()) return;
      const current = sheetSnapIndex.get(panel.id) ?? MOBILE_SHEET_DEFAULT_SNAP;
      let target;
      if (event.key === 'ArrowUp' || event.key === 'PageUp') target = Math.min(SHEET_SNAP_RATIOS.length - 1, current + 1);
      else if (event.key === 'ArrowDown' || event.key === 'PageDown') target = Math.max(0, current - 1);
      else if (event.key === 'Home') target = 0;
      else if (event.key === 'End') target = SHEET_SNAP_RATIOS.length - 1;
      else if (event.key === 'Escape') {
        closeActiveMobileSheet({ restoreFocus: true });
        event.preventDefault();
        return;
      } else return;
      sheetSnapTouched.add(panel.id);
      setMobileSheetHeight(panel, target);
      refreshMapSheetMetrics();
      event.preventDefault();
    });
    handle.addEventListener('pointerdown', event => {
      if (!isMobile() || event.button > 0) return;
      if (!beginMobileSheetDrag(panel, handle, event.pointerId, event.clientY)) return;
      try { handle.setPointerCapture?.(event.pointerId); } catch (_) {}
      event.preventDefault();
    });
    handle.addEventListener('pointermove', event => {
      if (moveMobileSheetDrag(panel, handle, event.pointerId, event.clientY)) event.preventDefault();
    });
    const finish = (event, cancelled = false) => {
      const drag = finishMobileSheetDrag(panel, handle, event.pointerId, event.clientY, { cancelled });
      if (!drag) return;
      handle.dataset.dragged = String(drag.moved);
      try { handle.releasePointerCapture?.(event.pointerId); } catch (_) {}
    };
    handle.addEventListener('pointerup', event => finish(event));
    handle.addEventListener('pointercancel', event => finish(event, true));
  }

  function bindMobileSheetSurface(panel) {
    if (!panel) return;
    const header = panel.querySelector('.surface-header');
    const handle = panel.querySelector('[data-sheet-handle]');
    if (handle) bindSheetDragHandle(handle);

    if (header) {
      header.addEventListener('pointerdown', event => {
        if (!isMobile() || event.button > 0 || event.target.closest('[data-sheet-handle], button, input, textarea, select')) return;
        if (!beginMobileSheetDrag(panel, header, event.pointerId, event.clientY)) return;
        try { header.setPointerCapture?.(event.pointerId); } catch (_) {}
        event.preventDefault();
      });
      header.addEventListener('pointermove', event => {
        if (moveMobileSheetDrag(panel, header, event.pointerId, event.clientY)) event.preventDefault();
      });
      const finishHeader = (event, cancelled = false) => {
        const drag = finishMobileSheetDrag(panel, header, event.pointerId, event.clientY, { cancelled });
        if (!drag) return;
        try { header.releasePointerCapture?.(event.pointerId); } catch (_) {}
      };
      header.addEventListener('pointerup', event => finishHeader(event));
      header.addEventListener('pointercancel', event => finishHeader(event, true));
    }
    panel.addEventListener('transitionend', event => {
      if (event.target !== panel || !['height', 'width', 'transform'].includes(event.propertyName)) return;
      if (layoutMode !== 'wide') (0, dependencies.mapHostViewB.queueMapResize)('panel-layout');
    });
  }

  function openSelectionEditor({ explicit = false, trigger = null, focus = false } = {}) {
    const panel = (0, dependencies.platform.$)('editorSurface');
    if (!panel) return false;
    let opened = surfaceController.isOpen('editor');
    if (!opened && explicit) opened = openSurface('editor', { trigger, automatic: false });
    else if (!opened && (layoutMode !== 'wide' || !surfaceState.editorManuallyCollapsed)) {
      opened = openSurface('editor', { automatic: true });
    }
    if (!opened) return false;
    if (panel.classList.contains('mobile-open')) (0, dependencies.platform.$)('editorScrollBody')?.scrollTo?.({ top: 0, behavior: 'instant' });
    if (focus) requestAnimationFrame(() => {
      if (!surfaceController.isOpen('editor')) return;
      const visible = element => element.getClientRects().length > 0 && !element.closest('[hidden], .hidden');
      const target = [...panel.querySelectorAll('[role="tab"][aria-selected="true"]:not([aria-disabled="true"])')].find(visible)
        || [...panel.querySelectorAll('input:not([type="hidden"]):not(:disabled), select:not(:disabled), textarea:not(:disabled), button:not(:disabled)')].find(visible);
      target?.focus({ preventScroll: true });
    });
    dependencies.domainControllers.syncSelectionToolbarInteraction();
    return true;
  }

  function openSurface(surface, { trigger = null, automatic = false } = {}) {
    if (!['create', 'search', 'display', 'editor', 'reference'].includes(surface)) return false;
    if (dependencies.projectState.state.projectReplacing) return false;
    closeFileMenu();
    const activeTrigger = trigger instanceof HTMLElement ? trigger : document.activeElement instanceof HTMLElement ? document.activeElement : null;
    if (!surfaceController.open(surface, { automatic })) return false;
    if (activeTrigger) lastOverlayTrigger = activeTrigger;
    if (isMobile()) {
      const kind = { create: 'create', search: 'search', display: 'display', editor: 'edit', reference: 'reference' }[surface];
      const panel = mobileSheetPanel(kind);
      resetMobileSheetSession(panel);
      trackMobileSheetHistory(kind);
    }
    syncOverlayState();
    if (surface === 'search') {
      cancelAnimationFrame(searchFocusFrame);
      searchFocusFrame = requestAnimationFrame(() => {
        searchFocusFrame = 0;
        if (surfaceController.isOpen('search') && !dependencies.projectState.state.projectReplacing
          && !document.activeElement?.closest('.ui-modal, [aria-modal="true"]')) {
          (0, dependencies.platform.$)('layerSearchInput')?.focus({ preventScroll: true });
        }
      });
    }
    if (surface === 'create' && !isMobile()) {
      requestAnimationFrame(() => {
        if (surfaceController.isOpen('create') && !isMobile()) activeCreateMenuItems()[0]?.focus({ preventScroll: true });
      });
    }
    return true;
  }

  function closeSurface(surface, { manual = false, restoreFocus = false, syncHistory = true } = {}) {
    if (surface === 'editor' && editorWorkspacePresentation.isDocked()) return;
    const mobileKind = isMobile() ? { create: 'create', search: 'search', display: 'display', editor: 'edit', reference: 'reference' }[surface] : null;
    const mobilePanel = mobileKind ? mobileSheetPanel(mobileKind) : null;
    if (!surfaceController.close(surface, { manual, selected: !!dependencies.projectState.state?.selected })) return;
    if (mobilePanel) resetMobileSheetSession(mobilePanel);
    if (surface === 'editor') (0, dependencies.colorPicker.closeAllColorPickers)();
    syncOverlayState();
    if (mobilePanel && syncHistory) releaseMobileSheetHistory();
    if (restoreFocus) focusSurfaceTrigger(surface);
    if (restoreFocus) lastOverlayTrigger = null;
  }

  function completeToolStart(started, { destination = 'map' } = {}) {
    if (!started) return false;
    if (destination === 'editor') {
      openSurface('editor');
      requestAnimationFrame(() => {
        if (!surfaceController.isOpen('editor')) return;
        const panel = (0, dependencies.platform.$)('editorSurface');
        const input = [...panel.querySelectorAll('input:not([type="hidden"]):not(:disabled), select:not(:disabled), textarea:not(:disabled)')]
          .find(element => element.getClientRects().length > 0 && !element.closest('[hidden], .hidden'));
        input?.focus({ preventScroll: true });
      });
    } else if (isMobile()) {
      closeMobileSheets();
      requestAnimationFrame(() => (0, dependencies.platform.$)('map')?.focus({ preventScroll: true }));
    } else {
      closeSurface('create');
    }
    return true;
  }

  function toggleSurface(surface, trigger = null) {
    if (surfaceController.isOpen(surface)) {
      closeSurface(surface, { manual: surface === 'editor', restoreFocus: true });
    } else {
      openSurface(surface, { trigger });
    }
  }

  function initializeIsPolygonDraftTool() {
    (isPolygonDraftTool = tool => (0, dependencies.countryEditingA.draftToolConfig)(tool)?.shape === 'polygon');

    (isGenericFeatureDraftTool = tool => !!(0, dependencies.countryEditingA.draftToolConfig)(tool));

    (clampViewZooms = view => {
      if (!view) return view;
      view.globeZoom = (0, dependencies.platform.clamp)(Number(view.globeZoom) || 1, dependencies.mapNavigation.ZOOM_LIMITS.globe.min, dependencies.mapNavigation.ZOOM_LIMITS.globe.max);
      view.flatZoom = (0, dependencies.platform.clamp)(Number(view.flatZoom) || 1, dependencies.mapNavigation.ZOOM_LIMITS.flat.min, dependencies.mapNavigation.ZOOM_LIMITS.flat.max);
      return view;
    });

    (uid = () => (0, dependencies.projectServices.createProjectObjectId)());

    (detectLayoutMode = () => dependencies.platformConfigurationB.LAYOUT_QUERIES.mobile.matches ? 'mobile' : dependencies.platformConfigurationB.LAYOUT_QUERIES.compact.matches ? 'compact' : 'wide');

    (layoutMode = detectLayoutMode());

    (isMobile = () => layoutMode === 'mobile');

    (lastOverlayTrigger = null);

    (fileMenuTrigger = null);


    (surfaceController = (0, dependencies.uiFactoriesB.createSurfaceController)({ getElement: dependencies.platform.$, getLayout: () => layoutMode, document }));

    (surfaceState = surfaceController.state);

    (editorWorkspacePresentation = (0, dependencies.uiFactoriesA.createEditorWorkspacePresentation)({
      document,
      panel: (0, dependencies.platform.$)('editorSurface'), task: (0, dependencies.platform.$)('modeEditingContext'),
      dockSlot: (0, dependencies.platform.$)('editorTaskSlot'), floatingSlot: (0, dependencies.platform.$)('mapTopContextSlot'),
      content: (0, dependencies.platform.$)('modeTaskWindowContent'), minimize: (0, dependencies.platform.$)('modeTaskMinimizeBtn'),
      isEditorOpen: () => surfaceController.isOpen('editor'),
      openEditor: () => {
        if (!surfaceController.isOpen('create')) openSurface('editor');
      },
      onLayoutChange: () => (0, dependencies.mapHostViewB.queueMapResize)('editor-task-layout'),
    }));

    (editorSurfaceTabs = null);

    (MOBILE_SHEET_SNAP_COLLAPSED_PX = 84);

    (MOBILE_SHEET_EDITOR_RATIOS = Object.freeze({ half: 0.48, expanded: 0.86 }));

    (MOBILE_SHEET_AUXILIARY_RATIOS = Object.freeze({ half: 0.52, expanded: 0.88 }));

    (SHEET_SNAP_RATIOS = Object.freeze([0, MOBILE_SHEET_EDITOR_RATIOS.half, MOBILE_SHEET_EDITOR_RATIOS.expanded]));

    (SHEET_SNAP_LABELS = Object.freeze(['접힌 상태', '중간 높이', '확장']));

    (SHEET_SNAP_DEFAULTS = Object.freeze({ create: 1, search: 1, display: 1, edit: 1, reference: 1 }));

    (MOBILE_SHEET_DEFAULT_SNAP = 1);

    (MOBILE_SHEET_IDS = Object.freeze({ create: 'createMenu', search: 'objectSearchSurface', display: 'mapDisplaySurface', edit: 'editorSurface', reference: 'referenceImageSurface' }));

    (sheetSnapIndex = new Map(Object.values(MOBILE_SHEET_IDS).map(id => [id, MOBILE_SHEET_DEFAULT_SNAP])));

    (sheetSnapTouched = new Set());

    (activeSheetDrag = null);

    (mapModeContextWasActive = false);

    (MOBILE_SHEET_HISTORY_KEY = '__atlaswrightMobileSheet');

    (ignoreNextMobileSheetPopstate = false);

    (mobileViewportHeight = () => window.visualViewport?.height || window.innerHeight);

    (mobileSheetSettlement = new Map());

    (menuPositionScheduler = createMenuPositionScheduler(positionWorkspaceMenus));
    searchToolbarPresentation = createSearchToolbarPresentation({
      document,
      panel: (0, dependencies.platform.$)('objectSearchSurface'),
      toolbar: document.querySelector('.map-command-toolbar'),
      slot: document.querySelector('.map-command-search'),
      results: (0, dependencies.platform.$)('layerSearchResults'),
      isOpen: () => surfaceController.isOpen('search'),
      isMobile,
      schedulePosition: () => menuPositionScheduler.schedule(),
    });
    (0, dependencies.platform.$)('layerSearchInput')?.addEventListener('compositionstart', () => { searchComposing = true; });
    (0, dependencies.platform.$)('layerSearchInput')?.addEventListener('compositionend', () => {
      searchComposing = false;
      if (pendingLayoutChange) applyLayoutMode();
    });
    window.addEventListener('resize', () => menuPositionScheduler.schedule());
    window.visualViewport?.addEventListener?.('resize', () => menuPositionScheduler.schedule());
    window.visualViewport?.addEventListener?.('scroll', () => menuPositionScheduler.schedule());
  }

  return Object.freeze({
    connect,
    registerReferenceImageSurface(adapter) {
      referenceSurface = adapter;
      bindMobileSheetSurface(adapter.panel);
      syncOverlayState();
      return () => { if (referenceSurface === adapter) referenceSurface = null; };
    },
    setReferenceImageGestureActive(active) {
      referenceGestureActive = active === true;
      if (!referenceGestureActive && pendingLayoutChange) applyLayoutMode();
    },
    initializeIsPolygonDraftTool,
    get MOBILE_SHEET_HISTORY_KEY() { return MOBILE_SHEET_HISTORY_KEY; },
    get MOBILE_SHEET_IDS() { return MOBILE_SHEET_IDS; },
    get activeCreateMenuItems() { return activeCreateMenuItems; },
    get applyLayoutMode() { return applyLayoutMode; },
    get bindMobileSheetSurface() { return bindMobileSheetSurface; },
    get clampViewZooms() { return clampViewZooms; },
    get closeActiveMobileSheet() { return closeActiveMobileSheet; },
    get closeFileMenu() { return closeFileMenu; },
    get closeMobileSheets() { return closeMobileSheets; },
    get closeSurface() { return closeSurface; },
    get editorSurfaceTabs() { return editorSurfaceTabs; },
    set editorSurfaceTabs(value) { editorSurfaceTabs = value; },
    get editorWorkspacePresentation() { return editorWorkspacePresentation; },
    get fileMenuTrigger() { return fileMenuTrigger; },
    set fileMenuTrigger(value) { fileMenuTrigger = value; },
    get ignoreNextMobileSheetPopstate() { return ignoreNextMobileSheetPopstate; },
    set ignoreNextMobileSheetPopstate(value) { ignoreNextMobileSheetPopstate = value; },
    get isGenericFeatureDraftTool() { return isGenericFeatureDraftTool; },
    get isMobile() { return isMobile; },
    get isPolygonDraftTool() { return isPolygonDraftTool; },
    get layoutMode() { return layoutMode; },
    get mapModeContextWasActive() { return mapModeContextWasActive; },
    set mapModeContextWasActive(value) { mapModeContextWasActive = value; },
    get openSelectionEditor() { return openSelectionEditor; },
    get openSurface() { return openSurface; },
    get refreshMapSheetMetrics() { return refreshMapSheetMetrics; },
    get completeToolStart() { return completeToolStart; },
    get focusSurfaceTrigger() { return focusSurfaceTrigger; },
    get setMobileSheetHeight() { return setMobileSheetHeight; },
    get surfaceController() { return surfaceController; },
    get surfaceState() { return surfaceState; },
    get syncOverlayState() { return syncOverlayState; },
    get toggleFileMenu() { return toggleFileMenu; },
    get toggleSurface() { return toggleSurface; },
    get trackMobileSheetHistory() { return trackMobileSheetHistory; },
    get uid() { return uid; },
  });
}
