import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import express, { type NextFunction, type Request, type Response } from 'express';

const WORKSPACE_ROOT = path.resolve(process.cwd());
const PROJECTS_ROOT = path.resolve(WORKSPACE_ROOT, 'projects');
const EXPORTS_ROOT = path.resolve(PROJECTS_ROOT, '_exports');
const PORT = Number(process.env.PORT || 3000);

fs.mkdirSync(PROJECTS_ROOT, { recursive: true });
fs.mkdirSync(EXPORTS_ROOT, { recursive: true });

function sanitizeSlug(raw: unknown, fallback = 'untitled'): string {
  const cleaned = String(raw ?? '')
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_-]+/g, '_')
    .replace(/^_+|_+$/g, '');
  if (!cleaned || cleaned === '.' || cleaned === '..' || cleaned.startsWith('_')) {
    return fallback;
  }
  return cleaned.slice(0, 48);
}

function confinePath(baseDir: string, ...segments: string[]): string {
  const resolved = path.resolve(baseDir, ...segments);
  const prefix = baseDir.endsWith(path.sep) ? baseDir : baseDir + path.sep;
  if (!resolved.startsWith(prefix)) {
    throw new Error('Path escapes confined workspace root');
  }
  return resolved;
}

function resolveMcpBinary(): string {
  const candidates = [
    path.resolve(WORKSPACE_ROOT, 'build/debug-none/nimbus_mcp_server'),
    path.resolve(WORKSPACE_ROOT, 'build/release-none/nimbus_mcp_server'),
  ];
  for (const bin of candidates) {
    if (fs.existsSync(bin)) return bin;
  }
  throw new Error('nimbus_mcp_server binary not built; run make all first');
}

interface PendingRpc {
  resolve: (value: unknown) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
}

class McpBridge {
  private proc: ChildProcessWithoutNullStreams | null = null;
  private nextId = 1;
  private pending = new Map<number, PendingRpc>();
  private queue: Promise<void> = Promise.resolve();
  private initialized = false;
  private availableTools = new Set<string>();
  private toolSchemas: Array<{ name: string; description: string; inputSchema: unknown }> = [];

  private startProcess(): void {
    if (this.proc && !this.proc.killed) return;
    const bin = resolveMcpBinary();
    const proc = spawn(bin, ['--workspace', PROJECTS_ROOT], {
      cwd: WORKSPACE_ROOT,
      stdio: ['pipe', 'pipe', 'pipe'],
      shell: false,
    });
    this.proc = proc;
    this.initialized = false;

    const rl = readline.createInterface({ input: proc.stdout });
    rl.on('line', (line) => {
      if (!line.trim()) return;
      try {
        const msg = JSON.parse(line) as {
          id?: number | null;
          result?: unknown;
          error?: { code?: number; message?: string };
        };
        if (typeof msg.id === 'number') {
          const item = this.pending.get(msg.id);
          if (item) {
            clearTimeout(item.timer);
            this.pending.delete(msg.id);
            if (msg.error) {
              item.reject(new Error(msg.error.message || 'MCP error'));
            } else {
              item.resolve(msg.result);
            }
          }
        }
      } catch {
        // Ignore non-JSON stdout noise
      }
    });

    proc.stderr.on('data', () => {
      // Captured inside C++ logs tool; consume stderr to prevent pipe backpressure
    });

    proc.on('exit', () => {
      this.proc = null;
      this.initialized = false;
      for (const [id, item] of this.pending.entries()) {
        clearTimeout(item.timer);
        item.reject(new Error('MCP server exited unexpectedly'));
        this.pending.delete(id);
      }
    });
  }

