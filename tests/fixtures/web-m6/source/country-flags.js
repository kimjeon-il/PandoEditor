const hasOwn = (value, key) => Object.prototype.hasOwnProperty.call(value || {}, key);
const text = value => String(value ?? '').trim();

const COUNTRY_FLAG_NATIVE_REVISION = 'c09927e63705529bbf59ca6684cd9b23225dddad';

const CURRENT_COUNTRY_FLAG_CODES = Object.freeze({
  IDN: 'id', MYS: 'my', CHL: 'cl', BOL: 'bo', PER: 'pe', ARG: 'ar', CYP: 'cy', IND: 'in',
  CHN: 'cn', ISR: 'il', PSX: 'ps', LBN: 'lb', ETH: 'et', SDS: 'ss', SOM: 'so', KEN: 'ke',
  MWI: 'mw', TZA: 'tz', SYR: 'sy', FRA: 'fr', SUR: 'sr', GUY: 'gy', KOR: 'kr', PRK: 'kp',
  MAR: 'ma', SAH: 'eh', CRI: 'cr', NIC: 'ni', COG: 'cg', COD: 'cd', BTN: 'bt', UKR: 'ua',
  BLR: 'by', NAM: 'na', ZAF: 'za', MAF: 'mf', SXM: 'sx', OMN: 'om', UZB: 'uz', KAZ: 'kz',
  TJK: 'tj', LTU: 'lt', BRA: 'br', URY: 'uy', MNG: 'mn', RUS: 'ru', CZE: 'cz', DEU: 'de',
  EST: 'ee', LVA: 'lv', NOR: 'no', SWE: 'se', FIN: 'fi', VNM: 'vn', KHM: 'kh', LUX: 'lu',
  ARE: 'ae', BEL: 'be', GEO: 'ge', MKD: 'mk', ALB: 'al', AZE: 'az', KOS: 'xk', TUR: 'tr',
  ESP: 'es', LAO: 'la', KGZ: 'kg', ARM: 'am', DNK: 'dk', LBY: 'ly', TUN: 'tn', ROU: 'ro',
  HUN: 'hu', SVK: 'sk', POL: 'pl', IRL: 'ie', GBR: 'gb', GRC: 'gr', ZMB: 'zm', SLE: 'sl',
  GIN: 'gn', LBR: 'lr', CAF: 'cf', SDN: 'sd', DJI: 'dj', ERI: 'er', AUT: 'at', IRQ: 'iq',
  ITA: 'it', CHE: 'ch', IRN: 'ir', NLD: 'nl', LIE: 'li', CIV: 'ci', SRB: 'rs', MLI: 'ml',
  SEN: 'sn', NGA: 'ng', BEN: 'bj', AGO: 'ao', HRV: 'hr', SVN: 'si', QAT: 'qa', SAU: 'sa',
  BWA: 'bw', ZWE: 'zw', PAK: 'pk', BGR: 'bg', THA: 'th', SMR: 'sm', HTI: 'ht', DOM: 'do',
  TCD: 'td', KWT: 'kw', SLV: 'sv', GTM: 'gt', TLS: 'tl', BRN: 'bn', MCO: 'mc', DZA: 'dz',
  MOZ: 'mz', SWZ: 'sz', BDI: 'bi', RWA: 'rw', MMR: 'mm', BGD: 'bd', AND: 'ad', AFG: 'af',
  MNE: 'me', BIH: 'ba', UGA: 'ug', CUB: 'cu', HND: 'hn', ECU: 'ec', COL: 'co', PRY: 'py',
  PRT: 'pt', MDA: 'md', TKM: 'tm', JOR: 'jo', NPL: 'np', LSO: 'ls', CMR: 'cm', GAB: 'ga',
  NER: 'ne', BFA: 'bf', TGO: 'tg', GHA: 'gh', GNB: 'gw', GIB: 'gi', USA: 'us', CAN: 'ca',
  MEX: 'mx', BLZ: 'bz', PAN: 'pa', VEN: 've', PNG: 'pg', EGY: 'eg', YEM: 'ye', MRT: 'mr',
  GNQ: 'gq', GMB: 'gm', HKG: 'hk', VAT: 'va', ATA: 'aq', AUS: 'au', GRL: 'gl', FJI: 'fj',
  NZL: 'nz', NCL: 'nc', MDG: 'mg', PHL: 'ph', LKA: 'lk', CUW: 'cw', ABW: 'aw', BHS: 'bs',
  TCA: 'tc', TWN: 'tw', JPN: 'jp', SPM: 'pm', ISL: 'is', PCN: 'pn', PYF: 'pf', ATF: 'tf',
  SYC: 'sc', KIR: 'ki', MHL: 'mh', TTO: 'tt', GRD: 'gd', VCT: 'vc', BRB: 'bb', LCA: 'lc',
  DMA: 'dm', UMI: 'um', MSR: 'ms', ATG: 'ag', KNA: 'kn', VIR: 'vi', BLM: 'bl', PRI: 'pr',
  AIA: 'ai', VGB: 'vg', JAM: 'jm', CYM: 'ky', BMU: 'bm', HMD: 'hm', SHN: 'sh', MUS: 'mu',
  COM: 'km', STP: 'st', CPV: 'cv', MLT: 'mt', JEY: 'je', GGY: 'gg', IMN: 'im', ALD: 'ax',
  FRO: 'fo', IOT: 'io', SGP: 'sg', NFK: 'nf', COK: 'ck', TON: 'to', WLF: 'wf', WSM: 'ws',
  SLB: 'sb', TUV: 'tv', MDV: 'mv', NRU: 'nr', FSM: 'fm', SGS: 'gs', FLK: 'fk', VUT: 'vu',
  NIU: 'nu', ASM: 'as', PLW: 'pw', GUM: 'gu', MNP: 'mp', BHR: 'bh', MAC: 'mo',
});

