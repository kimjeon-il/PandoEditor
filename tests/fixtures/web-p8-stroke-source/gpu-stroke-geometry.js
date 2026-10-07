const FLOATS_PER_INSTANCE = 10;
const FLOATS_PER_NODE = 8;
const CHAIN_EPSILON = 1e-9;

export const GPU_STROKE_FLAGS = Object.freeze({
  HAS_PREVIOUS: 1,
  HAS_NEXT: 2,
  CHAIN_START: 4,
  CHAIN_END: 8,
  CLOSED_CHAIN: 16,
});

const GPU_STROKE_NODE_KINDS = Object.freeze({
  JOIN: 1,
  START_CAP: 2,
  END_CAP: 3,
});

function pointEquals(left, right, epsilon = CHAIN_EPSILON) {
  return !!left && !!right
    && Math.abs(Number(left[0]) - Number(right[0])) <= epsilon
    && Math.abs(Number(left[1]) - Number(right[1])) <= epsilon;
}
function inputOwnerMap(ownerRanges) {
  const ownerByInput = new Map();
  if (!ownerRanges || typeof ownerRanges !== 'object') return ownerByInput;
  for (const [ownerId, range] of Object.entries(ownerRanges)) {
    const first = Math.max(0, Math.trunc(Number(range?.first) || 0));
    const count = Math.max(0, Math.trunc(Number(range?.count) || 0));
    for (let index = first; index < first + count; index += 1) ownerByInput.set(index, String(ownerId));
  }
  return ownerByInput;
}

function appendRange(output, ownerId, outputIndex) {
  if (!ownerId) return;
  const previous = output[ownerId];
  if (!previous) output[ownerId] = { first: outputIndex, count: 1 };
  else if (previous.first + previous.count === outputIndex) previous.count += 1;
  else {
    // Current public packets guarantee one contiguous range per owner. If a
    // malformed packet violates that contract, keep the earliest range rather
    // than accidentally drawing another owner's geometry.
  }
}

function freezeRanges(value) {
  return Object.freeze(Object.fromEntries(Object.entries(value).map(([key, range]) => [key, Object.freeze({ first: range.first, count: range.count })])));
}

function buildChains(segments) {
  const chains = [];
  let current = [];
  for (const segment of segments) {
    const previous = current[current.length - 1];
    if (previous && previous.ownerId === segment.ownerId && pointEquals(previous.end, segment.start)) {
      current.push(segment);
    } else {
      if (current.length) chains.push(current);
      current = [segment];
    }
  }
  if (current.length) chains.push(current);
  return chains;
}

export function buildGpuStrokeInstances(startsEnds, chainPhase = null, ownerRanges = null) {
  const source = startsEnds instanceof Float32Array ? startsEnds : new Float32Array(startsEnds || []);
  const ownerByInput = inputOwnerMap(ownerRanges);
  const valid = [];
  let invalidSegmentCount = 0;
  let accumulatedPhase = 0;
  let previousEnd = null;
  let previousOwner = null;

  for (let offset = 0; offset + 3 < source.length; offset += 4) {
    const inputIndex = offset / 4;
    const start = [Number(source[offset]), Number(source[offset + 1])];
    const end = [Number(source[offset + 2]), Number(source[offset + 3])];
    const ownerId = ownerByInput.get(inputIndex) || '';
    if (![...start, ...end].every(Number.isFinite) || pointEquals(start, end, 1e-12)) {
      invalidSegmentCount += 1;
      continue;
    }
    if (!previousEnd || !pointEquals(start, previousEnd) || ownerId !== previousOwner) accumulatedPhase = 0;
    const suppliedPhase = Number(chainPhase?.[inputIndex]);
    const phase = Number.isFinite(suppliedPhase) ? suppliedPhase : accumulatedPhase;
    valid.push({ inputIndex, ownerId, start, end, phase });
    accumulatedPhase = phase + Math.hypot(end[0] - start[0], end[1] - start[1]);
    previousEnd = end;
    previousOwner = ownerId;
  }

  const values = [];
  const nodes = [];
  const segmentRanges = {};
  const nodeRanges = {};
  let joinCount = 0;
  let capCount = 0;
  let closedChainCount = 0;

  const appendNode = (previous, point, next, phase, kind, ownerId) => {
    const outputIndex = nodes.length / FLOATS_PER_NODE;
    nodes.push(previous[0], previous[1], point[0], point[1], next[0], next[1], Number(phase || 0), Number(kind));
    appendRange(nodeRanges, ownerId, outputIndex);
    if (kind === GPU_STROKE_NODE_KINDS.JOIN) joinCount += 1;
    else capCount += 1;
  };

  for (const chain of buildChains(valid)) {
    const closed = chain.length > 1 && pointEquals(chain[0].start, chain[chain.length - 1].end);
    if (closed) closedChainCount += 1;
    for (let index = 0; index < chain.length; index += 1) {
      const segment = chain[index];
      const previousSegment = index > 0 ? chain[index - 1] : (closed ? chain[chain.length - 1] : null);
      const nextSegment = index + 1 < chain.length ? chain[index + 1] : (closed ? chain[0] : null);
      let flags = 0;
      if (previousSegment) flags |= GPU_STROKE_FLAGS.HAS_PREVIOUS;
      if (nextSegment) flags |= GPU_STROKE_FLAGS.HAS_NEXT;
      if (!previousSegment) flags |= GPU_STROKE_FLAGS.CHAIN_START;
      if (!nextSegment) flags |= GPU_STROKE_FLAGS.CHAIN_END;
      if (closed) flags |= GPU_STROKE_FLAGS.CLOSED_CHAIN;
      const previousPoint = previousSegment ? previousSegment.start : segment.start;
      const nextPoint = nextSegment ? nextSegment.end : segment.end;
      const outputIndex = values.length / FLOATS_PER_INSTANCE;
      values.push(
        previousPoint[0], previousPoint[1],
        segment.start[0], segment.start[1], segment.end[0], segment.end[1],
        nextPoint[0], nextPoint[1], segment.phase, flags,
      );
      appendRange(segmentRanges, segment.ownerId, outputIndex);
    }

    if (closed) {
      for (let index = 0; index < chain.length; index += 1) {
        const current = chain[index];
        const previous = chain[(index - 1 + chain.length) % chain.length];
        appendNode(previous.start, current.start, current.end, current.phase, GPU_STROKE_NODE_KINDS.JOIN, current.ownerId);
      }
    } else {
      const first = chain[0];
      appendNode(first.start, first.start, first.end, first.phase, GPU_STROKE_NODE_KINDS.START_CAP, first.ownerId);
      for (let index = 1; index < chain.length; index += 1) {
        const previous = chain[index - 1];
        const current = chain[index];
        appendNode(previous.start, current.start, current.end, current.phase, GPU_STROKE_NODE_KINDS.JOIN, current.ownerId);
      }
      const last = chain[chain.length - 1];
      appendNode(last.start, last.end, last.end, last.phase, GPU_STROKE_NODE_KINDS.END_CAP, last.ownerId);
    }
  }

  return Object.freeze({
    instances: new Float32Array(values),
    nodes: new Float32Array(nodes),
    segmentCount: values.length / FLOATS_PER_INSTANCE,
    nodeCount: nodes.length / FLOATS_PER_NODE,
    joinCount,
    capCount,
    closedChainCount,
    invalidSegmentCount,
    ownerRanges: freezeRanges(segmentRanges),
    ownerNodeRanges: freezeRanges(nodeRanges),
  });
}
