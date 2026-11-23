export interface EngineStatus {
  state: 'playing' | 'paused' | 'stopped';
  frame: number;
  elapsed: number;
  entities: number;
  nodes: number;
  width: number;
  height: number;
  lua: string;
  undoCount: number;
  redoCount: number;
  device: string;
  api: string;
  drawCalls: number;
  triangles: number;
  fps: number;
}

export interface BuildStatus {
  host: {
    compiler: string;
    lua: boolean;
    luaBackend: string;
  };
  android: {
    gradleProjectConfigured: boolean;
    localSdkPresent: boolean;
    apkStatus: string;
  };
}

export interface ProjectSummary {
  slug: string;
  name: string;
  template: string;
  hasManifest: boolean;
  hasPack: boolean;
}

export interface SceneEntity {
  entity: number;
  name: string;
  parent: number;
  position: [number, number, number];
  rotation: [number, number, number, number];
  scale: [number, number, number];
  components: string[];
  componentData: Record<string, Record<string, unknown>>;
}

export interface VsPin {
  name: string;
  type: 'exec' | 'float' | 'vec3' | 'bool' | 'entity' | string;
  input: boolean;
}

export interface VsNode {
  id: number;
  type: string;
  name: string;
  x: number;
  y: number;
  inputs: VsPin[];
  outputs: VsPin[];
}

export interface VsEdge {
  fromNode: number;
  fromPin: number;
  toNode: number;
  toPin: number;
}

export interface VisualScriptGraph {
  nodes: VsNode[];
  edges: VsEdge[];
}

export interface LogLine {
  level: string;
  text: string;
}

export interface PackAssetEntry {
  id: number;
  name: string;
  kind: string;
  size: number;
}

export interface McpToolSchema {
  name: string;
  description: string;
  inputSchema: Record<string, unknown>;
}
