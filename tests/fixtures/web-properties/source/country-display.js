// Match known default names only; never strip spaces from arbitrary user names.
const DEFAULT_NAMES = Object.freeze({
  TUR: ['터키', '튀르키예'], ESP: ['스페인', '에스파냐'],
  ALD: ['올란드 제도', '올란드제도'], FRO: ['페로 제도', '페로제도'],
  PCN: ['핏케언 제도', '핏케언제도'], MHL: ['마셜 제도', '마셜제도'],
  CYM: ['케이맨 제도', '케이맨제도'], COK: ['쿡 제도', '쿡제도'],
  SLB: ['솔로몬 제도', '솔로몬제도'], FLK: ['포클랜드 제도', '포클랜드제도'],
  MNP: ['북마리아나 제도', '북마리아나제도'], CSI: ['산호해 제도', '산호해제도'],
});

export function defaultGeographicName(id, name) {
  const entry = DEFAULT_NAMES[id];
  return entry && name === entry[0] ? entry[1] : name;
}

export function countryDisplayName(feature, override = {}) {
  if (override.name) return override.name;
  const name = feature?.properties?.name;
  return defaultGeographicName(feature?.id, name) || '국가';
}

export function countrySelectionStatus(view, area = '') {
  return [view.displayName, area].filter(Boolean).join(' · ');
}
