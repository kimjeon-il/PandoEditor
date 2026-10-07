import { buildGpuStrokeInstances } from './gpu-stroke-geometry.js';
export { buildGpuStrokeInstances, GPU_STROKE_FLAGS } from './gpu-stroke-geometry.js';
import { isRenderDevice } from './render-device.js';
import { createGpuResourceBudget } from './gpu-resource-budget.js';
import { applyGpuBlendMode, parseGpuColor, resetGpuNormalBlend } from './gpu-blend-utils.js';
import { linkGpuProgram } from './gpu-shader-utils.js';
import { GPU_VIEW_UNIFORM_NAMES, setGpuViewUniforms } from './gpu-view-uniforms.js';
import { scaleInteractionStroke, gpuStrokeStyle } from './map-interaction-style.js';

const FLOATS_PER_INSTANCE = 10;
const FLOATS_PER_NODE = 8;
const VERTICES_PER_SEGMENT = 6;
const VERTICES_PER_ROUND_NODE = 6;
const VERTICES_PER_BEVEL_JOIN = 3;
const SEGMENT_CORNERS = new Float32Array([-1, 0, 1, 0, -1, 1, -1, 1, 1, 0, 1, 1]);
const ROUND_NODE_CORNERS = new Float32Array([-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1]);
const BEVEL_VERTEX_IDS = new Float32Array([0, 1, 2]);
const DEFAULT_AA_RADIUS_PX = 1;

function linkProgram(device, vertexSource, fragmentSource, attributeNames, uniformNames) {
  const { gl } = device;
  const program = linkGpuProgram(gl, vertexSource, fragmentSource, { label: 'stroke' });
  const attributes = Object.freeze(Object.fromEntries(attributeNames.map(name => [name, gl.getAttribLocation(program, name)])));
  const uniforms = Object.freeze(Object.fromEntries(uniformNames.map(name => [name, gl.getUniformLocation(program, name)])));
  return Object.freeze({ program, attributes, uniforms });
}

function projectionSource() {
  return `
    vec3 projectStrokePoint(vec2 coord){
      float lon=coord.x*0.017453292519943;float lat=coord.y*0.017453292519943;vec2 p;float depth;
      if(uMode==0){
        vec3 q=vec3(cos(lat)*cos(lon),cos(lat)*sin(lon),sin(lat));
        p=uTranslate+uScale*vec2(dot(uRowX,q),dot(uRowY,q));depth=dot(uRowZ,q);
      }else{
        p=uTranslate+uScale*vec2(lon+uWorldOffset-uFlatCenter.x,-(lat-uFlatCenter.y));depth=1.0;
      }
      return vec3(p,depth);
    }
  `;
}

function segmentProgramSources(version) {
  const header = version === 2 ? '#version 300 es\nprecision highp float;precision highp int;' : 'precision highp float;precision highp int;';
  const attributes = version === 2
    ? 'in vec2 aCorner;in vec2 aPrevious;in vec4 aSegment;in vec2 aNext;in vec2 aMeta;out float vDepth;out float vAcross;out float vAlong;'
    : 'attribute vec2 aCorner;attribute vec2 aPrevious;attribute vec4 aSegment;attribute vec2 aNext;attribute vec2 aMeta;varying float vDepth;varying float vAcross;varying float vAlong;';
  const vertex = `${header}
    ${attributes}
    uniform vec2 uViewport;uniform vec2 uTranslate;uniform float uScale;
    uniform vec3 uRowX;uniform vec3 uRowY;uniform vec3 uRowZ;uniform vec2 uFlatCenter;
    uniform float uWorldOffset;uniform int uMode;uniform float uHalfWidth;uniform float uAaRadius;uniform int uJoinMode;uniform float uMiterLimit;
    ${projectionSource()}
    bool hasFlag(float flags,float flag){return mod(floor(flags/flag),2.0)>0.5;}
    vec2 safeDirection(vec2 delta){float lengthPx=length(delta);return lengthPx>0.0001?delta/lengthPx:vec2(1.0,0.0);}
    vec2 normalFor(vec2 direction){return vec2(-direction.y,direction.x);}
    vec2 miterOffset(vec2 incoming,vec2 outgoing,vec2 fallbackNormal,float side,float outerWidth){
      vec2 tangent=safeDirection(incoming+outgoing);vec2 miter=normalFor(tangent);float denominator=dot(miter,fallbackNormal);
      if(abs(denominator)<0.08)return side*fallbackNormal*outerWidth;
      float lengthScale=outerWidth/denominator;
      if(abs(lengthScale)>outerWidth*uMiterLimit)return side*fallbackNormal*outerWidth;
      return side*miter*lengthScale;
    }
    void main(){
      vec3 previous=projectStrokePoint(aPrevious);vec3 start=projectStrokePoint(aSegment.xy);vec3 end=projectStrokePoint(aSegment.zw);vec3 next=projectStrokePoint(aNext);
      vec2 current=safeDirection(end.xy-start.xy);vec2 currentNormal=normalFor(current);float flags=aMeta.y;float side=aCorner.x;float endpoint=aCorner.y;
      float outerWidth=uHalfWidth+uAaRadius;vec2 offset=side*currentNormal*outerWidth;
      if(uJoinMode==1){
        if(endpoint<0.5&&hasFlag(flags,1.0)){vec2 incoming=safeDirection(start.xy-previous.xy);offset=miterOffset(incoming,current,currentNormal,side,outerWidth);}
        if(endpoint>=0.5&&hasFlag(flags,2.0)){vec2 outgoing=safeDirection(next.xy-end.xy);offset=miterOffset(current,outgoing,currentNormal,side,outerWidth);}
      }
      vec2 base=mix(start.xy,end.xy,endpoint);vec2 p=base+offset;
      vDepth=mix(start.z,end.z,endpoint);vAcross=side*outerWidth;vAlong=aMeta.x*uScale*0.017453292519943295+length(end.xy-start.xy)*endpoint;
      gl_Position=vec4(p.x*2.0/uViewport.x-1.0,1.0-p.y*2.0/uViewport.y,0.0,1.0);
    }`;
  const fragmentHeader = version === 2
    ? '#version 300 es\nprecision highp float;precision highp int;in float vDepth;in float vAcross;in float vAlong;out vec4 outColor;'
    : 'precision highp float;precision highp int;varying float vDepth;varying float vAcross;varying float vAlong;';
  const output = `${version === 2 ? 'outColor' : 'gl_FragColor'}=vec4(uColor.rgb*(uMultiply==1?uColor.a*coverage:1.0),uColor.a*coverage);`;
  const fragment = `${fragmentHeader}
    uniform int uMode;uniform float uHalfWidth;uniform float uAaRadius;uniform float uInnerCutout;uniform vec2 uDash;uniform vec4 uColor;uniform int uMultiply;
    void main(){
      if(uMode==0&&vDepth<0.0)discard;
      float period=uDash.x+uDash.y;if(uDash.x>0.0&&uDash.y>0.0&&mod(vAlong,max(1.0,period))>uDash.x)discard;
      float edge=uHalfWidth-abs(vAcross);float coverage=uAaRadius<=0.0?step(0.0,edge):smoothstep(-uAaRadius,uAaRadius,edge);
      if(uInnerCutout>0.0){float innerEdge=abs(vAcross)-uInnerCutout;coverage*=uAaRadius<=0.0?step(0.0,innerEdge):smoothstep(-uAaRadius,uAaRadius,innerEdge);}
      if(coverage<=0.001)discard;${output}
    }`;
  return { vertex, fragment };
}

