const TERRITORIAL_KINDS = new Set(['country', 'subunit', 'region']);

const intersects = (left, right) => left.right > right.left
  && left.left < right.right
  && left.bottom > right.top
  && left.top < right.bottom;

export function createSelectionToolbarPresentation({
  window,
  document,
  getElement,
  getSelection = () => ({ items: [], primaryKey: null }),
  getView = () => null,
  commitFlag = () => false,
  openFlagLibrary = () => false,
  openEditor = () => false,
  closeEditor = () => false,
  isEditorOpen = () => false,
  isMutationBlocked = () => false,
  getProjectGeneration = () => 0,
  closeColorPickers = () => {},
  createSemanticIcon,
  getLayout = () => 'wide',
} = {}) {
  const $ = getElement;
  let activeRef = null;
  let flagReadRevision = 0;
  let bound = false;
  let resizeObserver = null;

  const root = () => $('selectionToolbar');
  const activeKind = () => activeRef?.type || '';
  const currentSelection = () => {
    const selection = getSelection() || { items: [], primaryKey: null };
    const ref = selection.items?.find(item => item.key === selection.primaryKey)
      || selection.items?.at?.(-1)
      || null;
    return selection.items?.length === 1 && ref?.domain === 'territorial' && TERRITORIAL_KINDS.has(ref.type)
      ? ref
      : null;
  };

  function closeFlag({ restoreFocus = false } = {}) {
    const menu = $('flagMenu');
    if (!menu?.matches?.(':popover-open')) return false;
    menu.hidePopover();
    if (restoreFocus && !$('flagMenuBtn')?.disabled) $('flagMenuBtn')?.focus({ preventScroll: true });
    return true;
  }

  function closeNotes({ restoreFocus = false } = {}) {
    const popover = $('selectionToolbarNotesPopover');
    if (!popover || popover.classList.contains('hidden')) return false;
    popover.classList.add('hidden');
    $('selectionToolbarNotesBtn')?.setAttribute('aria-expanded', 'false');
    if (restoreFocus) $('selectionToolbarNotesBtn')?.focus({ preventScroll: true });
    return true;
  }

  function closeTransient({ restoreFocus = false } = {}) {
    const colorTrigger = root()?.querySelector('[data-color-picker].is-open .ui-color-trigger');
    const colorOpen = !!colorTrigger;
    const notesClosed = closeNotes({ restoreFocus });
    const flagClosed = closeFlag({ restoreFocus: restoreFocus && !notesClosed && !colorOpen });
    closeColorPickers();
    if (restoreFocus && colorOpen && !notesClosed && !flagClosed) {
      colorTrigger.focus({ preventScroll: true });
    }
    return notesClosed || flagClosed || colorOpen;
  }

  function renderFlag(view) {
    const preview = $('flagPreview');
    if (!preview) return;
    preview.replaceChildren();
    const upload = $('flagUploadBtn');
    const reset = $('flagDefaultBtn');
    const remove = $('flagRemoveBtn');
    if (upload) upload.disabled = false;
    if (reset) reset.disabled = !view?.hasFlagOverride;
    if (remove) remove.disabled = !view?.flagUrl;
    if (!view?.flagUrl) {
      if (createSemanticIcon) preview.appendChild(createSemanticIcon(document, view?.ref?.type || 'country'));
      return;
    }
    const image = document.createElement('img');
    image.src = view.flagUrl;
    image.alt = `${view.name || '선택 객체'} 깃발`;
    const expectedKey = view.ref?.key;
    image.addEventListener('error', () => {
      if (activeRef?.key !== expectedKey || !preview.contains(image)) return;
      renderFlag({ ...view, flagUrl: null });
    }, { once: true });
    preview.appendChild(image);
  }

  function syncInteraction() {
    const toolbar = root();
    if (!toolbar || toolbar.classList.contains('hidden') || !activeRef) return false;
    const view = getView(activeRef);
    if (!view) {
      clear();
      return false;
    }
    const blocked = !!isMutationBlocked(activeRef, view);
    toolbar.classList.toggle('is-readonly', blocked);
    const kind = activeKind();
    const name = $(`${kind}NameInput`);
    const color = $(`${kind}ColorTrigger`);
    if (name) name.disabled = blocked;
    if (color) color.disabled = blocked;
    if ($('flagMenuBtn')) $('flagMenuBtn').disabled = blocked;
    const notes = toolbar.querySelector(`[data-selection-notes="${kind}"] textarea`);
    if (notes) {
      notes.readOnly = blocked;
      notes.setAttribute('aria-readonly', String(blocked));
    }
    const editButton = $('selectionToolbarEditBtn');
    const editorOpen = !!isEditorOpen();
    editButton?.setAttribute('aria-expanded', String(editorOpen));
    editButton?.setAttribute('aria-label', editorOpen ? '편집 닫기' : '편집 열기');
    const typeButton = $('selectionToolbarTypeBtn');
    const typeConvertible = kind === 'country' || kind === 'subunit';
    if (typeButton) {
      const label = kind === 'country' ? '하위단위로 전환' : '국가로 전환';
      typeButton.classList.toggle('hidden', !typeConvertible);
      typeButton.disabled = blocked || !typeConvertible;
      typeButton.setAttribute('aria-label', label);
      typeButton.dataset.tooltip = label;
    }
    if (blocked) {
      closeFlag();
      closeColorPickers();
    }
    return true;
  }

  function syncOcclusion() {
    const toolbar = root();
    if (!toolbar || toolbar.classList.contains('hidden')) return false;
    const mobile = getLayout() === 'mobile';
    const sheet = mobile ? document.querySelector('.workspace-surface.mobile-open') : null;
    const covered = !!(sheet && intersects(toolbar.getBoundingClientRect(), sheet.getBoundingClientRect()));
    toolbar.inert = covered;
    toolbar.classList.toggle('is-covered', covered);
    toolbar.setAttribute('aria-hidden', String(covered));
    if (covered) closeTransient();
    return covered;
  }

  function sync() {
    const toolbar = root();
    if (!toolbar) return false;
    const ref = currentSelection();
    if (!ref) {
      clear();
      return false;
    }
    if (activeRef?.key !== ref.key) closeTransient();
    activeRef = ref;
    const view = getView(ref);
    if (!view) {
      clear();
      return false;
    }
    toolbar.classList.remove('hidden');
    toolbar.inert = false;
    toolbar.dataset.objectKey = ref.key;
    toolbar.dataset.objectType = ref.type;
    toolbar.setAttribute('aria-hidden', 'false');
    toolbar.setAttribute('aria-label', `${view.name || '선택 객체'} 기본 정보`);
    for (const fields of toolbar.querySelectorAll('[data-selection-kind]')) {
      fields.classList.toggle('hidden', fields.dataset.selectionKind !== ref.type);
    }
    for (const field of toolbar.querySelectorAll('[data-selection-notes]')) {
      field.classList.toggle('hidden', field.dataset.selectionNotes !== ref.type);
    }
    renderFlag(view);
    syncInteraction();
    window.requestAnimationFrame(syncOcclusion);
    return true;
  }

  function clear() {
    const toolbar = root();
    flagReadRevision += 1;
    activeRef = null;
    closeTransient();
    if (!toolbar) return false;
    toolbar.classList.add('hidden');
    toolbar.classList.remove('is-readonly', 'is-covered');
    toolbar.inert = false;
    toolbar.removeAttribute('data-object-key');
    toolbar.removeAttribute('data-object-type');
    toolbar.setAttribute('aria-hidden', 'true');
    $('selectionToolbarEditBtn')?.setAttribute('aria-expanded', 'false');
    $('selectionToolbarEditBtn')?.setAttribute('aria-label', '편집 열기');
    return true;
  }

  function positionFlagMenu() {
    const menu = $('flagMenu');
    const trigger = $('flagMenuBtn');
    if (!menu?.matches?.(':popover-open') || !trigger) return;
    const edge = Number.parseFloat(window.getComputedStyle(document.documentElement).getPropertyValue('--ui-popover-screen-edge')) || 8;
    const gap = Number.parseFloat(window.getComputedStyle(document.documentElement).getPropertyValue('--ui-menu-trigger-gap')) || 6;
    const rect = trigger.getBoundingClientRect();
    const menuRect = menu.getBoundingClientRect();
    const left = Math.max(edge, Math.min(rect.left, window.innerWidth - menuRect.width - edge));
    const below = rect.bottom + gap;
    const top = below + menuRect.height <= window.innerHeight - edge
      ? below
      : Math.max(edge, rect.top - menuRect.height - gap);
    menu.style.left = `${Math.round(left)}px`;
    menu.style.top = `${Math.round(top)}px`;
  }

  function bind() {
    if (bound) return api;
    bound = true;
    $('selectionToolbarNotesBtn')?.addEventListener('click', () => {
      const popover = $('selectionToolbarNotesPopover');
      if (!popover || !activeRef) return;
      const opening = popover.classList.contains('hidden');
      closeFlag();
      closeColorPickers();
      popover.classList.toggle('hidden', !opening);
      $('selectionToolbarNotesBtn')?.setAttribute('aria-expanded', String(opening));
      if (opening) window.requestAnimationFrame(() => popover.querySelector(`[data-selection-notes="${activeKind()}"] textarea`)?.focus({ preventScroll: true }));
    });
    $('selectionToolbarEditBtn')?.addEventListener('click', event => {
      if (!activeRef) return;
      closeTransient();
      if (isEditorOpen()) closeEditor(activeRef, event.currentTarget);
      else openEditor(activeRef, event.currentTarget);
      syncInteraction();
    });
    $('selectionToolbarTypeBtn')?.addEventListener('click', () => {
      if (!activeRef) return;
      const targetId = activeKind() === 'country' ? 'changeCountryTypeBtn'
        : activeKind() === 'subunit' ? 'promoteSubunitBtn' : '';
      const target = targetId ? $(targetId) : null;
      if (!target || target.disabled) return;
      closeTransient();
      target.click();
    });
    $('flagMenuBtn')?.addEventListener('click', () => {
      const menu = $('flagMenu');
      if (!menu || $('flagMenuBtn')?.disabled) return;
      closeNotes();
      closeColorPickers();
      if (menu.matches(':popover-open')) {
        closeFlag();
        return;
      }
      menu.showPopover();
      window.requestAnimationFrame(() => {
        positionFlagMenu();
        menu.querySelector('button:not(:disabled):not(.hidden)')?.focus({ preventScroll: true });
      });
    });
    $('flagMenu')?.addEventListener('toggle', () => {
      const open = $('flagMenu')?.matches?.(':popover-open') || false;
      $('flagMenuBtn')?.setAttribute('aria-expanded', String(open));
      if (!open) {
        $('flagMenu')?.style.removeProperty('left');
        $('flagMenu')?.style.removeProperty('top');
      }
    });
    $('flagMenu')?.addEventListener('keydown', event => {
      const items = [...event.currentTarget.querySelectorAll('button:not(:disabled):not(.hidden)')];
      const index = items.indexOf(document.activeElement);
      if (['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
        event.preventDefault();
        const next = event.key === 'Home' ? 0 : event.key === 'End' ? items.length - 1
          : (index + (event.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
        items[next]?.focus({ preventScroll: true });
      } else if (event.key === 'Escape') {
        event.preventDefault();
        event.stopPropagation();
        closeFlag({ restoreFocus: true });
      } else if (event.key === 'Tab') closeFlag();
    });
    $('flagUploadBtn')?.addEventListener('click', () => {
      closeFlag({ restoreFocus: true });
      $('flagFileInput')?.click();
    });
    $('flagLibraryBtn')?.addEventListener('click', () => {
      const ref = activeRef;
      closeFlag();
      if (!ref || currentSelection()?.key !== ref.key) return;
      void Promise.resolve(openFlagLibrary({
        onPickFlag: value => {
          if (currentSelection()?.key === ref.key) commitFlag(ref, value);
        },
        restoreFocus: $('flagMenuBtn'),
      }));
    });
    $('flagDefaultBtn')?.addEventListener('click', () => {
      const ref = activeRef;
      closeFlag({ restoreFocus: true });
      if (ref && currentSelection()?.key === ref.key) commitFlag(ref, undefined);
    });
    $('flagRemoveBtn')?.addEventListener('click', () => {
      const ref = activeRef;
      closeFlag({ restoreFocus: true });
      if (ref && currentSelection()?.key === ref.key) commitFlag(ref, null);
    });
    $('flagFileInput')?.addEventListener('change', event => {
      const file = event.target.files?.[0];
      const ref = activeRef;
      const generation = getProjectGeneration();
      const revision = ++flagReadRevision;
      event.target.value = '';
      if (!file || !ref || currentSelection()?.key !== ref.key) return;
      const reader = new window.FileReader();
      reader.addEventListener('load', () => {
        if (revision !== flagReadRevision || generation !== getProjectGeneration() || currentSelection()?.key !== ref.key) return;
        commitFlag(ref, reader.result);
      }, { once: true });
      reader.readAsDataURL(file);
    });
    document.addEventListener('pointerdown', event => {
      if (!event.target.closest('#selectionToolbarNotesPopover, #selectionToolbarNotesBtn')) closeNotes();
      if (event.target.closest('.ui-color-trigger')) closeFlag();
    }, true);
    document.addEventListener('keydown', event => {
      if (event.key !== 'Escape' || !activeRef) return;
      if (closeTransient({ restoreFocus: true })) {
        event.preventDefault();
        event.stopImmediatePropagation();
      }
    }, true);
    window.addEventListener('resize', () => {
      positionFlagMenu();
      syncOcclusion();
    });
    if (typeof window.ResizeObserver === 'function') {
      resizeObserver = new window.ResizeObserver(syncOcclusion);
      document.querySelectorAll('.workspace-surface').forEach(element => resizeObserver.observe(element));
    }
    return api;
  }

  function dispose() {
    flagReadRevision += 1;
    resizeObserver?.disconnect();
    resizeObserver = null;
    clear();
  }

  const api = Object.freeze({ bind, clear, closeTransient, dispose, sync, syncInteraction, syncOcclusion });
  return api;
}