  private sendRaw(method: string, params: Record<string, unknown>, timeoutMs = 15000): Promise<unknown> {
    this.startProcess();
    const proc = this.proc;
    if (!proc) return Promise.reject(new Error('Failed to spawn nimbus_mcp_server'));
    const id = this.nextId++;
    const payload = JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n';
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`MCP call timed out (${method})`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      proc.stdin.write(payload, (err) => {
        if (err) {
          clearTimeout(timer);
          this.pending.delete(id);
          reject(err);
        }
      });
    });
  }

  async ensureInitialized(): Promise<void> {
    if (this.initialized && this.proc && !this.proc.killed) return;
    await this.sendRaw('initialize', { protocolVersion: '2025-06-18', capabilities: {} });
    if (this.proc && !this.proc.killed) {
      this.proc.stdin.write(
        JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized', params: {} }) + '\n'
      );
    }
    const listRes = (await this.sendRaw('tools/list', {})) as {
      tools?: Array<{ name: string; description: string; inputSchema: unknown }>;
    };
    this.toolSchemas = Array.isArray(listRes?.tools) ? listRes.tools : [];
    this.availableTools = new Set(this.toolSchemas.map((t) => t.name));
    this.initialized = true;
  }

  async listTools() {
    return this.runExclusive(async () => {
      await this.ensureInitialized();
      return this.toolSchemas;
    });
  }

  async callTool<T = unknown>(
    name: string,
    args: Record<string, unknown> = {},
    timeoutMs = 15000
  ): Promise<T> {
    return this.runExclusive(async () => {
      await this.ensureInitialized();
      if (!this.availableTools.has(name)) {
        throw new Error(`Unknown MCP tool: ${name}`);
      }
      const res = (await this.sendRaw('tools/call', { name, arguments: args }, timeoutMs)) as {
        content?: Array<{ type: string; text: string }>;
        isError?: boolean;
      };
      const text = res?.content?.[0]?.text ?? '';
      if (res?.isError) {
        throw new Error(text || `Tool ${name} failed`);
      }
      const parsed = JSON.parse(text) as { ok?: boolean; result?: T };
      if (!parsed.ok) {
        throw new Error(`Tool ${name} returned not ok`);
      }
      return parsed.result as T;
    });
  }

  private runExclusive<T>(fn: () => Promise<T>): Promise<T> {
    const next = this.queue.then(fn, fn);
    this.queue = next.then(
      () => undefined,
      () => undefined
    );
    return next;
  }

  shutdown(): void {
    if (this.proc && !this.proc.killed) {
      this.proc.kill('SIGTERM');
    }
  }
}

const mcp = new McpBridge();

function bootstrapStarterProject(): Promise<void> {
  const starterDir = confinePath(PROJECTS_ROOT, 'starter_arena');
  const projectFile = path.join(starterDir, 'project.json');
  if (!fs.existsSync(projectFile)) {
    return mcp
      .callTool('project_new', {
        path: starterDir,
        name: 'Starter Arena',
        template: '3d_action',
      })
      .then(() => undefined)
      .catch(() => undefined);
  }
  return mcp
    .callTool('project_open', { path: starterDir })
    .then(() => undefined)
    .catch(() => undefined);
}

const app = express();
app.use(express.json({ limit: '2mb' }));

// Error-safe async handler wrapper
const route =
  (fn: (req: Request, res: Response) => Promise<void>) =>
  (req: Request, res: Response, next: NextFunction) => {
    fn(req, res).catch(next);
  };

app.get(
  '/api/health',
  route(async (_req, res) => {
    res.json({ ok: true, workspace: PROJECTS_ROOT });
  })
);

app.get(
  '/api/status',
  route(async (_req, res) => {
    const [engine, build] = await Promise.all([
      mcp.callTool('engine_status'),
      mcp.callTool('build_status'),
    ]);
    res.json({ engine, build });
  })
);

app.get(
  '/api/tools',
  route(async (_req, res) => {
    const tools = await mcp.listTools();
    res.json({ tools });
  })
);

app.post(
  '/api/mcp/call',
  route(async (req, res) => {
    const name = typeof req.body?.name === 'string' ? req.body.name : '';
    const args =
      req.body?.arguments && typeof req.body.arguments === 'object' && !Array.isArray(req.body.arguments)
        ? (req.body.arguments as Record<string, unknown>)
        : {};
    const timeout = name === 'test_run' ? 60000 : 15000;
    const result = await mcp.callTool(name, args, timeout);
    res.json({ result });
  })
);

