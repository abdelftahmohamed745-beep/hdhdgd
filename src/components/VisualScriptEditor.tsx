import React, { useState } from 'react';
import { Play, Plus, Trash2, Code2, Sparkles } from 'lucide-react';
import type { VisualScriptGraph, VsNode } from '../types';

interface VisualScriptEditorProps {
  graph: VisualScriptGraph;
  onAddNode: (type: string, name: string, x: number, y: number) => Promise<void>;
  onRemoveNode: (id: number) => Promise<void>;
  onAddEdge: (fromNode: number, fromPin: number, toNode: number, toPin: number) => Promise<void>;
  onExecute: (entry: number, a: number, b: number, count: number) => Promise<{
    trace: number[];
    printed: number[];
  }>;
  onCompileLua: () => Promise<string>;
  onRunLua: (source: string) => Promise<void>;
}

const NODE_TYPES = [
  'event',
  'event_tick',
  'event_collision',
  'branch',
  'for_loop',
  'add',
  'sub',
  'mul',
  'div',
  'compare',
  'get_var',
  'set_var',
  'get_position',
  'set_position',
  'apply_force',
  'play_sound',
  'spawn_particle',
  'print',
];

const PIN_COLOR: Record<string, string> = {
  exec: '#F8FAFC',
  float: '#38BDF8',
  vec3: '#34D399',
  bool: '#FBBF24',
  entity: '#A78BFA',
};

