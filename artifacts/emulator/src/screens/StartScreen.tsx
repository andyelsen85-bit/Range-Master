import React, { useState } from 'react';
import { useGameStore, PortalSpieler } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import {
  ArrowLeft, Play, UserPlus, Trash2, Download, Loader2,
  AlertCircle, X, Check, KeyboardIcon,
} from 'lucide-react';
import { cn } from '@/lib/utils';

const MODI = [
  { value: 'NORMAL' as const,             label: 'Normal',             desc: 'A→G→H der Rei no' },
  { value: 'HARAKIRI' as const,           label: 'Harakiri',           desc: 'A–G gemëscht, H um Enn' },
  { value: 'HARAKIRI_DELAYED' as const,   label: 'Harakiri Verspéit',  desc: 'Gemëscht mat Versatz' },
  { value: 'HARAKIRI_FULL' as const,      label: 'Harakiri Full',      desc: 'All Schanzen gemëscht' },
  { value: 'CUSTOM_1' as const,           label: 'Custom 1',           desc: 'Benotzer-definéiert' },
  { value: 'CUSTOM_2' as const,           label: 'Custom 2',           desc: 'Benotzer-definéiert' },
];

const POSTS = [1, 2, 3, 4, 5] as const;

type PickerTab = 'portal' | 'manual';