app.get(
  '/api/projects',
  route(async (_req, res) => {
    const entries = fs.readdirSync(PROJECTS_ROOT, { withFileTypes: true });
    const projects = entries
      .filter((e) => e.isDirectory() && !e.name.startsWith('_'))
      .map((e) => {
        const dir = path.join(PROJECTS_ROOT, e.name);
        const manifestPath = path.join(dir, 'project.json');
        let name = e.name;
        let template = 'custom';
        if (fs.existsSync(manifestPath)) {
          try {
            const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8')) as {
              name?: string;
              template?: string;
            };
            if (typeof manifest.name === 'string' && manifest.name) name = manifest.name;
            if (typeof manifest.template === 'string') template = manifest.template;
          } catch {
            // Keep folder fallback
          }
        }
        return {
          slug: e.name,
          name,
          template,
          hasManifest: fs.existsSync(manifestPath),
          hasPack: fs.existsSync(path.join(dir, 'assets.nbpk')),
        };
      });
    res.json({ projects });
  })
);

app.post(
  '/api/projects/new',
  route(async (req, res) => {
    const displayName = String(req.body?.name || 'New Game').trim().slice(0, 64) || 'New Game';
    const slug = sanitizeSlug(req.body?.slug || displayName, 'game_' + Date.now());
    const allowedTemplates = new Set(['empty', '3d_action', 'ai_sandbox']);
    const template = allowedTemplates.has(String(req.body?.template))
      ? String(req.body.template)
      : 'empty';
    const targetDir = confinePath(PROJECTS_ROOT, slug);
    const result = await mcp.callTool('project_new', {
      path: targetDir,
      name: displayName,
      template,
    });
    res.json({ slug, name: displayName, template, result });
  })
);

app.post(
  '/api/projects/open',
  route(async (req, res) => {
    const slug = sanitizeSlug(req.body?.slug, '');
    if (!slug) {
      res.status(400).json({ error: 'Invalid project slug' });
      return;
    }
    const targetDir = confinePath(PROJECTS_ROOT, slug);
    if (!fs.existsSync(targetDir)) {
      res.status(404).json({ error: 'Project directory not found' });
      return;
    }
    const result = await mcp.callTool('project_open', { path: targetDir });
    res.json({ slug, result });
  })
);

app.post(
  '/api/projects/save',
  route(async (req, res) => {
    const slug = req.body?.slug ? sanitizeSlug(req.body.slug, '') : '';
    const args = slug ? { path: confinePath(PROJECTS_ROOT, slug) } : {};
    const result = await mcp.callTool('project_save', args);
    res.json({ result });
  })
);

app.post(
  '/api/projects/export',
  route(async (req, res) => {
    const bundleName = sanitizeSlug(req.body?.destName || 'nimbus_export', 'nimbus_export');
    const destDir = confinePath(EXPORTS_ROOT, bundleName);
    const result = await mcp.callTool('project_export', { dest: destDir });
    const files = fs.existsSync(destDir)
      ? fs.readdirSync(destDir, { recursive: true }).map((f) => String(f))
      : [];
    res.json({ bundleName, files, result });
  })
);

app.get(
  '/api/scene/entities',
  route(async (_req, res) => {
    const [entities, sceneInfo] = await Promise.all([
      mcp.callTool<
        Array<{
          entity: number;
          name: string;
          parent: number;
          position: [number, number, number];
          rotation: [number, number, number, number];
          scale: [number, number, number];
          components: string[];
        }>
      >('scene_list_entities'),
      mcp.callTool<{ active: string; scenes: string[] }>('scene_list_scenes'),
    ]);

    const enriched = await Promise.all(
      entities.map(async (ent) => {
        try {
          const componentData = await mcp.callTool<Record<string, unknown>>('component_list', {
            entity: ent.entity,
          });
          return { ...ent, componentData };
        } catch {
          return { ...ent, componentData: {} };
        }
      })
    );
    res.json({ scene: sceneInfo, entities: enriched });
  })
);

app.post(
  '/api/scene/prefab/save',
  route(async (req, res) => {
    const entity = Number(req.body?.entity);
    const prefabId = String(req.body?.prefabId || '').trim().slice(0, 64);
    if (!Number.isInteger(entity) || entity <= 0 || !prefabId) {
      res.status(400).json({ error: 'entity and prefabId are required' });
      return;
    }
    const result = await mcp.callTool('scene_save_prefab', { entity, prefabId });
    res.json({ result });
  })
);

app.post(
  '/api/scene/prefab/instantiate',
  route(async (req, res) => {
    const prefabId = String(req.body?.prefabId || '').trim().slice(0, 64);
    if (!prefabId) {
      res.status(400).json({ error: 'prefabId is required' });
      return;
    }
    const args: Record<string, unknown> = { prefabId };
    if (Number.isInteger(Number(req.body?.parent)) && Number(req.body.parent) > 0) {
      args.parent = Number(req.body.parent);
    }
    const result = await mcp.callTool('scene_instantiate_prefab', args);
    res.json({ result });
  })
);

