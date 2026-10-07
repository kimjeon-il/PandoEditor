import { buildReferenceImageCalibrationWarp } from './reference-image-georef.js';
import {
  buildReferenceImagePlacementWarp,
  referenceImagePlacementCoordinateAtUv,
} from './reference-image-transform.js';

export function buildReferenceImageSourceMapping(record = {}) {
  const calibration = buildReferenceImageCalibrationWarp({
    controlPoints: record.controlPoints,
    anchor: record.anchor,
    cornerPinEnabled: record.cornerPinEnabled,
    mapQuad: record.mapQuad,
    mode: record.warpMode,
  });
  if (calibration.ok) return calibration;

  if (!record.cornerPinEnabled) return calibration;

  const placement = buildReferenceImagePlacementWarp(record);
  if (!placement.ok) return calibration;

  return Object.freeze({
    ...placement,
    source: 'corner-pin',
    project(imageUv) {
      return referenceImagePlacementCoordinateAtUv(record, imageUv);
    },
  });
}
