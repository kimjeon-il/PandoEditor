const freezeList = values => Object.freeze([...(values || [])]);
const freezeType = type => Object.freeze({
  ...type,
  layerGroups: freezeList(type.layerGroups),
  presentationGroups: freezeList(type.presentationGroups),
  createMethods: freezeList(type.createMethods),
  allowedActions: freezeList(type.allowedActions),
});
const freezeCategory = category => Object.freeze({
  ...category,
  layerGroups: freezeList(category.layerGroups),
  createItems: freezeList(category.createItems),
  viewGroups: freezeList(category.viewGroups),
});

const MAP_OBJECT_DOMAINS = Object.freeze({
  TERRITORIAL: 'territorial',
  DISTRIBUTION: 'distribution',
  HYDRO: 'hydro',
  LABEL: 'label',
  GENERIC: 'generic',
});

/**
 * Canonical semantic metadata for map objects.
 *
 * This registry does not define storage schemas. It is the single source of
 * truth for object presentation and UI routing: domain, category, label, icon,
 * layer/presentation mapping, editor target and creation entry point.
 */
export const MAP_OBJECT_TYPES = Object.freeze({
  entity: freezeType({ key: 'entity', domain: 'territorial', type: 'entity', category: 'territorial', label: '객체', icon: 'icon-territory', layerGroup: 'countries', presentationGroup: 'countries', editor: 'entity', creatable: true, createButton: 'addEntityBtn', createAction: 'entity-create' }),
  distribution: freezeType({
    key: 'distribution',
    domain: MAP_OBJECT_DOMAINS.DISTRIBUTION,
    type: 'distribution',
    category: 'distribution',
    label: '분포',
    icon: 'icon-area-draw',
    layerGroup: 'distributions',
    layerGroups: ['distributions'],
    presentationGroup: 'distributions',
    presentationGroups: ['distributions'],
    editor: 'distribution',
    creatable: true,
    createButton: 'addDistributionBtn',
    createAction: 'distribution-create',
  }),
  label: freezeType({
    key: 'label',
    domain: MAP_OBJECT_DOMAINS.LABEL,
    type: 'label',
    category: 'features',
    label: '지명',
    icon: 'icon-place',
    layerGroup: 'labels',
    presentationGroup: 'labels',
    editor: 'label',
    creatable: true,
    createButton: 'addLabelBtn',
    createAction: 'label-placement',
  }),
  river: freezeType({
    key: 'river',
    domain: MAP_OBJECT_DOMAINS.HYDRO,
    type: 'river',
    category: 'features',
    label: '강',
    icon: 'icon-river',
    layerGroup: 'rivers',
    presentationGroup: 'rivers',
    editor: 'hydro',
    creatable: true,
    createButton: 'addRiverBtn',
    createAction: 'river-create',
  }),
  lake: freezeType({
    key: 'lake',
    domain: MAP_OBJECT_DOMAINS.HYDRO,
    type: 'lake',
    category: 'features',
    label: '호수',
    icon: 'icon-lake',
    layerGroup: 'lakes',
    presentationGroup: 'lakes',
    editor: 'hydro',
    creatable: true,
    createButton: 'addLakeBtn',
    createAction: 'lake-create',
  }),
  generic: freezeType({
    key: 'generic',
    domain: MAP_OBJECT_DOMAINS.GENERIC,
    type: 'feature',
    category: 'features',
    label: '기타 객체',
    icon: 'icon-shape',
    layerGroup: 'genericFeatures',
    presentationGroup: 'genericFeatures',
    editor: 'generic',
    creatable: false,
    fallbackOnly: true,
    allowedActions: ['focus', 'lock', 'delete'],
    createButton: null,
    createAction: null,
  }),
});

export const MAP_OBJECT_CATEGORIES = Object.freeze({
  territorial: freezeCategory({
    key: 'territorial',
    label: '영토·구역',
    layerGroups: ['countries', 'subunits', 'regions'],
    createItems: ['entity'],
  }),
  distribution: freezeCategory({
    key: 'distribution',
    label: '분포',
    layerGroups: ['distributions'],
    createItems: ['distribution'],
  }),
  features: freezeCategory({
    key: 'features',
    label: '지형지물',
    layerGroups: ['rivers', 'lakes', 'genericFeatures'],
    createItems: ['label', 'river', 'lake'],
    viewGroups: ['labels', 'countryLabels'],
  }),
});

export const MAP_OBJECT_CATEGORY_ORDER = Object.freeze(Object.keys(MAP_OBJECT_CATEGORIES));

export function objectTypeKeyForRef(value) {
  if (typeof value === 'string') return MAP_OBJECT_TYPES[value] ? value : '';
  const domain = String(value?.domain || '').trim();
  const type = String(value?.type || value?.kind || '').trim();
  if (domain === MAP_OBJECT_DOMAINS.TERRITORIAL) return type === 'entity' ? 'entity' : '';
  if (domain === MAP_OBJECT_DOMAINS.DISTRIBUTION) return 'distribution';
  if (domain === MAP_OBJECT_DOMAINS.GENERIC) return 'generic';
  if (domain === MAP_OBJECT_DOMAINS.LABEL) return 'label';
  if (domain === MAP_OBJECT_DOMAINS.HYDRO && (type === 'river' || type === 'lake')) return type;
  return MAP_OBJECT_TYPES[type] ? type : '';
}

export function objectTypeDescriptor(value) {
  const key = objectTypeKeyForRef(value);
  return key ? MAP_OBJECT_TYPES[key] : null;
}

export function objectTypeLabel(value, fallback = '') {
  return objectTypeDescriptor(value)?.label || fallback;
}

export function categoryForCreateItem(type) {
  return MAP_OBJECT_TYPES[type]?.category || '';
}

export function categoryForLayerGroup(group) {
  return MAP_OBJECT_CATEGORY_ORDER.find(category => MAP_OBJECT_CATEGORIES[category].layerGroups.includes(group)) || '';
}

export function layerGroupForObjectType(value) {
  const descriptor = objectTypeDescriptor(value);
  if (!descriptor) return '';
  return descriptor.layerGroup || '';
}

export function presentationGroupForObjectType(value) {
  const descriptor = objectTypeDescriptor(value);
  if (!descriptor) return '';
  if (descriptor.key === 'distribution') return layerGroupForObjectType(descriptor.key);
  return descriptor.presentationGroup || '';
}