app.post(
  '/api/entity/create',
  route(async (req, res) => {
    const name = String(req.body?.name || 'Entity').trim().slice(0, 64) || 'Entity';
    const args: Record<string, unknown> = { name };
    if (Number.isInteger(Number(req.body?.parent)) && Number(req.body.parent) > 0) {
      args.parent = Number(req.body.parent);
    }
    const result = await mcp.callTool('entity_create', args);
    res.json({ result });
  })
);

app.post(
  '/api/entity/destroy',
  route(async (req, res) => {
    const entity = Number(req.body?.entity);
    if (!Number.isInteger(entity) || entity <= 0) {
      res.status(400).json({ error: 'Valid entity id required' });
      return;
    }
    const result = await mcp.callTool('entity_destroy', { entity });
    res.json({ result });
  })
);

app.post(
  '/api/entity/transform',
  route(async (req, res) => {
    const entity = Number(req.body?.entity);
    if (!Number.isInteger(entity) || entity <= 0) {
      res.status(400).json({ error: 'Valid entity id required' });
      return;
    }
    const args: Record<string, unknown> = { entity };
    if (Array.isArray(req.body?.position) && req.body.position.length === 3) {
      args.position = req.body.position.map(Number);
    }
    if (Array.isArray(req.body?.rotation) && req.body.rotation.length === 4) {
      args.rotation = req.body.rotation.map(Number);
    }
    if (Array.isArray(req.body?.scale) && req.body.scale.length === 3) {
      args.scale = req.body.scale.map(Number);
    }
    const result = await mcp.callTool('entity_set_transform', args);
    res.json({ result });
  })
);

app.post(
  '/api/component/add',
  route(async (req, res) => {
    const entity = Number(req.body?.entity);
    const component = String(req.body?.component || '').trim();
    const data =
      req.body?.data && typeof req.body.data === 'object' && !Array.isArray(req.body.data)
        ? (req.body.data as Record<string, unknown>)
        : {};
    if (!Number.isInteger(entity) || entity <= 0 || !component) {
      res.status(400).json({ error: 'entity and component are required' });
      return;
    }
    const result = await mcp.callTool('component_add', { entity, component, data });
    res.json({ result });
  })
);

app.post(
  '/api/component/remove',
  route(async (req, res) => {
    const entity = Number(req.body?.entity);
    const component = String(req.body?.component || '').trim();
    if (!Number.isInteger(entity) || entity <= 0 || !component) {
      res.status(400).json({ error: 'entity and component are required' });
      return;
    }
    const result = await mcp.callTool('component_remove', { entity, component });
    res.json({ result });
  })
);

app.post(
  '/api/playback',
  route(async (req, res) => {
    const action = String(req.body?.action || '');
    if (!['play', 'pause', 'stop', 'step'].includes(action)) {
      res.status(400).json({ error: 'Invalid playback action' });
      return;
    }
    const result = await mcp.callTool(action);
    res.json({ result });
  })
);

app.post(
  '/api/editor/undo',
  route(async (_req, res) => {
    const result = await mcp.callTool('editor_undo');
    res.json({ result });
  })
);

app.post(
  '/api/editor/redo',
  route(async (_req, res) => {
    const result = await mcp.callTool('editor_redo');
    res.json({ result });
  })
);

app.get(
  '/api/visual-script',
  route(async (_req, res) => {
    const graph = await mcp.callTool('visual_script_list');
    res.json({ graph });
  })
);

app.post(
  '/api/visual-script/node',
  route(async (req, res) => {
    const type = String(req.body?.type || 'add');
    const name = String(req.body?.name || type).slice(0, 48);
    const x = Number.isFinite(Number(req.body?.x)) ? Number(req.body.x) : 80;
    const y = Number.isFinite(Number(req.body?.y)) ? Number(req.body.y) : 80;
    const result = await mcp.callTool('visual_script_add_node', { type, name, x, y });
    res.json({ result });
  })
);

