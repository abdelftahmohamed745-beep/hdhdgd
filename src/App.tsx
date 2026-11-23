import React, { useCallback, useEffect, useState } from 'react';
import {
  Box,
  Code2,
  Download,
  FolderOpen,
  Layers,
  Package,
  Pause,
  Play,
  Plus,
  Redo2,
  Save,
  SkipForward,
  Sliders,
  Square,
  Terminal,
  Trash2,
  Undo2,
  X,
} from 'lucide-react';
import { api } from './api';
import { InspectorPanel } from './components/InspectorPanel';
import { Viewport3D } from './components/Viewport3D';
import { VisualScriptEditor } from './components/VisualScriptEditor';
import { usePWAInstall } from './hooks/usePWAInstall';
import type {
  BuildStatus,
  EngineStatus,
  LogLine,
  PackAssetEntry,
  ProjectSummary,
  SceneEntity,
  VisualScriptGraph,
} from './types';

type CenterTab = 'scene' | 'graph' | 'assets' | 'console';
type MobileDrawer = 'none' | 'hierarchy' | 'inspector' | 'assets' | 'console';

const SAMPLE_OBJ = `# Nimbus Sample Crate\nv -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv 0.5 0.5 0.5\nv -0.5 0.5 0.5\nf 1 2 3\nf 1 3 4`;