export const VisualScriptEditor: React.FC<VisualScriptEditorProps> = ({
  graph,
  onAddNode,
  onRemoveNode,
  onAddEdge,
  onExecute,
  onCompileLua,
  onRunLua,
}) => {
  const [newNodeType, setNewNodeType] = useState('event');
  const [pendingSource, setPendingSource] = useState<{
    nodeId: number;
    pinIndex: number;
    type: string;
  } | null>(null);
  const [offsets, setOffsets] = useState<Record<number, { x: number; y: number }>>({});
  const [dragging, setDragging] = useState<{
    id: number;
    startX: number;
    startY: number;
    origX: number;
    origY: number;
  } | null>(null);
  const [inputA, setInputA] = useState(4);
  const [inputB, setInputB] = useState(7);
  const [trace, setTrace] = useState<number[]>([]);
  const [printed, setPrinted] = useState<number[]>([]);
  const [luaCode, setLuaCode] = useState<string>(
    'local e = nimbus.create_entity("Orbiter")\nnimbus.set_position(e, 1.5, 2.0, 0.0)\nnimbus.emit_event("spawned", e)'
  );
  const [errorText, setErrorText] = useState<string | null>(null);

  const nodePos = (n: VsNode) => offsets[n.id] ?? { x: n.x, y: n.y };

  const pinScreenPos = (n: VsNode, isInput: boolean, idx: number) => {
    const p = nodePos(n);
    const x = isInput ? p.x + 8 : p.x + 176;
    const y = p.y + 42 + idx * 24;
    return { x, y };
  };

  const handleTapOutputPin = (nodeId: number, pinIndex: number, type: string) => {
    setErrorText(null);
    setPendingSource({ nodeId, pinIndex, type });
  };

  const handleTapInputPin = async (nodeId: number, pinIndex: number) => {
    if (!pendingSource) return;
    if (pendingSource.nodeId === nodeId) {
      setPendingSource(null);
      return;
    }
    try {
      await onAddEdge(pendingSource.nodeId, pendingSource.pinIndex, nodeId, pinIndex);
      setErrorText(null);
    } catch (err) {
      setErrorText(err instanceof Error ? err.message : 'Failed to connect pins');
    } finally {
      setPendingSource(null);
    }
  };

  const handleExecuteFirst = async () => {
    const entryNode =
      graph.nodes.find((n) => n.type.startsWith('event')) ?? graph.nodes[0];
    if (!entryNode) {
      setErrorText('Add an event node first');
      return;
    }
    try {
      const res = await onExecute(entryNode.id, inputA, inputB, 3);
      setTrace(res.trace);
      setPrinted(res.printed);
      setErrorText(null);
    } catch (err) {
      setErrorText(err instanceof Error ? err.message : 'Execution failed');
    }
  };

  const handleCompile = async () => {
    try {
      const code = await onCompileLua();
      setLuaCode(code);
      setErrorText(null);
    } catch (err) {
      setErrorText(err instanceof Error ? err.message : 'Compile failed');
    }
  };

  return (
    <div className="flex h-full flex-col bg-[#0F1115]">
      {/* Toolbar */}
      <div className="flex flex-wrap items-center gap-2 border-b border-white/[0.08] bg-[#171A21] px-3 py-2">
        <select
          value={newNodeType}
          onChange={(e) => setNewNodeType(e.target.value)}
          className="h-9 rounded border border-white/10 bg-[#1E222B] px-2 text-xs text-slate-100"
        >
          {NODE_TYPES.map((t) => (
            <option key={t} value={t}>
              {t}
            </option>
          ))}
        </select>
        <button
          type="button"
          onClick={() =>
            onAddNode(
              newNodeType,
              newNodeType,
              60 + (graph.nodes.length % 4) * 200,
              60 + Math.floor(graph.nodes.length / 4) * 140
            )
          }
          className="flex h-9 items-center gap-1.5 rounded bg-[#38BDF8] px-3 text-xs font-semibold text-[#0F1115]"
        >
          <Plus className="h-3.5 w-3.5" />
          Add Node
        </button>

        <div className="flex items-center gap-1.5 text-xs text-slate-300">
          <span>a:</span>
          <input
            type="number"
            value={inputA}
            onChange={(e) => setInputA(Number(e.target.value))}
            className="h-8 w-14 rounded border border-white/10 bg-[#1E222B] px-1.5 font-mono text-xs"
          />
          <span>b:</span>
          <input
            type="number"
            value={inputB}
            onChange={(e) => setInputB(Number(e.target.value))}
            className="h-8 w-14 rounded border border-white/10 bg-[#1E222B] px-1.5 font-mono text-xs"
          />
        </div>

        <button
          type="button"
          onClick={handleExecuteFirst}
          className="flex h-9 items-center gap-1.5 rounded border border-white/10 bg-[#1E222B] px-3 text-xs font-medium text-slate-100 hover:bg-white/[0.08]"
        >
          <Play className="h-3.5 w-3.5 text-emerald-400" />
          Execute
        </button>
        <button
          type="button"
          onClick={handleCompile}
          className="flex h-9 items-center gap-1.5 rounded border border-white/10 bg-[#1E222B] px-3 text-xs font-medium text-slate-100 hover:bg-white/[0.08]"
        >
          <Code2 className="h-3.5 w-3.5 text-[#38BDF8]" />
          To Lua
        </button>

        {pendingSource && (
          <button
            type="button"
            onClick={() => setPendingSource(null)}
            className="ml-auto rounded border border-[#38BDF8]/40 bg-[#38BDF8]/10 px-2.5 py-1 text-xs text-[#38BDF8]"
          >
            Tap input pin to wire #{pendingSource.nodeId} (Cancel)
          </button>
        )}
      </div>

      {errorText && (
        <div className="border-b border-red-500/30 bg-red-500/10 px-3 py-1.5 text-xs text-red-300">
          {errorText}
        </div>
      )}

      {/* Node Canvas + Lua Workbench split */}
      <div className="grid flex-1 min-h-0 grid-cols-1 lg:grid-cols-3">
        <div
          className="relative col-span-2 overflow-auto studio-scroll bg-[radial-gradient(#1f2430_1px,transparent_1px)] [background-size:20px_20px]"
          onPointerMove={(e) => {
            if (!dragging) return;
            const dx = e.clientX - dragging.startX;
            const dy = e.clientY - dragging.startY;
            setOffsets((prev) => ({
              ...prev,
              [dragging.id]: {
                x: Math.max(12, dragging.origX + dx),
                y: Math.max(12, dragging.origY + dy),
              },
            }));
          }}
          onPointerUp={() => setDragging(null)}
        >
          <div className="relative min-h-[520px] min-w-[780px]">
            <svg className="pointer-events-none absolute inset-0 h-full w-full">
              {graph.edges.map((edge, idx) => {
                const fromNode = graph.nodes.find((n) => n.id === edge.fromNode);
                const toNode = graph.nodes.find((n) => n.id === edge.toNode);
                if (!fromNode || !toNode) return null;
                const p1 = pinScreenPos(fromNode, false, edge.fromPin);
                const p2 = pinScreenPos(toNode, true, edge.toPin);
                const dx = Math.max(40, Math.abs(p2.x - p1.x) * 0.5);
                const pinType = fromNode.outputs[edge.fromPin]?.type ?? 'float';
                const color = PIN_COLOR[pinType] ?? '#38BDF8';
                return (
                  <path
                    key={`${edge.fromNode}-${edge.fromPin}-${edge.toNode}-${edge.toPin}-${idx}`}
                    d={`M ${p1.x} ${p1.y} C ${p1.x + dx} ${p1.y}, ${p2.x - dx} ${p2.y}, ${p2.x} ${p2.y}`}
                    stroke={color}
                    strokeWidth={2}
                    fill="none"
                    strokeOpacity={0.85}
                  />
                );
              })}
            </svg>

            {graph.nodes.map((node) => {
              const p = nodePos(node);
              const rows = Math.max(node.inputs.length, node.outputs.length);
              const executed = trace.includes(node.id);
              return (
                <div
                  key={node.id}
                  style={{ left: p.x, top: p.y }}
                  className={`absolute w-[184px] rounded-md border bg-[#171A21] shadow-lg ${
                    executed ? 'border-emerald-400' : 'border-white/10'
                  }`}
                >
                  <div
                    onPointerDown={(e) =>
                      setDragging({
                        id: node.id,
                        startX: e.clientX,
                        startY: e.clientY,
                        origX: p.x,
                        origY: p.y,
                      })
                    }
                    className="flex cursor-grab items-center justify-between border-b border-white/[0.08] bg-[#1E222B] px-2.5 py-1.5"
                  >
                    <span className="truncate font-mono text-xs font-medium text-slate-100">
                      #{node.id} {node.type}
                    </span>
                    <button
                      type="button"
                      onClick={() => onRemoveNode(node.id)}
                      className="text-slate-400 hover:text-red-400"
                    >
                      <Trash2 className="h-3.5 w-3.5" />
                    </button>
                  </div>
                  <div className="px-2 py-2 space-y-1.5" style={{ minHeight: rows * 24 + 8 }}>
                    {Array.from({ length: rows }).map((_, i) => {
                      const inp = node.inputs[i];
                      const outp = node.outputs[i];
                      return (
                        <div key={i} className="flex items-center justify-between gap-2 text-[11px]">
                          {inp ? (
                            <button
                              type="button"
                              onClick={() => handleTapInputPin(node.id, i)}
                              className="flex items-center gap-1.5 text-left text-slate-300 hover:text-white"
                            >
                              <span
                                className="inline-block h-3 w-3 rounded-full border border-white/30"
                                style={{ backgroundColor: PIN_COLOR[inp.type] ?? '#38BDF8' }}
                              />
                              <span>{inp.name}</span>
                            </button>
                          ) : (
                            <span />
                          )}
                          {outp ? (
                            <button
                              type="button"
                              onClick={() => handleTapOutputPin(node.id, i, outp.type)}
                              className="flex items-center gap-1.5 text-right text-slate-200 hover:text-white"
                            >
                              <span>{outp.name}</span>
                              <span
                                className={`inline-block h-3 w-3 rounded-full border ${
                                  pendingSource?.nodeId === node.id && pendingSource.pinIndex === i
                                    ? 'ring-2 ring-[#38BDF8]'
                                    : 'border-white/30'
                                }`}
                                style={{ backgroundColor: PIN_COLOR[outp.type] ?? '#38BDF8' }}
                              />
                            </button>
                          ) : (
                            <span />
                          )}
                        </div>
                      );
                    })}
                  </div>
                </div>
              );
            })}

            {graph.nodes.length === 0 && (
              <div className="flex h-full min-h-[360px] items-center justify-center text-sm text-slate-400">
                Add nodes from the toolbar and tap output to input pins to build logic.
              </div>
            )}
          </div>
        </div>

        {/* Lua Compiler & Sandboxed Runner */}
        <div className="flex flex-col border-t border-white/[0.08] bg-[#171A21] lg:border-t-0 lg:border-l">
          <div className="flex items-center justify-between border-b border-white/[0.08] px-3 py-2">
            <span className="text-xs font-medium text-slate-200">LuaJIT Sandbox & Output</span>
            <button
              type="button"
              onClick={() => onRunLua(luaCode)}
              className="flex h-8 items-center gap-1.5 rounded bg-[#38BDF8] px-2.5 text-xs font-semibold text-[#0F1115]"
            >
              <Sparkles className="h-3.5 w-3.5" />
              Run Lua
            </button>
          </div>
          <textarea
            value={luaCode}
            onChange={(e) => setLuaCode(e.target.value)}
            spellCheck={false}
            className="flex-1 min-h-[140px] resize-none bg-[#0F1115] p-3 font-mono text-xs text-slate-200 outline-none"
          />
          <div className="border-t border-white/[0.08] px-3 py-2 font-mono text-[11px] text-slate-300 space-y-1">
            <div>Trace: {trace.length ? trace.join(' -> ') : 'none'}</div>
            <div>Printed: {printed.length ? printed.join(', ') : 'none'}</div>
          </div>
        </div>
      </div>
    </div>
  );
};
