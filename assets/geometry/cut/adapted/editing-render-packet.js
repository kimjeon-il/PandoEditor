const immutableGeometries = new WeakSet();

/** Only call for privately owned calculation results, never live project geometry. */
export function freezeEditingGeometry(value) {
  if (!value || immutableGeometries.has(value)) return value;
  const freezeCoordinates = coordinates => {
    if (!Array.isArray(coordinates) || Object.isFrozen(coordinates)) return;
    for (const child of coordinates) freezeCoordinates(child);
    Object.freeze(coordinates);
  };
  freezeCoordinates(value.coordinates);
  Object.freeze(value);
  immutableGeometries.add(value);
  return value;
}
