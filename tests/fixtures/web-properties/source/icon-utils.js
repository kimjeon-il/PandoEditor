const SVG_NS = 'http://www.w3.org/2000/svg';

/**
 * Semantic icon vocabulary shared by the editor surfaces.  The sprite remains
 * the source of truth for the actual paths; this registry only gives callers a
 * stable meaning-to-symbol mapping without duplicating SVG markup.
 */
const ICON_REGISTRY = Object.freeze({
  add: 'icon-plus',
  close: 'icon-close',
  check: 'icon-check',
  save: 'icon-save',
  openProject: 'icon-folder-open',
  importGis: 'icon-map-import',
  exportGis: 'icon-map-export',
  preferences: 'icon-gear',
  more: 'icon-more',
  chevronRight: 'icon-chevron-right',
  chevronDown: 'icon-chevron-down',
  eye: 'icon-eye',
  eyeOff: 'icon-eye-off',
  settings: 'icon-tune',
  focus: 'icon-focus-target',
  lock: 'icon-lock-closed',
  unlock: 'icon-lock-open',
  delete: 'icon-trash',
  country: 'icon-country',
  territory: 'icon-territory',
  subunit: 'icon-territory',
  administrative: 'icon-administrative',
  region: 'icon-region',
  distribution: 'icon-language',
  language: 'icon-language',
  ethnicity: 'icon-ethnicity',
  religion: 'icon-religion',
  place: 'icon-place',
  river: 'icon-river',
  lake: 'icon-lake',
  boundary: 'icon-boundary-edit',
  coastline: 'icon-coastline',
  merge: 'icon-merge',
  split: 'icon-split',
  transfer: 'icon-transfer',
  transform: 'icon-type',
  library: 'icon-library',
  map: 'icon-map',
  projectionFlat: 'icon-projection-flat',
  globe: 'icon-globe',
});

/** Create a sprite-backed icon without duplicating inline SVG markup. */
function createSvgIcon(documentRef, symbolId, className = 'ui-icon') {
  const icon = documentRef.createElementNS(SVG_NS, 'svg');
  icon.setAttribute('class', className);
  icon.setAttribute('viewBox', '0 0 24 24');
  icon.setAttribute('aria-hidden', 'true');
  const use = documentRef.createElementNS(SVG_NS, 'use');
  use.setAttribute('href', `#${symbolId}`);
  icon.appendChild(use);
  return icon;
}

export function createSemanticIcon(documentRef, semanticName, className = 'ui-icon') {
  const symbolId = ICON_REGISTRY[semanticName];
  if (!symbolId) throw new Error(`Unknown semantic icon: ${semanticName}`);
  return createSvgIcon(documentRef, symbolId, className);
}
