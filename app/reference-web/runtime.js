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
    cornerQuad: () => cornerQuad
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
  return __toCommonJS(adapter_exports);
})();