function roundProgramSources(version) {
  const header = version === 2 ? '#version 300 es\nprecision highp float;precision highp int;' : 'precision highp float;precision highp int;';
  const attributes = version === 2
    ? 'in vec2 aCorner;in vec2 aNodePrevious;in vec2 aNodePoint;in vec2 aNodeNext;in vec2 aNodeMeta;out float vDepth;out vec2 vLocal;out vec2 vIncoming;out vec2 vOutgoing;out float vKind;'
    : 'attribute vec2 aCorner;attribute vec2 aNodePrevious;attribute vec2 aNodePoint;attribute vec2 aNodeNext;attribute vec2 aNodeMeta;varying float vDepth;varying vec2 vLocal;varying vec2 vIncoming;varying vec2 vOutgoing;varying float vKind;';
  const vertex = `${header}
    ${attributes}
    uniform vec2 uViewport;uniform vec2 uTranslate;uniform float uScale;
    uniform vec3 uRowX;uniform vec3 uRowY;uniform vec3 uRowZ;uniform vec2 uFlatCenter;
    uniform float uWorldOffset;uniform int uMode;uniform float uHalfWidth;uniform float uAaRadius;
    ${projectionSource()}
    vec2 safeDirection(vec2 delta){float lengthPx=length(delta);return lengthPx>0.0001?delta/lengthPx:vec2(1.0,0.0);}
    void main(){
      vec3 previous=projectStrokePoint(aNodePrevious);vec3 point=projectStrokePoint(aNodePoint);vec3 next=projectStrokePoint(aNodeNext);
      float radius=uHalfWidth+uAaRadius;vec2 local=aCorner*radius;vec2 p=point.xy+local;
      vDepth=point.z;vLocal=local;vIncoming=safeDirection(point.xy-previous.xy);vOutgoing=safeDirection(next.xy-point.xy);vKind=aNodeMeta.y;
      gl_Position=vec4(p.x*2.0/uViewport.x-1.0,1.0-p.y*2.0/uViewport.y,0.0,1.0);
    }`;
  const fragmentHeader = version === 2
    ? '#version 300 es\nprecision highp float;precision highp int;in float vDepth;in vec2 vLocal;in vec2 vIncoming;in vec2 vOutgoing;in float vKind;out vec4 outColor;'
    : 'precision highp float;precision highp int;varying float vDepth;varying vec2 vLocal;varying vec2 vIncoming;varying vec2 vOutgoing;varying float vKind;';
  const output = `${version === 2 ? 'outColor' : 'gl_FragColor'}=vec4(uColor.rgb*(uMultiply==1?uColor.a*coverage:1.0),uColor.a*coverage);`;
  const fragment = `${fragmentHeader}
    uniform int uMode;uniform int uJoinMode;uniform int uRoundCap;uniform float uHalfWidth;uniform float uAaRadius;uniform float uInnerCutout;uniform vec4 uColor;uniform int uMultiply;
    float cross2(vec2 left,vec2 right){return left.x*right.y-left.y*right.x;}
    void main(){
      if(uMode==0&&vDepth<0.0)discard;
      bool interior=vKind<1.5;if(interior&&uJoinMode!=0)discard;if(!interior&&uRoundCap==0)discard;
      if(interior){
        bool insideIncoming=dot(vLocal,vIncoming)<=0.0&&abs(cross2(vIncoming,vLocal))<=uHalfWidth;
        bool insideOutgoing=dot(vLocal,vOutgoing)>=0.0&&abs(cross2(vOutgoing,vLocal))<=uHalfWidth;
        if(insideIncoming||insideOutgoing)discard;
      }else if(vKind<2.5){
        if(dot(vLocal,vOutgoing)>=0.0)discard;
      }else{
        if(dot(vLocal,vIncoming)<=0.0)discard;
      }
      float edge=uHalfWidth-length(vLocal);float coverage=uAaRadius<=0.0?step(0.0,edge):smoothstep(-uAaRadius,uAaRadius,edge);
      if(uInnerCutout>0.0){float innerEdge=length(vLocal)-uInnerCutout;coverage*=uAaRadius<=0.0?step(0.0,innerEdge):smoothstep(-uAaRadius,uAaRadius,innerEdge);}
      if(coverage<=0.001)discard;${output}
    }`;
  return { vertex, fragment };
}

