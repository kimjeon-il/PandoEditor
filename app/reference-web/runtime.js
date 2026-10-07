var ReferenceWeb = (() => {
  var __defProp = Object.defineProperty;
  var __defProps = Object.defineProperties;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropDescs = Object.getOwnPropertyDescriptors;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __getOwnPropSymbols = Object.getOwnPropertySymbols;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __propIsEnum = Object.prototype.propertyIsEnumerable;
  var __defNormalProp = (obj, key, value) => key in obj ? __defProp(obj, key, { enumerable: true, configurable: true, writable: true, value }) : obj[key] = value;
  var __spreadValues = (a, b) => {
    for (var prop in b || (b = {}))
      if (__hasOwnProp.call(b, prop))
        __defNormalProp(a, prop, b[prop]);
    if (__getOwnPropSymbols)
      for (var prop of __getOwnPropSymbols(b)) {
        if (__propIsEnum.call(b, prop))
          __defNormalProp(a, prop, b[prop]);
      }
    return a;
  };
  var __spreadProps = (a, b) => __defProps(a, __getOwnPropDescs(b));
  var __export = (target, all) => {
    for (var name in all)
      __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = (to, from, except, desc) => {
    if (from && typeof from === "object" || typeof from === "function") {
      for (let key of __getOwnPropNames(from))
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
    }
    return to;
  };
  var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

  // app/reference-web/adapter.js
  var adapter_exports = {};
  __export(adapter_exports, {
    anchor: () => anchor,
    calibration: () => calibration,
    cornerQuad: () => cornerQuad,
    refine: () => refine,
    trace: () => trace
  });

  // app/reference-web/reference-image-georef.js
  var REFERENCE_IMAGE_GEOREF_SCHEMA_VERSION = 2;
  var REFERENCE_IMAGE_WARP_MODES = Object.freeze({
    AUTO: "auto",
    SIMILARITY: "similarity",
    AFFINE: "affine",
    PROJECTIVE: "projective",
    TPS: "tps"
  });
  var MODE_MIN_POINTS = Object.freeze({
    [REFERENCE_IMAGE_WARP_MODES.SIMILARITY]: 2,
    [REFERENCE_IMAGE_WARP_MODES.AFFINE]: 3,
    [REFERENCE_IMAGE_WARP_MODES.PROJECTIVE]: 4,
    [REFERENCE_IMAGE_WARP_MODES.TPS]: 3
  });
  var EPSILON = 1e-10;
  var EARTH_RADIUS_METERS = 63710088e-1;
  var finiteNumber = (value) => {
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  };
  function finitePair(value) {
    if (!Array.isArray(value) || value.length < 2) return null;
    const x = finiteNumber(value[0]);
    const y = finiteNumber(value[1]);
    return x === null || y === null ? null : [x, y];
  }
  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
  }
  function wrapLongitude(value) {
    if (!Number.isFinite(value)) return value;
    let result = value;
    while (result > 180) result -= 360;
    while (result < -180) result += 360;
    return result;
  }
  function unwrapLongitude(value, reference) {
    if (!Number.isFinite(value) || !Number.isFinite(reference)) return value;
    let result = value;
    while (result - reference > 180) result -= 360;
    while (result - reference < -180) result += 360;
    return result;
  }
  function normalizeReferenceControlPoints(values = []) {
    const result = [];
    const ids = /* @__PURE__ */ new Set();
    for (let index = 0; index < values.length; index += 1) {
      const value = values[index];
      const image = finitePair((value == null ? void 0 : value.image) || (value == null ? void 0 : value.uv) || (value == null ? void 0 : value.source));
      const coordinate = finitePair((value == null ? void 0 : value.coordinate) || (value == null ? void 0 : value.geo) || (value == null ? void 0 : value.target));
      if (!image || !coordinate) continue;
      const id = String((value == null ? void 0 : value.id) || `gcp-${index + 1}`).trim() || `gcp-${index + 1}`;
      if (ids.has(id)) continue;
      ids.add(id);
      result.push(Object.freeze({
        id,
        image: Object.freeze([image[0], image[1]]),
        coordinate: Object.freeze([wrapLongitude(coordinate[0]), clamp(coordinate[1], -90, 90)]),
        pinned: (value == null ? void 0 : value.pinned) === true
      }));
    }
    return Object.freeze(result);
  }
  function unwrappedControlPoints(points) {
    if (!points.length) return points;
    const reference = points[0].coordinate[0];
    return points.map((point) => __spreadProps(__spreadValues({}, point), {
      coordinate: [unwrapLongitude(point.coordinate[0], reference), point.coordinate[1]]
    }));
  }
  function solveLinearSystem(matrix, values) {
    const size = matrix.length;
    if (!size || values.length !== size || matrix.some((row) => row.length !== size)) return null;
    const augmented = matrix.map((row, index) => [...row.map(Number), Number(values[index])]);
    for (let column = 0; column < size; column += 1) {
      let pivot = column;
      let pivotValue = Math.abs(augmented[pivot][column]);
      for (let row = column + 1; row < size; row += 1) {
        const candidate = Math.abs(augmented[row][column]);
        if (candidate > pivotValue) {
          pivot = row;
          pivotValue = candidate;
        }
      }
      if (!Number.isFinite(pivotValue) || pivotValue < EPSILON) return null;
      if (pivot !== column) [augmented[pivot], augmented[column]] = [augmented[column], augmented[pivot]];
      const divisor = augmented[column][column];
      for (let cell = column; cell <= size; cell += 1) augmented[column][cell] /= divisor;
      for (let row = 0; row < size; row += 1) {
        if (row === column) continue;
        const factor = augmented[row][column];
        if (Math.abs(factor) < EPSILON) continue;
        for (let cell = column; cell <= size; cell += 1) {
          augmented[row][cell] -= factor * augmented[column][cell];
        }
      }
    }
    const solution = augmented.map((row) => row[size]);
    return solution.every(Number.isFinite) ? solution : null;
  }
  function solveLeastSquares(rows, values, regularization = 0) {
    var _a;
    if (!rows.length || rows.length !== values.length) return null;
    const width = ((_a = rows[0]) == null ? void 0 : _a.length) || 0;
    if (!width || rows.some((row) => row.length !== width)) return null;
    const normal = Array.from({ length: width }, () => Array(width).fill(0));
    const target = Array(width).fill(0);
    for (let rowIndex = 0; rowIndex < rows.length; rowIndex += 1) {
      const row = rows[rowIndex];
      const value = Number(values[rowIndex]);
      for (let i = 0; i < width; i += 1) {
        target[i] += row[i] * value;
        for (let j = 0; j < width; j += 1) normal[i][j] += row[i] * row[j];
      }
    }
    for (let index = 0; index < width; index += 1) normal[index][index] += regularization;
    return solveLinearSystem(normal, target);
  }
  function solveConstrainedLeastSquares(rows, values, constraintRows = [], constraintValues = [], regularization = 0) {
    var _a, _b;
    if (rows.length !== values.length || constraintRows.length !== constraintValues.length) return null;
    const width = ((_a = rows[0]) == null ? void 0 : _a.length) || ((_b = constraintRows[0]) == null ? void 0 : _b.length) || 0;
    if (!width) return null;
    if (rows.some((row) => row.length !== width) || constraintRows.some((row) => row.length !== width)) return null;
    if (!constraintRows.length) return solveLeastSquares(rows, values, regularization);
    const normal = Array.from({ length: width }, () => Array(width).fill(0));
    const target = Array(width).fill(0);
    for (let rowIndex = 0; rowIndex < rows.length; rowIndex += 1) {
      const row = rows[rowIndex];
      const value = Number(values[rowIndex]);
      if (!Number.isFinite(value)) return null;
      for (let i = 0; i < width; i += 1) {
        target[i] += row[i] * value;
        for (let j = 0; j < width; j += 1) normal[i][j] += row[i] * row[j];
      }
    }
    for (let index = 0; index < width; index += 1) normal[index][index] += regularization;
    const constraintCount = constraintRows.length;
    const size = width + constraintCount;
    const system = Array.from({ length: size }, () => Array(size).fill(0));
    const rhs = Array(size).fill(0);
    for (let row = 0; row < width; row += 1) {
      rhs[row] = target[row];
      for (let column = 0; column < width; column += 1) system[row][column] = normal[row][column];
    }
    for (let constraint = 0; constraint < constraintCount; constraint += 1) {
      const constraintRow = constraintRows[constraint];
      const value = Number(constraintValues[constraint]);
      if (!Number.isFinite(value)) return null;
      rhs[width + constraint] = value;
      for (let column = 0; column < width; column += 1) {
        system[column][width + constraint] = constraintRow[column];
        system[width + constraint][column] = constraintRow[column];
      }
    }
    const solution = solveLinearSystem(system, rhs);
    return solution ? solution.slice(0, width) : null;
  }
  function fitSimilarity(points) {
    const rows = [];
    const values = [];
    const constraintRows = [];
    const constraintValues = [];
    for (const point of points) {
      const [u, v] = point.image;
      const [lon, lat] = point.coordinate;
      const targetRows = point.pinned ? constraintRows : rows;
      const targetValues = point.pinned ? constraintValues : values;
      targetRows.push([u, -v, 1, 0]);
      targetValues.push(lon);
      targetRows.push([v, u, 0, 1]);
      targetValues.push(lat);
    }
    const coefficients = solveConstrainedLeastSquares(rows, values, constraintRows, constraintValues);
    if (!coefficients) return null;
    const [a, b, tx, ty] = coefficients;
    return ([u, v]) => [a * u - b * v + tx, b * u + a * v + ty];
  }
  function fitAffine(points) {
    const rows = [];
    const lonValues = [];
    const latValues = [];
    const constraintRows = [];
    const constraintLonValues = [];
    const constraintLatValues = [];
    for (const point of points) {
      const row = [point.image[0], point.image[1], 1];
      if (point.pinned) {
        constraintRows.push(row);
        constraintLonValues.push(point.coordinate[0]);
        constraintLatValues.push(point.coordinate[1]);
      } else {
        rows.push(row);
        lonValues.push(point.coordinate[0]);
        latValues.push(point.coordinate[1]);
      }
    }
    const lonCoefficients = solveConstrainedLeastSquares(
      rows,
      lonValues,
      constraintRows,
      constraintLonValues
    );
    const latCoefficients = solveConstrainedLeastSquares(
      rows,
      latValues,
      constraintRows,
      constraintLatValues
    );
    if (!lonCoefficients || !latCoefficients) return null;
    return ([u, v]) => [
      lonCoefficients[0] * u + lonCoefficients[1] * v + lonCoefficients[2],
      latCoefficients[0] * u + latCoefficients[1] * v + latCoefficients[2]
    ];
  }
  function fitProjective(points) {
    const rows = [];
    const values = [];
    const constraintRows = [];
    const constraintValues = [];
    for (const point of points) {
      const [u, v] = point.image;
      const [lon, lat] = point.coordinate;
      const targetRows = point.pinned ? constraintRows : rows;
      const targetValues = point.pinned ? constraintValues : values;
      targetRows.push([u, v, 1, 0, 0, 0, -lon * u, -lon * v]);
      targetValues.push(lon);
      targetRows.push([0, 0, 0, u, v, 1, -lat * u, -lat * v]);
      targetValues.push(lat);
    }
    const h = solveConstrainedLeastSquares(rows, values, constraintRows, constraintValues, 1e-14);
    if (!h) return null;
    return ([u, v]) => {
      const denominator = h[6] * u + h[7] * v + 1;
      if (!Number.isFinite(denominator) || Math.abs(denominator) < EPSILON) return null;
      return [
        (h[0] * u + h[1] * v + h[2]) / denominator,
        (h[3] * u + h[4] * v + h[5]) / denominator
      ];
    };
  }
  function tpsKernel(distanceSquared) {
    if (!Number.isFinite(distanceSquared) || distanceSquared <= EPSILON) return 0;
    return distanceSquared * Math.log(distanceSquared);
  }
  function fitThinPlateSpline(points) {
    const count = points.length;
    const width = count + 3;
    const basisRow = (image) => {
      const [u, v] = image;
      const row = [];
      for (const point of points) {
        const du = u - point.image[0];
        const dv = v - point.image[1];
        row.push(tpsKernel(du * du + dv * dv));
      }
      row.push(1, u, v);
      return row;
    };
    const rows = [];
    const lonValues = [];
    const latValues = [];
    const constraintRows = [];
    const constraintLonValues = [];
    const constraintLatValues = [];
    for (const point of points) {
      const row = basisRow(point.image);
      if (point.pinned) {
        constraintRows.push(row);
        constraintLonValues.push(point.coordinate[0]);
        constraintLatValues.push(point.coordinate[1]);
      } else {
        rows.push(row);
        lonValues.push(point.coordinate[0]);
        latValues.push(point.coordinate[1]);
      }
    }
    const sideConditions = [
      [...Array(count).fill(1), 0, 0, 0],
      [...points.map((point) => point.image[0]), 0, 0, 0],
      [...points.map((point) => point.image[1]), 0, 0, 0]
    ];
    for (const row of sideConditions) {
      constraintRows.push(row);
      constraintLonValues.push(0);
      constraintLatValues.push(0);
    }
    const regularization = points.some((point) => point.pinned) ? 1e-8 : 1e-12;
    const lonCoefficients = solveConstrainedLeastSquares(
      rows,
      lonValues,
      constraintRows,
      constraintLonValues,
      regularization
    );
    const latCoefficients = solveConstrainedLeastSquares(
      rows,
      latValues,
      constraintRows,
      constraintLatValues,
      regularization
    );
    if (!lonCoefficients || !latCoefficients || lonCoefficients.length !== width || latCoefficients.length !== width) return null;
    return (image) => {
      const basis = basisRow(image);
      let lon = 0;
      let lat = 0;
      for (let index = 0; index < width; index += 1) {
        lon += lonCoefficients[index] * basis[index];
        lat += latCoefficients[index] * basis[index];
      }
      return [lon, lat];
    };
  }
  function haversineMeters(a, b) {
    if (!a || !b) return Number.POSITIVE_INFINITY;
    const toRadians = Math.PI / 180;
    const lat1 = a[1] * toRadians;
    const lat2 = b[1] * toRadians;
    const deltaLat = (b[1] - a[1]) * toRadians;
    const deltaLon = (unwrapLongitude(b[0], a[0]) - a[0]) * toRadians;
    const sinLat = Math.sin(deltaLat / 2);
    const sinLon = Math.sin(deltaLon / 2);
    const h = sinLat * sinLat + Math.cos(lat1) * Math.cos(lat2) * sinLon * sinLon;
    return 2 * EARTH_RADIUS_METERS * Math.asin(Math.min(1, Math.sqrt(Math.max(0, h))));
  }
  function imageCoverage(points) {
    if (!points.length) return 0;
    const xs = points.map((point) => point.image[0]);
    const ys = points.map((point) => point.image[1]);
    const width = Math.max(...xs) - Math.min(...xs);
    const height = Math.max(...ys) - Math.min(...ys);
    return Math.max(0, width * height);
  }
  function resolveMode(mode, pointCount) {
    const normalized = Object.values(REFERENCE_IMAGE_WARP_MODES).includes(mode) ? mode : REFERENCE_IMAGE_WARP_MODES.AUTO;
    if (normalized !== REFERENCE_IMAGE_WARP_MODES.AUTO) return normalized;
    if (pointCount >= 6) return REFERENCE_IMAGE_WARP_MODES.TPS;
    if (pointCount >= 4) return REFERENCE_IMAGE_WARP_MODES.PROJECTIVE;
    if (pointCount >= 3) return REFERENCE_IMAGE_WARP_MODES.AFFINE;
    return REFERENCE_IMAGE_WARP_MODES.SIMILARITY;
  }
  function diagnosticsFor(points, project, mode) {
    const residuals = points.map((point) => {
      const projected = project(point.image);
      return Object.freeze({
        id: point.id,
        pinned: point.pinned === true,
        meters: haversineMeters(point.coordinate, projected)
      });
    });
    const softResiduals = residuals.filter((item) => !item.pinned && Number.isFinite(item.meters));
    const hardResiduals = residuals.filter((item) => item.pinned && Number.isFinite(item.meters));
    const rmsSource = softResiduals.length ? softResiduals : hardResiduals;
    const rmsMeters = rmsSource.length ? Math.sqrt(rmsSource.reduce((sum, item) => sum + item.meters * item.meters, 0) / rmsSource.length) : Number.POSITIVE_INFINITY;
    const maxMeters = softResiduals.length ? Math.max(...softResiduals.map((item) => item.meters)) : hardResiduals.length ? Math.max(...hardResiduals.map((item) => item.meters)) : Number.POSITIVE_INFINITY;
    const hardMaxMeters = hardResiduals.length ? Math.max(...hardResiduals.map((item) => item.meters)) : 0;
    const coverage = imageCoverage(points);
    const warnings = [];
    if (coverage < 0.08) warnings.push("control-points-concentrated");
    if (mode === REFERENCE_IMAGE_WARP_MODES.TPS && points.length < 5) warnings.push("tps-underconstrained");
    if (Number.isFinite(rmsMeters) && rmsMeters > 5e4) warnings.push("high-residual");
    if (hardMaxMeters > 0.01) warnings.push("hard-constraint-residual");
    return Object.freeze({
      rmsMeters,
      maxMeters,
      hardMaxMeters,
      hardPointCount: hardResiduals.length,
      softPointCount: softResiduals.length,
      imageCoverage: coverage,
      residuals: Object.freeze(residuals),
      warnings: Object.freeze(warnings)
    });
  }
  function buildReferenceImageWarp(values = [], { mode = REFERENCE_IMAGE_WARP_MODES.AUTO } = {}) {
    const normalizedPoints = normalizeReferenceControlPoints(values);
    const resolvedMode = resolveMode(mode, normalizedPoints.length);
    const minimum = MODE_MIN_POINTS[resolvedMode] || 2;
    if (normalizedPoints.length < minimum) {
      return Object.freeze({
        ok: false,
        mode: resolvedMode,
        minimumPoints: minimum,
        pointCount: normalizedPoints.length,
        reason: "insufficient-control-points"
      });
    }
    const points = unwrappedControlPoints(normalizedPoints);
    const fitter = {
      [REFERENCE_IMAGE_WARP_MODES.SIMILARITY]: fitSimilarity,
      [REFERENCE_IMAGE_WARP_MODES.AFFINE]: fitAffine,
      [REFERENCE_IMAGE_WARP_MODES.PROJECTIVE]: fitProjective,
      [REFERENCE_IMAGE_WARP_MODES.TPS]: fitThinPlateSpline
    }[resolvedMode];
    const rawProject = fitter == null ? void 0 : fitter(points);
    if (!rawProject) {
      return Object.freeze({
        ok: false,
        mode: resolvedMode,
        minimumPoints: minimum,
        pointCount: normalizedPoints.length,
        reason: "singular-control-points"
      });
    }
    const project = (imagePoint) => {
      const pair = finitePair(imagePoint);
      if (!pair) return null;
      const coordinate = rawProject(pair);
      if (!coordinate || !coordinate.every(Number.isFinite)) return null;
      return Object.freeze([wrapLongitude(coordinate[0]), clamp(coordinate[1], -90, 90)]);
    };
    const diagnostics = diagnosticsFor(normalizedPoints, project, resolvedMode);
    return Object.freeze({
      ok: true,
      schemaVersion: REFERENCE_IMAGE_GEOREF_SCHEMA_VERSION,
      mode: resolvedMode,
      minimumPoints: minimum,
      pointCount: normalizedPoints.length,
      controlPoints: normalizedPoints,
      diagnostics,
      project
    });
  }
  function buildReferenceImageCalibrationWarp({
    controlPoints = [],
    anchor: anchor2 = null,
    cornerPinEnabled = false,
    mapQuad = null,
    mode = REFERENCE_IMAGE_WARP_MODES.AUTO
  } = {}) {
    const calibrationPoints = Array.isArray(controlPoints) ? controlPoints.map((point) => ({
      id: point.id,
      image: Array.isArray(point.image) ? [...point.image] : point.image,
      coordinate: Array.isArray(point.coordinate) ? [...point.coordinate] : point.coordinate,
      pinned: false
    })) : [];
    if (anchor2) {
      calibrationPoints.unshift({
        id: "anchor",
        image: Array.isArray(anchor2.image) ? [...anchor2.image] : anchor2.image,
        coordinate: Array.isArray(anchor2.coordinate) ? [...anchor2.coordinate] : anchor2.coordinate,
        pinned: true
      });
    }
    if (!cornerPinEnabled || !calibrationPoints.length) {
      return buildReferenceImageWarp(calibrationPoints, { mode });
    }
    if (!Array.isArray(mapQuad) || mapQuad.length !== 4) {
      return Object.freeze({
        ok: false,
        mode: REFERENCE_IMAGE_WARP_MODES.TPS,
        minimumPoints: MODE_MIN_POINTS[REFERENCE_IMAGE_WARP_MODES.TPS],
        pointCount: calibrationPoints.length,
        reason: "invalid-corner-pin-quad"
      });
    }
    const imageCorners = [[0, 0], [1, 0], [1, 1], [0, 1]];
    const cornerPoints = mapQuad.map((coordinate, index) => ({
      id: `corner-pin-${index}`,
      image: imageCorners[index],
      coordinate: Array.isArray(coordinate) ? [...coordinate] : coordinate,
      pinned: true
    }));
    return buildReferenceImageWarp(
      [...cornerPoints, ...calibrationPoints],
      { mode: REFERENCE_IMAGE_WARP_MODES.TPS }
    );
  }
  function buildReferenceImageProjectiveWarpFromQuad(mapQuad) {
    if (!Array.isArray(mapQuad) || mapQuad.length !== 4) {
      return Object.freeze({
        ok: false,
        mode: REFERENCE_IMAGE_WARP_MODES.PROJECTIVE,
        minimumPoints: 4,
        pointCount: 0,
        reason: "invalid-map-quad"
      });
    }
    const imageCorners = [[0, 0], [1, 0], [1, 1], [0, 1]];
    const values = mapQuad.map((coordinate, index) => ({
      id: `corner-${index}`,
      image: imageCorners[index],
      coordinate
    }));
    return buildReferenceImageWarp(values, { mode: REFERENCE_IMAGE_WARP_MODES.PROJECTIVE });
  }
  function buildReferenceImageMesh(warp, { columns = 24, rows = 16 } = {}) {
    if (!(warp == null ? void 0 : warp.ok) || typeof warp.project !== "function") return null;
    const columnCount = Math.max(1, Math.min(128, Math.round(Number(columns) || 24)));
    const rowCount = Math.max(1, Math.min(128, Math.round(Number(rows) || 16)));
    const vertices = [];
    for (let row = 0; row <= rowCount; row += 1) {
      const v = row / rowCount;
      for (let column = 0; column <= columnCount; column += 1) {
        const u = column / columnCount;
        const coordinate = warp.project([u, v]);
        vertices.push(Object.freeze({ uv: Object.freeze([u, v]), coordinate }));
      }
    }
    const triangles = [];
    const stride = columnCount + 1;
    for (let row = 0; row < rowCount; row += 1) {
      for (let column = 0; column < columnCount; column += 1) {
        const a = row * stride + column;
        const b = a + 1;
        const c = a + stride;
        const d = c + 1;
        triangles.push(Object.freeze([a, b, d]), Object.freeze([a, d, c]));
      }
    }
    return Object.freeze({
      columns: columnCount,
      rows: rowCount,
      vertices: Object.freeze(vertices),
      triangles: Object.freeze(triangles)
    });
  }

  // app/reference-web/reference-image-transform.js
  var REFERENCE_IMAGE_TRANSFORM = Object.freeze({
    handleRadius: 8,
    hitRadius: 12,
    rotateHandleOffset: 30,
    minimumWidth: 48,
    minimumHeight: 36
  });
  var HANDLE_ORDER = Object.freeze(["nw", "n", "ne", "e", "se", "s", "sw", "w"]);
  function finitePair2(value) {
    if (!Array.isArray(value) || value.length < 2) return null;
    const x = Number(value[0]);
    const y = Number(value[1]);
    return Number.isFinite(x) && Number.isFinite(y) ? [x, y] : null;
  }
  function normalizeCoordinate(value) {
    const pair = finitePair2(value);
    if (!pair) return null;
    let lon = pair[0];
    while (lon > 180) lon -= 360;
    while (lon < -180) lon += 360;
    return [lon, Math.max(-90, Math.min(90, pair[1]))];
  }
  function angularDistanceDegrees(a, b) {
    if (!a || !b) return Number.POSITIVE_INFINITY;
    const factor = Math.PI / 180;
    const lonA = a[0] * factor;
    const latA = a[1] * factor;
    const lonB = b[0] * factor;
    const latB = b[1] * factor;
    const dot = Math.sin(latA) * Math.sin(latB) + Math.cos(latA) * Math.cos(latB) * Math.cos(lonA - lonB);
    return Math.acos(Math.max(-1, Math.min(1, dot))) / factor;
  }
  function projectVisible(host, coordinate) {
    var _a, _b, _c;
    const projected = finitePair2((_a = host == null ? void 0 : host.project) == null ? void 0 : _a.call(host, coordinate));
    if (!projected) return null;
    if (((_b = host.getProjectionKind) == null ? void 0 : _b.call(host)) !== "globe") return projected;
    const roundTrip = normalizeCoordinate((_c = host.unproject) == null ? void 0 : _c.call(host, projected));
    return angularDistanceDegrees(coordinate, roundTrip) <= 0.25 ? projected : null;
  }
  function projectReferenceImageMapQuad(record, host) {
    if (!(record == null ? void 0 : record.mapQuad) || record.mapQuad.length !== 4 || !(host == null ? void 0 : host.project)) return null;
    const corners = record.mapQuad.map((coordinate) => projectVisible(host, coordinate));
    return corners.every(Boolean) ? corners : null;
  }
  function buildReferenceImagePlacementWarp(record) {
    if (!(record == null ? void 0 : record.mapQuad) || record.mapQuad.length !== 4) {
      return buildReferenceImageProjectiveWarpFromQuad(null);
    }
    return buildReferenceImageProjectiveWarpFromQuad(record.mapQuad);
  }
  function referenceImagePlacementCoordinateAtUv(record, imageUv) {
    const pair = finitePair2(imageUv);
    if (!pair || pair.some((component) => component < 0 || component > 1)) return null;
    const warp = buildReferenceImagePlacementWarp(record);
    if (!warp.ok) return null;
    const u = record.flipX ? 1 - pair[0] : pair[0];
    const v = record.flipY ? 1 - pair[1] : pair[1];
    return warp.project([u, v]);
  }
  function alignReferenceImageAnchor(record) {
    if (!(record == null ? void 0 : record.anchor) || !record.mapQuad) return false;
    const current = referenceImagePlacementCoordinateAtUv(record, record.anchor.image);
    const target = normalizeCoordinate(record.anchor.coordinate);
    if (!current || !target) return false;
    const targetLon = unwrapLongitude2(target[0], current[0]);
    const deltaLon = targetLon - current[0];
    const deltaLat = target[1] - current[1];
    const original = record.mapQuad.map((coordinate) => [...coordinate]);
    const translated = [];
    for (const coordinate of original) {
      const lon = unwrapLongitude2(coordinate[0], current[0]) + deltaLon;
      const lat = coordinate[1] + deltaLat;
      if (!Number.isFinite(lon) || !Number.isFinite(lat) || lat < -90 || lat > 90) return false;
      translated.push(normalizeCoordinate([lon, lat]));
    }
    if (translated.some((coordinate) => !coordinate)) return false;
    record.mapQuad = translated;
    const aligned = referenceImagePlacementCoordinateAtUv(record, record.anchor.image);
    if (!aligned) {
      record.mapQuad = original;
      return false;
    }
    const lonError = Math.abs(unwrapLongitude2(aligned[0], target[0]) - target[0]);
    const latError = Math.abs(aligned[1] - target[1]);
    if (lonError > 1e-8 || latError > 1e-8) {
      record.mapQuad = original;
      return false;
    }
    return true;
  }
  function unwrapLongitude2(value, reference) {
    let result = value;
    while (result - reference > 180) result -= 360;
    while (result - reference < -180) result += 360;
    return result;
  }
  function signedArea(points) {
    let area = 0;
    for (let index = 0; index < points.length; index += 1) {
      const current = points[index];
      const next = points[(index + 1) % points.length];
      area += current[0] * next[1] - next[0] * current[1];
    }
    return area / 2;
  }
  function orientation(a, b, c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  }
  function properSegmentsIntersect(a, b, c, d) {
    const abC = orientation(a, b, c);
    const abD = orientation(a, b, d);
    const cdA = orientation(c, d, a);
    const cdB = orientation(c, d, b);
    return abC * abD < 0 && cdA * cdB < 0;
  }
  function strictlyConvexQuad(points) {
    if (!Array.isArray(points) || points.length !== 4) return false;
    const turns = [];
    for (let index = 0; index < 4; index += 1) {
      const a = points[index];
      const b = points[(index + 1) % 4];
      const c = points[(index + 2) % 4];
      const turn = orientation(a, b, c);
      if (Math.abs(turn) < 1e-6) return false;
      turns.push(Math.sign(turn));
    }
    return turns.every((sign) => sign === turns[0]);
  }
  function usableFreeTransformQuad(record, host) {
    const points = projectReferenceImageMapQuad(record, host);
    if (!points || Math.abs(signedArea(points)) < 64 || !strictlyConvexQuad(points)) return false;
    if (properSegmentsIntersect(points[0], points[1], points[2], points[3])) return false;
    if (properSegmentsIntersect(points[1], points[2], points[3], points[0])) return false;
    return buildReferenceImagePlacementWarp(record).ok;
  }
  function applyReferenceImageFreeTransformDrag(record, drag, point, host) {
    var _a;
    if (!record || !drag || !host) return false;
    const coordinate = normalizeCoordinate((_a = host.unproject) == null ? void 0 : _a.call(host, point));
    if (!coordinate) return false;
    const previous = record.mapQuad;
    const candidate = drag.startMapQuad.map((value) => [...value]);
    candidate[drag.index] = coordinate;
    record.mapQuad = candidate;
    if (!usableFreeTransformQuad(record, host)) {
      record.mapQuad = previous;
      return false;
    }
    return true;
  }

  // app/reference-web/reference-image-live-wire.js
  var DEFAULT_MAX_DIMENSION = 1024;
  var DEFAULT_SNAP_RADIUS = 8;
  var DEFAULT_SEARCH_RADIUS = 256;
  var DEFAULT_SIMPLIFY_TOLERANCE = 1.5;
  var MINIMUM_PEAK_EDGE = 0.02;
  var MINIMUM_MEAN_EDGE = 0.035;
  var SQRT2 = Math.SQRT2;
  var SOBEL_MAX = 4 * SQRT2 * 255;
  var clamp2 = (value, min, max) => Math.max(min, Math.min(max, value));
  var finitePoint = (point) => Array.isArray(point) && point.length >= 2 && Number.isFinite(Number(point[0])) && Number.isFinite(Number(point[1]));
  var clonePoint = (point) => [Number(point[0]), Number(point[1])];
  var squaredDistance = (left, right) => {
    const dx = Number(left[0]) - Number(right[0]);
    const dy = Number(left[1]) - Number(right[1]);
    return dx * dx + dy * dy;
  };
  function pointSegmentDistanceSquared(point, start, end) {
    const dx = end[0] - start[0];
    const dy = end[1] - start[1];
    const length2 = dx * dx + dy * dy;
    if (length2 <= 1e-12) return squaredDistance(point, start);
    const t = clamp2(((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / length2, 0, 1);
    const px = start[0] + t * dx;
    const py = start[1] + t * dy;
    return squaredDistance(point, [px, py]);
  }
  function grayscaleAt(data, offset) {
    var _a;
    const alpha = Number((_a = data[offset + 3]) != null ? _a : 255) / 255;
    const value = 0.2126 * Number(data[offset] || 0) + 0.7152 * Number(data[offset + 1] || 0) + 0.0722 * Number(data[offset + 2] || 0);
    return value * alpha + 255 * (1 - alpha);
  }
  function buildReferenceImageLiveWireField(imageData, {
    sourceWidth = imageData == null ? void 0 : imageData.width,
    sourceHeight = imageData == null ? void 0 : imageData.height
  } = {}) {
    const width = Math.max(1, Math.floor(Number(imageData == null ? void 0 : imageData.width) || 0));
    const height = Math.max(1, Math.floor(Number(imageData == null ? void 0 : imageData.height) || 0));
    const data = imageData == null ? void 0 : imageData.data;
    if (!data || data.length < width * height * 4) throw new TypeError("RGBA image data is required.");
    const naturalWidth = Math.max(1, Number(sourceWidth) || width);
    const naturalHeight = Math.max(1, Number(sourceHeight) || height);
    const gray = new Float32Array(width * height);
    for (let index = 0; index < width * height; index += 1) gray[index] = grayscaleAt(data, index * 4);
    const gradient = new Float32Array(width * height);
    const gradientX = new Float32Array(width * height);
    const gradientY = new Float32Array(width * height);
    let peak = 0;
    let sum = 0;
    if (width >= 3 && height >= 3) {
      for (let y = 1; y < height - 1; y += 1) {
        for (let x = 1; x < width - 1; x += 1) {
          const top = (y - 1) * width;
          const mid = y * width;
          const bottom = (y + 1) * width;
          const gx = -gray[top + x - 1] + gray[top + x + 1] - 2 * gray[mid + x - 1] + 2 * gray[mid + x + 1] - gray[bottom + x - 1] + gray[bottom + x + 1];
          const gy = -gray[top + x - 1] - 2 * gray[top + x] - gray[top + x + 1] + gray[bottom + x - 1] + 2 * gray[bottom + x] + gray[bottom + x + 1];
          const index = mid + x;
          const normalizedX = clamp2(gx / SOBEL_MAX, -1, 1);
          const normalizedY = clamp2(gy / SOBEL_MAX, -1, 1);
          const magnitude = clamp2(Math.hypot(gx, gy) / SOBEL_MAX, 0, 1);
          gradientX[index] = normalizedX;
          gradientY[index] = normalizedY;
          gradient[index] = magnitude;
          peak = Math.max(peak, magnitude);
          sum += magnitude;
        }
      }
    }
    const interiorCount = Math.max(1, Math.max(0, width - 2) * Math.max(0, height - 2));
    return Object.freeze({
      width,
      height,
      sourceWidth: naturalWidth,
      sourceHeight: naturalHeight,
      scaleX: (width - 1) / Math.max(1, naturalWidth - 1),
      scaleY: (height - 1) / Math.max(1, naturalHeight - 1),
      gradient,
      gradientX,
      gradientY,
      peakEdgeStrength: peak,
      meanEdgeStrength: sum / interiorCount
    });
  }
  function fieldGradient(field, x, y) {
    if (x < 0 || y < 0 || x >= field.width || y >= field.height) return 0;
    return Number(field.gradient[y * field.width + x] || 0);
  }
  function snapLiveWireAnchor(field, point, { radius = DEFAULT_SNAP_RADIUS } = {}) {
    if (!(field == null ? void 0 : field.gradient) || !finitePoint(point)) return null;
    const centerX = clamp2(Math.round(point[0]), 0, field.width - 1);
    const centerY = clamp2(Math.round(point[1]), 0, field.height - 1);
    const searchRadius = Math.max(0, Number(radius) || 0);
    const scan = Math.ceil(searchRadius);
    const radius2 = searchRadius * searchRadius;
    let best = [centerX, centerY];
    let bestScore = Number.NEGATIVE_INFINITY;
    let bestDistance2 = Number.POSITIVE_INFINITY;
    for (let y = Math.max(0, centerY - scan); y <= Math.min(field.height - 1, centerY + scan); y += 1) {
      for (let x = Math.max(0, centerX - scan); x <= Math.min(field.width - 1, centerX + scan); x += 1) {
        const distance2 = squaredDistance([x, y], point);
        if (distance2 > radius2 + 1e-9) continue;
        const edge = fieldGradient(field, x, y);
        const score = edge - 45e-4 * Math.sqrt(distance2);
        if (score < bestScore - 1e-9) continue;
        if (Math.abs(score - bestScore) <= 1e-9 && distance2 >= bestDistance2) continue;
        best = [x, y];
        bestScore = score;
        bestDistance2 = distance2;
      }
    }
    return best;
  }
  var MinHeap = class {
    constructor() {
      this.items = [];
    }
    push(value) {
      const items = this.items;
      items.push(value);
      let index = items.length - 1;
      while (index > 0) {
        const parent = Math.floor((index - 1) / 2);
        if (items[parent].priority <= value.priority) break;
        items[index] = items[parent];
        index = parent;
      }
      items[index] = value;
    }
    pop() {
      const items = this.items;
      if (!items.length) return null;
      const root = items[0];
      const tail = items.pop();
      if (!items.length) return root;
      let index = 0;
      while (true) {
        const left = index * 2 + 1;
        const right = left + 1;
        if (left >= items.length) break;
        let child = left;
        if (right < items.length && items[right].priority < items[left].priority) child = right;
        if (items[child].priority >= tail.priority) break;
        items[index] = items[child];
        index = child;
      }
      items[index] = tail;
      return root;
    }
    get size() {
      return this.items.length;
    }
  };
  var NEIGHBORS = Object.freeze([
    Object.freeze({ dx: -1, dy: -1, step: SQRT2, ux: -1 / SQRT2, uy: -1 / SQRT2 }),
    Object.freeze({ dx: 0, dy: -1, step: 1, ux: 0, uy: -1 }),
    Object.freeze({ dx: 1, dy: -1, step: SQRT2, ux: 1 / SQRT2, uy: -1 / SQRT2 }),
    Object.freeze({ dx: -1, dy: 0, step: 1, ux: -1, uy: 0 }),
    Object.freeze({ dx: 1, dy: 0, step: 1, ux: 1, uy: 0 }),
    Object.freeze({ dx: -1, dy: 1, step: SQRT2, ux: -1 / SQRT2, uy: 1 / SQRT2 }),
    Object.freeze({ dx: 0, dy: 1, step: 1, ux: 0, uy: 1 }),
    Object.freeze({ dx: 1, dy: 1, step: SQRT2, ux: 1 / SQRT2, uy: 1 / SQRT2 })
  ]);
  function directionCost(field, x, y, move) {
    var _a, _b;
    const index = y * field.width + x;
    const gx = Number(((_a = field.gradientX) == null ? void 0 : _a[index]) || 0);
    const gy = Number(((_b = field.gradientY) == null ? void 0 : _b[index]) || 0);
    const length = Math.hypot(gx, gy);
    if (length < 1e-5) return 1;
    const tangentX = -gy / length;
    const tangentY = gx / length;
    return 1 - Math.abs(move.ux * tangentX + move.uy * tangentY);
  }
  function turnCost(previousDirection, nextDirection) {
    if (previousDirection < 0 || previousDirection >= NEIGHBORS.length) return 0;
    const previous = NEIGHBORS[previousDirection];
    const next = NEIGHBORS[nextDirection];
    const dot = clamp2(previous.ux * next.ux + previous.uy * next.uy, -1, 1);
    return (1 - dot) * 0.5;
  }
  function invalidTree(reason) {
    return Object.freeze({ ok: false, reason, parents: null });
  }
  function buildLiveWireTree(field, anchor2, {
    target = null,
    searchRadius = DEFAULT_SEARCH_RADIUS,
    snapRadius = DEFAULT_SNAP_RADIUS,
    gradientWeight = 0.67,
    directionWeight = 0.23,
    turnWeight = 0.1,
    corridorWeight = 0.08,
    minimumPeakEdge = MINIMUM_PEAK_EDGE,
    maxExpansions = 65e4
  } = {}) {
    if (!(field == null ? void 0 : field.gradient) || !(field == null ? void 0 : field.gradientX) || !(field == null ? void 0 : field.gradientY) || !finitePoint(anchor2)) {
      return invalidTree("invalid-gradient-field");
    }
    if (Number(field.peakEdgeStrength || 0) < Number(minimumPeakEdge)) {
      return invalidTree("insufficient-edge-strength");
    }
    const start = snapLiveWireAnchor(field, anchor2, { radius: snapRadius });
    const goal = finitePoint(target) ? snapLiveWireAnchor(field, target, { radius: snapRadius }) : null;
    if (!start) return invalidTree("invalid-anchor");
    const radius = Math.max(24, Math.floor(Number(searchRadius) || DEFAULT_SEARCH_RADIUS));
    const endpoints = goal ? [start, goal] : [start, start];
    const minX = clamp2(Math.floor(Math.min(endpoints[0][0], endpoints[1][0]) - radius), 0, field.width - 1);
    const maxX = clamp2(Math.ceil(Math.max(endpoints[0][0], endpoints[1][0]) + radius), 0, field.width - 1);
    const minY = clamp2(Math.floor(Math.min(endpoints[0][1], endpoints[1][1]) - radius), 0, field.height - 1);
    const maxY = clamp2(Math.ceil(Math.max(endpoints[0][1], endpoints[1][1]) + radius), 0, field.height - 1);
    const boxWidth = maxX - minX + 1;
    const boxHeight = maxY - minY + 1;
    const cellCount = boxWidth * boxHeight;
    const distances = new Float64Array(cellCount);
    distances.fill(Number.POSITIVE_INFINITY);
    const parents = new Int32Array(cellCount);
    parents.fill(-1);
    const incomingDirections = new Int8Array(cellCount);
    incomingDirections.fill(-1);
    const closed = new Uint8Array(cellCount);
    const toIndex = (x, y) => (y - minY) * boxWidth + (x - minX);
    const fromIndex = (index) => [minX + index % boxWidth, minY + Math.floor(index / boxWidth)];
    const startIndex = toIndex(start[0], start[1]);
    const goalIndex = goal ? toIndex(goal[0], goal[1]) : -1;
    const heap = new MinHeap();
    distances[startIndex] = 0;
    heap.push({ index: startIndex, priority: 0 });
    let expansions = 0;
    const expansionLimit = Math.min(cellCount, Math.max(2e3, Number(maxExpansions) || 65e4));
    const corridorScale = Math.max(16, radius);
    while (heap.size && expansions < expansionLimit) {
      const current = heap.pop();
      if (!current || closed[current.index]) continue;
      closed[current.index] = 1;
      expansions += 1;
      if (current.index === goalIndex) break;
      const [x, y] = fromIndex(current.index);
      const previousDirection = incomingDirections[current.index];
      for (let directionIndex = 0; directionIndex < NEIGHBORS.length; directionIndex += 1) {
        const move = NEIGHBORS[directionIndex];
        const nx = x + move.dx;
        const ny = y + move.dy;
        if (nx < minX || nx > maxX || ny < minY || ny > maxY) continue;
        const nextIndex = toIndex(nx, ny);
        if (closed[nextIndex]) continue;
        const edge = fieldGradient(field, nx, ny);
        const edgeCost = Math.pow(1 - edge, 2);
        const tangentCost = directionCost(field, nx, ny, move);
        const bendCost = turnCost(previousDirection, directionIndex);
        const corridorDistance = goal ? Math.sqrt(pointSegmentDistanceSquared([nx, ny], start, goal)) / corridorScale : 0;
        const localCost = 0.025 + Number(gradientWeight) * edgeCost + Number(directionWeight) * tangentCost + Number(turnWeight) * bendCost + Number(corridorWeight) * corridorDistance * corridorDistance;
        const candidate = distances[current.index] + move.step * localCost;
        if (candidate >= distances[nextIndex]) continue;
        distances[nextIndex] = candidate;
        parents[nextIndex] = current.index;
        incomingDirections[nextIndex] = directionIndex;
        heap.push({ index: nextIndex, priority: candidate });
      }
    }
    return Object.freeze({
      ok: true,
      reason: "",
      field,
      anchor: Object.freeze(clonePoint(start)),
      target: goal ? Object.freeze(clonePoint(goal)) : null,
      minX,
      minY,
      maxX,
      maxY,
      boxWidth,
      boxHeight,
      startIndex,
      parents,
      distances,
      expansions
    });
  }
  function traceLiveWirePath(tree, target, {
    snapRadius = DEFAULT_SNAP_RADIUS,
    minimumMeanEdge = MINIMUM_MEAN_EDGE
  } = {}) {
    if (!(tree == null ? void 0 : tree.ok) || !tree.field || !tree.parents || !finitePoint(target)) {
      return Object.freeze({ ok: false, reason: (tree == null ? void 0 : tree.reason) || "invalid-tree", points: [] });
    }
    const field = tree.field;
    const snapped = snapLiveWireAnchor(field, target, { radius: snapRadius });
    if (!snapped) return Object.freeze({ ok: false, reason: "invalid-target", points: [] });
    const [x, y] = snapped;
    if (x < tree.minX || x > tree.maxX || y < tree.minY || y > tree.maxY) {
      return Object.freeze({ ok: false, reason: "target-outside-tree", points: [] });
    }
    const targetIndex = (y - tree.minY) * tree.boxWidth + (x - tree.minX);
    if (!Number.isFinite(tree.distances[targetIndex])) {
      return Object.freeze({ ok: false, reason: "path-not-found", points: [] });
    }
    const points = [];
    let cursor = targetIndex;
    let guard = tree.parents.length + 1;
    while (cursor >= 0 && guard-- > 0) {
      points.push([tree.minX + cursor % tree.boxWidth, tree.minY + Math.floor(cursor / tree.boxWidth)]);
      if (cursor === tree.startIndex) break;
      cursor = tree.parents[cursor];
    }
    if (!points.length || cursor !== tree.startIndex) {
      return Object.freeze({ ok: false, reason: "path-not-found", points: [] });
    }
    points.reverse();
    const meanEdgeStrength = points.reduce((sum, point) => sum + fieldGradient(field, point[0], point[1]), 0) / points.length;
    if (meanEdgeStrength < Number(minimumMeanEdge)) {
      return Object.freeze({ ok: false, reason: "insufficient-edge-strength", points: [], meanEdgeStrength });
    }
    return Object.freeze({
      ok: true,
      reason: "",
      points: Object.freeze(points.map((point) => Object.freeze(point))),
      meanEdgeStrength
    });
  }
  function rdp(points, tolerance) {
    if (points.length <= 2 || !(tolerance > 0)) return points.map(clonePoint);
    const tolerance2 = tolerance * tolerance;
    const keep = new Uint8Array(points.length);
    keep[0] = 1;
    keep[points.length - 1] = 1;
    const stack = [[0, points.length - 1]];
    while (stack.length) {
      const [startIndex, endIndex] = stack.pop();
      let farthestIndex = -1;
      let farthestDistance2 = tolerance2;
      for (let index = startIndex + 1; index < endIndex; index += 1) {
        const distance2 = pointSegmentDistanceSquared(points[index], points[startIndex], points[endIndex]);
        if (distance2 <= farthestDistance2) continue;
        farthestDistance2 = distance2;
        farthestIndex = index;
      }
      if (farthestIndex < 0) continue;
      keep[farthestIndex] = 1;
      stack.push([startIndex, farthestIndex], [farthestIndex, endIndex]);
    }
    return points.filter((_, index) => keep[index]).map(clonePoint);
  }
  function simplifyLiveWireSegments(segments, { tolerance = DEFAULT_SIMPLIFY_TOLERANCE } = {}) {
    const result = [];
    for (const segment of segments || []) {
      const source = (segment || []).filter(finitePoint).map(clonePoint);
      if (source.length < 2) continue;
      const simplified = rdp(source, Math.max(0, Number(tolerance) || 0));
      if (result.length && squaredDistance(result.at(-1), simplified[0]) <= 1e-12) result.push(...simplified.slice(1));
      else result.push(...simplified);
    }
    return result;
  }
  function analysisPointFromUv(field, uv) {
    if (!field || !finitePoint(uv)) return null;
    return [
      clamp2(Number(uv[0]), 0, 1) * Math.max(1, field.width - 1),
      clamp2(Number(uv[1]), 0, 1) * Math.max(1, field.height - 1)
    ];
  }
  function sourcePixelsFromAnalysis(field, points) {
    if (!field) return [];
    const scaleX = Number(field.scaleX) || (field.width - 1) / Math.max(1, field.sourceWidth - 1);
    const scaleY = Number(field.scaleY) || (field.height - 1) / Math.max(1, field.sourceHeight - 1);
    return (points || []).filter(finitePoint).map((point) => [
      scaleX > 0 ? Number(point[0]) / scaleX : 0,
      scaleY > 0 ? Number(point[1]) / scaleY : 0
    ]);
  }
  var REFERENCE_IMAGE_LIVE_WIRE_DEFAULTS = Object.freeze({
    maxDimension: DEFAULT_MAX_DIMENSION,
    snapRadius: DEFAULT_SNAP_RADIUS,
    searchRadius: DEFAULT_SEARCH_RADIUS,
    simplifyTolerance: DEFAULT_SIMPLIFY_TOLERANCE
  });

  // app/reference-web/reference-image-source-mapping.js
  function buildReferenceImageSourceMapping(record = {}) {
    const calibration2 = buildReferenceImageCalibrationWarp({
      controlPoints: record.controlPoints,
      anchor: record.anchor,
      cornerPinEnabled: record.cornerPinEnabled,
      mapQuad: record.mapQuad,
      mode: record.warpMode
    });
    if (calibration2.ok) return calibration2;
    if (!record.cornerPinEnabled) return calibration2;
    const placement = buildReferenceImagePlacementWarp(record);
    if (!placement.ok) return calibration2;
    return Object.freeze(__spreadProps(__spreadValues({}, placement), {
      source: "corner-pin",
      project(imageUv) {
        return referenceImagePlacementCoordinateAtUv(record, imageUv);
      }
    }));
  }

  // app/reference-web/reference-image-line-refiner.js
  var DEFAULT_MAX_DIMENSION2 = 1024;
  var DEFAULT_CORRIDOR_RADIUS = 12;
  var DEFAULT_SIMPLIFY_TOLERANCE2 = 1.5;
  var MINIMUM_PEAK_EDGE2 = 0.02;
  var MINIMUM_MEAN_EDGE2 = 0.06;
  var SQRT22 = Math.SQRT2;
  var SOBEL_MAX2 = 4 * SQRT22 * 255;
  var clamp3 = (value, min, max) => Math.max(min, Math.min(max, value));
  var finitePoint2 = (value) => Array.isArray(value) && value.length >= 2 && Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1]));
  var clonePoint2 = (value) => [Number(value[0]), Number(value[1])];
  var squaredDistance2 = (left, right) => {
    const dx = Number(left[0]) - Number(right[0]);
    const dy = Number(left[1]) - Number(right[1]);
    return dx * dx + dy * dy;
  };
  function pointSegmentDistanceSquared2(point, start, end) {
    const dx = end[0] - start[0];
    const dy = end[1] - start[1];
    const length2 = dx * dx + dy * dy;
    if (length2 <= 1e-12) return squaredDistance2(point, start);
    const t = clamp3(((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / length2, 0, 1);
    return squaredDistance2(point, [start[0] + dx * t, start[1] + dy * t]);
  }
  function rdp2(points, tolerance) {
    if (points.length <= 2 || !(tolerance > 0)) return points.map(clonePoint2);
    const tolerance2 = tolerance * tolerance;
    const keep = new Uint8Array(points.length);
    keep[0] = 1;
    keep[points.length - 1] = 1;
    const stack = [[0, points.length - 1]];
    while (stack.length) {
      const [startIndex, endIndex] = stack.pop();
      let farthestIndex = -1;
      let farthestDistance2 = tolerance2;
      for (let index = startIndex + 1; index < endIndex; index += 1) {
        const distance2 = pointSegmentDistanceSquared2(points[index], points[startIndex], points[endIndex]);
        if (distance2 <= farthestDistance2) continue;
        farthestDistance2 = distance2;
        farthestIndex = index;
      }
      if (farthestIndex < 0) continue;
      keep[farthestIndex] = 1;
      stack.push([startIndex, farthestIndex], [farthestIndex, endIndex]);
    }
    return points.filter((_, index) => keep[index]).map(clonePoint2);
  }
  function removeDuplicatePoints(points) {
    const result = [];
    for (const point of points) {
      if (!finitePoint2(point)) continue;
      const next = clonePoint2(point);
      if (result.length && squaredDistance2(result.at(-1), next) <= 1e-12) continue;
      result.push(next);
    }
    return result;
  }
  function grayscaleAt2(data, offset) {
    var _a;
    const alpha = Number((_a = data[offset + 3]) != null ? _a : 255) / 255;
    const value = 0.2126 * Number(data[offset] || 0) + 0.7152 * Number(data[offset + 1] || 0) + 0.0722 * Number(data[offset + 2] || 0);
    return value * alpha + 255 * (1 - alpha);
  }
  function buildReferenceImageGradientField(imageData, {
    sourceWidth = imageData == null ? void 0 : imageData.width,
    sourceHeight = imageData == null ? void 0 : imageData.height
  } = {}) {
    const width = Math.max(1, Math.floor(Number(imageData == null ? void 0 : imageData.width) || 0));
    const height = Math.max(1, Math.floor(Number(imageData == null ? void 0 : imageData.height) || 0));
    const data = imageData == null ? void 0 : imageData.data;
    if (!data || data.length < width * height * 4) throw new TypeError("RGBA image data is required.");
    const naturalWidth = Math.max(1, Number(sourceWidth) || width);
    const naturalHeight = Math.max(1, Number(sourceHeight) || height);
    const gray = new Float32Array(width * height);
    for (let index = 0; index < width * height; index += 1) gray[index] = grayscaleAt2(data, index * 4);
    const gradient = new Float32Array(width * height);
    let peak = 0;
    let sum = 0;
    if (width >= 3 && height >= 3) {
      for (let y = 1; y < height - 1; y += 1) {
        for (let x = 1; x < width - 1; x += 1) {
          const top = (y - 1) * width;
          const mid = y * width;
          const bottom = (y + 1) * width;
          const gx = -gray[top + x - 1] + gray[top + x + 1] - 2 * gray[mid + x - 1] + 2 * gray[mid + x + 1] - gray[bottom + x - 1] + gray[bottom + x + 1];
          const gy = -gray[top + x - 1] - 2 * gray[top + x] - gray[top + x + 1] + gray[bottom + x - 1] + 2 * gray[bottom + x] + gray[bottom + x + 1];
          const magnitude = clamp3(Math.hypot(gx, gy) / SOBEL_MAX2, 0, 1);
          gradient[mid + x] = magnitude;
          peak = Math.max(peak, magnitude);
          sum += magnitude;
        }
      }
    }
    const interiorCount = Math.max(1, Math.max(0, width - 2) * Math.max(0, height - 2));
    return Object.freeze({
      width,
      height,
      sourceWidth: naturalWidth,
      sourceHeight: naturalHeight,
      scaleX: (width - 1) / Math.max(1, naturalWidth - 1),
      scaleY: (height - 1) / Math.max(1, naturalHeight - 1),
      gradient,
      peakEdgeStrength: peak,
      meanEdgeStrength: sum / interiorCount
    });
  }
  var MinHeap2 = class {
    constructor() {
      this.items = [];
    }
    push(value) {
      const items = this.items;
      items.push(value);
      let index = items.length - 1;
      while (index > 0) {
        const parent = Math.floor((index - 1) / 2);
        if (items[parent].priority <= value.priority) break;
        items[index] = items[parent];
        index = parent;
      }
      items[index] = value;
    }
    pop() {
      const items = this.items;
      if (!items.length) return null;
      const root = items[0];
      const tail = items.pop();
      if (!items.length) return root;
      let index = 0;
      while (true) {
        const left = index * 2 + 1;
        const right = left + 1;
        if (left >= items.length) break;
        let child = left;
        if (right < items.length && items[right].priority < items[left].priority) child = right;
        if (items[child].priority >= tail.priority) break;
        items[index] = items[child];
        index = child;
      }
      items[index] = tail;
      return root;
    }
    get size() {
      return this.items.length;
    }
  };
  function fieldGradient2(field, x, y) {
    if (x < 0 || y < 0 || x >= field.width || y >= field.height) return 0;
    return Number(field.gradient[y * field.width + x] || 0);
  }
  function resamplePolyline(points, spacing) {
    const source = removeDuplicatePoints(points);
    if (source.length <= 1 || !(spacing > 0)) return source;
    const result = [clonePoint2(source[0])];
    let previous = clonePoint2(source[0]);
    let remaining = spacing;
    for (let index = 1; index < source.length; index += 1) {
      const target = source[index];
      let dx = target[0] - previous[0];
      let dy = target[1] - previous[1];
      let length = Math.hypot(dx, dy);
      while (length >= remaining && length > 1e-9) {
        const ratio = remaining / length;
        previous = [previous[0] + dx * ratio, previous[1] + dy * ratio];
        result.push(clonePoint2(previous));
        dx = target[0] - previous[0];
        dy = target[1] - previous[1];
        length = Math.hypot(dx, dy);
        remaining = spacing;
      }
      remaining -= length;
      previous = clonePoint2(target);
      if (remaining <= 1e-9) remaining = spacing;
    }
    const last = source.at(-1);
    if (squaredDistance2(result.at(-1), last) > 1e-8) result.push(clonePoint2(last));
    return result;
  }
  function snapToEdge(field, point, radius) {
    const centerX = clamp3(Math.round(point[0]), 0, field.width - 1);
    const centerY = clamp3(Math.round(point[1]), 0, field.height - 1);
    const search = Math.max(1, Math.ceil(radius));
    const radius2 = radius * radius;
    let best = [centerX, centerY];
    let bestScore = fieldGradient2(field, centerX, centerY);
    let bestDistance2 = 0;
    for (let y = Math.max(0, centerY - search); y <= Math.min(field.height - 1, centerY + search); y += 1) {
      for (let x = Math.max(0, centerX - search); x <= Math.min(field.width - 1, centerX + search); x += 1) {
        const distance2 = squaredDistance2([x, y], point);
        if (distance2 > radius2) continue;
        const edge = fieldGradient2(field, x, y);
        const score = edge - 6e-3 * Math.sqrt(distance2);
        if (score < bestScore - 1e-9 || Math.abs(score - bestScore) <= 1e-9 && distance2 >= bestDistance2) continue;
        best = [x, y];
        bestScore = score;
        bestDistance2 = distance2;
      }
    }
    return best;
  }
  var NEIGHBORS2 = Object.freeze([
    [-1, -1, SQRT22],
    [0, -1, 1],
    [1, -1, SQRT22],
    [-1, 0, 1],
    [1, 0, 1],
    [-1, 1, SQRT22],
    [0, 1, 1],
    [1, 1, SQRT22]
  ]);
  function traceCorridorSegment(field, roughStart, roughEnd, start, end, radius) {
    var _a, _b;
    const margin = Math.ceil(radius) + 2;
    const minX = clamp3(Math.floor(Math.min(roughStart[0], roughEnd[0], start[0], end[0]) - margin), 0, field.width - 1);
    const maxX = clamp3(Math.ceil(Math.max(roughStart[0], roughEnd[0], start[0], end[0]) + margin), 0, field.width - 1);
    const minY = clamp3(Math.floor(Math.min(roughStart[1], roughEnd[1], start[1], end[1]) - margin), 0, field.height - 1);
    const maxY = clamp3(Math.ceil(Math.max(roughStart[1], roughEnd[1], start[1], end[1]) + margin), 0, field.height - 1);
    const boxWidth = maxX - minX + 1;
    const boxHeight = maxY - minY + 1;
    const cellCount = boxWidth * boxHeight;
    const distances = new Float64Array(cellCount);
    distances.fill(Number.POSITIVE_INFINITY);
    const parents = new Int32Array(cellCount);
    parents.fill(-1);
    const closed = new Uint8Array(cellCount);
    const toLocalIndex = (x, y) => (y - minY) * boxWidth + (x - minX);
    const fromLocalIndex = (index) => [minX + index % boxWidth, minY + Math.floor(index / boxWidth)];
    const startPoint = [clamp3(Math.round(start[0]), minX, maxX), clamp3(Math.round(start[1]), minY, maxY)];
    const endPoint = [clamp3(Math.round(end[0]), minX, maxX), clamp3(Math.round(end[1]), minY, maxY)];
    const startIndex = toLocalIndex(startPoint[0], startPoint[1]);
    const endIndex = toLocalIndex(endPoint[0], endPoint[1]);
    const radius2 = radius * radius;
    const allowed = (x, y) => {
      if (x === startPoint[0] && y === startPoint[1]) return true;
      if (x === endPoint[0] && y === endPoint[1]) return true;
      return pointSegmentDistanceSquared2([x, y], roughStart, roughEnd) <= radius2 + 1e-9;
    };
    const heap = new MinHeap2();
    distances[startIndex] = 0;
    heap.push({ index: startIndex, priority: 0 });
    let expansions = 0;
    const maxExpansions = Math.min(15e4, Math.max(2e3, cellCount * 2));
    while (heap.size && expansions < maxExpansions) {
      const current = heap.pop();
      if (!current || closed[current.index]) continue;
      closed[current.index] = 1;
      expansions += 1;
      if (current.index === endIndex) break;
      const [x, y] = fromLocalIndex(current.index);
      for (const [dx, dy, stepDistance] of NEIGHBORS2) {
        const nx = x + dx;
        const ny = y + dy;
        if (nx < minX || nx > maxX || ny < minY || ny > maxY || !allowed(nx, ny)) continue;
        const nextIndex = toLocalIndex(nx, ny);
        if (closed[nextIndex]) continue;
        const edge = fieldGradient2(field, nx, ny);
        const corridorDistance = Math.sqrt(pointSegmentDistanceSquared2([nx, ny], roughStart, roughEnd));
        const edgeCost = 0.08 + 3.4 * (1 - edge) * (1 - edge);
        const corridorCost = 0.18 * Math.pow(corridorDistance / Math.max(1, radius), 2);
        const candidate = distances[current.index] + stepDistance * (edgeCost + corridorCost);
        if (candidate >= distances[nextIndex]) continue;
        distances[nextIndex] = candidate;
        parents[nextIndex] = current.index;
        const heuristic = Math.hypot(nx - endPoint[0], ny - endPoint[1]) * 0.08;
        heap.push({ index: nextIndex, priority: candidate + heuristic });
      }
    }
    if (!Number.isFinite(distances[endIndex])) return null;
    const path = [];
    let cursor = endIndex;
    let guard = cellCount + 1;
    while (cursor >= 0 && guard-- > 0) {
      path.push(fromLocalIndex(cursor));
      if (cursor === startIndex) break;
      cursor = parents[cursor];
    }
    if (((_a = path.at(-1)) == null ? void 0 : _a[0]) !== startPoint[0] || ((_b = path.at(-1)) == null ? void 0 : _b[1]) !== startPoint[1]) return null;
    return path.reverse();
  }
  function toAnalysisPoint(field, point) {
    return [
      clamp3(Number(point[0]) * field.scaleX, 0, field.width - 1),
      clamp3(Number(point[1]) * field.scaleY, 0, field.height - 1)
    ];
  }
  function toSourcePoint(field, point) {
    return [
      field.scaleX > 0 ? Number(point[0]) / field.scaleX : 0,
      field.scaleY > 0 ? Number(point[1]) / field.scaleY : 0
    ];
  }
  function refineReferenceImageLine({
    field,
    roughPoints,
    corridorRadius = DEFAULT_CORRIDOR_RADIUS,
    simplifyTolerance = DEFAULT_SIMPLIFY_TOLERANCE2,
    minimumPeakEdge = MINIMUM_PEAK_EDGE2,
    minimumMeanEdge = MINIMUM_MEAN_EDGE2
  } = {}) {
    if (!(field == null ? void 0 : field.gradient) || !Number.isFinite(field.width) || !Number.isFinite(field.height)) {
      return Object.freeze({ ok: false, reason: "invalid-gradient-field", points: [] });
    }
    const source = removeDuplicatePoints(roughPoints || []);
    if (source.length < 2) return Object.freeze({ ok: false, reason: "too-few-points", points: [] });
    if (Number(field.peakEdgeStrength || 0) < minimumPeakEdge) {
      return Object.freeze({ ok: false, reason: "insufficient-edge-strength", points: [] });
    }
    const analysisRough = source.map((point) => toAnalysisPoint(field, point));
    const analysisScale = Math.max(1e-6, Math.min(field.scaleX || 1, field.scaleY || 1));
    const radius = Math.max(2, Number(corridorRadius) * analysisScale);
    const spacing = Math.max(4, Math.min(18, radius * 1.15));
    const anchors = resamplePolyline(analysisRough, spacing);
    if (anchors.length < 2) return Object.freeze({ ok: false, reason: "too-few-points", points: [] });
    const snapRadius = Math.max(1.5, Math.min(radius * 0.9, 8));
    const snapped = anchors.map((point) => snapToEdge(field, point, snapRadius));
    const traced = [];
    for (let index = 1; index < anchors.length; index += 1) {
      const segment = traceCorridorSegment(
        field,
        anchors[index - 1],
        anchors[index],
        snapped[index - 1],
        snapped[index],
        radius
      );
      if (!(segment == null ? void 0 : segment.length)) return Object.freeze({ ok: false, reason: "path-not-found", points: [] });
      if (traced.length && squaredDistance2(traced.at(-1), segment[0]) <= 1e-12) traced.push(...segment.slice(1));
      else traced.push(...segment);
    }
    const uniqueTrace = removeDuplicatePoints(traced);
    if (uniqueTrace.length < 2) return Object.freeze({ ok: false, reason: "path-not-found", points: [] });
    const meanEdgeStrength = uniqueTrace.reduce((sum, point) => sum + fieldGradient2(field, Math.round(point[0]), Math.round(point[1])), 0) / uniqueTrace.length;
    if (meanEdgeStrength < minimumMeanEdge) {
      return Object.freeze({ ok: false, reason: "insufficient-edge-strength", points: [], meanEdgeStrength });
    }
    const sourceTrace = uniqueTrace.map((point) => toSourcePoint(field, point));
    const points = rdp2(sourceTrace, Math.max(0, Number(simplifyTolerance) || 0));
    return Object.freeze({
      ok: true,
      reason: "",
      points: Object.freeze(points.map((point) => Object.freeze(point))),
      rawPointCount: uniqueTrace.length,
      simplifiedPointCount: points.length,
      meanEdgeStrength,
      corridorRadius: Number(corridorRadius)
    });
  }
  function referenceImagePixelsToCoordinates(points, { sourceWidth, sourceHeight, warp } = {}) {
    if (!(warp == null ? void 0 : warp.ok) || typeof warp.project !== "function") throw new Error("A ready reference image warp is required.");
    const width = Math.max(2, Number(sourceWidth) || 0);
    const height = Math.max(2, Number(sourceHeight) || 0);
    const xDenominator = width - 1;
    const yDenominator = height - 1;
    return (points || []).filter(finitePoint2).map((point) => {
      const uv = [clamp3(Number(point[0]) / xDenominator, 0, 1), clamp3(Number(point[1]) / yDenominator, 0, 1)];
      const projected = warp.project(uv);
      if (!finitePoint2(projected)) throw new Error("Reference image warp returned an invalid coordinate.");
      return clonePoint2(projected);
    });
  }
  var REFERENCE_IMAGE_LINE_REFINER_DEFAULTS = Object.freeze({
    maxDimension: DEFAULT_MAX_DIMENSION2,
    corridorRadius: DEFAULT_CORRIDOR_RADIUS,
    simplifyTolerance: DEFAULT_SIMPLIFY_TOLERANCE2
  });

  // app/reference-web/adapter.js
  function calibration(record) {
    const calibrated = buildReferenceImageCalibrationWarp(__spreadProps(__spreadValues({}, record), { mode: record.warpMode }));
    let warp = calibrated;
    if (!warp.ok && record.mapQuad) warp = buildReferenceImageProjectiveWarpFromQuad(record.mapQuad);
    const mesh = warp.ok ? buildReferenceImageMesh(warp) : null;
    return { ok: warp.ok, calibrationOk: calibrated.ok, calibrationReason: calibrated.reason || "", mode: warp.mode, reason: warp.reason || "", diagnostics: calibrated.diagnostics || {}, mesh: mesh ? __spreadProps(__spreadValues({}, mesh), { vertices: mesh.vertices.map((vertex) => __spreadProps(__spreadValues({}, vertex), { uv: [record.flipX ? 1 - vertex.uv[0] : vertex.uv[0], record.flipY ? 1 - vertex.uv[1] : vertex.uv[1]] })) }) : null };
  }
  function cornerQuad(input) {
    const record = __spreadProps(__spreadValues({}, input.record), { mapQuad: input.quad });
    const host = { unproject: () => input.quad[0], project: (coordinate) => {
      const index = record.mapQuad.findIndex((p) => p[0] === coordinate[0] && p[1] === coordinate[1]);
      return index >= 0 ? input.screenQuad[index] : null;
    } };
    let ok = applyReferenceImageFreeTransformDrag(record, { index: 0, startMapQuad: input.quad }, input.screenQuad[0], host);
    const warp = buildReferenceImageCalibrationWarp(__spreadProps(__spreadValues({}, record), { cornerPinEnabled: true, mode: record.warpMode }));
    if ((record.controlPoints || []).length || record.anchor) ok = ok && warp.ok && warp.diagnostics.hardMaxMeters <= 0.01;
    return { ok, mapQuad: record.mapQuad };
  }
  function anchor(input) {
    const record = __spreadProps(__spreadValues({}, input.record), { anchor: input.anchor });
    const warp = buildReferenceImageCalibrationWarp(__spreadProps(__spreadValues({}, record), { mode: record.warpMode }));
    const ok = warp.ok ? warp.diagnostics.hardMaxMeters <= 0.01 : warp.reason === "singular-control-points" && warp.pointCount >= warp.minimumPoints ? false : alignReferenceImageAnchor(record);
    return { ok, mapQuad: record.mapQuad, anchor: record.anchor };
  }
  function trace(input) {
    const field = buildReferenceImageLiveWireField(input.image, { sourceWidth: input.sourceWidth, sourceHeight: input.sourceHeight });
    const warp = buildReferenceImageSourceMapping(input.record);
    if (!warp.ok) return { ok: false, reason: warp.reason };
    const anchors = input.anchors.map((uv2) => analysisPointFromUv(field, uv2)), segments = [];
    for (let i = 1; i < anchors.length; i++) {
      const tree = buildLiveWireTree(field, anchors[i - 1], { target: anchors[i], corridorWeight: 0.08 });
      const result = traceLiveWirePath(tree, anchors[i]);
      if (!result.ok) return { ok: false, reason: result.reason };
      segments.push(result.points);
    }
    const points = sourcePixelsFromAnalysis(field, simplifyLiveWireSegments(segments, { tolerance: 1.5 }));
    const uv = points.map((p) => [p[0] / Math.max(1, input.sourceWidth - 1), p[1] / Math.max(1, input.sourceHeight - 1)]);
    const coordinates = uv.map((p) => warp.project(p));
    return { ok: true, reason: "", coordinates, uv };
  }
  function refine(input) {
    const field = buildReferenceImageGradientField(input.image, { sourceWidth: input.sourceWidth, sourceHeight: input.sourceHeight });
    const warp = buildReferenceImageSourceMapping(input.record);
    if (!warp.ok) return { ok: false, reason: warp.reason };
    const roughPoints = input.anchors.map((p) => [p[0] * (input.sourceWidth - 1), p[1] * (input.sourceHeight - 1)]);
    const result = refineReferenceImageLine({ field, roughPoints, corridorRadius: 12, simplifyTolerance: 1.5 });
    return result.ok ? { ok: true, coordinates: referenceImagePixelsToCoordinates(result.points, { sourceWidth: input.sourceWidth, sourceHeight: input.sourceHeight, warp }), uv: result.points.map((p) => [p[0] / Math.max(1, input.sourceWidth - 1), p[1] / Math.max(1, input.sourceHeight - 1)]) } : { ok: false, reason: result.reason };
  }
  return __toCommonJS(adapter_exports);
})();
