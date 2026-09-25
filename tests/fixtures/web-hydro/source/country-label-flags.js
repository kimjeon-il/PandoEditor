const overlaps = (a, b) => !(a.right + 3 < b.left || b.right + 3 < a.left || a.bottom + 3 < b.top || b.bottom + 3 < a.top);

export function countryLabelFlag(feature, { zoom, enabled, flagUrl, isVisible }) {
  if (!enabled || zoom < 1.8 || !isVisible(feature)) return null;
  const url = flagUrl(feature);
  return url ? { url, width: 18, height: 12, gap: 5 } : null;
}

// Decorate already placed labels: flags must never evict an existing name.
export function layoutCountryFlags(placed, options) {
  const flags = new Map();
  if (!options.enabled || options.zoom < 1.8) return flags;
  const boxes = placed.map(item => ({ ...item.box }));
  placed.forEach((item, index) => {
    if (item.sourceType !== 'country') return;
    const flag = countryLabelFlag(item.source, options);
    if (!flag) return;
    const box = item.nameVisible === false ? boxes[index]
      : { ...boxes[index], left: boxes[index].left - 12, right: boxes[index].right + 12 };
    if (boxes.some((other, otherIndex) => otherIndex !== index && overlaps(box, other))) return;
    boxes[index] = box;
    flags.set(String(item.source.id), flag);
  });
  return flags;
}
