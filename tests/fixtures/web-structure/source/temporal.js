const TEMPORAL_PATTERN = /^([+-]?)(\d{4,6})(?:-(\d{2})-(\d{2}))?$/;
const text = value => String(value ?? '').trim();

function temporalError(value, message) {
  const error = new Error(`날짜 ${text(value) || '(없음)'}이(가) 올바르지 않습니다. ${message}`);
  error.code = 'PL-TEMPORAL-001';
  return error;
}

function isLeapYear(year) {
  const absoluteYear = Math.abs(Number(year));
  return absoluteYear % 4 === 0 && (absoluteYear % 100 !== 0 || absoluteYear % 400 === 0);
}

function daysInMonth(year, month) {
  if (month === 2) return isLeapYear(year) ? 29 : 28;
  return [4, 6, 9, 11].includes(month) ? 30 : 31;
}

export function parseTemporal(value, { nullable = true } = {}) {
  const source = text(value);
  if (!source) {
    if (nullable) return null;
    throw temporalError(value, '값이 필요합니다.');
  }
  const match = TEMPORAL_PATTERN.exec(source);
  if (!match) throw temporalError(value, 'YYYY 또는 YYYY-MM-DD 형식을 사용하세요.');
  const sign = match[1];
  const digits = match[2];
  if (!sign && digits.length !== 4) throw temporalError(value, '확장 연도는 + 또는 - 부호가 필요합니다.');
  const magnitude = Number(digits);
  const year = sign === '-' ? -magnitude : magnitude;
  if (year === 0) throw temporalError(value, '연도 0은 사용할 수 없습니다. -0001 다음은 0001입니다.');
  const hasDate = match[3] != null;
  const month = hasDate ? Number(match[3]) : null;
  const day = hasDate ? Number(match[4]) : null;
  if (hasDate && (month < 1 || month > 12)) throw temporalError(value, '월은 01~12여야 합니다.');
  if (hasDate && (day < 1 || day > daysInMonth(year, month))) throw temporalError(value, '해당 월에 존재하지 않는 날짜입니다.');
  const canonicalYear = `${year < 0 ? '-' : sign === '+' ? '+' : ''}${digits}`;
  return Object.freeze({
    source,
    canonical: hasDate ? `${canonicalYear}-${match[3]}-${match[4]}` : canonicalYear,
    precision: hasDate ? 'date' : 'year',
    year,
    month,
    day,
    startKey: Object.freeze([year, hasDate ? month : 1, hasDate ? day : 1]),
    endKey: Object.freeze([year, hasDate ? month : 12, hasDate ? day : 31]),
  });
}

export function normalizeTemporal(value, options) {
  return parseTemporal(value, options)?.canonical ?? null;
}

function compareKeys(left, right) {
  for (let index = 0; index < 3; index += 1) {
    if (left[index] !== right[index]) return left[index] < right[index] ? -1 : 1;
  }
  return 0;
}

export function compareTemporal(left, right, { leftBoundary = 'start', rightBoundary = 'start' } = {}) {
  const leftValue = typeof left === 'object' && left?.startKey ? left : parseTemporal(left, { nullable: false });
  const rightValue = typeof right === 'object' && right?.startKey ? right : parseTemporal(right, { nullable: false });
  return compareKeys(leftValue[leftBoundary === 'end' ? 'endKey' : 'startKey'], rightValue[rightBoundary === 'end' ? 'endKey' : 'startKey']);
}

export function normalizeTemporalInterval(validFrom, validTo) {
  const start = parseTemporal(validFrom);
  const end = parseTemporal(validTo);
  if (start && end && compareKeys(start.startKey, end.endKey) > 0) {
    const error = new Error('유효기간 시작은 종료보다 늦을 수 없습니다.');
    error.code = 'PL-TEMPORAL-INTERVAL';
    throw error;
  }
  return Object.freeze({ validFrom: start?.canonical ?? null, validTo: end?.canonical ?? null, start, end });
}

export function temporalContains(interval, value) {
  const point = typeof value === 'object' && value?.startKey ? value : parseTemporal(value, { nullable: false });
  const start = interval?.start || parseTemporal(interval?.validFrom);
  const end = interval?.end || parseTemporal(interval?.validTo);
  return (!start || compareKeys(start.startKey, point.endKey) <= 0)
    && (!end || compareKeys(end.endKey, point.startKey) >= 0);
}

export function temporalIntervalsOverlap(left, right) {
  const leftStart = left?.start || parseTemporal(left?.validFrom);
  const leftEnd = left?.end || parseTemporal(left?.validTo);
  const rightStart = right?.start || parseTemporal(right?.validFrom);
  const rightEnd = right?.end || parseTemporal(right?.validTo);
  if (leftEnd && rightStart && compareKeys(leftEnd.endKey, rightStart.startKey) < 0) return false;
  if (rightEnd && leftStart && compareKeys(rightEnd.endKey, leftStart.startKey) < 0) return false;
  return true;
}