export default function App() {
  const { isInstallable, isInstalled, isIOS, install } = usePWAInstall();

  const [centerTab, setCenterTab] = useState<CenterTab>('scene');
  const [mobileDrawer, setMobileDrawer] = useState<MobileDrawer>('none');
  const [showProjectModal, setShowProjectModal] = useState(false);

  const [engineStatus, setEngineStatus] = useState<EngineStatus | null>(null);
  const [buildStatus, setBuildStatus] = useState<BuildStatus | null>(null);
  const [projects, setProjects] = useState<ProjectSummary[]>([]);
  const [activeSlug, setActiveSlug] = useState('starter_arena');
  const [entities, setEntities] = useState<SceneEntity[]>([]);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [vsGraph, setVsGraph] = useState<VisualScriptGraph>({ nodes: [], edges: [] });
  const [logs, setLogs] = useState<LogLine[]>([]);
  const [packEntries, setPackEntries] = useState<PackAssetEntry[]>([]);

  const [showGrid, setShowGrid] = useState(true);
  const [terrainAmp, setTerrainAmp] = useState(1.2);
  const [newEntityName, setNewEntityName] = useState('');
  const [newProjName, setNewProjName] = useState('My Nimbus Game');
  const [newProjTemplate, setNewProjTemplate] = useState<'empty' | '3d_action' | 'ai_sandbox'>('3d_action');
  const [prefabToSpawn, setPrefabToSpawn] = useState('PropPrefab');
  const [objText, setObjText] = useState(SAMPLE_OBJ);
  const [objResult, setObjResult] = useState<string | null>(null);
  const [testResult, setTestResult] = useState<string | null>(null);
  const [exportInfo, setExportInfo] = useState<string | null>(null);
  const [bannerError, setBannerError] = useState<string | null>(null);
  const [showIOSPrompt, setShowIOSPrompt] = useState(false);

  const refreshAll = useCallback(async () => {
    try {
      const [statusRes, sceneRes, graphRes, projRes, logRes, assetRes] = await Promise.all([
        api.getStatus(),
        api.getScene(),
        api.getVisualScript(),
        api.getProjects(),
        api.getLogs(0, 80),
        api.getAssets(),
      ]);
      setEngineStatus(statusRes.engine);
      setBuildStatus(statusRes.build);
      setEntities(sceneRes.entities);
      setVsGraph(graphRes.graph);
      setProjects(projRes.projects);
      setLogs(logRes.logs);
      setPackEntries(assetRes.packEntries);
      setBannerError(null);
    } catch (err) {
      setBannerError(err instanceof Error ? err.message : 'Unable to reach Nimbus Engine backend');
    }
  }, []);

  useEffect(() => {
    void refreshAll();
    const timer = setInterval(() => {
      void api.getStatus().then((r) => setEngineStatus(r.engine)).catch(() => {});
    }, 1500);
    return () => clearInterval(timer);
  }, [refreshAll]);

  const selectedEntity = entities.find((e) => e.entity === selectedId) ?? null;

  const handlePlayback = async (action: 'play' | 'pause' | 'stop' | 'step') => {
    try {
      await api.playback(action);
      await refreshAll();
    } catch (err) {
      setBannerError(err instanceof Error ? err.message : 'Playback error');
    }
  };

  const handleCreateEntity = async (e?: React.FormEvent) => {
    e?.preventDefault();
    const name = newEntityName.trim() || `Entity_${entities.length + 1}`;
    try {
      const res = await api.createEntity(name);
      setNewEntityName('');
      setSelectedId(res.result.entity);
      await refreshAll();
    } catch (err) {
      setBannerError(err instanceof Error ? err.message : 'Failed to create entity');
    }
  };

  const handleDeleteEntity = async (id: number) => {
    try {
      await api.destroyEntity(id);
      if (selectedId === id) setSelectedId(null);
      await refreshAll();
    } catch (err) {
      setBannerError(err instanceof Error ? err.message : 'Failed to delete entity');
    }
  };

  const handleNewProject = async () => {
    try {
      const res = await api.newProject(newProjName, newProjTemplate);
      setActiveSlug(res.slug);
      setShowProjectModal(false);
      await refreshAll();
    } catch (err) {
      setBannerError(err instanceof Error ? err.message : 'Failed to create project');
    }
  };

  const handleOpenProject = async (slug: string) => {
    try {
      await api.openProject(slug);
      setActiveSlug(slug);
      setShowProjectModal(false);
      await refreshAll();
    } catch (err) {
      setBannerError(err instanceof Error ? err.message : 'Failed to open project');
    }
  };

  const handleExportProject = async () => {
    try {
      const res = await api.exportProject(`${activeSlug}_bundle`);
      setExportInfo(`Exported ${res.files.length} files to projects/_exports/${res.bundleName}`);
      await refreshAll();
    } catch (err) {
      setBannerError(err instanceof Error ? err.message : 'Export failed');
    }
  };

  const handleRunCppTests = async () => {
    setTestResult('Running C++ engine test suite...');
    try {
      const res = await api.runTests();
      setTestResult(
        res.result.failures === 0
          ? 'C++ Engine Internal Suite: PASS (0 failures)'
          : `C++ Engine Internal Suite: ${res.result.failures} failures`
      );
      await refreshAll();
    } catch (err) {
      setTestResult(err instanceof Error ? err.message : 'Test run failed');
    }
  };

  const HierarchyView = (
    <div className="flex h-full flex-col">
      <div className="border-b border-white/[0.08] px-3 py-2.5 flex items-center justify-between">
        <span className="text-xs font-semibold text-slate-200">Scene Hierarchy</span>
        <button
          type="button"
          onClick={() => api.saveProject(activeSlug).then(refreshAll)}
          className="flex h-7 items-center gap-1 rounded border border-white/10 bg-[#1E222B] px-2 text-[11px] text-slate-200 hover:bg-white/10"
        >
          <Save className="h-3 w-3 text-[#38BDF8]" />
          Save
        </button>
      </div>

      <form onSubmit={handleCreateEntity} className="flex gap-1.5 border-b border-white/[0.08] p-2.5">
        <input
          type="text"
          placeholder="New entity name..."
          value={newEntityName}
          onChange={(e) => setNewEntityName(e.target.value)}
          className="h-9 flex-1 rounded border border-white/10 bg-[#0F1115] px-2.5 text-xs text-slate-100 outline-none focus:border-[#38BDF8]"
        />
        <button
          type="submit"
          className="flex h-9 w-9 items-center justify-center rounded bg-[#38BDF8] text-[#0F1115]"
          title="Add Entity"
        >
          <Plus className="h-4 w-4" />
        </button>
      </form>

      <div className="flex-1 overflow-y-auto studio-scroll p-2 space-y-1">
        {entities.map((ent) => {
          const active = selectedId === ent.entity;
          return (
            <div
              key={ent.entity}
              className={`flex min-h-[40px] items-center justify-between rounded px-2.5 py-1.5 text-xs transition ${
                active ? 'bg-[#38BDF8]/15 text-[#38BDF8]' : 'text-slate-300 hover:bg-white/[0.04]'
              }`}
            >
              <button
                type="button"
                onClick={() => {
                  setSelectedId(ent.entity);
                  if (mobileDrawer === 'hierarchy') setMobileDrawer('inspector');
                }}
                className="flex flex-1 items-center gap-2 text-left truncate"
              >
                <Box className="h-3.5 w-3.5 shrink-0 opacity-75" />
                <span className="truncate font-medium">{ent.name}</span>
                <span className="ml-auto font-mono text-[10px] text-slate-500">#{ent.entity}</span>
              </button>
              <button
                type="button"
                onClick={() => handleDeleteEntity(ent.entity)}
                className="ml-2 p-1 text-slate-500 hover:text-red-400"
                title="Destroy Entity"
              >
                <Trash2 className="h-3.5 w-3.5" />
              </button>
            </div>
          );
        })}
        {entities.length === 0 && (
          <div className="p-4 text-center text-xs text-slate-500">Empty scene</div>
        )}
      </div>

      {/* Prefab Instantiate */}
      <div className="border-t border-white/[0.08] p-2.5 space-y-1.5">
        <div className="text-[11px] text-slate-400">Instantiate Prefab</div>
        <div className="flex gap-1.5">
          <input
            type="text"
            value={prefabToSpawn}
            onChange={(e) => setPrefabToSpawn(e.target.value)}
            className="h-8 flex-1 rounded border border-white/10 bg-[#0F1115] px-2 text-xs text-slate-100"
          />
          <button
            type="button"
            onClick={() => api.instantiatePrefab(prefabToSpawn).then(refreshAll).catch((e) => setBannerError(e.message))}
            className="h-8 rounded border border-white/10 bg-[#1E222B] px-2.5 text-xs text-slate-200 hover:bg-white/10"
          >
            Spawn
          </button>
        </div>
      </div>
    </div>
  );

  const AssetsView = (
    <div className="flex h-full flex-col gap-4 overflow-y-auto studio-scroll p-4 text-xs">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <div>
          <h2 className="text-sm font-semibold text-slate-100">Asset Pipeline & .nbpk Bundler</h2>
          <p className="text-slate-400">Import OBJ geometry or package workspace files into CRC-verified .nbpk</p>
        </div>
        <button
          type="button"
          onClick={() => api.buildPack(activeSlug).then(refreshAll).catch((e) => setBannerError(e.message))}
          className="flex h-9 items-center gap-1.5 rounded bg-[#38BDF8] px-3 font-semibold text-[#0F1115]"
        >
          <Package className="h-3.5 w-3.5" />
          Build .nbpk Pack
        </button>
      </div>

      <div className="rounded-md border border-white/[0.08] bg-[#171A21] p-3 space-y-2">
        <div className="flex items-center justify-between">
          <span className="font-medium text-slate-200">OBJ Mesh Importer</span>
          <button
            type="button"
            onClick={async () => {
              try {
                const res = await api.importObj(objText);
                setObjResult(
                  `Imported OK: ${res.result.vertices} vertices, ${res.result.indices} indices`
                );
              } catch (err) {
                setObjResult(err instanceof Error ? err.message : 'Import error');
              }
            }}
            className="h-8 rounded border border-white/10 bg-[#1E222B] px-3 font-medium text-slate-100 hover:bg-white/10"
          >
            Parse OBJ
          </button>
        </div>
        <textarea
          value={objText}
          onChange={(e) => setObjText(e.target.value)}
          rows={5}
          spellCheck={false}
          className="w-full rounded border border-white/10 bg-[#0F1115] p-2.5 font-mono text-[11px] text-slate-200 outline-none"
        />
        {objResult && <div className="font-mono text-[11px] text-[#38BDF8]">{objResult}</div>}
      </div>

      <div className="rounded-md border border-white/[0.08] bg-[#171A21] p-3 space-y-2">
        <div className="font-medium text-slate-200">Loaded Pack Entries ({packEntries.length})</div>
        {packEntries.length === 0 ? (
          <div className="text-slate-500">No pack currently mounted. Build or open an exported project pack.</div>
        ) : (
          <div className="divide-y divide-white/[0.06] font-mono text-[11px]">
            {packEntries.map((entry) => (
              <div key={entry.id} className="flex items-center justify-between py-1.5">
                <span className="text-slate-200">{entry.name}</span>
                <span className="text-slate-400">
                  {entry.kind} · {entry.size} B
                </span>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );

  const ConsoleView = (
    <div className="flex h-full flex-col overflow-hidden bg-[#0F1115] text-xs">
      <div className="flex flex-wrap items-center justify-between gap-2 border-b border-white/[0.08] bg-[#171A21] px-3 py-2">
        <span className="font-medium text-slate-200">Engine Logs & Diagnostics</span>
        <div className="flex items-center gap-2">
          <button
            type="button"
            onClick={handleRunCppTests}
            className="h-8 rounded bg-[#38BDF8] px-3 font-semibold text-[#0F1115]"
          >
            Run C++ Tests
          </button>
          <button
            type="button"
            onClick={refreshAll}
            className="h-8 rounded border border-white/10 bg-[#1E222B] px-3 text-slate-200"
          >
            Refresh
          </button>
        </div>
      </div>

      {testResult && (
        <div className="border-b border-white/[0.08] bg-[#1E222B] px-3 py-2 font-mono text-emerald-300">
          {testResult}
        </div>
      )}

      {buildStatus && (
        <div className="grid grid-cols-1 gap-2 border-b border-white/[0.08] bg-[#171A21]/60 px-3 py-2.5 sm:grid-cols-2 font-mono text-[11px]">
          <div>
            Host: {buildStatus.host.compiler} · Lua: {buildStatus.host.luaBackend}
          </div>
          <div>
            Android Gradle: {buildStatus.android.gradleProjectConfigured ? 'Configured' : 'No'} · Local SDK:{' '}
            {buildStatus.android.localSdkPresent ? 'Present' : 'Not installed'} ({buildStatus.android.apkStatus})
          </div>
        </div>
      )}

      <div className="flex-1 overflow-y-auto studio-scroll p-3 font-mono text-[11px] space-y-1">
        {logs.map((l, idx) => (
          <div key={idx} className="flex gap-2">
            <span
              className={`w-14 shrink-0 ${
                l.level === 'error' || l.level === 'fatal'
                  ? 'text-red-400'
                  : l.level === 'warning'
                    ? 'text-amber-300'
                    : 'text-slate-500'
              }`}
            >
              [{l.level}]
            </span>
            <span className="text-slate-200 break-all">{l.text}</span>
          </div>
        ))}
      </div>
    </div>
  );

  return (
    <div className="flex h-screen w-screen flex-col overflow-hidden bg-[#0F1115] text-[#F1F5F9]">
      {/* Top Studio Bar (50px single row) */}
      <header className="flex h-[50px] shrink-0 items-center justify-between gap-2 border-b border-white/[0.08] bg-[#171A21] px-2.5 sm:px-3">
        {/* Left: Brand & Project Selector */}
        <div className="flex items-center gap-2 min-w-0">
          <span
            className="text-base font-bold tracking-tight text-slate-100"
            style={{ fontFamily: 'var(--font-display)' }}
          >
            NIMBUS
          </span>
          <button
            type="button"
            onClick={() => setShowProjectModal(true)}
            className="flex h-9 items-center gap-1.5 rounded border border-white/10 bg-[#1E222B] px-2.5 text-xs text-slate-200 hover:bg-white/10 truncate max-w-[145px] sm:max-w-[200px]"
          >
            <FolderOpen className="h-3.5 w-3.5 shrink-0 text-[#38BDF8]" />
            <span className="truncate">{activeSlug}</span>
          </button>
        </div>

        {/* Center: Playback + Undo/Redo */}
        <div className="flex items-center gap-1 rounded border border-white/[0.08] bg-[#0F1115] p-0.5">
          <button
            type="button"
            onClick={() => handlePlayback('play')}
            title="Play"
            className={`flex h-8 w-8 items-center justify-center rounded ${
              engineStatus?.state === 'playing'
                ? 'bg-[#38BDF8] text-[#0F1115]'
                : 'text-slate-300 hover:bg-white/[0.06]'
            }`}
          >
            <Play className="h-3.5 w-3.5" />
          </button>
          <button
            type="button"
            onClick={() => handlePlayback('pause')}
            title="Pause"
            className={`flex h-8 w-8 items-center justify-center rounded ${
              engineStatus?.state === 'paused'
                ? 'bg-[#38BDF8] text-[#0F1115]'
                : 'text-slate-300 hover:bg-white/[0.06]'
            }`}
          >
            <Pause className="h-3.5 w-3.5" />
          </button>
          <button
            type="button"
            onClick={() => handlePlayback('stop')}
            title="Stop"
            className="flex h-8 w-8 items-center justify-center rounded text-slate-300 hover:bg-white/[0.06]"
          >
            <Square className="h-3.5 w-3.5" />
          </button>
          <button
            type="button"
            onClick={() => handlePlayback('step')}
            title="Step Frame"
            className="flex h-8 w-8 items-center justify-center rounded text-slate-300 hover:bg-white/[0.06]"
          >
            <SkipForward className="h-3.5 w-3.5" />
          </button>
          <div className="mx-0.5 hidden h-5 w-px bg-white/[0.08] sm:block" />
          <button
            type="button"
            onClick={() => api.undo().then(refreshAll)}
            disabled={!engineStatus?.undoCount}
            title="Undo"
            className="hidden h-8 w-8 items-center justify-center rounded text-slate-300 hover:bg-white/[0.06] disabled:opacity-30 sm:flex"
          >
            <Undo2 className="h-3.5 w-3.5" />
          </button>
          <button
            type="button"
            onClick={() => api.redo().then(refreshAll)}
            disabled={!engineStatus?.redoCount}
            title="Redo"
            className="hidden h-8 w-8 items-center justify-center rounded text-slate-300 hover:bg-white/[0.06] disabled:opacity-30 sm:flex"
          >
            <Redo2 className="h-3.5 w-3.5" />
          </button>
        </div>

        {/* Right: Mode Tabs (Desktop) & PWA Install */}
        <div className="flex items-center gap-1.5">
          <div className="hidden lg:flex items-center gap-1 rounded border border-white/[0.08] bg-[#0F1115] p-0.5 text-xs">
            {(
              [
                ['scene', 'Viewport'],
                ['graph', 'Graph'],
                ['assets', 'Assets'],
                ['console', 'Console'],
              ] as Array<[CenterTab, string]>
            ).map(([key, label]) => (
              <button
                key={key}
                type="button"
                onClick={() => setCenterTab(key)}
                className={`h-8 rounded px-2.5 font-medium transition ${
                  centerTab === key
                    ? 'bg-[#38BDF8] text-[#0F1115]'
                    : 'text-slate-300 hover:bg-white/[0.05]'
                }`}
              >
                {label}
              </button>
            ))}
          </div>

          {!isInstalled && isInstallable && (
            <button
              type="button"
              onClick={install}
              className="flex h-9 items-center gap-1.5 rounded bg-[#38BDF8] px-2.5 text-xs font-semibold text-[#0F1115]"
            >
              <Download className="h-3.5 w-3.5" />
              <span className="hidden sm:inline">Install App</span>
            </button>
          )}
          {!isInstalled && !isInstallable && isIOS && (
            <button
              type="button"
              onClick={() => setShowIOSPrompt(true)}
              className="flex h-9 items-center gap-1 rounded border border-white/10 bg-[#1E222B] px-2.5 text-xs text-slate-200"
            >
              Install
            </button>
          )}
        </div>
      </header>

      {bannerError && (
        <div className="flex items-center justify-between border-b border-red-500/30 bg-red-500/15 px-3 py-1.5 text-xs text-red-200">
          <span>{bannerError}</span>
          <button type="button" onClick={() => setBannerError(null)}>
            <X className="h-3.5 w-3.5" />
          </button>
        </div>
      )}

      {/* Main Workbench */}
      <div className="flex flex-1 min-h-0 overflow-hidden">
        {/* Desktop Left Column: Hierarchy */}
        <aside className="hidden lg:flex w-68 shrink-0 flex-col border-r border-white/[0.08] bg-[#171A21]">
          {HierarchyView}
        </aside>

        {/* Center Canvas / Node Graph / Assets / Console */}
        <main className="relative flex-1 min-w-0 min-h-0">
          {centerTab === 'scene' && (
            <Viewport3D
              entities={entities}
              selectedEntityId={selectedId}
              engineStatus={engineStatus}
              terrainAmplitude={terrainAmp}
              showGrid={showGrid}
              onToggleGrid={() => setShowGrid((g) => !g)}
              onSelectEntity={setSelectedId}
              onTransformCommit={(id, patch) => {
                void api.setTransform(id, patch).then(refreshAll);
              }}
            />
          )}
          {centerTab === 'graph' && (
            <VisualScriptEditor
              graph={vsGraph}
              onAddNode={async (type, name, x, y) => {
                await api.addVsNode(type, name, x, y);
                await refreshAll();
              }}
              onRemoveNode={async (id) => {
                await api.removeVsNode(id);
                await refreshAll();
              }}
              onAddEdge={async (fromNode, fromPin, toNode, toPin) => {
                await api.addVsEdge(fromNode, fromPin, toNode, toPin);
                await refreshAll();
              }}
              onExecute={async (entry, a, b, count) => {
                const res = await api.executeVs(entry, a, b, count);
                await refreshAll();
                return res.result;
              }}
              onCompileLua={async () => {
                const res = await api.compileVsLua();
                return res.lua;
              }}
              onRunLua={async (source) => {
                await api.runLua(source);
                await refreshAll();
              }}
            />
          )}
          {centerTab === 'assets' && AssetsView}
          {centerTab === 'console' && ConsoleView}
        </main>

        {/* Desktop Right Column: Inspector */}
        <aside className="hidden lg:flex w-80 shrink-0 flex-col border-l border-white/[0.08] bg-[#171A21]">
          <InspectorPanel
            entity={selectedEntity}
            terrainAmplitude={terrainAmp}
            onTerrainAmplitudeChange={setTerrainAmp}
            onTransformChange={async (id, patch) => {
              await api.setTransform(id, patch);
              await refreshAll();
            }}
            onAddComponent={async (id, component, data) => {
              await api.addComponent(id, component, data);
              await refreshAll();
            }}
            onRemoveComponent={async (id, component) => {
              await api.removeComponent(id, component);
              await refreshAll();
            }}
            onSavePrefab={async (id, prefabId) => {
              await api.savePrefab(id, prefabId);
              await refreshAll();
            }}
            onAttachLua={async (id, source) => {
              await api.attachLua(id, source);
              await refreshAll();
            }}
          />
        </aside>
      </div>

      {/* Mobile Sticky Bottom Thumb Dock (44px+ hit targets) */}
      <nav className="flex lg:hidden h-14 shrink-0 items-center justify-around border-t border-white/[0.08] bg-[#171A21] px-1 pb-[env(safe-area-inset-bottom)]">
        <button
          type="button"
          onClick={() => {
            setCenterTab('scene');
            setMobileDrawer((d) => (d === 'hierarchy' ? 'none' : 'hierarchy'));
          }}
          className={`flex h-11 flex-1 flex-col items-center justify-center gap-0.5 rounded text-[11px] ${
            mobileDrawer === 'hierarchy' ? 'text-[#38BDF8]' : 'text-slate-300'
          }`}
        >
          <Layers className="h-4 w-4" />
          Scene
        </button>
        <button
          type="button"
          onClick={() => {
            setCenterTab('scene');
            setMobileDrawer((d) => (d === 'inspector' ? 'none' : 'inspector'));
          }}
          className={`flex h-11 flex-1 flex-col items-center justify-center gap-0.5 rounded text-[11px] ${
            mobileDrawer === 'inspector' ? 'text-[#38BDF8]' : 'text-slate-300'
          }`}
        >
          <Sliders className="h-4 w-4" />
          Inspect
        </button>
        <button
          type="button"
          onClick={() => {
            setMobileDrawer('none');
            setCenterTab((t) => (t === 'graph' ? 'scene' : 'graph'));
          }}
          className={`flex h-11 flex-1 flex-col items-center justify-center gap-0.5 rounded text-[11px] ${
            centerTab === 'graph' && mobileDrawer === 'none' ? 'text-[#38BDF8]' : 'text-slate-300'
          }`}
        >
          <Code2 className="h-4 w-4" />
          Graph
        </button>
        <button
          type="button"
          onClick={() => setMobileDrawer((d) => (d === 'assets' ? 'none' : 'assets'))}
          className={`flex h-11 flex-1 flex-col items-center justify-center gap-0.5 rounded text-[11px] ${
            mobileDrawer === 'assets' ? 'text-[#38BDF8]' : 'text-slate-300'
          }`}
        >
          <Package className="h-4 w-4" />
          Assets
        </button>
        <button
          type="button"
          onClick={() => setMobileDrawer((d) => (d === 'console' ? 'none' : 'console'))}
          className={`flex h-11 flex-1 flex-col items-center justify-center gap-0.5 rounded text-[11px] ${
            mobileDrawer === 'console' ? 'text-[#38BDF8]' : 'text-slate-300'
          }`}
        >
          <Terminal className="h-4 w-4" />
          Logs
        </button>
      </nav>

      {/* Mobile Bottom Sheet Drawer */}
      {mobileDrawer !== 'none' && (
        <div className="fixed inset-x-0 bottom-14 top-28 z-40 flex flex-col border-t border-white/10 bg-[#171A21] shadow-2xl lg:hidden">
          <div className="flex items-center justify-between border-b border-white/[0.08] px-3 py-2">
            <span className="text-xs font-semibold uppercase tracking-wide text-slate-300">
              {mobileDrawer}
            </span>
            <button
              type="button"
              onClick={() => setMobileDrawer('none')}
              className="flex h-8 w-8 items-center justify-center rounded text-slate-300 hover:bg-white/10"
            >
              <X className="h-4 w-4" />
            </button>
          </div>
          <div className="flex-1 min-h-0 overflow-hidden">
            {mobileDrawer === 'hierarchy' && HierarchyView}
            {mobileDrawer === 'inspector' && (
              <InspectorPanel
                entity={selectedEntity}
                terrainAmplitude={terrainAmp}
                onTerrainAmplitudeChange={setTerrainAmp}
                onTransformChange={async (id, patch) => {
                  await api.setTransform(id, patch);
                  await refreshAll();
                }}
                onAddComponent={async (id, component, data) => {
                  await api.addComponent(id, component, data);
                  await refreshAll();
                }}
                onRemoveComponent={async (id, component) => {
                  await api.removeComponent(id, component);
                  await refreshAll();
                }}
                onSavePrefab={async (id, prefabId) => {
                  await api.savePrefab(id, prefabId);
                  await refreshAll();
                }}
                onAttachLua={async (id, source) => {
                  await api.attachLua(id, source);
                  await refreshAll();
                }}
              />
            )}
            {mobileDrawer === 'assets' && AssetsView}
            {mobileDrawer === 'console' && ConsoleView}
          </div>
        </div>
      )}

      {/* Project Manager / Templates / Export Modal */}
      {showProjectModal && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4">
          <div className="w-full max-w-lg rounded-lg border border-white/10 bg-[#171A21] p-4 shadow-2xl space-y-4">
            <div className="flex items-center justify-between">
              <h2 className="text-sm font-semibold text-slate-100">Projects & Export</h2>
              <button
                type="button"
                onClick={() => setShowProjectModal(false)}
                className="flex h-8 w-8 items-center justify-center rounded text-slate-400 hover:bg-white/10"
              >
                <X className="h-4 w-4" />
              </button>
            </div>

            <div className="rounded border border-white/[0.08] bg-[#0F1115] p-3 space-y-2.5 text-xs">
              <div className="font-medium text-slate-200">Create New Project</div>
              <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
                <input
                  type="text"
                  value={newProjName}
                  onChange={(e) => setNewProjName(e.target.value)}
                  placeholder="Project Name"
                  className="h-9 rounded border border-white/10 bg-[#171A21] px-2.5 text-slate-100"
                />
                <select
                  value={newProjTemplate}
                  onChange={(e) =>
                    setNewProjTemplate(e.target.value as 'empty' | '3d_action' | 'ai_sandbox')
                  }
                  className="h-9 rounded border border-white/10 bg-[#171A21] px-2 text-slate-100"
                >
                  <option value="empty">Empty Template</option>
                  <option value="3d_action">3D Action Arena</option>
                  <option value="ai_sandbox">AI NavMesh Sandbox</option>
                </select>
              </div>
              <button
                type="button"
                onClick={handleNewProject}
                className="h-9 w-full rounded bg-[#38BDF8] font-semibold text-[#0F1115]"
              >
                Create & Open Project
              </button>
            </div>

            <div className="space-y-1.5 text-xs">
              <div className="font-medium text-slate-300">Workspace Projects</div>
              <div className="max-h-40 overflow-y-auto studio-scroll divide-y divide-white/[0.06] rounded border border-white/[0.08] bg-[#0F1115]">
                {projects.map((p) => (
                  <button
                    key={p.slug}
                    type="button"
                    onClick={() => handleOpenProject(p.slug)}
                    className="flex w-full items-center justify-between px-3 py-2 text-left hover:bg-white/[0.05]"
                  >
                    <span className="font-medium text-slate-100">{p.name}</span>
                    <span className="font-mono text-[11px] text-slate-400">
                      {p.template} · {p.slug}
                    </span>
                  </button>
                ))}
              </div>
            </div>

            <div className="flex flex-col gap-2 border-t border-white/[0.08] pt-3 text-xs">
              <button
                type="button"
                onClick={handleExportProject}
                className="flex h-9 items-center justify-center gap-1.5 rounded border border-white/10 bg-[#1E222B] font-medium text-slate-100 hover:bg-white/10"
              >
                <Download className="h-3.5 w-3.5 text-[#38BDF8]" />
                Export Playable Bundle (.nbpk + scenes + compiled Lua)
              </button>
              {exportInfo && <div className="font-mono text-[11px] text-emerald-300">{exportInfo}</div>}
            </div>
          </div>
        </div>
      )}

      {showIOSPrompt && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4">
          <div className="w-full max-w-sm rounded-lg border border-white/10 bg-[#171A21] p-5 text-xs space-y-3">
            <h3 className="text-sm font-semibold text-slate-100">Install Nimbus Studio</h3>
            <p className="text-slate-300 leading-relaxed">
              Tap the Share icon in Safari, then choose Add to Home Screen for full standalone touch mode.
            </p>
            <button
              type="button"
              onClick={() => setShowIOSPrompt(false)}
              className="h-9 w-full rounded bg-[#38BDF8] font-semibold text-[#0F1115]"
            >
              Close
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
