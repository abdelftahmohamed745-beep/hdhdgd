import React, { useEffect, useRef, useState } from 'react';
import { Move, RotateCw, Maximize2, Compass, Grid, Mountain } from 'lucide-react';
import type { EngineStatus, SceneEntity } from '../types';

export type GizmoMode = 'move' | 'rotate' | 'scale';

interface Viewport3DProps {
  entities: SceneEntity[];
  selectedEntityId: number | null;
  engineStatus: EngineStatus | null;
  terrainAmplitude: number;
  showGrid: boolean;
  onToggleGrid: () => void;
  onSelectEntity: (id: number | null) => void;
  onTransformCommit: (
    id: number,
    patch: {
      position?: [number, number, number];
      rotation?: [number, number, number, number];
      scale?: [number, number, number];
    }
  ) => void;
}

type Vec3 = [number, number, number];

function mat4Identity(): Float32Array {
  const m = new Float32Array(16);
  m[0] = m[5] = m[10] = m[15] = 1;
  return m;
}

function mat4Multiply(a: Float32Array, b: Float32Array): Float32Array {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; c++) {
    for (let r = 0; r < 4; r++) {
      out[c * 4 + r] =
        a[0 * 4 + r] * b[c * 4 + 0] +
        a[1 * 4 + r] * b[c * 4 + 1] +
        a[2 * 4 + r] * b[c * 4 + 2] +
        a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
  return out;
}

function mat4Perspective(fovRad: number, aspect: number, near: number, far: number): Float32Array {
  const f = 1.0 / Math.tan(fovRad / 2);
  const nf = 1 / (near - far);
  const out = new Float32Array(16);
  out[0] = f / aspect;
  out[5] = f;
  out[10] = (far + near) * nf;
  out[11] = -1;
  out[14] = 2 * far * near * nf;
  return out;
}

function vec3Sub(a: Vec3, b: Vec3): Vec3 {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

function vec3Cross(a: Vec3, b: Vec3): Vec3 {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

function vec3Dot(a: Vec3, b: Vec3): number {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

function vec3Norm(a: Vec3): Vec3 {
  const l = Math.hypot(a[0], a[1], a[2]) || 1;
  return [a[0] / l, a[1] / l, a[2] / l];
}

function mat4LookAt(eye: Vec3, target: Vec3, up: Vec3): Float32Array {
  const z = vec3Norm(vec3Sub(eye, target));
  const x = vec3Norm(vec3Cross(up, z));
  const y = vec3Cross(z, x);
  const out = mat4Identity();
  out[0] = x[0];
  out[4] = x[1];
  out[8] = x[2];
  out[1] = y[0];
  out[5] = y[1];
  out[9] = y[2];
  out[2] = z[0];
  out[6] = z[1];
  out[10] = z[2];
  out[12] = -vec3Dot(x, eye);
  out[13] = -vec3Dot(y, eye);
  out[14] = -vec3Dot(z, eye);
  return out;
}

function mat4TRS(pos: Vec3, q: [number, number, number, number], scale: Vec3): Float32Array {
  const [x, y, z, w] = q;
  const x2 = x + x,
    y2 = y + y,
    z2 = z + z;
  const xx = x * x2,
    xy = x * y2,
    xz = x * z2;
  const yy = y * y2,
    yz = y * z2,
    zz = z * z2;
  const wx = w * x2,
    wy = w * y2,
    wz = w * z2;
  const out = new Float32Array(16);
  out[0] = (1 - (yy + zz)) * scale[0];
  out[1] = (xy + wz) * scale[0];
  out[2] = (xz - wy) * scale[0];
  out[4] = (xy - wz) * scale[1];
  out[5] = (1 - (xx + zz)) * scale[1];
  out[6] = (yz + wx) * scale[1];
  out[8] = (xz + wy) * scale[2];
  out[9] = (yz - wx) * scale[2];
  out[10] = (1 - (xx + yy)) * scale[2];
  out[12] = pos[0];
  out[13] = pos[1];
  out[14] = pos[2];
  out[15] = 1;
  return out;
}

function quatMultiply(
  a: [number, number, number, number],
  b: [number, number, number, number]
): [number, number, number, number] {
  const [ax, ay, az, aw] = a;
  const [bx, by, bz, bw] = b;
  return [
    aw * bx + ax * bw + ay * bz - az * by,
    aw * by - ax * bz + ay * bw + az * bx,
    aw * bz + ax * by - ay * bx + az * bw,
    aw * bw - ax * bx - ay * by - az * bz,
  ];
}

function quatAxisAngle(axis: Vec3, rad: number): [number, number, number, number] {
  const s = Math.sin(rad * 0.5);
  return [axis[0] * s, axis[1] * s, axis[2] * s, Math.cos(rad * 0.5)];
}

function projectToScreen(
  p: Vec3,
  vp: Float32Array,
  width: number,
  height: number
): { x: number; y: number; z: number; visible: boolean } {
  const cx = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12];
  const cy = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13];
  const cz = vp[2] * p[0] + vp[6] * p[1] + vp[10] * p[2] + vp[14];
  const cw = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15];
  if (cw <= 0.001) return { x: 0, y: 0, z: 0, visible: false };
  const ndcX = cx / cw;
  const ndcY = cy / cw;
  return {
    x: (ndcX * 0.5 + 0.5) * width,
    y: (1 - (ndcY * 0.5 + 0.5)) * height,
    z: cz / cw,
    visible: ndcX >= -1.2 && ndcX <= 1.2 && ndcY >= -1.2 && ndcY <= 1.2,
  };
}

function createBoxGeometry(): { pos: Float32Array; norm: Float32Array } {
  const faces: Array<{ n: Vec3; v: Vec3[] }> = [
    { n: [0, 0, 1], v: [[-0.5, -0.5, 0.5], [0.5, -0.5, 0.5], [0.5, 0.5, 0.5], [-0.5, 0.5, 0.5]] },
    { n: [0, 0, -1], v: [[0.5, -0.5, -0.5], [-0.5, -0.5, -0.5], [-0.5, 0.5, -0.5], [0.5, 0.5, -0.5]] },
    { n: [1, 0, 0], v: [[0.5, -0.5, 0.5], [0.5, -0.5, -0.5], [0.5, 0.5, -0.5], [0.5, 0.5, 0.5]] },
    { n: [-1, 0, 0], v: [[-0.5, -0.5, -0.5], [-0.5, -0.5, 0.5], [-0.5, 0.5, 0.5], [-0.5, 0.5, -0.5]] },
    { n: [0, 1, 0], v: [[-0.5, 0.5, 0.5], [0.5, 0.5, 0.5], [0.5, 0.5, -0.5], [-0.5, 0.5, -0.5]] },
    { n: [0, -1, 0], v: [[-0.5, -0.5, -0.5], [0.5, -0.5, -0.5], [0.5, -0.5, 0.5], [-0.5, -0.5, 0.5]] },
  ];
  const pos: number[] = [];
  const norm: number[] = [];
  for (const f of faces) {
    const idx = [0, 1, 2, 0, 2, 3];
    for (const i of idx) {
      pos.push(f.v[i][0], f.v[i][1], f.v[i][2]);
      norm.push(f.n[0], f.n[1], f.n[2]);
    }
  }
  return { pos: new Float32Array(pos), norm: new Float32Array(norm) };
}

function createSphereGeometry(lat = 12, lon = 18): { pos: Float32Array; norm: Float32Array } {
  const pos: number[] = [];
  const norm: number[] = [];
  const r = 0.5;
  const pt = (i: number, j: number): Vec3 => {
    const theta = (i / lat) * Math.PI;
    const phi = (j / lon) * Math.PI * 2;
    return [
      r * Math.sin(theta) * Math.cos(phi),
      r * Math.cos(theta),
      r * Math.sin(theta) * Math.sin(phi),
    ];
  };
  for (let i = 0; i < lat; i++) {
    for (let j = 0; j < lon; j++) {
      const a = pt(i, j);
      const b = pt(i + 1, j);
      const c = pt(i + 1, j + 1);
      const d = pt(i, j + 1);
      for (const v of [a, b, c, a, c, d]) {
        pos.push(v[0], v[1], v[2]);
        const n = vec3Norm(v);
        norm.push(n[0], n[1], n[2]);
      }
    }
  }
  return { pos: new Float32Array(pos), norm: new Float32Array(norm) };
}

function createTerrainMesh(amp: number): { pos: Float32Array; norm: Float32Array } {
  const N = 24;
  const size = 18;
  const pos: number[] = [];
  const norm: number[] = [];
  const h = (x: number, z: number) =>
    amp <= 0.01
      ? -0.02
      : (Math.sin(x * 0.45) * Math.cos(z * 0.45) + 0.5 * Math.sin(x * 0.9 - z * 0.6)) * amp * 0.45 -
        0.3;
  const normalAt = (x: number, z: number): Vec3 => {
    const e = 0.2;
    const dx = h(x + e, z) - h(x - e, z);
    const dz = h(x, z + e) - h(x, z - e);
    return vec3Norm([-dx, 2 * e, -dz]);
  };
  for (let ix = 0; ix < N; ix++) {
    for (let iz = 0; iz < N; iz++) {
      const x0 = ((ix / N) - 0.5) * size;
      const x1 = (((ix + 1) / N) - 0.5) * size;
      const z0 = ((iz / N) - 0.5) * size;
      const z1 = (((iz + 1) / N) - 0.5) * size;
      const pts: Array<[number, number]> = [
        [x0, z0],
        [x0, z1],
        [x1, z1],
        [x0, z0],
        [x1, z1],
        [x1, z0],
      ];
      for (const [px, pz] of pts) {
        pos.push(px, h(px, pz), pz);
        const n = normalAt(px, pz);
        norm.push(n[0], n[1], n[2]);
      }
    }
  }
  return { pos: new Float32Array(pos), norm: new Float32Array(norm) };
}

export const Viewport3D: React.FC<Viewport3DProps> = ({
  entities,
  selectedEntityId,
  engineStatus,
  terrainAmplitude,
  showGrid,
  onToggleGrid,
  onSelectEntity,
  onTransformCommit,
}) => {
  const glCanvasRef = useRef<HTMLCanvasElement | null>(null);
  const hudCanvasRef = useRef<HTMLCanvasElement | null>(null);

  const [gizmoMode, setGizmoMode] = useState<GizmoMode>('move');
  const [contextLost, setContextLost] = useState(false);
  const [showTerrain, setShowTerrain] = useState(false);

  const cameraRef = useRef({
    yaw: 0.72,
    pitch: 0.44,
    distance: 9.5,
    target: [0, 0.5, 0] as Vec3,
  });

  const dragPreviewRef = useRef<{
    id: number;
    position: [number, number, number];
    rotation: [number, number, number, number];
    scale: [number, number, number];
  } | null>(null);

  const entitiesRef = useRef(entities);
  entitiesRef.current = entities;
  const selectedRef = useRef(selectedEntityId);
  selectedRef.current = selectedEntityId;
  const gizmoModeRef = useRef(gizmoMode);
  gizmoModeRef.current = gizmoMode;
  const showGridRef = useRef(showGrid);
  showGridRef.current = showGrid;
  const showTerrainRef = useRef(showTerrain);
  showTerrainRef.current = showTerrain;
  const terrainAmpRef = useRef(terrainAmplitude);
  terrainAmpRef.current = terrainAmplitude;
  const stateRef = useRef(engineStatus?.state ?? 'stopped');
  stateRef.current = engineStatus?.state ?? 'stopped';

  const vpMatrixRef = useRef<Float32Array>(mat4Identity());
  const pointersRef = useRef<Map<number, { x: number; y: number }>>(new Map());
  const gestureRef = useRef<{
    kind: 'none' | 'orbit' | 'pan' | 'gizmo';
    axis?: 'x' | 'y' | 'z';
    startX: number;
    startY: number;
    moved: boolean;
    lastPinchDist: number;
    origPos: [number, number, number];
    origRot: [number, number, number, number];
    origScale: [number, number, number];
  }>({
    kind: 'none',
    startX: 0,
    startY: 0,
    moved: false,
    lastPinchDist: 0,
    origPos: [0, 0, 0],
    origRot: [0, 0, 0, 1],
    origScale: [1, 1, 1],
  });

  useEffect(() => {
    const glCanvas = glCanvasRef.current;
    const hudCanvas = hudCanvasRef.current;
    if (!glCanvas || !hudCanvas) return;

    const gl = glCanvas.getContext('webgl2', { antialias: true, alpha: false });
    const ctx2d = hudCanvas.getContext('2d');

    const handleLost = (e: Event) => {
      e.preventDefault();
      setContextLost(true);
    };
    const handleRestored = () => {
      setContextLost(false);
    };
    glCanvas.addEventListener('webglcontextlost', handleLost);
    glCanvas.addEventListener('webglcontextrestored', handleRestored);

    let program: WebGLProgram | null = null;
    let boxVao: WebGLVertexArrayObject | null = null;
    let boxCount = 0;
    let sphereVao: WebGLVertexArrayObject | null = null;
    let sphereCount = 0;
    let gridVao: WebGLVertexArrayObject | null = null;
    let gridCount = 0;
    let terrainVao: WebGLVertexArrayObject | null = null;
    let terrainPosBuf: WebGLBuffer | null = null;
    let terrainNormBuf: WebGLBuffer | null = null;
    let terrainCount = 0;
    let lastBuiltAmp = -1;

    let uMvp: WebGLUniformLocation | null = null;
    let uModel: WebGLUniformLocation | null = null;
    let uColor: WebGLUniformLocation | null = null;
    let uUnlit: WebGLUniformLocation | null = null;

    if (gl) {
      const vsSource = `#version 300 es
        layout(location=0) in vec3 aPos;
        layout(location=1) in vec3 aNormal;
        uniform mat4 uMvp;
        uniform mat4 uModel;
        out vec3 vNormal;
        void main() {
          gl_Position = uMvp * vec4(aPos, 1.0);
          vNormal = mat3(uModel) * aNormal;
        }`;
      const fsSource = `#version 300 es
        precision mediump float;
        in vec3 vNormal;
        uniform vec3 uColor;
        uniform float uUnlit;
        out vec4 outColor;
        void main() {
          if (uUnlit > 0.5) {
            outColor = vec4(uColor, 1.0);
            return;
          }
          vec3 n = normalize(vNormal + vec3(0.0, 0.0001, 0.0));
          vec3 lightDir = normalize(vec3(0.65, 1.0, 0.45));
          float diff = max(dot(n, lightDir), 0.0);
          float rim = pow(max(1.0 - abs(n.y), 0.0), 2.0) * 0.16;
          vec3 lit = uColor * (0.28 + diff * 0.72) + vec3(0.18, 0.35, 0.5) * rim;
          outColor = vec4(lit, 1.0);
        }`;

      const compile = (type: number, src: string) => {
        const s = gl.createShader(type);
        if (!s) return null;
        gl.shaderSource(s, src);
        gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) return null;
        return s;
      };
      const vs = compile(gl.VERTEX_SHADER, vsSource);
      const fs = compile(gl.FRAGMENT_SHADER, fsSource);
      if (vs && fs) {
        program = gl.createProgram();
        if (program) {
          gl.attachShader(program, vs);
          gl.attachShader(program, fs);
          gl.linkProgram(program);
          uMvp = gl.getUniformLocation(program, 'uMvp');
          uModel = gl.getUniformLocation(program, 'uModel');
          uColor = gl.getUniformLocation(program, 'uColor');
          uUnlit = gl.getUniformLocation(program, 'uUnlit');
        }
      }

      const makeMeshVao = (pos: Float32Array, norm: Float32Array) => {
        const vao = gl.createVertexArray();
        gl.bindVertexArray(vao);
        const pb = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, pb);
        gl.bufferData(gl.ARRAY_BUFFER, pos, gl.STATIC_DRAW);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);

        const nb = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, nb);
        gl.bufferData(gl.ARRAY_BUFFER, norm, gl.STATIC_DRAW);
        gl.enableVertexAttribArray(1);
        gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
        gl.bindVertexArray(null);
        return vao;
      };

      const box = createBoxGeometry();
      boxVao = makeMeshVao(box.pos, box.norm);
      boxCount = box.pos.length / 3;

      const sphere = createSphereGeometry();
      sphereVao = makeMeshVao(sphere.pos, sphere.norm);
      sphereCount = sphere.pos.length / 3;

      // Grid lines (-10..10)
      const gridLines: number[] = [];
      for (let i = -10; i <= 10; i++) {
        gridLines.push(i, 0, -10, i, 0, 10);
        gridLines.push(-10, 0, i, 10, 0, i);
      }
      const gridNorm = new Float32Array(gridLines.length).fill(0);
      gridVao = makeMeshVao(new Float32Array(gridLines), gridNorm);
      gridCount = gridLines.length / 3;

      // Terrain dynamic VAO
      terrainVao = gl.createVertexArray();
      gl.bindVertexArray(terrainVao);
      terrainPosBuf = gl.createBuffer();
      terrainNormBuf = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, terrainPosBuf);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, terrainNormBuf);
      gl.enableVertexAttribArray(1);
      gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
      gl.bindVertexArray(null);
    }

    let raf = 0;
    const renderFrame = (nowMs: number) => {
      const parent = glCanvas.parentElement;
      const w = parent ? parent.clientWidth : 640;
      const h = parent ? parent.clientHeight : 420;
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const pw = Math.max(1, Math.floor(w * dpr));
      const ph = Math.max(1, Math.floor(h * dpr));

      if (glCanvas.width !== pw || glCanvas.height !== ph) {
        glCanvas.width = pw;
        glCanvas.height = ph;
        hudCanvas.width = pw;
        hudCanvas.height = ph;
      }

      const cam = cameraRef.current;
      const eye: Vec3 = [
        cam.target[0] + cam.distance * Math.cos(cam.pitch) * Math.sin(cam.yaw),
        cam.target[1] + cam.distance * Math.sin(cam.pitch),
        cam.target[2] + cam.distance * Math.cos(cam.pitch) * Math.cos(cam.yaw),
      ];
      const proj = mat4Perspective((50 * Math.PI) / 180, w / Math.max(h, 1), 0.1, 140);
      const view = mat4LookAt(eye, cam.target, [0, 1, 0]);
      const vp = mat4Multiply(proj, view);
      vpMatrixRef.current = vp;

      const playingBob = stateRef.current === 'playing' ? Math.sin(nowMs * 0.004) * 0.08 : 0;

      if (gl && program && !gl.isContextLost()) {
        gl.viewport(0, 0, pw, ph);
        gl.enable(gl.DEPTH_TEST);
        gl.clearColor(0.059, 0.067, 0.082, 1.0);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.useProgram(program);

        if (showGridRef.current && gridVao) {
          const ident = mat4Identity();
          gl.bindVertexArray(gridVao);
          gl.uniformMatrix4fv(uModel, false, ident);
          gl.uniformMatrix4fv(uMvp, false, vp);
          gl.uniform3f(uColor, 0.17, 0.2, 0.26);
          gl.uniform1f(uUnlit, 1.0);
          gl.drawArrays(gl.LINES, 0, gridCount);
        }

        if (showTerrainRef.current && terrainVao && terrainPosBuf && terrainNormBuf) {
          if (Math.abs(lastBuiltAmp - terrainAmpRef.current) > 0.01) {
            const tm = createTerrainMesh(terrainAmpRef.current);
            gl.bindBuffer(gl.ARRAY_BUFFER, terrainPosBuf);
            gl.bufferData(gl.ARRAY_BUFFER, tm.pos, gl.DYNAMIC_DRAW);
            gl.bindBuffer(gl.ARRAY_BUFFER, terrainNormBuf);
            gl.bufferData(gl.ARRAY_BUFFER, tm.norm, gl.DYNAMIC_DRAW);
            terrainCount = tm.pos.length / 3;
            lastBuiltAmp = terrainAmpRef.current;
          }
          const ident = mat4Identity();
          gl.bindVertexArray(terrainVao);
          gl.uniformMatrix4fv(uModel, false, ident);
          gl.uniformMatrix4fv(uMvp, false, vp);
          gl.uniform3f(uColor, 0.18, 0.29, 0.26);
          gl.uniform1f(uUnlit, 0.0);
          gl.drawArrays(gl.TRIANGLES, 0, terrainCount);
        }

        for (const ent of entitiesRef.current) {
          const preview =
            dragPreviewRef.current && dragPreviewRef.current.id === ent.entity
              ? dragPreviewRef.current
              : null;
          const pos: Vec3 = preview
            ? [...preview.position]
            : [ent.position[0], ent.position[1], ent.position[2]];
          if (stateRef.current === 'playing' && ent.components.includes('RigidBody')) {
            pos[1] += playingBob;
          }
          const rot = preview ? preview.rotation : ent.rotation;
          const scl = preview ? preview.scale : ent.scale;
          const model = mat4TRS(pos, rot, scl);
          const mvp = mat4Multiply(vp, model);
          const isSel = selectedRef.current === ent.entity;

          const isSphere = ent.components.includes('SphereCollider');
          const isLight = ent.components.includes('Light');
          const isCamera = ent.components.includes('Camera');

          gl.uniformMatrix4fv(uModel, false, model);
          gl.uniformMatrix4fv(uMvp, false, mvp);
          gl.uniform1f(uUnlit, isLight ? 1.0 : 0.0);

          if (isSel) gl.uniform3f(uColor, 0.22, 0.74, 0.97);
          else if (isLight) gl.uniform3f(uColor, 0.98, 0.8, 0.25);
          else if (isCamera) gl.uniform3f(uColor, 0.65, 0.55, 0.98);
          else if (ent.components.includes('RigidBody')) gl.uniform3f(uColor, 0.3, 0.72, 0.58);
          else gl.uniform3f(uColor, 0.56, 0.62, 0.72);

          if (isSphere && sphereVao) {
            gl.bindVertexArray(sphereVao);
            gl.drawArrays(gl.TRIANGLES, 0, sphereCount);
          } else if (boxVao) {
            gl.bindVertexArray(boxVao);
            gl.drawArrays(gl.TRIANGLES, 0, boxCount);
          }
        }
        gl.bindVertexArray(null);
      }

      // Draw crisp 2D gizmos, entity labels & selection brackets
      if (ctx2d) {
        ctx2d.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx2d.clearRect(0, 0, w, h);

        if (!gl || gl.isContextLost()) {
          ctx2d.fillStyle = '#0F1115';
          ctx2d.fillRect(0, 0, w, h);
        }

        for (const ent of entitiesRef.current) {
          const preview =
            dragPreviewRef.current && dragPreviewRef.current.id === ent.entity
              ? dragPreviewRef.current
              : null;
          const p = preview ? preview.position : ent.position;
          const scr = projectToScreen(p, vp, w, h);
          if (!scr.visible) continue;
          const isSel = selectedRef.current === ent.entity;

          if (!gl || gl.isContextLost()) {
            ctx2d.strokeStyle = isSel ? '#38BDF8' : '#64748B';
            ctx2d.lineWidth = isSel ? 2 : 1.25;
            ctx2d.strokeRect(scr.x - 16, scr.y - 16, 32, 32);
          }

          if (isSel) {
            // Draw interactive X / Y / Z gizmo handles
            const axes: Array<{ key: 'x' | 'y' | 'z'; dir: Vec3; color: string }> = [
              { key: 'x', dir: [1.35, 0, 0], color: '#F87171' },
              { key: 'y', dir: [0, 1.35, 0], color: '#34D399' },
              { key: 'z', dir: [0, 0, 1.35], color: '#38BDF8' },
            ];
            for (const ax of axes) {
              const tip = projectToScreen(
                [p[0] + ax.dir[0], p[1] + ax.dir[1], p[2] + ax.dir[2]],
                vp,
                w,
                h
              );
              if (!tip.visible) continue;
              ctx2d.strokeStyle = ax.color;
              ctx2d.lineWidth = 2.5;
              ctx2d.beginPath();
              ctx2d.moveTo(scr.x, scr.y);
              ctx2d.lineTo(tip.x, tip.y);
              ctx2d.stroke();

              ctx2d.fillStyle = ax.color;
              ctx2d.beginPath();
              if (gizmoModeRef.current === 'scale') {
                ctx2d.fillRect(tip.x - 5, tip.y - 5, 10, 10);
              } else {
                ctx2d.arc(tip.x, tip.y, 6, 0, Math.PI * 2);
                ctx2d.fill();
              }
            }
          }

          ctx2d.font = '500 11px "JetBrains Mono", monospace';
          ctx2d.fillStyle = isSel ? '#38BDF8' : 'rgba(226,232,240,0.72)';
          ctx2d.fillText(ent.name, scr.x + 10, scr.y - 10);
        }
      }

      raf = requestAnimationFrame(renderFrame);
    };

    raf = requestAnimationFrame(renderFrame);
    return () => {
      cancelAnimationFrame(raf);
      glCanvas.removeEventListener('webglcontextlost', handleLost);
      glCanvas.removeEventListener('webglcontextrestored', handleRestored);
    };
  }, []);

  const getLocalCoords = (clientX: number, clientY: number) => {
    const rect = hudCanvasRef.current?.getBoundingClientRect();
    if (!rect) return { x: 0, y: 0, w: 1, h: 1 };
    return { x: clientX - rect.left, y: clientY - rect.top, w: rect.width, h: rect.height };
  };

  const handlePointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    e.currentTarget.setPointerCapture(e.pointerId);
    const { x, y, w, h } = getLocalCoords(e.clientX, e.clientY);
    pointersRef.current.set(e.pointerId, { x, y });

    if (pointersRef.current.size === 2) {
      const pts = Array.from(pointersRef.current.values());
      gestureRef.current.kind = 'pan';
      gestureRef.current.lastPinchDist = Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
      gestureRef.current.startX = (pts[0].x + pts[1].x) * 0.5;
      gestureRef.current.startY = (pts[0].y + pts[1].y) * 0.5;
      return;
    }

    // Check gizmo handles of selected entity
    const sel = entities.find((item) => item.entity === selectedEntityId);
    if (sel) {
      const axes: Array<{ key: 'x' | 'y' | 'z'; dir: Vec3 }> = [
        { key: 'x', dir: [1.35, 0, 0] },
        { key: 'y', dir: [0, 1.35, 0] },
        { key: 'z', dir: [0, 0, 1.35] },
      ];
      for (const ax of axes) {
        const tip = projectToScreen(
          [
            sel.position[0] + ax.dir[0],
            sel.position[1] + ax.dir[1],
            sel.position[2] + ax.dir[2],
          ],
          vpMatrixRef.current,
          w,
          h
        );
        if (tip.visible && Math.hypot(x - tip.x, y - tip.y) <= 22) {
          gestureRef.current = {
            kind: 'gizmo',
            axis: ax.key,
            startX: x,
            startY: y,
            moved: false,
            lastPinchDist: 0,
            origPos: [...sel.position],
            origRot: [...sel.rotation],
            origScale: [...sel.scale],
          };
          dragPreviewRef.current = {
            id: sel.entity,
            position: [...sel.position],
            rotation: [...sel.rotation],
            scale: [...sel.scale],
          };
          return;
        }
      }
    }

    gestureRef.current = {
      kind: e.button === 2 || e.shiftKey ? 'pan' : 'orbit',
      startX: x,
      startY: y,
      moved: false,
      lastPinchDist: 0,
      origPos: [0, 0, 0],
      origRot: [0, 0, 0, 1],
      origScale: [1, 1, 1],
    };
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (!pointersRef.current.has(e.pointerId)) return;
    const { x, y } = getLocalCoords(e.clientX, e.clientY);
    pointersRef.current.set(e.pointerId, { x, y });

    if (pointersRef.current.size === 2) {
      const pts = Array.from(pointersRef.current.values());
      const dist = Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
      const cx = (pts[0].x + pts[1].x) * 0.5;
      const cy = (pts[0].y + pts[1].y) * 0.5;
      const dDist = dist - gestureRef.current.lastPinchDist;
      const dx = cx - gestureRef.current.startX;
      const dy = cy - gestureRef.current.startY;
      cameraRef.current.distance = Math.max(
        2,
        Math.min(40, cameraRef.current.distance - dDist * 0.03)
      );
      const yaw = cameraRef.current.yaw;
      cameraRef.current.target[0] -= (dx * Math.cos(yaw)) * 0.015;
      cameraRef.current.target[2] += (dx * Math.sin(yaw)) * 0.015;
      cameraRef.current.target[1] += dy * 0.015;
      gestureRef.current.lastPinchDist = dist;
      gestureRef.current.startX = cx;
      gestureRef.current.startY = cy;
      gestureRef.current.moved = true;
      return;
    }

    const dx = x - gestureRef.current.startX;
    const dy = y - gestureRef.current.startY;
    if (Math.hypot(dx, dy) > 4) gestureRef.current.moved = true;

    if (gestureRef.current.kind === 'gizmo' && dragPreviewRef.current && gestureRef.current.axis) {
      const ax = gestureRef.current.axis;
      const idx = ax === 'x' ? 0 : ax === 'y' ? 1 : 2;
      const delta = (ax === 'y' ? -dy : dx + dy * 0.25) * 0.025;
      if (gizmoMode === 'move') {
        const next: [number, number, number] = [...gestureRef.current.origPos];
        next[idx] = Number((next[idx] + delta).toFixed(3));
        dragPreviewRef.current.position = next;
      } else if (gizmoMode === 'scale') {
        const next: [number, number, number] = [...gestureRef.current.origScale];
        next[idx] = Math.max(0.1, Number((next[idx] + delta * 0.6).toFixed(3)));
        dragPreviewRef.current.scale = next;
      } else {
        const axisVec: Vec3 = ax === 'x' ? [1, 0, 0] : ax === 'y' ? [0, 1, 0] : [0, 0, 1];
        const dq = quatAxisAngle(axisVec, delta * 1.2);
        dragPreviewRef.current.rotation = quatMultiply(gestureRef.current.origRot, dq);
      }
      return;
    }

    if (gestureRef.current.kind === 'orbit') {
      cameraRef.current.yaw -= dx * 0.008;
      cameraRef.current.pitch = Math.max(
        -1.35,
        Math.min(1.45, cameraRef.current.pitch + dy * 0.008)
      );
      gestureRef.current.startX = x;
      gestureRef.current.startY = y;
    } else if (gestureRef.current.kind === 'pan') {
      const yaw = cameraRef.current.yaw;
      cameraRef.current.target[0] -= dx * Math.cos(yaw) * 0.015;
      cameraRef.current.target[2] += dx * Math.sin(yaw) * 0.015;
      cameraRef.current.target[1] += dy * 0.015;
      gestureRef.current.startX = x;
      gestureRef.current.startY = y;
    }
  };

  const handlePointerUp = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const { x, y, w, h } = getLocalCoords(e.clientX, e.clientY);
    pointersRef.current.delete(e.pointerId);

    if (gestureRef.current.kind === 'gizmo' && dragPreviewRef.current) {
      const committed = dragPreviewRef.current;
      dragPreviewRef.current = null;
      if (gestureRef.current.moved) {
        onTransformCommit(committed.id, {
          position: committed.position,
          rotation: committed.rotation,
          scale: committed.scale,
        });
      }
      gestureRef.current.kind = 'none';
      return;
    }

    if (!gestureRef.current.moved && gestureRef.current.kind !== 'none') {
      // Screen-space raycast pick closest entity within 28px radius
      let closestId: number | null = null;
      let closestDist = 30;
      for (const ent of entities) {
        const scr = projectToScreen(ent.position, vpMatrixRef.current, w, h);
        if (!scr.visible) continue;
        const d = Math.hypot(x - scr.x, y - scr.y);
        if (d < closestDist) {
          closestDist = d;
          closestId = ent.entity;
        }
      }
      onSelectEntity(closestId);
    }
    gestureRef.current.kind = 'none';
  };

  const handleWheel = (e: React.WheelEvent<HTMLCanvasElement>) => {
    cameraRef.current.distance = Math.max(
      2,
      Math.min(40, cameraRef.current.distance + e.deltaY * 0.01)
    );
  };

  return (
    <div className="relative h-full w-full overflow-hidden bg-[#0F1115] select-none">
      <canvas ref={glCanvasRef} className="absolute inset-0 h-full w-full block" />
      <canvas
        ref={hudCanvasRef}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        onWheel={handleWheel}
        onContextMenu={(e) => e.preventDefault()}
        className="absolute inset-0 h-full w-full block touch-none cursor-crosshair"
      />

      {/* Minimal top-left Gizmo & Viewport Controls */}
      <div className="absolute top-3 left-3 flex flex-wrap items-center gap-1 rounded-md border border-white/[0.08] bg-[#171A21]/90 p-1 backdrop-blur-sm">
        <button
          type="button"
          onClick={() => setGizmoMode('move')}
          title="Move Gizmo"
          className={`flex h-9 w-9 items-center justify-center rounded transition ${
            gizmoMode === 'move'
              ? 'bg-[#38BDF8] text-[#0F1115]'
              : 'text-slate-300 hover:bg-white/[0.06]'
          }`}
        >
          <Move className="h-4 w-4" />
        </button>
        <button
          type="button"
          onClick={() => setGizmoMode('rotate')}
          title="Rotate Gizmo"
          className={`flex h-9 w-9 items-center justify-center rounded transition ${
            gizmoMode === 'rotate'
              ? 'bg-[#38BDF8] text-[#0F1115]'
              : 'text-slate-300 hover:bg-white/[0.06]'
          }`}
        >
          <RotateCw className="h-4 w-4" />
        </button>
        <button
          type="button"
          onClick={() => setGizmoMode('scale')}
          title="Scale Gizmo"
          className={`flex h-9 w-9 items-center justify-center rounded transition ${
            gizmoMode === 'scale'
              ? 'bg-[#38BDF8] text-[#0F1115]'
              : 'text-slate-300 hover:bg-white/[0.06]'
          }`}
        >
          <Maximize2 className="h-4 w-4" />
        </button>
        <div className="mx-0.5 h-5 w-px bg-white/[0.08]" />
        <button
          type="button"
          onClick={onToggleGrid}
          title="Toggle Grid"
          className={`flex h-9 w-9 items-center justify-center rounded transition ${
            showGrid ? 'bg-white/10 text-slate-100' : 'text-slate-400 hover:bg-white/[0.06]'
          }`}
        >
          <Grid className="h-4 w-4" />
        </button>
        <button
          type="button"
          onClick={() => setShowTerrain((v) => !v)}
          title="Toggle Procedural Terrain Mesh"
          className={`flex h-9 w-9 items-center justify-center rounded transition ${
            showTerrain ? 'bg-white/10 text-slate-100' : 'text-slate-400 hover:bg-white/[0.06]'
          }`}
        >
          <Mountain className="h-4 w-4" />
        </button>
        <button
          type="button"
          onClick={() => {
            cameraRef.current = { yaw: 0.72, pitch: 0.44, distance: 9.5, target: [0, 0.5, 0] };
          }}
          title="Reset Camera"
          className="flex h-9 w-9 items-center justify-center rounded text-slate-300 hover:bg-white/[0.06]"
        >
          <Compass className="h-4 w-4" />
        </button>
      </div>

      {/* Numeric Viewport Readout */}
      <div className="pointer-events-none absolute top-3 right-3 rounded-md border border-white/[0.08] bg-[#171A21]/90 px-2.5 py-1.5 font-mono text-[11px] text-slate-300 tabular-nums">
        <span>{engineStatus?.state ?? 'stopped'}</span>
        <span className="mx-2 text-slate-600">|</span>
        <span>F:{engineStatus?.frame ?? 0}</span>
        <span className="mx-2 text-slate-600">|</span>
        <span>E:{entities.length}</span>
      </div>

      {contextLost && (
        <div className="absolute bottom-3 left-3 rounded border border-amber-400/30 bg-[#171A21]/95 px-3 py-1.5 text-xs text-amber-300">
          WebGL context lost — 2D wireframe projection active
        </div>
      )}
    </div>
  );
};