export function StartScreen() {
  const store = useGameStore();

  // Which post slot is open for assignment (null = none)
  const [activePost, setActivePost] = useState<number | null>(null);
  const [pickerTab, setPickerTab] = useState<PickerTab>('portal');
  const [manualName, setManualName] = useState('');

  // ── helpers ────────────────────────────────────────────────────────────────

  const playerAtPost = (post: number) =>
    store.spieler.find(s => s.startPosten === post) ?? null;

  const isAssigned = (id: number) => store.spieler.some(s => s.id === id);

  const openPicker = (post: number) => {
    setActivePost(post);
    setPickerTab('portal');
    setManualName('');
  };

  const closePicker = () => setActivePost(null);

  const assignFromPortal = (ps: PortalSpieler) => {
    if (activePost === null) return;
    store.setSpielerAufPosten(activePost, { id: ps.id, name: ps.name });
    closePicker();
  };

  const assignManual = () => {
    if (activePost === null || !manualName.trim()) return;
    store.setSpielerAufPosten(activePost, { id: Date.now(), name: manualName.trim() });
    closePicker();
  };

  const clearPost = (post: number) => {
    store.setSpielerAufPosten(post, null);
    if (activePost === post) closePicker();
  };

  const handleLaden = async () => {
    await store.ladeSpielerVomPortal();
  };

  const assignedCount = store.spieler.length;

  return (
    <div className="flex h-full w-full bg-background flex-col">
      {/* ── Header ───────────────────────────────────────────────────────── */}
      <header className="h-16 border-b-2 border-border flex items-center px-6 bg-card gap-4 shrink-0 pr-20">
        <TouchButton onClick={() => store.setScreen('dashboard')} className="w-12 h-12 p-0">
          <ArrowLeft className="w-6 h-6" />
        </TouchButton>
        <h1 className="text-xl font-bold tracking-wider text-primary">SPILL ASTELLEN</h1>
        <div className="ml-auto">
          <TouchButton
            size="lg"
            variant="primary"
            className="gap-3 h-12 px-6 text-base shadow-xl"
            onClick={() => store.startSpiel()}
            disabled={assignedCount === 0}
          >
            <Play className="w-5 h-5" />
            STARTEN
          </TouchButton>
        </div>
      </header>

      <div className="flex flex-1 overflow-hidden">

        {/* ── Left: Post slots ──────────────────────────────────────────── */}
        <div className="w-[420px] border-r-2 border-border flex flex-col">

          {/* Section header */}
          <div className="p-4 border-b-2 border-border bg-card/50 shrink-0">
            <h2 className="text-base font-bold uppercase tracking-widest text-foreground/80">
              Posten{' '}
              <span className="text-primary font-black">{assignedCount}</span>
              <span className="text-muted-foreground font-normal">/5 besetzt</span>
            </h2>
            <p className="text-xs text-muted-foreground mt-0.5">
              Tippen op <span className="text-primary font-bold">+</span> fir e Schützen ze wielen
            </p>
          </div>

          {/* 5 post slot cards */}
          <div className="flex-1 overflow-y-auto p-4 flex flex-col gap-3">
            {POSTS.map((post) => {
              const player = playerAtPost(post);
              const isActive = activePost === post;

              return (
                <div key={post} className="flex flex-col gap-0">
                  {/* Post slot row */}
                  <div className={cn(
                    "flex items-center gap-3 border-2 rounded-xl p-3 transition-all",
                    isActive
                      ? "border-primary bg-primary/5"
                      : player
                        ? "border-border bg-card"
                        : "border-border/40 bg-background/40",
                  )}>
                    {/* Post badge */}
                    <div className={cn(
                      "w-12 h-12 rounded-xl flex items-center justify-center font-black text-lg shrink-0 transition-all",
                      isActive
                        ? "bg-primary text-primary-foreground"
                        : player
                          ? "bg-primary/20 text-primary"
                          : "bg-muted/30 text-muted-foreground/50",
                    )}>
                      P{post}
                    </div>

                    {/* Player name or empty label */}
                    <div className="flex-1 min-w-0">
                      {player ? (
                        <div>
                          <div className="font-bold text-base text-foreground truncate">
                            {player.name}
                          </div>
                          <div className="text-xs text-muted-foreground mt-0.5">
                            Startposten {post}
                          </div>
                        </div>
                      ) : (
                        <div className="text-muted-foreground/60 font-medium text-sm italic">
                          {isActive ? 'Schützen wielen…' : 'Eidel'}
                        </div>
                      )}
                    </div>

                    {/* Action buttons */}
                    <div className="flex gap-2 shrink-0">
                      {player ? (
                        <TouchButton
                          variant="ghost"
                          className="w-10 h-10 p-0 text-destructive hover:bg-destructive/10"
                          onClick={() => clearPost(post)}
                        >
                          <Trash2 className="w-4 h-4" />
                        </TouchButton>
                      ) : (
                        <TouchButton
                          variant={isActive ? 'primary' : 'outline'}
                          className="w-10 h-10 p-0 text-lg font-bold"
                          onClick={() => isActive ? closePicker() : openPicker(post)}
                        >
                          {isActive ? <X className="w-4 h-4" /> : '+'}
                        </TouchButton>
                      )}
                    </div>
                  </div>

                  {/* Inline picker — expands under the active post */}
                  {isActive && (
                    <div className="border-2 border-primary/40 border-t-0 rounded-b-xl bg-card/80 overflow-hidden">

                      {/* Picker tabs */}
                      <div className="flex border-b border-border/60">
                        <button
                          onClick={() => setPickerTab('portal')}
                          className={cn(
                            "flex-1 h-10 flex items-center justify-center gap-2 text-sm font-bold transition-colors",
                            pickerTab === 'portal'
                              ? "bg-primary/10 text-primary border-b-2 border-primary"
                              : "text-muted-foreground hover:text-foreground",
                          )}
                        >
                          <Download className="w-3.5 h-3.5" />
                          Portal
                        </button>
                        <button
                          onClick={() => setPickerTab('manual')}
                          className={cn(
                            "flex-1 h-10 flex items-center justify-center gap-2 text-sm font-bold transition-colors",
                            pickerTab === 'manual'
                              ? "bg-primary/10 text-primary border-b-2 border-primary"
                              : "text-muted-foreground hover:text-foreground",
                          )}
                        >
                          <KeyboardIcon className="w-3.5 h-3.5" />
                          Manuell
                        </button>
                      </div>

                      {/* Portal tab content */}
                      {pickerTab === 'portal' && (
                        <div className="p-3 flex flex-col gap-2">
                          <div className="flex items-center justify-between">
                            <span className="text-xs text-muted-foreground">
                              {store.spielerAusCache
                                ? '📦 Offline-Cache'
                                : store.portalSpieler.length > 0
                                  ? `${store.portalSpieler.length} Spillesch`
                                  : 'Spillesch aus Portal lueden'}
                            </span>
                            <TouchButton
                              variant="outline"
                              className="h-7 px-3 gap-1 text-xs"
                              onClick={handleLaden}
                              disabled={store.portalLaden}
                            >
                              {store.portalLaden
                                ? <Loader2 className="w-3 h-3 animate-spin" />
                                : <Download className="w-3 h-3" />}
                              Laden
                            </TouchButton>
                          </div>

                          {store.portalFehler && (
                            <div className="flex items-center gap-2 text-xs text-red-400 bg-red-500/10 border border-red-500/30 rounded-lg px-3 py-2">
                              <AlertCircle className="w-3 h-3 shrink-0" />
                              {store.portalFehler}
                            </div>
                          )}

                          {store.portalSpieler.length > 0 ? (
                            <div className="flex flex-col gap-1 max-h-44 overflow-y-auto pr-1">
                              {store.portalSpieler.map(ps => {
                                const already = isAssigned(ps.id);
                                return (
                                  <button
                                    key={ps.id}
                                    onClick={() => !already && assignFromPortal(ps)}
                                    disabled={already}
                                    className={cn(
                                      "flex items-center gap-3 px-3 py-2 rounded-lg border text-left transition-all",
                                      already
                                        ? "border-green-500/30 bg-green-500/5 text-green-500/60 cursor-default"
                                        : "border-border hover:border-primary/60 hover:bg-primary/10 cursor-pointer active:scale-[0.98]",
                                    )}
                                  >
                                    {already
                                      ? <Check className="w-4 h-4 text-green-500/60 shrink-0" />
                                      : <UserPlus className="w-4 h-4 text-muted-foreground shrink-0" />}
                                    <span className="font-bold text-sm flex-1">{ps.name}</span>
                                    {ps.mitgliedNr && (
                                      <span className="text-xs text-muted-foreground font-mono">{ps.mitgliedNr}</span>
                                    )}
                                  </button>
                                );
                              })}
                            </div>
                          ) : !store.portalLaden && !store.portalFehler && (
                            <div className="text-xs text-muted-foreground italic text-center py-3">
                              Dréckt "Laden" fir Spillesch aus dem Portal ze lueden
                            </div>
                          )}
                        </div>
                      )}

                      {/* Manual tab content */}
                      {pickerTab === 'manual' && (
                        <div className="p-3 flex gap-2">
                          <input
                            autoFocus
                            type="text"
                            value={manualName}
                            onChange={e => setManualName(e.target.value)}
                            onKeyDown={e => e.key === 'Enter' && assignManual()}
                            placeholder="Numm aginn…"
                            className="flex-1 bg-background border-2 border-border rounded-lg h-10 px-3 text-sm font-bold focus:border-primary focus:outline-none"
                          />
                          <TouchButton
                            variant="primary"
                            className="h-10 w-10 p-0 shrink-0"
                            onClick={assignManual}
                            disabled={!manualName.trim()}
                          >
                            <Check className="w-4 h-4" />
                          </TouchButton>
                        </div>
                      )}
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        </div>

        {/* ── Right: Mode + Machines + Summary ─────────────────────────── */}
        <div className="flex-1 flex flex-col overflow-hidden p-3 gap-3">

          {/* Mode selector */}
          <div className="bg-card border-2 border-border rounded-xl p-3">
            <h2 className="text-sm font-bold uppercase tracking-widest text-foreground/80 mb-2">Modus</h2>
            <div className="grid grid-cols-2 gap-2">
              {MODI.map(m => (
                <TouchButton
                  key={m.value}
                  variant={store.modus === m.value ? 'primary' : 'default'}
                  className="h-12 flex-col gap-0.5 items-start px-3"
                  onClick={() => store.setModus(m.value)}
                >
                  <span className="font-bold text-sm">{m.label}</span>
                  <span className="text-[10px] opacity-70 font-normal leading-tight">{m.desc}</span>
                </TouchButton>
              ))}
            </div>
          </div>

          {/* Machine toggles */}
          <div className="bg-card border-2 border-border rounded-xl p-3">
            <h2 className="text-sm font-bold uppercase tracking-widest text-foreground/80 mb-2">
              Schanzen
            </h2>
            <div className="grid grid-cols-4 gap-2">
              {(['A','B','C','D','E','F','G','H'] as const).map(m => {
                const aktiv = store.maschinenAktiv[m];
                return (
                  <button
                    key={m}
                    onClick={() => store.toggleMaschineAktiv(m)}
                    className={cn(
                      "h-12 rounded-xl border-2 flex flex-col items-center justify-center transition-all active:scale-95",
                      aktiv
                        ? m === 'H'
                          ? "border-amber-500/60 bg-amber-500/15 text-amber-400"
                          : "border-primary/60 bg-primary/15 text-primary"
                        : "border-border/30 bg-background/40 text-muted-foreground/40",
                    )}
                  >
                    <span className="text-xl font-black">{m}</span>
                    <span className="text-[9px] font-bold uppercase tracking-widest">
                      {m === 'H' ? 'Doubl.' : aktiv ? '●' : '○'}
                    </span>
                  </button>
                );
              })}
            </div>
          </div>

          {/* Game summary */}
          <div className="bg-card/50 border-2 border-border rounded-xl p-3">
            <h2 className="text-xs font-bold uppercase tracking-widest text-muted-foreground mb-2">
              Spillübersicht
            </h2>
            <div className="grid grid-cols-3 gap-2 text-center">
              <div className="bg-background rounded-lg p-2">
                <div className="text-xl font-black text-primary">{assignedCount}</div>
                <div className="text-[10px] text-muted-foreground font-bold mt-0.5">Schützen</div>
              </div>
              <div className="bg-background rounded-lg p-2">
                <div className="text-xl font-black text-primary">2</div>
                <div className="text-[10px] text-muted-foreground font-bold mt-0.5">Läufe</div>
              </div>
              <div className="bg-background rounded-lg p-2">
                <div className="text-xl font-black text-primary">
                  {(() => {
                    const active = Object.values(store.maschinenAktiv).filter(Boolean).length;
                    const hasH = store.maschinenAktiv['H'];
                    return hasH ? active - 1 + 2 : active;
                  })()}
                </div>
                <div className="text-[10px] text-muted-foreground font-bold mt-0.5">Tauben/Lauf</div>
              </div>
            </div>
          </div>

        </div>
      </div>
    </div>
  );
}
