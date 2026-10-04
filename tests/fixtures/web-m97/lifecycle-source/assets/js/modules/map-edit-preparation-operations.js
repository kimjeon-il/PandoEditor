import { createBoundaryPreparation } from './boundary-preparation.js';
import { createTerritoryComponentPlan } from './territory-component-plan.js';
import { normalizePolygonGeometry } from './map-edit-geometry.js';

export { createBoundaryPreparation };

// These are asynchronous service adapters, not pure calculations. Services own their local caches.
export async function prepareBoundaryOperation(operation, payload, features, service, checkpoint) {
  await service.sync(features, checkpoint);
  return operation === 'boundary-prepare' ? service.prepare(payload, checkpoint) : service.move(payload);
}

export async function prepareComponentOperation(operation, payload, clipper, checkpoint) {
  const plan = createTerritoryComponentPlan({ clipper, normalize: normalizePolygonGeometry, checkpoint });
  const method = operation === 'territory-components' ? 'prepare' : operation === 'territory-selection' ? 'selection' : 'slivers';
  return plan[method](payload);
}
