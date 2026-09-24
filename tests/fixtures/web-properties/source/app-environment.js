import { interactionCssProperties } from './map-interaction-style.js';
/** Environment: extracted application responsibility.
 * Dependencies are explicitly wired once by the composition modules.
 * Mutable bindings stay local; exported accessors retain live identity.
 */
export function createEnvironment() {
  let dependencies;
  let d3;
  let territorialGeometry;
  let APP_VERSION;
  let HYDRO_DATA_VERSION;
  let FLAT_PROJECTION_KIND;
  let FLAT_LATITUDE_LIMIT;
  let ASSET_REVISION;
  let DATA_REVISION;
  let PANDOLAB_ASSET_BASE_URL;
  let PHYSICAL_DATA_BASE_URL;
  let HISTORICAL_LIBRARY_DATA_URL;
  let PHYSICAL_DATASET;
  let TERRAIN_DATASET;
  let HYDRO_DATASET;
  let STORAGE_KEY;
  let AUTOSAVE_DB_NAME;
  let AUTOSAVE_STORE_NAME;
  let AUTOSAVE_RECORD_KEY;
  let AUTOSAVE_VIEW_KEY;
  let BASE_DATASET;
  let DARK_DEFAULT_COLOR;
  let LIGHT_DEFAULT_COLOR;
  let DEFAULT_GENERIC_FEATURE_COLOR;
  let COLOR_PRESETS;
  let COLOR_PALETTE_TONES;
  let COLOR_PALETTE_NEUTRALS;
  let COLOR_PALETTE_HUES;
  let COLOR_PALETTE_COLORS;
  let ZOOM_LIMITS;
  let HYDRO_TOOL_CONFIG;
  let HYDRO_LAYER_META;
  let TERRAIN_OCEAN_REPRESENTATIVE;
  let MAX_HISTORY;
  let CUT_ENDPOINT_SNAP_DISTANCE;
  let LAYOUT_QUERIES;
  let systemThemeQuery;
  let systemTheme;
  let userPreferences;
  let runtimeReady;
  let MAP_LABEL_FONT_STACKS;
  let resolvedAccentColor;
  let resolvedInteractionStyle;
  let $;
  let selectController;
  let tooltipController;
  let bindUiTooltips;
  let REQUIRED_UI_IDS;
  let CACHE_MISMATCH_MESSAGE;
  let deepClone;
  let reliabilityDiagnostic;
  let clamp;
  function connect(ports) {
    if (dependencies) throw new Error('environment already connected');
    dependencies = ports;
  }

  function applyMapLabelPreferences() {
    const root = document.documentElement.style;
    const labels = userPreferences.labels;
    root.setProperty('--country-label-font-family', MAP_LABEL_FONT_STACKS[labels.country.font] || MAP_LABEL_FONT_STACKS.default);
    root.setProperty('--place-label-font-family', MAP_LABEL_FONT_STACKS[labels.place.font] || MAP_LABEL_FONT_STACKS.default);
    if (labels.country.color) root.setProperty('--country-label-color', labels.country.color);
    else root.removeProperty('--country-label-color');
    if (labels.place.color) root.setProperty('--place-label-color', labels.place.color);
    else root.removeProperty('--place-label-color');
    if (labels.place.pointColor) root.setProperty('--place-label-point-color', labels.place.pointColor);
    else root.removeProperty('--place-label-point-color');
  }

  function resolveCurrentInteractionStyle() {
    const theme = (0, dependencies.preferences.effectiveTheme)(userPreferences, systemTheme === 'dark');
    const computed = getComputedStyle(document.documentElement);
    return (0, dependencies.applicationServicesB.resolveMapInteractionStyle)({
      theme,
      selectionColor: userPreferences.selection.color || resolvedAccentColor,
      outlineVisible: userPreferences.selection.outlineVisible,
      fillStrength: userPreferences.selection.fillStrength,
      tokens: {
        accent: computed.getPropertyValue('--accent').trim(),
        textStrong: computed.getPropertyValue('--text-strong').trim(),
      },
    });
  }

  function enableKeyboardNavigation(event) {
    if (event.key === 'Tab') document.documentElement.classList.add('keyboard-navigation');
  }

  function disableKeyboardNavigation() {
    document.documentElement.classList.remove('keyboard-navigation');
  }

  function mapTheme() {
    const terrainVisible = dependencies.projectState.state?.physicalSettings?.terrainVisible !== false;
    const terrainStyle = dependencies.projectState.state?.physicalSettings?.terrainStyle || 'political';
    const terrainFillAlpha = terrainVisible
      ? (terrainStyle === 'physical' ? 0.22 : 0.68)
      : null;
    const countryStyle = dependencies.projectState.state?.layerPresentation ? (0, dependencies.applicationServicesB.layerStyle)(dependencies.projectState.state.layerPresentation, 'countries') : { opacity: 1, boundaryVisible: true, boundaryWidth: 1 };
    const riverStyle = dependencies.projectState.state?.layerPresentation ? (0, dependencies.applicationServicesB.layerStyle)(dependencies.projectState.state.layerPresentation, 'rivers') : { opacity: 1, boundaryVisible: true, boundaryWidth: 1 };
    const lakeStyle = dependencies.projectState.state?.layerPresentation ? (0, dependencies.applicationServicesB.layerStyle)(dependencies.projectState.state.layerPresentation, 'lakes') : { opacity: 1, boundaryVisible: true, boundaryWidth: 1 };
    const base = (document.documentElement.dataset.theme || window.__PANDOLAB_THEME__ || systemTheme) === 'light'
      ? { defaultLand: LIGHT_DEFAULT_COLOR, fillAlpha: terrainFillAlpha ?? 1, border: '#ffffff', borderGpu: [1, 1, 1], borderAlpha: 1, ocean: '#ffffff', oceanGpu: [1, 1, 1] }
      : { defaultLand: DARK_DEFAULT_COLOR, fillAlpha: terrainFillAlpha ?? 0.74, border: '#323c46', borderGpu: [0.196, 0.235, 0.275], borderAlpha: 0.92, ocean: '#0d2837', oceanGpu: [0.051, 0.157, 0.216] };
    base.terrainColorAlpha = terrainFillAlpha ?? 1;
    base.countryColorAlpha = base.fillAlpha;
    base.fillAlpha *= countryStyle.opacity;
    base.fillAlphaByte = Math.round(base.fillAlpha * 255);
    base.borderAlpha = countryStyle.boundaryVisible ? base.borderAlpha * countryStyle.opacity : 0;
    base.borderWidth = countryStyle.boundaryWidth;
    base.riverOpacity = riverStyle.opacity;
    base.riverWidth = riverStyle.boundaryWidth;
    base.lakeOpacity = lakeStyle.opacity;
    base.lakeBoundaryVisible = lakeStyle.boundaryVisible;
    base.lakeBoundaryWidth = lakeStyle.boundaryWidth;
    return base;
  }

  function defaultCountryColor() {
    return mapTheme().defaultLand;
  }

  function applySystemTheme(matchesDark) {
    const nextTheme = matchesDark ? 'dark' : 'light';
    if (nextTheme === systemTheme && userPreferences.appearance.theme !== 'system') return;
    systemTheme = nextTheme;
    document.documentElement.dataset.systemTheme = systemTheme;
    document.documentElement.dataset.theme = (0, dependencies.preferences.effectiveTheme)(userPreferences, systemTheme === 'dark');
    window.__PANDOLAB_THEME__ = (0, dependencies.preferences.effectiveTheme)(userPreferences, systemTheme === 'dark');
    resolvedAccentColor = (0, dependencies.preferences.applyAppAccent)(document, userPreferences.appearance.accentColor);
    (0, dependencies.preferences.syncResolvedInteractionStyle)();
    dependencies.rendering.gpuMapRenderer.invalidateCountryPalette({ base: true, emphasis: true }, 'system-theme');
    dependencies.rendering.gpuMapRenderer.invalidatePhysicalStyle('system-theme');
    if (dependencies.projectState.state?.selected?.domain === 'territorial' && dependencies.projectState.state.selected.type === dependencies.territorialModel.TERRITORIAL_UNIT_TYPES.COUNTRY) {
      const id = String(dependencies.projectState.state.selected.id);
      const feature = (0, dependencies.countries.countryFeatureById)(id);
      const color = (0, dependencies.colorModel.readDomainColor)(dependencies.colorModel.COLOR_DOMAINS.COUNTRY, { feature, override: dependencies.projectState.state.countryOverrides[id] }, { fallback: defaultCountryColor() });
      if (color.isDefault && $('countryColorInput')) $('countryColorInput').value = color.value;
      (0, dependencies.colorPicker.syncColorPicker)('country', {
        value: color.value,
        defaultColor: defaultCountryColor(),
        isDefault: color.isDefault,
      });
    }
    if (dependencies.mapLayers.svg) {
      (0, dependencies.layers.markLayerTreeDirty)();
      dependencies.domains.layerTreeController?.render();
      dependencies.domains.renderingDomain?.invalidateBaseScene?.('system-theme');
    }
  }

  function createEmptyState(title, description, { tag = 'div', compact = false } = {}) {
    const empty = document.createElement(tag);
    empty.className = `ui-empty-state${compact ? ' ui-empty-state--compact' : ''}`;
    const heading = document.createElement('strong');
    const text = document.createElement('p');
    heading.textContent = title;
    text.textContent = description;
    empty.append(heading, text);
    return empty;
  }

  function syncSearchClearButton(input, button) {
    button?.classList.toggle('hidden', !String(input?.value || '').length);
  }

  function runtimeAssetUrl(relativePath) {
    const url = new URL(relativePath, PANDOLAB_ASSET_BASE_URL);
    url.searchParams.set('v', ASSET_REVISION);
    return url;
  }

  function assertRuntimeCompatibility() {
    const htmlVersion = $('app')?.dataset.appVersion;
    const bootstrapVersion = window.PANDOLAB_APP_VERSION;
    if (htmlVersion !== APP_VERSION || bootstrapVersion !== APP_VERSION) throw new Error(CACHE_MISMATCH_MESSAGE);
    const missingIds = REQUIRED_UI_IDS.filter(id => !$(id));
    const missingSelectors = ['.workspace'].filter(selector => !document.querySelector(selector));
    if (missingIds.length || missingSelectors.length) throw new Error(CACHE_MISMATCH_MESSAGE);
  }

  function initializeD3() {
    (d3 = window.d3);
  }

  function initializeTerritorialGeometry() {
    (territorialGeometry = (0, dependencies.territorialServicesA.createTerritorialGeometryKernel)(window.polygonClipping));

    (APP_VERSION = String(globalThis.PANDOLAB_BUILD_META?.appVersion || ''));

    (HYDRO_DATA_VERSION = '0.13.1');

    (FLAT_PROJECTION_KIND = 'equirectangular');

    (FLAT_LATITUDE_LIMIT = 89.999);

    (ASSET_REVISION = String(window.PANDOLAB_ASSET_REVISION || globalThis.PANDOLAB_BUILD_META?.assetRevision || ''));

    (DATA_REVISION = String(window.PANDOLAB_DATA_REVISION || globalThis.PANDOLAB_BUILD_META?.dataRevision || `data-${APP_VERSION}`));

    if (!APP_VERSION || !ASSET_REVISION) throw new Error('빌드 메타데이터가 불완전합니다.');

    (PANDOLAB_ASSET_BASE_URL = window.PANDOLAB_ASSET_BASE_URL || new URL('./assets/js/', location.href).href);

    (PHYSICAL_DATA_BASE_URL = new URL('../data/', PANDOLAB_ASSET_BASE_URL));

    (HISTORICAL_LIBRARY_DATA_URL = new URL('historical-library-pilot.json', PHYSICAL_DATA_BASE_URL));

    HISTORICAL_LIBRARY_DATA_URL.searchParams.set('v', ASSET_REVISION);

    (PHYSICAL_DATASET = 'HydroRIVERS 1.0 · Natural Earth 5.0.0 호수 · raster 3.2.0');

    (TERRAIN_DATASET = 'Natural Earth raster 3.2.0 1:10m');

    (HYDRO_DATASET = 'HydroRIVERS 1.0 · Natural Earth 5.0.0 1:10m lakes');

    (STORAGE_KEY = 'pandolab-editor-project');

    (AUTOSAVE_DB_NAME = 'pandolab-editor');

    (AUTOSAVE_STORE_NAME = 'projects');

    (AUTOSAVE_RECORD_KEY = 'active-project');

    (AUTOSAVE_VIEW_KEY = 'active-view');

    (BASE_DATASET = 'Natural Earth 5.1.1 · Admin 0 Countries · 1:10m · de facto');

    (DARK_DEFAULT_COLOR = '#63758a');

    (LIGHT_DEFAULT_COLOR = '#cccccc');

    (DEFAULT_GENERIC_FEATURE_COLOR = '#8c68d8');

    (COLOR_PRESETS = Object.freeze([
      '#000000', '#4b5563', '#9ca3af', '#ffffff', '#7f1d1d', '#dc2626',
      '#f97316', '#f59e0b', '#facc15', '#166534', '#22c55e', '#14b8a6',
      '#0ea5e9', '#2563eb', '#4338ca', '#7c3aed', '#a855f7', '#db2777',
      '#f43f5e', '#8b5e3c', '#cda95d', '#63758a', '#cccccc', '#8c68d8',
    ]));

    (COLOR_PALETTE_TONES = Object.freeze(['아주 밝음', '밝음', '기본', '어두움', '아주 어두움']));

    (COLOR_PALETTE_NEUTRALS = Object.freeze([
      Object.freeze({ color: '#f3f4f6', label: '회색 아주 밝음' }),
      Object.freeze({ color: '#d1d5db', label: '회색 밝음' }),
      Object.freeze({ color: '#9ca3af', label: '회색 기본' }),
      Object.freeze({ color: '#4b5563', label: '회색 어두움' }),
      Object.freeze({ color: '#1f2937', label: '회색 아주 어두움' }),
    ]));

    (COLOR_PALETTE_HUES = Object.freeze([
      Object.freeze({ name: '빨강', colors: Object.freeze(['#fee2e2', '#fca5a5', '#ef4444', '#b91c1c', '#7f1d1d']) }),
      Object.freeze({ name: '주황', colors: Object.freeze(['#ffedd5', '#fdba74', '#f97316', '#c2410c', '#7c2d12']) }),
      Object.freeze({ name: '황금', colors: Object.freeze(['#fef3c7', '#fcd34d', '#f59e0b', '#b45309', '#78350f']) }),
      Object.freeze({ name: '노랑', colors: Object.freeze(['#fef9c3', '#fde047', '#eab308', '#a16207', '#713f12']) }),
      Object.freeze({ name: '연두', colors: Object.freeze(['#ecfccb', '#bef264', '#84cc16', '#4d7c0f', '#365314']) }),
      Object.freeze({ name: '초록', colors: Object.freeze(['#dcfce7', '#86efac', '#22c55e', '#15803d', '#14532d']) }),
      Object.freeze({ name: '청록', colors: Object.freeze(['#ccfbf1', '#5eead4', '#14b8a6', '#0f766e', '#134e4a']) }),
      Object.freeze({ name: '시안', colors: Object.freeze(['#cffafe', '#67e8f9', '#06b6d4', '#0e7490', '#164e63']) }),
      Object.freeze({ name: '파랑', colors: Object.freeze(['#dbeafe', '#93c5fd', '#3b82f6', '#1d4ed8', '#1e3a8a']) }),
      Object.freeze({ name: '인디고', colors: Object.freeze(['#e0e7ff', '#a5b4fc', '#6366f1', '#4338ca', '#312e81']) }),
      Object.freeze({ name: '보라', colors: Object.freeze(['#ede9fe', '#c4b5fd', '#8b5cf6', '#6d28d9', '#4c1d95']) }),
      Object.freeze({ name: '분홍', colors: Object.freeze(['#fce7f3', '#f9a8d4', '#ec4899', '#be185d', '#831843']) }),
    ]));

    (COLOR_PALETTE_COLORS = Object.freeze(COLOR_PALETTE_TONES.flatMap((tone, toneIndex) => (
      COLOR_PALETTE_HUES.map(hue => Object.freeze({
        color: hue.colors[toneIndex],
        label: `${hue.name} ${tone}`,
        family: hue.name,
        tone,
      }))
    ))));

    (ZOOM_LIMITS = Object.freeze({
      globe: Object.freeze({ min: 0.72, max: 32 }),
      flat: Object.freeze({ min: 0.75, max: 64 }),
    }));

    (HYDRO_TOOL_CONFIG = Object.freeze({
      river: Object.freeze({ geometry: 'LineString', category: 'river', label: '강', color: '#3b82c4', prefix: 'river' }),
      lake: Object.freeze({ geometry: 'Polygon', category: 'lake', label: '호수', color: '#5aa9d6', prefix: 'lake' }),
    }));

    (HYDRO_LAYER_META = Object.freeze({
      rivers_hydro: Object.freeze({ label: '강', shortLabel: '강', sourceLabel: 'HydroRIVERS', category: 'river', color: '#3b82c4' }),
      lakes_natural_earth: Object.freeze({ label: '호수', shortLabel: '호수', sourceLabel: 'Natural Earth', category: 'lake', color: '#5aa9d6' }),
    }));

    (TERRAIN_OCEAN_REPRESENTATIVE = '#6aa8d2');

    (MAX_HISTORY = 30);

    (CUT_ENDPOINT_SNAP_DISTANCE = Object.freeze({ mouse: 10, touch: 18 }));

    (LAYOUT_QUERIES = {
      mobile: window.matchMedia('(max-width: 799px)'),
      compact: window.matchMedia('(min-width: 800px) and (max-width: 1359px)'),
    });

    (systemThemeQuery = window.matchMedia('(prefers-color-scheme: dark)'));

    (systemTheme = systemThemeQuery.matches ? 'dark' : 'light');

    (userPreferences = (0, dependencies.applicationServicesB.loadUserPreferences)());

    (runtimeReady = false);

    document.documentElement.dataset.systemTheme = systemTheme;

    document.documentElement.dataset.theme = (0, dependencies.preferences.effectiveTheme)(userPreferences, systemTheme === 'dark');
    document.documentElement.dataset.statusBarVisible = String(userPreferences.appearance?.statusBarVisible !== false);

    (MAP_LABEL_FONT_STACKS = Object.freeze({
      default: 'var(--ui-font-family)',
      gothic: '"Pretendard Variable", Pretendard, "Noto Sans KR", "Apple SD Gothic Neo", sans-serif',
      serif: '"Noto Serif KR", "Nanum Myeongjo", "AppleMyungjo", serif',
    }));

    applyMapLabelPreferences();

    (resolvedAccentColor = (0, dependencies.preferences.applyAppAccent)(document, userPreferences.appearance.accentColor));

    (resolvedInteractionStyle = resolveCurrentInteractionStyle());

    for (const [property, value] of Object.entries(interactionCssProperties(resolvedInteractionStyle))) document.documentElement.style.setProperty(property, value);

    (0, dependencies.selectionServices.setSelectionColor)(resolvedInteractionStyle.selection.color);

    (0, dependencies.selectionServices.setSelectionInteractionStyle)(resolvedInteractionStyle);

    window.__PANDOLAB_THEME__ = (0, dependencies.preferences.effectiveTheme)(userPreferences, systemTheme === 'dark');

    document.addEventListener('keydown', enableKeyboardNavigation, true);

    if ('PointerEvent' in window) document.addEventListener('pointerdown', disableKeyboardNavigation, true);
    else {
      document.addEventListener('mousedown', disableKeyboardNavigation, true);
      document.addEventListener('touchstart', disableKeyboardNavigation, { capture: true, passive: true });
    }

    ($ = (id) => document.getElementById(id));

    (selectController = (0, dependencies.uiFactoriesB.createSelectController)({ document, window }));

    selectController.enhanceAll();

    (tooltipController = (0, dependencies.uiFactoriesB.createTooltipController)({
      document,
      window,
      tooltip: $('uiTooltip'),
      clamp: (value, minimum, maximum) => clamp(value, minimum, maximum),
    }));

    (bindUiTooltips = () => tooltipController.bind());

    (REQUIRED_UI_IDS = Object.freeze([
      'app', 'map', 'mapBottomStatus', 'statusView', 'projectionStatus', 'statusSelection', 'projectSaveStatus', 'projectSaveStatusText', 'uiTooltip',
      'objectSearchSurface', 'objectSearchSection', 'mapDisplaySurface', 'mapViewSection', 'mapViewProjectionSlot', 'projectionControl',
      'globeBtn', 'flatBtn', 'countriesVisible', 'subunitsVisible', 'regionsVisible', 'languagesVisible', 'ethnicitiesVisible', 'religionsVisible', 'riversVisible', 'lakesVisible', 'genericFeaturesVisible', 'labelsVisible', 'basemapLabelsVisible', 'countryFlagsVisible', 'subunitLabelsVisible', 'subunitFlagsVisible', 'regionLabelsVisible', 'regionFlagsVisible', 'distributionLayerModeInput', 'distributionBoundaryVisibleInput',
      'createMenuBtn', 'mobileCreateBtn', 'objectSearchBtn', 'mapDisplayBtn', 'mobileSearchBtn', 'mobileDisplayBtn', 'resetViewBtn', 'terrainVisible', 'terrainPoliticalRadio', 'terrainPhysicalRadio', 'selectionToolbar', 'selectionToolbarNotesBtn', 'selectionToolbarTypeBtn', 'selectionToolbarEditBtn', 'selectionToolbarNotesPopover', 'flagMenuBtn', 'flagMenu', 'flagPreview', 'countryNameInput', 'countryColorInput', 'notesInput',
      'debugMapPanel', 'countryAreaValue',
      'flagUploadBtn', 'flagFileInput', 'flagRemoveBtn',
      'genericFeatureConversionSection', 'genericFeatureConvertType', 'genericFeatureConvertCountryField', 'genericFeatureConvertCountryInput', 'genericFeatureConvertDistributionField', 'genericFeatureConvertDistributionInput', 'convertGenericFeatureBtn', 'genericFeatureRoleValue', 'genericFeatureTopologyValue',
      'labelNameInput', 'labelKindInput', 'labelNotesInput', 'labelPositionValue',
      'editorScrollBody', 'editorObjectHeader', 'editorObjectStatus', 'emptyProperties', 'propertyTitle', 'propertyTypeLabel', 'editorTabBtn', 'actionsTabBtn', 'relationTabBtn', 'objectVisibilityBtn', 'objectVisibilityIcon', 'objectLockBtn', 'objectDeleteBtn', 'objectActionsMenu',
      'countryProperties', 'subunitProperties', 'regionProperties', 'distributionProperties', 'subunitNameConflict', 'regionNameConflict', 'regionNameInput', 'regionCountryInput', 'regionParentInput', 'regionColorInput', 'regionValidFromInput', 'regionValidToInput', 'regionNotesInput', 'distributionNameInput', 'distributionTypeValue', 'distributionColorInput', 'distributionParentInput', 'distributionRenderModeInput', 'distributionEntryList', 'distributionTerritorialUnitInput', 'distributionShareInput', 'addTerritorialDistributionBtn', 'addGeometryDistributionBtn', 'genericFeatureProperties', 'labelProperties', 'hydroProperties',
      'editBorderBtn', 'editCoastBtn', 'changeCountryTypeBtn', 'reconcileSubunitCoastBtn', 'territorialTypeModal', 'territorialTypeTitle', 'territorialTypeContext', 'territorialTypeInput', 'territorialTypeSovereignRow', 'territorialTypeSovereignInput', 'territorialTypeParentRow', 'territorialTypeParentInput', 'territorialTypeImpact', 'territorialTypeImpactSummary', 'territorialTypeImpactList', 'territorialTypeCancelBtn', 'territorialTypeConfirmBtn',
      'genericFeatureIdInput', 'hydroCategoryValue', 'hydroIdLabel', 'hydroIdValue', 'hydroSystemRow', 'hydroSystemValue', 'hydroTributaryValue', 'hydroSourceValue', 'hydroBuiltinHelp', 'hydroEditFields', 'hydroNameInput', 'hydroColorInput', 'hydroNotesInput', 'copyHydroBtn',
      'undoBtn', 'redoBtn', 'rightPanel',
      'mapTopContextSlot', 'modeEditingContext', 'modeEditingHud', 'modeTaskWindowContent', 'modeTaskMinimizeBtn', 'modeActionBar', 'modeTaskName', 'modeTaskStage', 'modeTaskStatus', 'modeCancelIcon', 'modePrimaryIcon', 'modeTaskInstruction', 'modeTaskTargets', 'modeTaskTargetsLabel', 'modeTaskTargetList', 'modeTaskTargetsFocusBtn', 'modeTaskDisabledReason', 'annexCountryFlow', 'annexTargetCountryFlag', 'annexTargetCountryName', 'annexDonorCountryFlag', 'annexDonorCountryName',
      'modeMethodSwitch', 'modeDirectLineMethodInput', 'modePolygonMethodOption', 'modePolygonMethodInput', 'modeComponentsMethodInput', 'modeRiverBoundaryOption', 'modeRiverBoundaryInput', 'modeDraftActions', 'modeDraftInsertBtn', 'modeDraftDoneBtn', 'modeDraftRedrawBtn', 'modeDraftDeleteBtn', 'geometryPreviewSummary', 'modePrimaryBtn', 'modeCancelBtn',
      'multiDrawnActions', 'multiDrawnCount', 'multiDrawnAddBtn', 'multiDrawnUndoBtn',
      'multiPropertiesVisibilityInput', 'multiCountryActions', 'multiBorderEditBtn', 'multiBorderEditHelp',
      'saveProjectBtn', 'openProjectBtn', 'projectFileInput', 'openGisBtn', 'gisFileInput', 'newProjectBtn', 'dataExportBtn', 'preferencesBtn', 'preferencesModal', 'preferencesThemeInput', 'preferencesStatusBarVisibleInput', 'preferencesApplyBtn', 'preferencesResetBtn', 'preferencesCancelBtn', 'helpBtn', 'helpModal', 'helpCloseBtn', 'helpDoneBtn',
      'createBuildPanel', 'addCountryBtn', 'addSubunitBtn', 'addRegionBtn', 'territorialCreateSetup', 'territorialCreateNameLabel', 'territorialCreateNameInput', 'territorialCreateSovereignInput', 'territorialCreateParentInput', 'territorialCreateSourceInput', 'territorialCreateReference', 'territorialCreateReferenceLabel', 'territorialCreateReferenceCount', 'territorialCreateReferenceList',
      'gisTargetCountry', 'gisParentUnit', 'gisExportModal', 'gisExportConfirmBtn', 'confirmModalChoiceRow', 'confirmModalChoice',
      'coastReconciliationModal', 'coastReconciliationTitle', 'coastReconciliationMessage', 'coastReconciliationImpact', 'coastReconciliationImpactList', 'coastReconciliationCountryBtn', 'coastReconciliationAdminBtn', 'coastReconciliationIndependentBtn', 'coastReconciliationCancelBtn',
      'layerSearchInput', 'layerSearchClearBtn', 'addFromLibraryBtn', 'historicalLibraryModal', 'historicalLibraryCloseBtn', 'historicalLibrarySearchInput', 'historicalLibrarySearchClearBtn', 'historicalLibraryTypeInput', 'historicalLibraryStatusInput', 'historicalLibraryYearInput', 'historicalLibraryGeographicRegionInput', 'historicalLibraryResults', 'historicalLibraryPreview', 'historicalLibrarySnapshotInput', 'historicalLibrarySnapshotBtn', 'historicalLibraryChildDepthInput', 'historicalLibraryAddBtn',
    ]));

    (CACHE_MISMATCH_MESSAGE = '화면 파일과 스크립트 버전이 다릅니다. 페이지를 강력 새로고침하세요. PC에서는 Ctrl+F5를 사용할 수 있습니다.');

    (deepClone = (obj) => JSON.parse(JSON.stringify(obj)));

    (reliabilityDiagnostic = (0, dependencies.applicationFactories.createDiagnosticLog)({ limit: 250 }));

    window.__PANDOLAB_RELIABILITY_LOG__ = reliabilityDiagnostic;
  }

  function initializeClamp() {
    (clamp = (v, min, max) => Math.max(min, Math.min(max, v)));
  }

  return Object.freeze({
    connect,
    initializeD3,
    initializeTerritorialGeometry,
    initializeClamp,
    get $() { return $; },
    get APP_VERSION() { return APP_VERSION; },
    get ASSET_REVISION() { return ASSET_REVISION; },
    get AUTOSAVE_DB_NAME() { return AUTOSAVE_DB_NAME; },
    get AUTOSAVE_RECORD_KEY() { return AUTOSAVE_RECORD_KEY; },
    get AUTOSAVE_STORE_NAME() { return AUTOSAVE_STORE_NAME; },
    get AUTOSAVE_VIEW_KEY() { return AUTOSAVE_VIEW_KEY; },
    get BASE_DATASET() { return BASE_DATASET; },
    get COLOR_PALETTE_COLORS() { return COLOR_PALETTE_COLORS; },
    get COLOR_PALETTE_NEUTRALS() { return COLOR_PALETTE_NEUTRALS; },
    get COLOR_PRESETS() { return COLOR_PRESETS; },
    get CUT_ENDPOINT_SNAP_DISTANCE() { return CUT_ENDPOINT_SNAP_DISTANCE; },
    get DATA_REVISION() { return DATA_REVISION; },
    get DEFAULT_GENERIC_FEATURE_COLOR() { return DEFAULT_GENERIC_FEATURE_COLOR; },
    get FLAT_LATITUDE_LIMIT() { return FLAT_LATITUDE_LIMIT; },
    get FLAT_PROJECTION_KIND() { return FLAT_PROJECTION_KIND; },
    get HISTORICAL_LIBRARY_DATA_URL() { return HISTORICAL_LIBRARY_DATA_URL; },
    get HYDRO_DATASET() { return HYDRO_DATASET; },
    get HYDRO_DATA_VERSION() { return HYDRO_DATA_VERSION; },
    get HYDRO_LAYER_META() { return HYDRO_LAYER_META; },
    get HYDRO_TOOL_CONFIG() { return HYDRO_TOOL_CONFIG; },
    get LAYOUT_QUERIES() { return LAYOUT_QUERIES; },
    get MAX_HISTORY() { return MAX_HISTORY; },
    get PHYSICAL_DATASET() { return PHYSICAL_DATASET; },
    get PHYSICAL_DATA_BASE_URL() { return PHYSICAL_DATA_BASE_URL; },
    get STORAGE_KEY() { return STORAGE_KEY; },
    get TERRAIN_DATASET() { return TERRAIN_DATASET; },
    get TERRAIN_OCEAN_REPRESENTATIVE() { return TERRAIN_OCEAN_REPRESENTATIVE; },
    get ZOOM_LIMITS() { return ZOOM_LIMITS; },
    get applyMapLabelPreferences() { return applyMapLabelPreferences; },
    get applySystemTheme() { return applySystemTheme; },
    get assertRuntimeCompatibility() { return assertRuntimeCompatibility; },
    get bindUiTooltips() { return bindUiTooltips; },
    get clamp() { return clamp; },
    get coarsePointer() { return globalThis.matchMedia?.('(pointer: coarse)')?.matches === true; },
    get createEmptyState() { return createEmptyState; },
    get d3() { return d3; },
    get deepClone() { return deepClone; },
    get defaultCountryColor() { return defaultCountryColor; },
    get mapTheme() { return mapTheme; },
    get polygonClipping() { return globalThis.polygonClipping; },
    get reliabilityDiagnostic() { return reliabilityDiagnostic; },
    get resolveCurrentInteractionStyle() { return resolveCurrentInteractionStyle; },
    get resolvedAccentColor() { return resolvedAccentColor; },
    set resolvedAccentColor(value) { resolvedAccentColor = value; },
    get resolvedInteractionStyle() { return resolvedInteractionStyle; },
    set resolvedInteractionStyle(value) { resolvedInteractionStyle = value; },
    get runtimeAssetUrl() { return runtimeAssetUrl; },
    get runtimeReady() { return runtimeReady; },
    set runtimeReady(value) { runtimeReady = value; },
    get selectController() { return selectController; },
    get syncSearchClearButton() { return syncSearchClearButton; },
    get systemTheme() { return systemTheme; },
    get systemThemeQuery() { return systemThemeQuery; },
    get territorialGeometry() { return territorialGeometry; },
    get userPreferences() { return userPreferences; },
    set userPreferences(value) { userPreferences = value; },
  });
}
