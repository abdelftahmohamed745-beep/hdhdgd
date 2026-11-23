import React, { useState } from 'react';
import { Layers, Plus, Trash2, Volume2, Code2, Box } from 'lucide-react';
import type { SceneEntity } from '../types';

interface InspectorPanelProps {
  entity: SceneEntity | null;
  terrainAmplitude: number;
  onTerrainAmplitudeChange: (v: number) => void;
  onTransformChange: (
    id: number,
    patch: { position?: [number, number, number]; scale?: [number, number, number] }
  ) => Promise<void>;
  onAddComponent: (id: number, component: string, data: Record<string, unknown>) => Promise<void>;
  onRemoveComponent: (id: number, component: string) => Promise<void>;
  onSavePrefab: (id: number, prefabId: string) => Promise<void>;
  onAttachLua: (id: number, source: string) => Promise<void>;
}

const AVAILABLE_COMPONENTS = [
  'RigidBody',
  'BoxCollider',
  'SphereCollider',
  'CapsuleCollider',
  'Camera',
  'Light',
];

export const InspectorPanel: React.FC<InspectorPanelProps> = ({
  entity,
  terrainAmplitude,
  onTerrainAmplitudeChange,
  onTransformChange,
  onAddComponent,
  onRemoveComponent,
  onSavePrefab,
  onAttachLua,
}) => {
  const [compToAdd, setCompToAdd] = useState('RigidBody');
  const [prefabName, setPrefabName] = useState('PropPrefab');
  const [entityLua, setEntityLua] = useState(
    'function on_tick(dt)\n  -- Entity tick hook\nend'
  );
  const [toneFreq, setToneFreq] = useState(440);

  const playWebAudioPreview = () => {
    try {
      const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      const ctx = new AudioCtx();
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.value = toneFreq;
      gain.gain.setValueAtTime(0.15, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.25);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start();
      osc.stop(ctx.currentTime + 0.25);
    } catch {
      // AudioContext blocked or unsupported
    }
  };

  if (!entity) {
    return (
      <div className="flex h-full flex-col gap-4 overflow-y-auto studio-scroll p-4 text-sm">
        <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/60 p-4 text-center">
          <Box className="mx-auto mb-2 h-6 w-6 text-slate-400" />
          <p className="font-medium text-slate-200">No entity selected</p>
          <p className="mt-1 text-xs text-slate-400">
            Tap any object in the 3D viewport or Hierarchy to inspect components.
          </p>
        </div>

        <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/40 p-3 space-y-2.5">
          <div className="text-xs font-semibold text-slate-200">World Terrain Preview</div>
          <label className="flex items-center justify-between text-xs text-slate-300">
            <span>Height Amplitude</span>
            <span className="font-mono tabular-nums">{terrainAmplitude.toFixed(2)} m</span>
          </label>
          <input
            type="range"
            min={0}
            max={4}
            step={0.1}
            value={terrainAmplitude}
            onChange={(e) => onTerrainAmplitudeChange(Number(e.target.value))}
            className="w-full accent-[#38BDF8]"
          />
        </div>

        <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/40 p-3 space-y-2.5">
          <div className="text-xs font-semibold text-slate-200">Audio Synth Tester</div>
          <div className="flex items-center gap-2">
            <input
              type="number"
              min={80}
              max={2000}
              value={toneFreq}
              onChange={(e) => setToneFreq(Number(e.target.value))}
              className="h-9 flex-1 rounded border border-white/10 bg-[#0F1115] px-2 font-mono text-xs"
            />
            <button
              type="button"
              onClick={playWebAudioPreview}
              className="flex h-9 items-center gap-1.5 rounded bg-[#1E222B] border border-white/10 px-3 text-xs font-medium text-slate-100 hover:bg-white/10"
            >
              <Volume2 className="h-3.5 w-3.5 text-[#38BDF8]" />
              Play Tone
            </button>
          </div>
        </div>
      </div>
    );
  }

  const updatePosAxis = (idx: 0 | 1 | 2, val: number) => {
    if (!Number.isFinite(val)) return;
    const next: [number, number, number] = [...entity.position];
    next[idx] = val;
    void onTransformChange(entity.entity, { position: next });
  };

  const updateScaleAxis = (idx: 0 | 1 | 2, val: number) => {
    if (!Number.isFinite(val) || val <= 0) return;
    const next: [number, number, number] = [...entity.scale];
    next[idx] = val;
    void onTransformChange(entity.entity, { scale: next });
  };

  const rb = entity.componentData.RigidBody as
    | { mass?: number; restitution?: number; friction?: number; kinematic?: boolean; useGravity?: boolean }
    | undefined;
  const light = entity.componentData.Light as
    | { type?: number; intensity?: number; range?: number; color?: [number, number, number] }
    | undefined;
  const cam = entity.componentData.Camera as
    | { fov?: number; near?: number; far?: number }
    | undefined;
  const sphere = entity.componentData.SphereCollider as { radius?: number } | undefined;
  const box = entity.componentData.BoxCollider as { halfExtents?: [number, number, number] } | undefined;
  const cap = entity.componentData.CapsuleCollider as { radius?: number; height?: number } | undefined;

  return (
    <div className="flex h-full flex-col gap-3 overflow-y-auto studio-scroll p-3 text-xs">
      {/* Header */}
      <div className="flex items-center justify-between border-b border-white/[0.08] pb-2.5">
        <div>
          <div className="text-sm font-semibold text-slate-100">{entity.name}</div>
          <div className="font-mono text-[11px] text-slate-400">
            Entity #{entity.entity} · Parent {entity.parent >= 0 ? `#${entity.parent}` : 'Root'}
          </div>
        </div>
      </div>

      {/* Transform */}
      <section className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2.5">
        <div className="font-semibold text-slate-200">Transform</div>
        <div>
          <div className="mb-1 text-[11px] text-slate-400">Position</div>
          <div className="grid grid-cols-3 gap-1.5">
            {(['X', 'Y', 'Z'] as const).map((label, idx) => (
              <label key={label} className="flex items-center rounded border border-white/10 bg-[#0F1115] px-2">
                <span className="mr-1 font-mono text-[11px] text-slate-400">{label}</span>
                <input
                  type="number"
                  step={0.25}
                  value={Number(entity.position[idx].toFixed(3))}
                  onChange={(e) => updatePosAxis(idx as 0 | 1 | 2, Number(e.target.value))}
                  className="h-9 w-full bg-transparent font-mono text-xs text-slate-100 outline-none tabular-nums"
                />
              </label>
            ))}
          </div>
        </div>
        <div>
          <div className="mb-1 text-[11px] text-slate-400">Scale</div>
          <div className="grid grid-cols-3 gap-1.5">
            {(['X', 'Y', 'Z'] as const).map((label, idx) => (
              <label key={label} className="flex items-center rounded border border-white/10 bg-[#0F1115] px-2">
                <span className="mr-1 font-mono text-[11px] text-slate-400">{label}</span>
                <input
                  type="number"
                  step={0.1}
                  min={0.05}
                  value={Number(entity.scale[idx].toFixed(3))}
                  onChange={(e) => updateScaleAxis(idx as 0 | 1 | 2, Number(e.target.value))}
                  className="h-9 w-full bg-transparent font-mono text-xs text-slate-100 outline-none tabular-nums"
                />
              </label>
            ))}
          </div>
        </div>
      </section>

      {/* Active Components */}
      <section className="space-y-2">
        <div className="flex items-center justify-between">
          <span className="font-semibold text-slate-200">Components</span>
          <div className="flex items-center gap-1">
            <select
              value={compToAdd}
              onChange={(e) => setCompToAdd(e.target.value)}
              className="h-8 rounded border border-white/10 bg-[#1E222B] px-2 text-xs text-slate-100"
            >
              {AVAILABLE_COMPONENTS.map((c) => (
                <option key={c} value={c}>
                  {c}
                </option>
              ))}
            </select>
            <button
              type="button"
              onClick={() => onAddComponent(entity.entity, compToAdd, {})}
              className="flex h-8 items-center gap-1 rounded bg-[#38BDF8] px-2.5 text-xs font-semibold text-[#0F1115]"
            >
              <Plus className="h-3.5 w-3.5" />
              Add
            </button>
          </div>
        </div>

        {rb && (
          <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2">
            <div className="flex items-center justify-between">
              <span className="font-medium text-emerald-300">RigidBody</span>
              <button
                type="button"
                onClick={() => onRemoveComponent(entity.entity, 'RigidBody')}
                className="text-slate-400 hover:text-red-400"
              >
                <Trash2 className="h-3.5 w-3.5" />
              </button>
            </div>
            <div className="grid grid-cols-2 gap-2">
              <label className="space-y-1">
                <span className="text-[11px] text-slate-400">Mass</span>
                <input
                  type="number"
                  step={0.5}
                  value={rb.mass ?? 1}
                  onChange={(e) =>
                    onAddComponent(entity.entity, 'RigidBody', { ...rb, mass: Number(e.target.value) })
                  }
                  className="h-8 w-full rounded border border-white/10 bg-[#0F1115] px-2 font-mono"
                />
              </label>
              <label className="space-y-1">
                <span className="text-[11px] text-slate-400">Restitution</span>
                <input
                  type="number"
                  step={0.05}
                  value={rb.restitution ?? 0.2}
                  onChange={(e) =>
                    onAddComponent(entity.entity, 'RigidBody', {
                      ...rb,
                      restitution: Number(e.target.value),
                    })
                  }
                  className="h-8 w-full rounded border border-white/10 bg-[#0F1115] px-2 font-mono"
                />
              </label>
            </div>
            <label className="flex items-center gap-2 text-slate-300">
              <input
                type="checkbox"
                checked={!!rb.useGravity}
                onChange={(e) =>
                  onAddComponent(entity.entity, 'RigidBody', { ...rb, useGravity: e.target.checked })
                }
              />
              Use Gravity
            </label>
          </div>
        )}

        {box && (
          <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2">
            <div className="flex items-center justify-between">
              <span className="font-medium text-sky-300">BoxCollider</span>
              <button
                type="button"
                onClick={() => onRemoveComponent(entity.entity, 'BoxCollider')}
                className="text-slate-400 hover:text-red-400"
              >
                <Trash2 className="h-3.5 w-3.5" />
              </button>
            </div>
            <div className="font-mono text-[11px] text-slate-300">
              Half Extents: [{(box.halfExtents ?? [0.5, 0.5, 0.5]).join(', ')}]
            </div>
          </div>
        )}

        {sphere && (
          <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2">
            <div className="flex items-center justify-between">
              <span className="font-medium text-sky-300">SphereCollider</span>
              <button
                type="button"
                onClick={() => onRemoveComponent(entity.entity, 'SphereCollider')}
                className="text-slate-400 hover:text-red-400"
              >
                <Trash2 className="h-3.5 w-3.5" />
              </button>
            </div>
            <label className="flex items-center gap-2">
              <span className="text-[11px] text-slate-400">Radius</span>
              <input
                type="number"
                step={0.1}
                value={sphere.radius ?? 0.5}
                onChange={(e) =>
                  onAddComponent(entity.entity, 'SphereCollider', { radius: Number(e.target.value) })
                }
                className="h-8 flex-1 rounded border border-white/10 bg-[#0F1115] px-2 font-mono"
              />
            </label>
          </div>
        )}

        {cap && (
          <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2">
            <div className="flex items-center justify-between">
              <span className="font-medium text-sky-300">CapsuleCollider</span>
              <button
                type="button"
                onClick={() => onRemoveComponent(entity.entity, 'CapsuleCollider')}
                className="text-slate-400 hover:text-red-400"
              >
                <Trash2 className="h-3.5 w-3.5" />
              </button>
            </div>
            <div className="font-mono text-[11px] text-slate-300">
              r={cap.radius ?? 0.25}, h={cap.height ?? 1}
            </div>
          </div>
        )}

        {light && (
          <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2">
            <div className="flex items-center justify-between">
              <span className="font-medium text-amber-300">Light</span>
              <button
                type="button"
                onClick={() => onRemoveComponent(entity.entity, 'Light')}
                className="text-slate-400 hover:text-red-400"
              >
                <Trash2 className="h-3.5 w-3.5" />
              </button>
            </div>
            <div className="grid grid-cols-2 gap-2">
              <label className="space-y-1">
                <span className="text-[11px] text-slate-400">Intensity</span>
                <input
                  type="number"
                  step={0.2}
                  value={light.intensity ?? 1}
                  onChange={(e) =>
                    onAddComponent(entity.entity, 'Light', {
                      ...light,
                      intensity: Number(e.target.value),
                    })
                  }
                  className="h-8 w-full rounded border border-white/10 bg-[#0F1115] px-2 font-mono"
                />
              </label>
              <label className="space-y-1">
                <span className="text-[11px] text-slate-400">Range</span>
                <input
                  type="number"
                  step={1}
                  value={light.range ?? 10}
                  onChange={(e) =>
                    onAddComponent(entity.entity, 'Light', {
                      ...light,
                      range: Number(e.target.value),
                    })
                  }
                  className="h-8 w-full rounded border border-white/10 bg-[#0F1115] px-2 font-mono"
                />
              </label>
            </div>
          </div>
        )}

        {cam && (
          <div className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2">
            <div className="flex items-center justify-between">
              <span className="font-medium text-violet-300">Camera</span>
              <button
                type="button"
                onClick={() => onRemoveComponent(entity.entity, 'Camera')}
                className="text-slate-400 hover:text-red-400"
              >
                <Trash2 className="h-3.5 w-3.5" />
              </button>
            </div>
            <label className="flex items-center gap-2">
              <span className="text-[11px] text-slate-400">FOV</span>
              <input
                type="number"
                step={2}
                value={cam.fov ?? 60}
                onChange={(e) =>
                  onAddComponent(entity.entity, 'Camera', { ...cam, fov: Number(e.target.value) })
                }
                className="h-8 flex-1 rounded border border-white/10 bg-[#0F1115] px-2 font-mono"
              />
            </label>
          </div>
        )}
      </section>

      {/* Save as Prefab */}
      <section className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2">
        <div className="flex items-center gap-1.5 font-semibold text-slate-200">
          <Layers className="h-3.5 w-3.5 text-[#38BDF8]" />
          Save Subtree Prefab
        </div>
        <div className="flex gap-1.5">
          <input
            type="text"
            value={prefabName}
            onChange={(e) => setPrefabName(e.target.value)}
            className="h-9 flex-1 rounded border border-white/10 bg-[#0F1115] px-2 text-xs text-slate-100"
          />
          <button
            type="button"
            onClick={() => onSavePrefab(entity.entity, prefabName)}
            className="h-9 rounded border border-white/10 bg-[#171A21] px-3 text-xs font-medium text-slate-200 hover:bg-white/10"
          >
            Save
          </button>
        </div>
      </section>

      {/* Attach Lua Script */}
      <section className="rounded-md border border-white/[0.08] bg-[#1E222B]/50 p-3 space-y-2">
        <div className="flex items-center justify-between font-semibold text-slate-200">
          <span className="flex items-center gap-1.5">
            <Code2 className="h-3.5 w-3.5 text-[#38BDF8]" />
            Entity Lua Script
          </span>
          <button
            type="button"
            onClick={() => onAttachLua(entity.entity, entityLua)}
            className="h-7 rounded bg-[#38BDF8] px-2.5 text-[11px] font-semibold text-[#0F1115]"
          >
            Attach
          </button>
        </div>
        <textarea
          value={entityLua}
          onChange={(e) => setEntityLua(e.target.value)}
          spellCheck={false}
          rows={4}
          className="w-full rounded border border-white/10 bg-[#0F1115] p-2 font-mono text-[11px] text-slate-200 outline-none"
        />
      </section>
    </div>
  );
};