const CURRENT_COUNTRY_FLAG_LEGACY_4X3_CODES = new Set(['cd', 'sm', 'ga', 'pg']);

export function currentCountryFlagCode(countryId) {
  return CURRENT_COUNTRY_FLAG_CODES[text(countryId).toUpperCase()] || '';
}

export function currentCountryFlagUrl(countryId, { assetRevision = '' } = {}) {
  const code = currentCountryFlagCode(countryId);
  const special = { CYN: 'cyn', SOL: 'sol' }[text(countryId).toUpperCase()];
  if (special) {
    const url = new URL(`../../vendor/political-flags/${special}.svg`, import.meta.url);
    if (assetRevision) url.searchParams.set('v', text(assetRevision));
    return url.href;
  }
  if (!code) return null;
  const assetPath = CURRENT_COUNTRY_FLAG_LEGACY_4X3_CODES.has(code)
    ? `../../vendor/flag-icons/7.5.0/flags/4x3/${code}.svg`
    : `../../vendor/country-flags/${COUNTRY_FLAG_NATIVE_REVISION}/svg/${code}.svg`;
  const url = new URL(assetPath, import.meta.url);
  if (assetRevision) url.searchParams.set('v', text(assetRevision));
  return url.href;
}

function normalizedFlagUrl(value) {
  return typeof value === 'string' && value.trim() ? value : null;
}

export function effectiveCountryFlagUrl({
  countryId,
  override = {},
  assetRevision = '',
} = {}) {
  if (hasOwn(override, 'flagDataUrl')) return normalizedFlagUrl(override.flagDataUrl);
  return currentCountryFlagUrl(countryId, { assetRevision });
}

export function effectiveTerritorialFlagUrl(feature, { assetRevision = '' } = {}) {
  const metadata = feature?.properties?.metadata || {};
  if (hasOwn(metadata, 'flagDataUrl')) return normalizedFlagUrl(metadata.flagDataUrl);
  if (metadata.defaultFlagDataUrl) return normalizedFlagUrl(metadata.defaultFlagDataUrl);
  const converted = metadata.convertedFromCountry;
  if (converted) return effectiveCountryFlagUrl({
    countryId: converted.countryId,
    override: converted.override || {},
    assetRevision,
  });
  const sourceCountryId = metadata.builtinSubunit?.sourceCountryId;
  return sourceCountryId ? currentCountryFlagUrl(sourceCountryId, { assetRevision }) : null;
}