function bevelProgramSources(version) {
  const header = version === 2 ? '#version 300 es\nprecision highp float;precision highp int;' : 'precision highp float;precision highp int;';
  const attributes = version === 2
    ? 'in float aVertexId;in vec2 aNodePrevious;in vec2 aNodePoint;in vec2 aNodeNext;in vec2 aNodeMeta;out float vDepth;'
    : 'attribute float aVertexId;attribute vec2 aNodePrevious;attribute vec2 aNodePoint;attribute vec2 aNodeNext;attribute vec2 aNodeMeta;varying float vDepth;';
  const vertex = `${header}
    ${attributes}
    uniform vec2 uViewport;uniform vec2 uTranslate;uniform float uScale;
    uniform vec3 uRowX;uniform vec3 uRowY;uniform vec3 uRowZ;uniform vec2 uFlatCenter;
    uniform float uWorldOffset;uniform int uMode;uniform float uHalfWidth;uniform float uAaRadius;
    ${projectionSource()}
    vec2 safeDirection(vec2 delta){float lengthPx=length(delta);return lengthPx>0.0001?delta/lengthPx:vec2(1.0,0.0);}
    void main(){
      vec3 previous=projectStrokePoint(aNodePrevious);vec3 point=projectStrokePoint(aNodePoint);vec3 next=projectStrokePoint(aNodeNext);
      if(aNodeMeta.y>1.5){gl_Position=vec4(point.xy*0.0,0.0,1.0);vDepth=-1.0;return;}
      vec2 incoming=safeDirection(point.xy-previous.xy);vec2 outgoing=safeDirection(next.xy-point.xy);float crossValue=incoming.x*outgoing.y-incoming.y*outgoing.x;
      float side=crossValue>=0.0?-1.0:1.0;float width=uHalfWidth+uAaRadius;vec2 n0=vec2(-incoming.y,incoming.x)*side;vec2 n1=vec2(-outgoing.y,outgoing.x)*side;
      vec2 p=aVertexId<0.5?point.xy:(aVertexId<1.5?point.xy+n0*width:point.xy+n1*width);vDepth=point.z;
      gl_Position=vec4(p.x*2.0/uViewport.x-1.0,1.0-p.y*2.0/uViewport.y,0.0,1.0);
    }`;
  const fragmentHeader = version === 2
    ? '#version 300 es\nprecision highp float;precision highp int;in float vDepth;out vec4 outColor;'
    : 'precision highp float;precision highp int;varying float vDepth;';
  const output = `${version === 2 ? 'outColor' : 'gl_FragColor'}=vec4(uColor.rgb*(uMultiply==1?uColor.a:1.0),uColor.a);`;
  const fragment = `${fragmentHeader}
    uniform int uMode;uniform vec4 uColor;uniform int uMultiply;
    void main(){if(uMode==0&&vDepth<0.0)discard;${output}}`;
  return { vertex, fragment };
}

function createPrograms(device, only = null) {
  const segmentSources = segmentProgramSources(device.version);
  const roundSources = roundProgramSources(device.version);
  const bevelSources = bevelProgramSources(device.version);
  const viewUniforms = GPU_VIEW_UNIFORM_NAMES;
  return Object.freeze({
    segment: (!only || only === 'segment') && linkProgram(device, segmentSources.vertex, segmentSources.fragment,
      ['aCorner', 'aPrevious', 'aSegment', 'aNext', 'aMeta'],
      [...viewUniforms, 'uHalfWidth', 'uAaRadius', 'uInnerCutout', 'uJoinMode', 'uMiterLimit', 'uDash', 'uColor', 'uMultiply']),
    round: (!only || only === 'round') && linkProgram(device, roundSources.vertex, roundSources.fragment,
      ['aCorner', 'aNodePrevious', 'aNodePoint', 'aNodeNext', 'aNodeMeta'],
      [...viewUniforms, 'uHalfWidth', 'uAaRadius', 'uInnerCutout', 'uJoinMode', 'uRoundCap', 'uColor', 'uMultiply']),
    bevel: (!only || only === 'bevel') && linkProgram(device, bevelSources.vertex, bevelSources.fragment,
      ['aVertexId', 'aNodePrevious', 'aNodePoint', 'aNodeNext', 'aNodeMeta'],
      [...viewUniforms, 'uHalfWidth', 'uAaRadius', 'uInnerCutout', 'uColor', 'uMultiply']),
  });
}

export function resolveGpuStrokeRanges(resource, ownerIds = null, rangeProperty = 'ownerRanges', countProperty = 'instanceCount') {
  const instanceCount = Math.max(0, Math.trunc(Number(resource?.[countProperty]) || 0));
  if (!instanceCount) return Object.freeze([]);
  if (!Array.isArray(ownerIds)) return Object.freeze([Object.freeze({ first: 0, count: instanceCount })]);
  const seen = new Set();
  const ranges = [];
  for (const ownerId of ownerIds) {
    const key = String(ownerId || '');
    if (!key || seen.has(key)) continue;
    seen.add(key);
    const source = resource?.[rangeProperty]?.[key];
    const first = Math.max(0, Math.trunc(Number(source?.first) || 0));
    const count = Math.max(0, Math.trunc(Number(source?.count) || 0));
    if (!count || first >= instanceCount) continue;
    ranges.push(Object.freeze({ first, count: Math.min(count, instanceCount - first) }));
  }
  return Object.freeze(ranges);
}

function joinModeValue(join) {
  if (join === 'miter') return 1;
  if (join === 'bevel') return 2;
  return 0;
}

