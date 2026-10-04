const text = value => String(value ?? '');

export function isPlaceholderOption(option) {
  return option?.placeholder === true
    || option?.dataset?.placeholder === 'true'
    || option?.getAttribute?.('data-placeholder') === 'true';
}

export function selectableOptions(options = []) {
  return [...options].filter(option => !isPlaceholderOption(option)
    && option?.disabled !== true
    && option?.hidden !== true);
}

export function resolveSelectChoice(options = [], selectedValue = '', { autoSelectSingle = false, preserveInvalid = false } = {}) {
  const values = [...options];
  const candidates = selectableOptions(values);
  const requestedValue = text(selectedValue);
  const selected = values.find(option => !isPlaceholderOption(option)
    && option?.disabled !== true
    && option?.hidden !== true
    && text(option?.value) === requestedValue);
  const invalid = requestedValue !== '' && !selected;
  const placeholder = values.find(isPlaceholderOption);
  let value = selected ? requestedValue : text(placeholder?.value);
  if (autoSelectSingle && candidates.length === 1 && !(preserveInvalid && invalid)) value = text(candidates[0]?.value);
  return Object.freeze({
    candidateCount: candidates.length,
    invalid,
    single: candidates.length === 1 && !(preserveInvalid && invalid),
    value,
  });
}

export function markPlaceholderOption(option) {
  if (!option) return option;
  option.disabled = true;
  option.hidden = true;
  option.dataset.placeholder = 'true';
  return option;
}