app.delete(
  '/api/visual-script/node/:id',
  route(async (req, res) => {
    const id = Number(req.params.id);
    if (!Number.isInteger(id) || id <= 0) {
      res.status(400).json({ error: 'Invalid node id' });
      return;
    }
    const result = await mcp.callTool('visual_script_remove_node', { id });
    res.json({ result });
  })
);

app.post(
  '/api/visual-script/edge',
  route(async (req, res) => {
    const result = await mcp.callTool('visual_script_add_edge', {
      fromNode: Number(req.body?.fromNode),
      fromPin: Number(req.body?.fromPin),
      toNode: Number(req.body?.toNode),
      toPin: Number(req.body?.toPin),
    });
    res.json({ result });
  })
);

app.post(
  '/api/visual-script/execute',
  route(async (req, res) => {
    const result = await mcp.callTool('visual_script_execute', {
      entry: Number(req.body?.entry || 1),
      a: Number(req.body?.a ?? 0),
      b: Number(req.body?.b ?? 0),
      count: Number(req.body?.count ?? 3),
    });
    res.json({ result });
  })
);

app.get(
  '/api/visual-script/lua',
  route(async (_req, res) => {
    const lua = await mcp.callTool<string>('visual_script_to_lua');
    res.json({ lua });
  })
);

app.post(
  '/api/script/run',
  route(async (req, res) => {
    const source = String(req.body?.source || '');
    const result = await mcp.callTool('script_run', { source });
    res.json({ result });
  })
);

app.post(
  '/api/script/attach',
  route(async (req, res) => {
    const entity = Number(req.body?.entity);
    const source = String(req.body?.source || '');
    if (!Number.isInteger(entity) || entity <= 0) {
      res.status(400).json({ error: 'Valid entity required' });
      return;
    }
    const result = await mcp.callTool('script_attach', { entity, source });
    res.json({ result });
  })
);

app.get(
  '/api/logs',
  route(async (req, res) => {
    const offset = Math.max(0, Number(req.query.offset || 0));
    const limit = Math.min(200, Math.max(1, Number(req.query.limit || 80)));
    const logs = await mcp.callTool('logs', { offset, limit });
    res.json({ logs });
  })
);

app.get(
  '/api/assets',
  route(async (_req, res) => {
    const packEntries = await mcp.callTool('asset_list');
    res.json({ packEntries });
  })
);

app.post(
  '/api/assets/import-obj',
  route(async (req, res) => {
    const text = String(req.body?.text || '');
    if (!text.trim()) {
      res.status(400).json({ error: 'OBJ text is required' });
      return;
    }
    const result = await mcp.callTool('asset_import_obj', { text });
    res.json({ result });
  })
);

app.post(
  '/api/assets/build-pack',
  route(async (req, res) => {
    const slug = sanitizeSlug(req.body?.slug || 'starter_arena', 'starter_arena');
    const sourceDir = confinePath(PROJECTS_ROOT, slug);
    const outputFile = confinePath(PROJECTS_ROOT, slug, 'assets.nbpk');
    const result = await mcp.callTool('asset_build_pack', { sourceDir, outputFile });
    res.json({ result });
  })
);

app.post(
  '/api/tests/run',
  route(async (_req, res) => {
    const result = await mcp.callTool('test_run', {}, 60000);
    res.json({ result });
  })
);

app.use((err: Error, _req: Request, res: Response, _next: NextFunction) => {
  res.status(500).json({ error: err.message || 'Internal studio error' });
});

async function main() {
  await bootstrapStarterProject();

  if (process.env.NODE_ENV !== 'production') {
    const { createServer: createViteServer } = await import('vite');
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: 'spa',
    });
    app.use(vite.middlewares);
  } else {
    const distDir = path.resolve(WORKSPACE_ROOT, 'dist');
    app.use(express.static(distDir));
    app.get('/{*splat}', (_req, res) => {
      res.sendFile(path.join(distDir, 'index.html'));
    });
  }

  const server = app.listen(PORT, '0.0.0.0', () => {
    console.log(`Nimbus Studio running on http://0.0.0.0:${PORT}`);
  });

  const cleanExit = () => {
    mcp.shutdown();
    server.close(() => process.exit(0));
  };
  process.on('SIGINT', cleanExit);
  process.on('SIGTERM', cleanExit);
}

main().catch((err) => {
  console.error('Failed to start Nimbus Studio:', err);
  process.exit(1);
});