export function createGpuStrokeRenderer({ onError = null, onResourceReady = null, isInputActive = () => false, getUploadBudget = () => 4 * 1024 * 1024, aaRadiusPx = DEFAULT_AA_RADIUS_PX } = {}) {
  let uploadScheduler = null;
  const pendingUploads = new Map();
  const scheduleUpload = () => {
    if (uploadScheduler) {
      if (!pendingUploads.size) return;
      void uploadScheduler.enqueueUpload({ key: 'stroke-prepared', priority: 60, step: ({ byteBudget }) => {
        const before = uploadBytes;
        drainUploads(byteBudget);
        return { bytes: uploadBytes - before, done: !pendingUploads.size };
      } }).catch(error => { if (error.name !== 'AbortError') onError?.({ stage: 'stroke-upload', error }); });
      return;
    }
    if (pendingUploads.size) throw new Error('Prepared stroke uploads require the shared upload scheduler');
  };
  const cancelUploads = () => {
    if (gl && !gl.isContextLost?.()) for (const job of pendingUploads.values()) {
      for (const buffer of job.buffers) if (buffer) gl.deleteBuffer(buffer);
    }
    pendingUploads.clear();
  };
  function drainUploads(byteLimit = Infinity) {
    if (!gl || contextLost) return;
    if (isInputActive() || globalThis.document?.hidden || globalThis.navigator?.scheduling?.isInputPending?.()) return scheduleUpload();
    const started = performance.now();
    let budget = Math.min(byteLimit, Math.max(64 * 1024, Number(getUploadBudget()) || 4 * 1024 * 1024));
    const savedBuffer = gl.getParameter(gl.ARRAY_BUFFER_BINDING);
    try {
      for (const [key, job] of pendingUploads) {
        while (job.part < 2 && budget > 0 && performance.now() - started < 4) {
          const data = job.part === 0 ? job.geometry.instances : job.geometry.nodes;
          if (!data.byteLength) { job.part += 1; job.offset = 0; continue; }
          if (!job.buffers[job.part]) {
            job.buffers[job.part] = gl.createBuffer();
            if (!job.buffers[job.part]) throw new Error('stroke staging allocation failed');
            gl.bindBuffer(gl.ARRAY_BUFFER, job.buffers[job.part]);
            gl.bufferData(gl.ARRAY_BUFFER, data.byteLength, gl.STATIC_DRAW);
            return; // Allocation is an indivisible driver call; upload on a later step.
          } else gl.bindBuffer(gl.ARRAY_BUFFER, job.buffers[job.part]);
          const bytes = Math.min(budget, 512 * 1024, data.byteLength - job.offset);
          gl.bufferSubData(gl.ARRAY_BUFFER, job.offset, new Uint8Array(data.buffer, data.byteOffset + job.offset, bytes));
          job.offset += bytes;
          budget -= bytes;
          uploadBytes += bytes;
          if (job.offset === data.byteLength) { job.part += 1; job.offset = 0; }
        }
        if (job.part === 2) {
          const previous = resources.get(key);
          if (previous) deleteGpuResource(previous);
          const g = job.geometry;
          const resource = Object.freeze({ ...g, key, signature: job.signature, buffer: job.buffers[0], segmentBuffer: job.buffers[0], nodeBuffer: job.buffers[1], instanceCount: g.segmentCount, byteLength: g.instances.byteLength + g.nodes.byteLength });
          resources.set(key, resource);
          resourceBudget.track(key, resource.byteLength, job.priority);
          pendingUploads.delete(key);
          buildCount += 1;
          onResourceReady?.(key);
        }
        if (budget <= 0 || performance.now() - started >= 4) break;
      }
    } catch (error) {
      cancelUploads();
      onError?.({ stage: 'stroke-staging-upload', error });
    } finally {
      if (gl) gl.bindBuffer(gl.ARRAY_BUFFER, savedBuffer && gl.isBuffer(savedBuffer) ? savedBuffer : null);
      scheduleUpload();
    }
  }
  let device = null;
  let gl = null;
  let programs = null;
  let segmentCornerBuffer = null;
  let roundCornerBuffer = null;
  let bevelVertexBuffer = null;
  let instancing = null;
  const resources = new Map();
  const resourceBudget = createGpuResourceBudget();
  let contextLost = false;
  let gpuHealth = 'unchecked';
  let selfTestPassed = false;
  let selfTestCount = 0;
  let selfTestMs = 0;
  let selfTestFailureReason = '';
  let uploadBytes = 0;
  let buildCount = 0;
  let buildMs = 0;
  let drawCount = 0;
  let drawCallCount = 0;
  let drawMs = 0;
  let cacheHits = 0;
  let cacheMisses = 0;
  let failureCount = 0;
  let lastFailureStage = '';
  let topologyJoinCount = 0;
  let topologyCapCount = 0;
  let topologyClosedChainCount = 0;
  let topologyInvalidSegmentCount = 0;
  const aaRadius = Math.max(0.5, Math.min(2, Number(aaRadiusPx) || DEFAULT_AA_RADIUS_PX));

  const setDivisor = (location, value) => {
    if (location < 0) return;
    if (device.version === 2) gl.vertexAttribDivisor(location, value);
    else instancing.vertexAttribDivisorANGLE(location, value);
  };
  const drawInstanced = (count, instances) => {
    if (device.version === 2) gl.drawArraysInstanced(gl.TRIANGLES, 0, count, instances);
    else instancing.drawArraysInstancedANGLE(gl.TRIANGLES, 0, count, instances);
  };

  function captureState() {
    return {
      framebuffer: gl.getParameter(gl.FRAMEBUFFER_BINDING),
      viewport: Array.from(gl.getParameter(gl.VIEWPORT)),
      program: gl.getParameter(gl.CURRENT_PROGRAM),
      arrayBuffer: gl.getParameter(gl.ARRAY_BUFFER_BINDING),
      activeTexture: gl.getParameter(gl.ACTIVE_TEXTURE),
      texture: gl.getParameter(gl.TEXTURE_BINDING_2D),
      blend: gl.isEnabled(gl.BLEND),
      depth: gl.isEnabled(gl.DEPTH_TEST),
      cull: gl.isEnabled(gl.CULL_FACE),
      scissor: gl.isEnabled(gl.SCISSOR_TEST),
      colorMask: Array.from(gl.getParameter(gl.COLOR_WRITEMASK)),
      clearColor: Array.from(gl.getParameter(gl.COLOR_CLEAR_VALUE)),
    };
  }

  function restoreState(state) {
    const enabled = (cap, value) => value ? gl.enable(cap) : gl.disable(cap);
    gl.bindFramebuffer(gl.FRAMEBUFFER, state.framebuffer);
    gl.viewport(...state.viewport);
    gl.useProgram(state.program);
    gl.bindBuffer(gl.ARRAY_BUFFER, state.arrayBuffer);
    gl.activeTexture(state.activeTexture);
    gl.bindTexture(gl.TEXTURE_2D, state.texture);
    enabled(gl.BLEND, state.blend);
    enabled(gl.DEPTH_TEST, state.depth);
    enabled(gl.CULL_FACE, state.cull);
    enabled(gl.SCISSOR_TEST, state.scissor);
    gl.colorMask(...state.colorMask);
    gl.clearColor(...state.clearColor);
  }

  function setViewUniforms(programInfo, frameContext, worldOffset) {
    return setGpuViewUniforms(gl, { frameContext, worldOffset, getLocation: name => programInfo.uniforms[name] });
  }

  function bindStaticAttribute(buffer, location, size) {
    if (location < 0) return;
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.enableVertexAttribArray(location);
    gl.vertexAttribPointer(location, size, gl.FLOAT, false, 0, 0);
    setDivisor(location, 0);
  }

  function bindInstancedAttribute(buffer, location, size, stride, offset) {
    if (location < 0) return;
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.enableVertexAttribArray(location);
    gl.vertexAttribPointer(location, size, gl.FLOAT, false, stride, offset);
    setDivisor(location, 1);
  }

  function releaseAttributes(locations) {
    for (const location of locations) {
      if (location < 0) continue;
      setDivisor(location, 0);
      gl.disableVertexAttribArray(location);
    }
  }

  function bindSegmentGeometry(resource, firstInstance = 0) {
    const attributes = programs.segment.attributes;
    const stride = FLOATS_PER_INSTANCE * Float32Array.BYTES_PER_ELEMENT;
    const baseOffset = firstInstance * stride;
    bindStaticAttribute(segmentCornerBuffer, attributes.aCorner, 2);
    bindInstancedAttribute(resource.segmentBuffer, attributes.aPrevious, 2, stride, baseOffset);
    bindInstancedAttribute(resource.segmentBuffer, attributes.aSegment, 4, stride, baseOffset + 2 * Float32Array.BYTES_PER_ELEMENT);
    bindInstancedAttribute(resource.segmentBuffer, attributes.aNext, 2, stride, baseOffset + 6 * Float32Array.BYTES_PER_ELEMENT);
    bindInstancedAttribute(resource.segmentBuffer, attributes.aMeta, 2, stride, baseOffset + 8 * Float32Array.BYTES_PER_ELEMENT);
    return Object.values(attributes);
  }

  function bindNodeGeometry(programInfo, staticBuffer, staticAttributeName, resource, firstInstance = 0) {
    const attributes = programInfo.attributes;
    const stride = FLOATS_PER_NODE * Float32Array.BYTES_PER_ELEMENT;
    const baseOffset = firstInstance * stride;
    bindStaticAttribute(staticBuffer, attributes[staticAttributeName], 1 + Number(staticAttributeName === 'aCorner'));
    bindInstancedAttribute(resource.nodeBuffer, attributes.aNodePrevious, 2, stride, baseOffset);
    bindInstancedAttribute(resource.nodeBuffer, attributes.aNodePoint, 2, stride, baseOffset + 2 * Float32Array.BYTES_PER_ELEMENT);
    bindInstancedAttribute(resource.nodeBuffer, attributes.aNodeNext, 2, stride, baseOffset + 4 * Float32Array.BYTES_PER_ELEMENT);
    bindInstancedAttribute(resource.nodeBuffer, attributes.aNodeMeta, 2, stride, baseOffset + 6 * Float32Array.BYTES_PER_ELEMENT);
    return Object.values(attributes);
  }

  function applyCommonStyleUniforms(programInfo, style, frameContext) {
    const [red, green, blue] = parseGpuColor(style.color);
    const alpha = Math.max(0, Math.min(1, Number(style.alpha ?? 1)));
    const width = Math.max(0.25, Number(style.width || 1));
    if (programInfo.uniforms.uInnerCutout) gl.uniform1f(programInfo.uniforms.uInnerCutout, Number(style.innerCutout || 0) / 2);
    if (programInfo.uniforms.uHalfWidth) gl.uniform1f(programInfo.uniforms.uHalfWidth, width / 2);
    const smoothLines = globalThis.document?.documentElement?.dataset.smoothLines !== 'false';
    if (programInfo.uniforms.uAaRadius) gl.uniform1f(programInfo.uniforms.uAaRadius, style.antiAlias === false || !smoothLines ? 0 : aaRadius * Math.max(1, Number(frameContext.dpr || 1)));
    if (programInfo.uniforms.uColor) gl.uniform4f(programInfo.uniforms.uColor, red, green, blue, alpha);
    if (programInfo.uniforms.uMultiply) gl.uniform1i(programInfo.uniforms.uMultiply, style.blendMode === 'multiply' ? 1 : 0);
    return { width, alpha };
  }

  function drawSegments(resource, style, frameContext, ownerIds) {
    const programInfo = programs.segment;
    gl.useProgram(programInfo.program);
    applyCommonStyleUniforms(programInfo, style, frameContext);
    gl.uniform1i(programInfo.uniforms.uJoinMode, joinModeValue(style.join));
    gl.uniform1f(programInfo.uniforms.uMiterLimit, Math.max(1, Number(style.miterLimit) || 4));
    gl.uniform2f(programInfo.uniforms.uDash, Math.max(0, Number(style.dash?.[0]) || 0), Math.max(0, Number(style.dash?.[1]) || 0));
    const ranges = resolveGpuStrokeRanges(resource, ownerIds);
    let completedDraws = 0;
    for (const range of ranges) {
      const locations = bindSegmentGeometry(resource, range.first);
      for (const worldOffset of frameContext.worldOffsets || [0]) {
        setViewUniforms(programInfo, frameContext, worldOffset);
        drawInstanced(VERTICES_PER_SEGMENT, range.count);
        drawCallCount += 1;
        completedDraws += 1;
      }
      releaseAttributes(locations);
    }
    return completedDraws;
  }

  function drawRoundNodes(resource, style, frameContext, ownerIds) {
    if (!resource.nodeCount) return 0;
    const programInfo = programs.round;
    gl.useProgram(programInfo.program);
    applyCommonStyleUniforms(programInfo, style, frameContext);
    gl.uniform1i(programInfo.uniforms.uJoinMode, joinModeValue(style.join));
    gl.uniform1i(programInfo.uniforms.uRoundCap, style.cap === 'round' ? 1 : 0);
    const ranges = resolveGpuStrokeRanges(resource, ownerIds, 'ownerNodeRanges', 'nodeCount');
    let completedDraws = 0;
    for (const range of ranges) {
      const locations = bindNodeGeometry(programInfo, roundCornerBuffer, 'aCorner', resource, range.first);
      for (const worldOffset of frameContext.worldOffsets || [0]) {
        setViewUniforms(programInfo, frameContext, worldOffset);
        drawInstanced(VERTICES_PER_ROUND_NODE, range.count);
        drawCallCount += 1;
        completedDraws += 1;
      }
      releaseAttributes(locations);
    }
    return completedDraws;
  }

  function drawBevelJoins(resource, style, frameContext, ownerIds) {
    if (!resource.nodeCount || style.join === 'round') return 0;
    const programInfo = programs.bevel;
    gl.useProgram(programInfo.program);
    applyCommonStyleUniforms(programInfo, style, frameContext);
    const ranges = resolveGpuStrokeRanges(resource, ownerIds, 'ownerNodeRanges', 'nodeCount');
    let completedDraws = 0;
    for (const range of ranges) {
      const locations = bindNodeGeometry(programInfo, bevelVertexBuffer, 'aVertexId', resource, range.first);
      for (const worldOffset of frameContext.worldOffsets || [0]) {
        setViewUniforms(programInfo, frameContext, worldOffset);
        drawInstanced(VERTICES_PER_BEVEL_JOIN, range.count);
        drawCallCount += 1;
        completedDraws += 1;
      }
      releaseAttributes(locations);
    }
    return completedDraws;
  }

  function drawStyle(resource, style, frameContext, ownerIds = null) {
    let draws = drawSegments(resource, style, frameContext, ownerIds);
    // Solid join/cap patches would fill the gaps of densely segmented dashes.
    if (style.dash?.[0] > 0 && style.dash?.[1] > 0) return draws;
    draws += drawRoundNodes(resource, style, frameContext, ownerIds);
    draws += drawBevelJoins(resource, style, frameContext, ownerIds);
    return draws;
  }

  function createBuffer(data) {
    const buffer = gl.createBuffer();
    if (!buffer) throw new Error('stroke buffer allocation failed');
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    return buffer;
  }

  function createGpuResource(key, signature, geometry) {
    const segmentBuffer = createBuffer(geometry.instances);
    let nodeBuffer = null;
    try {
      nodeBuffer = geometry.nodeCount ? createBuffer(geometry.nodes) : null;
    } catch (error) {
      gl.deleteBuffer(segmentBuffer);
      throw error;
    }
    return Object.freeze({
      key,
      signature,
      buffer: segmentBuffer,
      segmentBuffer,
      nodeBuffer,
      instanceCount: geometry.segmentCount,
      nodeCount: geometry.nodeCount,
      joinCount: geometry.joinCount,
      capCount: geometry.capCount,
      closedChainCount: geometry.closedChainCount,
      invalidSegmentCount: geometry.invalidSegmentCount,
      ownerRanges: geometry.ownerRanges,
      ownerNodeRanges: geometry.ownerNodeRanges,
      byteLength: geometry.instances.byteLength + geometry.nodes.byteLength,
    });
  }

  function deleteGpuResource(resource) {
    if (!resource || !gl || gl.isContextLost?.()) return;
    if (resource.segmentBuffer) gl.deleteBuffer(resource.segmentBuffer);
    if (resource.nodeBuffer) gl.deleteBuffer(resource.nodeBuffer);
  }

  function runSelfTest() {
    const started = performance.now();
    selfTestCount += 1;
    let saved = null;
    let texture = null;
    let framebuffer = null;
    let resource = null;
    try {
      if (!gl || !programs || !segmentCornerBuffer || !roundCornerBuffer || !bevelVertexBuffer || !instancing) throw new Error('shader-program-unavailable');
      saved = captureState();
      texture = gl.createTexture();
      framebuffer = gl.createFramebuffer();
      if (!texture || !framebuffer) throw new Error('framebuffer-incomplete');
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 16, 16, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) throw new Error('framebuffer-incomplete');
      const geometry = buildGpuStrokeInstances(new Float32Array([-0.5, 0, 0, 0, 0, 0, 0.5, 0]));
      resource = createGpuResource('self-test', 'self-test', geometry);
      gl.viewport(0, 0, 16, 16);
      gl.disable(gl.BLEND);
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.disable(gl.SCISSOR_TEST);
      gl.colorMask(true, true, true, true);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
      drawStyle(resource, { color: '#ffffff', alpha: 1, width: 3, cap: 'round', join: 'round', dash: [0, 0], miterLimit: 4 }, {
        viewport: [16, 16], translate: [8, 8], scale: 500,
        rowX: [1, 0, 0], rowY: [0, 1, 0], rowZ: [0, 0, 1], flatCenter: [0, 0], mode: 1, worldOffsets: [0],
      });
      const pixels = new Uint8Array(16 * 16 * 4);
      gl.readPixels(0, 0, 16, 16, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      if (!pixels.some((value, index) => index % 4 === 3 && value > 0)) throw new Error('zero-alpha-output');
      selfTestPassed = true;
      gpuHealth = 'healthy';
      selfTestFailureReason = '';
      return true;
    } catch (error) {
      selfTestPassed = false;
      gpuHealth = contextLost ? 'context-lost' : 'unhealthy';
      selfTestFailureReason = error?.message || 'draw-failed';
      failureCount += 1;
      lastFailureStage = 'self-test';
      onError?.({ stage: 'gpu-stroke-self-test', error });
      return false;
    } finally {
      try {
        deleteGpuResource(resource);
        if (framebuffer) gl?.deleteFramebuffer?.(framebuffer);
        if (texture) gl?.deleteTexture?.(texture);
        if (saved) restoreState(saved);
      } catch (error) {
        selfTestPassed = false;
        gpuHealth = 'unhealthy';
        selfTestFailureReason = 'state-restore-failed';
        onError?.({ stage: 'gpu-stroke-self-test-restore', error });
      }
      selfTestMs += performance.now() - started;
    }
  }

  function initialize(nextDevice, preparedPrograms = null) {
    dispose();
    if (!isRenderDevice(nextDevice) || !nextDevice.capabilities.instancing) return false;
    device = nextDevice;
    gl = nextDevice.gl;
    instancing = nextDevice.version === 2 ? gl : gl.getExtension('ANGLE_instanced_arrays');
    try {
      programs = preparedPrograms || createPrograms(nextDevice);
      segmentCornerBuffer = createBuffer(SEGMENT_CORNERS);
      roundCornerBuffer = createBuffer(ROUND_NODE_CORNERS);
      bevelVertexBuffer = createBuffer(BEVEL_VERTEX_IDS);
      if (!instancing) throw new Error('stroke instancing unavailable');
      contextLost = false;
      return runSelfTest();
    } catch (error) {
      failureCount += 1;
      lastFailureStage = 'initialize';
      onError?.({ stage: 'gpu-stroke-initialize', error });
      return false;
    }
  }

  const resourceSignature = packet => `${String(packet?.key || '')}:${String(packet?.geometryRevision ?? 0)}:${String(packet?.lod || 'high')}`;

  function deleteResource(key) {
    const normalized = String(key || '');
    const resource = resources.get(normalized);
    if (!resource) return false;
    deleteGpuResource(resource);
    resources.delete(normalized);
    resourceBudget.remove(normalized);
    return true;
  }

  function ensureResource(packet) {
    const key = String(packet?.key || '');
    if (!key || !gl || !programs || contextLost || gpuHealth !== 'healthy') return { resource: null, reason: 'unavailable' };
    const signature = resourceSignature(packet);
    const previous = resources.get(key);
    if (previous?.signature === signature) {
      cacheHits += 1;
      resourceBudget.touch(key, packet?.priority);
      return { resource: previous, reason: '' };
    }
    if (packet.preparedGeometry || uploadScheduler) {
      const pending = pendingUploads.get(key);
      if (pending?.signature !== signature) {
        if (pending) for (const buffer of pending.buffers) if (buffer) gl.deleteBuffer(buffer);
        const geometry = packet.preparedGeometry || buildGpuStrokeInstances(packet.startsEnds, packet.chainPhase, packet.ownerRanges);
        pendingUploads.set(key, { signature, geometry, priority: packet.priority, part: 0, offset: 0, buffers: [null, null] });
      }
      scheduleUpload();
      return { resource: null, reason: 'upload-pending' };
    }
    const started = performance.now();
    let resource = null;
    try {
      const geometry = buildGpuStrokeInstances(packet.startsEnds, packet.chainPhase, packet.ownerRanges);
      if (!geometry.segmentCount) return { resource: null, reason: 'empty-geometry' };
      resource = createGpuResource(key, signature, geometry);
      if (previous) {
        deleteGpuResource(previous);
        resourceBudget.remove(key);
      }
      resources.set(key, resource);
      resourceBudget.track(key, resource.byteLength, packet?.priority);
      uploadBytes += resource.byteLength;
      buildCount += 1;
      cacheMisses += 1;
      topologyJoinCount += geometry.joinCount;
      topologyCapCount += geometry.capCount;
      topologyClosedChainCount += geometry.closedChainCount;
      topologyInvalidSegmentCount += geometry.invalidSegmentCount;
      return { resource, reason: '' };
    } catch (error) {
      deleteGpuResource(resource);
      failureCount += 1;
      lastFailureStage = 'buffer-build';
      onError?.({ stage: 'gpu-stroke-buffer-build', key, error });
      return { resource: null, reason: error?.message || 'buffer-build-failed' };
    } finally {
      buildMs += performance.now() - started;
    }
  }

  function drawBatches(batches = [], frameContext, { preparedOnly = false } = {}) {
    const started = performance.now();
    drawCount += 1;
    const renderedKeys = [];
    const missingKeys = [];
    const failures = [];
    if (!gl || !programs || contextLost || gpuHealth !== 'healthy' || gl.isContextLost?.() || !frameContext) {
      return Object.freeze({ succeeded: false, renderedKeys, missingKeys: batches.map(batch => String(batch?.key || '')).filter(Boolean), failures });
    }
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    for (const batch of batches) {
      const key = String(batch?.key || '');
      const { resource, reason } = preparedOnly
        ? { resource: resources.get(key)?.signature === resourceSignature(batch) ? resources.get(key) : null, reason: 'resource-not-prepared' }
        : ensureResource(batch);
      if (!resource) {
        if (key) missingKeys.push(key);
        failures.push({ key, reason });
        continue;
      }
      try {
        resourceBudget.touch(key, batch?.priority);
        const style = { ...gpuStrokeStyle(scaleInteractionStroke(batch.style || {}, frameContext), frameContext), blendMode: batch.blendMode || batch.style?.blendMode || 'normal' };
        applyGpuBlendMode(gl, style.blendMode);
        let completedDraws = 0;
        if (style.casing?.width > style.width && style.casing.alpha > 0) {
          completedDraws += drawStyle(resource, { ...style, ...style.casing, cap: style.cap, join: style.join, dash: style.dash, miterLimit: style.miterLimit }, frameContext, batch.ownerIds);
        }
        if (Number(style.alpha ?? 1) > 0 && Number(style.width || 0) > 0) completedDraws += drawStyle(resource, style, frameContext, batch.ownerIds);
        if (completedDraws > 0) {
          if (key) renderedKeys.push(key);
        } else {
          if (key) missingKeys.push(key);
          failures.push({ key, reason: Array.isArray(batch.ownerIds) ? 'owner-range-missing' : 'nothing-drawn' });
          failureCount += 1;
          lastFailureStage = 'draw-coverage';
        }
      } catch (error) {
        if (key) missingKeys.push(key);
        failures.push({ key, reason: error?.message || 'draw-failed' });
        failureCount += 1;
        lastFailureStage = 'draw';
        onError?.({ stage: 'gpu-stroke-draw', key, error });
      }
    }
    resetGpuNormalBlend(gl);
    drawMs += performance.now() - started;
    return Object.freeze({
      succeeded: missingKeys.length === 0,
      renderedKeys: Object.freeze(renderedKeys),
      missingKeys: Object.freeze(missingKeys),
      failures: Object.freeze(failures),
    });
  }

  function retain(keys = [], { protectedKeys = [] } = {}) {
    if (!gl || gl.isContextLost?.()) return [];
    const current = new Set(keys);
    for (const [key, job] of pendingUploads) if (!current.has(key)) {
      for (const buffer of job.buffers) if (buffer) gl.deleteBuffer(buffer);
      pendingUploads.delete(key);
    }
    const evicted = resourceBudget.reconcile({ active: keys, protected: protectedKeys });
    for (const key of evicted) deleteResource(key);
    return evicted;
  }

  function setByteBudget(value) {
    const evicted = resourceBudget.setByteBudget(value);
    for (const key of evicted) deleteResource(key);
    return resourceBudget.stats().byteBudget;
  }

  function handleContextLost() {
    cancelUploads();
    contextLost = true;
    gpuHealth = 'context-lost';
    selfTestPassed = false;
    resources.clear();
    resourceBudget.clear();
    programs = null;
    segmentCornerBuffer = null;
    roundCornerBuffer = null;
    bevelVertexBuffer = null;
    device = null;
    gl = null;
    instancing = null;
  }

  function dispose() {
    cancelUploads();
    if (gl && !gl.isContextLost?.()) {
      for (const resource of resources.values()) deleteGpuResource(resource);
      if (segmentCornerBuffer) gl.deleteBuffer(segmentCornerBuffer);
      if (roundCornerBuffer) gl.deleteBuffer(roundCornerBuffer);
      if (bevelVertexBuffer) gl.deleteBuffer(bevelVertexBuffer);
      for (const programInfo of Object.values(programs || {})) if (programInfo?.program) gl.deleteProgram(programInfo.program);
    }
    resources.clear();
    resourceBudget.clear();
    programs = null;
    segmentCornerBuffer = null;
    roundCornerBuffer = null;
    bevelVertexBuffer = null;
    device = null;
    gl = null;
    instancing = null;
    contextLost = false;
    gpuHealth = 'unchecked';
    selfTestPassed = false;
  }

  return Object.freeze({
    initialize,
    ensureResource,
    cancelPendingUploads: cancelUploads,
    drawBatches,
    setUploadScheduler: scheduler => { uploadScheduler = scheduler; },
    async initializeProgressively(nextDevice, enqueue) {
      const prepared = {};
      try {
        for (const name of ['segment', 'round', 'bevel']) {
          prepared[name] = await enqueue('stroke-' + name, () => createPrograms(nextDevice, name)[name]);
        }
        return await enqueue('stroke-ready', () => initialize(nextDevice, prepared));
      } catch (error) {
        for (const info of Object.values(prepared)) nextDevice.gl.deleteProgram(info.program);
        throw error;
      }
    },
    retain,
    releaseResource: deleteResource,
    setByteBudget,
    handleContextLost,
    dispose,
    runSelfTest,
    hasResource: key => resources.has(String(key || '')),
    hasPreparedResource: packet => resources.get(String(packet?.key || ''))?.signature === resourceSignature(packet),
    resourceByteLength: key => Number(resources.get(String(key || ''))?.byteLength || 0),
    isAvailable: () => !!gl && !!programs && !contextLost && gpuHealth === 'healthy' && selfTestPassed,
    stats: () => Object.freeze({
      resourceCount: resources.size,
      uploadBytes,
      buildCount,
      buildMs,
      drawCount,
      drawCallCount,
      drawMs,
      cacheHits,
      cacheMisses,
      failureCount,
      lastFailureStage,
      contextLost,
      gpuHealth,
      selfTestPassed,
      selfTestCount,
      selfTestMs,
      selfTestFailureReason,
      analyticAa: true,
      aaRadiusPx: aaRadius,
      connectedTopology: true,
      topologyJoinCount,
      topologyCapCount,
      topologyClosedChainCount,
      topologyInvalidSegmentCount,
      activeBufferBytes: [...resources.values()].reduce((sum, resource) => sum + resource.byteLength, 0),
      resourceBudget: resourceBudget.stats(),
    }),
  });
}
