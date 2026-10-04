const RETRY_PRECISIONS = [9, 8, 7, 6];
const retryableClippingError = error => /SweepLine tree|Unable to find segment/i.test(String(error?.message || error || ''));

export function quantizePolygonCoordinates(value, precision) {
  const factor = 10 ** precision;
  const visit = item => {
    if (Array.isArray(item) && item.length >= 2
      && Number.isFinite(Number(item[0])) && Number.isFinite(Number(item[1]))) {
      return [
        Math.round(Number(item[0]) * factor) / factor,
        Math.round(Number(item[1]) * factor) / factor,
      ];
    }
    return Array.isArray(item) ? item.map(visit) : item;
  };
  return visit(value);
}

/** Runs one polygon-clipping method and retries only its known sweep failures. */
export function clippingOperationWithPrecisionRetry(clipper, method, ...inputs) {
  let originalError = null;
  for (const precision of [null, ...RETRY_PRECISIONS]) {
    try {
      const operationInputs = precision == null
        ? inputs
        : inputs.map(input => quantizePolygonCoordinates(input, precision));
      return clipper[method](...operationInputs);
    } catch (error) {
      if (!retryableClippingError(error)) throw error;
      originalError ||= error;
    }
  }
  throw originalError;
}
