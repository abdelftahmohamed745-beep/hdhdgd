import type {
  BuildStatus,
  EngineStatus,
  LogLine,
  McpToolSchema,
  PackAssetEntry,
  ProjectSummary,
  SceneEntity,
  VisualScriptGraph,
} from './types';

async function request<T>(url: string, init?: RequestInit): Promise<T> {
  const res = await fetch(url, {
    headers: { 'Content-Type': 'application/json', ...(init?.headers || {}) },
    ...init,
  });
  const data = (await res.json()) as T & { error?: string };
  if (!res.ok) {
    throw new Error(data.error || `Request failed (${res.status})`);
  }
  return data;
}

export const api = {
  getStatus: () => request<{ engine: EngineStatus; build: BuildStatus }>('/api/status'),
  getTools: () => request<{ tools: McpToolSchema[] }>('/api/tools'),
  callMcp: (name: string, args: Record<string, unknown> = {}) =>
    request<{ result: unknown }>('/api/mcp/call', {
      method: 'POST',
      body: JSON.stringify({ name, arguments: args }),
    }),
  getProjects: () => request<{ projects: ProjectSummary[] }>('/api/projects'),
  newProject: (name: string, template: string) =>
    request<{ slug: string; name: string; template: string }>('/api/projects/new', {
      method: 'POST',
      body: JSON.stringify({ name, template }),
    }),
  openProject: (slug: string) =>
    request<{ slug: string; result: { name: string } }>('/api/projects/open', {
      method: 'POST',
      body: JSON.stringify({ slug }),
    }),
  saveProject: (slug?: string) =>
    request<{ result: { path: string } }>('/api/projects/save', {
      method: 'POST',
      body: JSON.stringify(slug ? { slug } : {}),
    }),
  exportProject: (destName: string) =>
    request<{ bundleName: string; files: string[] }>('/api/projects/export', {
      method: 'POST',
      body: JSON.stringify({ destName }),
    }),
  getScene: () =>
    request<{ scene: { active: string; scenes: string[] }; entities: SceneEntity[] }>(
      '/api/scene/entities'
    ),
  createEntity: (name: string, parent?: number) =>
    request<{ result: { entity: number; name: string } }>('/api/entity/create', {
      method: 'POST',
      body: JSON.stringify({ name, parent }),
    }),
  destroyEntity: (entity: number) =>
    request('/api/entity/destroy', {
      method: 'POST',
      body: JSON.stringify({ entity }),
    }),
  setTransform: (
    entity: number,
    patch: {
      position?: [number, number, number];
      rotation?: [number, number, number, number];
      scale?: [number, number, number];
    }
  ) =>
    request('/api/entity/transform', {
      method: 'POST',
      body: JSON.stringify({ entity, ...patch }),
    }),
  addComponent: (entity: number, component: string, data: Record<string, unknown> = {}) =>
    request('/api/component/add', {
      method: 'POST',
      body: JSON.stringify({ entity, component, data }),
    }),
  removeComponent: (entity: number, component: string) =>
    request('/api/component/remove', {
      method: 'POST',
      body: JSON.stringify({ entity, component }),
    }),
  savePrefab: (entity: number, prefabId: string) =>
    request('/api/scene/prefab/save', {
      method: 'POST',
      body: JSON.stringify({ entity, prefabId }),
    }),
  instantiatePrefab: (prefabId: string, parent?: number) =>
    request<{ result: { entity: number } }>('/api/scene/prefab/instantiate', {
      method: 'POST',
      body: JSON.stringify({ prefabId, parent }),
    }),
  playback: (action: 'play' | 'pause' | 'stop' | 'step') =>
    request('/api/playback', { method: 'POST', body: JSON.stringify({ action }) }),
  undo: () => request<{ result: { undone: boolean } }>('/api/editor/undo', { method: 'POST' }),
  redo: () => request<{ result: { redone: boolean } }>('/api/editor/redo', { method: 'POST' }),
  getVisualScript: () => request<{ graph: VisualScriptGraph }>('/api/visual-script'),
  addVsNode: (type: string, name: string, x: number, y: number) =>
    request<{ result: { id: number } }>('/api/visual-script/node', {
      method: 'POST',
      body: JSON.stringify({ type, name, x, y }),
    }),
  removeVsNode: (id: number) =>
    request(`/api/visual-script/node/${id}`, { method: 'DELETE' }),
  addVsEdge: (fromNode: number, fromPin: number, toNode: number, toPin: number) =>
    request('/api/visual-script/edge', {
      method: 'POST',
      body: JSON.stringify({ fromNode, fromPin, toNode, toPin }),
    }),
  executeVs: (entry: number, a: number, b: number, count = 3) =>
    request<{ result: { executed: boolean; trace: number[]; printed: number[] } }>(
      '/api/visual-script/execute',
      { method: 'POST', body: JSON.stringify({ entry, a, b, count }) }
    ),
  compileVsLua: () => request<{ lua: string }>('/api/visual-script/lua'),
  runLua: (source: string) =>
    request<{ result: { ran: boolean } }>('/api/script/run', {
      method: 'POST',
      body: JSON.stringify({ source }),
    }),
  attachLua: (entity: number, source: string) =>
    request('/api/script/attach', {
      method: 'POST',
      body: JSON.stringify({ entity, source }),
    }),
  getLogs: (offset = 0, limit = 80) =>
    request<{ logs: LogLine[] }>(`/api/logs?offset=${offset}&limit=${limit}`),
  getAssets: () => request<{ packEntries: PackAssetEntry[] }>('/api/assets'),
  importObj: (text: string) =>
    request<{ result: { ok: boolean; vertices: number; indices: number } }>(
      '/api/assets/import-obj',
      { method: 'POST', body: JSON.stringify({ text }) }
    ),
  buildPack: (slug: string) =>
    request<{ result: { entries: number; output: string } }>('/api/assets/build-pack', {
      method: 'POST',
      body: JSON.stringify({ slug }),
    }),
  runTests: () => request<{ result: { failures: number } }>('/api/tests/run', { method: 'POST' }),
};
