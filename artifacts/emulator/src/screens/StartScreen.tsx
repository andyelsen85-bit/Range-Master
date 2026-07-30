import React, { useState } from 'react';
import { useGameStore, PortalSpieler, Spieler } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { ArrowLeft, Play, UserPlus, Trash2, Download, Loader2, AlertCircle, CheckCircle2 } from 'lucide-react';
import { cn } from '@/lib/utils';

const MODI = [
  { value: 'NORMAL' as const,         label: 'Normal',          desc: 'A→G→H der Rei no' },
  { value: 'HARAKIRI' as const,       label: 'Harakiri',        desc: 'A–G gemëscht, H um Enn' },
  { value: 'HARAKIRI_DELAYED' as const, label: 'Harakiri Verspéit', desc: 'Gemëscht mat Versatz' },
  { value: 'HARAKIRI_FULL' as const,  label: 'Harakiri Full',   desc: 'All Schanzen gemëscht' },
  { value: 'CUSTOM_1' as const,       label: 'Custom 1',        desc: 'Benotzer-definéiert' },
  { value: 'CUSTOM_2' as const,       label: 'Custom 2',        desc: 'Benotzer-definéiert' },
];

export function StartScreen() {
  const store = useGameStore();
  const [showPortalPanel, setShowPortalPanel] = useState(false);

  // ── Player management ────────────────────────────────────────────────────

  const addManualPlayer = () => {
    if (store.spieler.length >= 6) return;
    const newSpieler: Spieler[] = [
      ...store.spieler,
      {
        id: Date.now(),
        name: `Schütze ${store.spieler.length + 1}`,
        punkte: 0,
        startPosten: (store.spieler.length % 5) + 1,
      },
    ];
    store.setSpieler(newSpieler);
  };

  const removePlayer = (id: number) => {
    store.setSpieler(store.spieler.filter(s => s.id !== id));
  };

  const addFromPortal = (ps: PortalSpieler) => {
    if (store.spieler.length >= 6) return;
    if (store.spieler.some(s => s.id === ps.id)) return; // already added
    const newSpieler: Spieler[] = [
      ...store.spieler,
      {
        id: ps.id,
        name: ps.name,
        punkte: 0,
        startPosten: (store.spieler.length % 5) + 1,
      },
    ];
    store.setSpieler(newSpieler);
  };

  const isInGame = (id: number) => store.spieler.some(s => s.id === id);

  // ── Portal panel ─────────────────────────────────────────────────────────

  const handleLaden = async () => {
    await store.ladeSpielerVomPortal();
  };

  return (
    <div className="flex h-full w-full bg-background flex-col">
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
            disabled={store.spieler.length === 0}
          >
            <Play className="w-5 h-5" />
            STARTEN
          </TouchButton>
        </div>
      </header>

      <div className="flex flex-1 overflow-hidden">

        {/* ── Left: Players ─────────────────────────────────────────── */}
        <div className="w-[420px] border-r-2 border-border flex flex-col">

          <div className="p-4 border-b-2 border-border flex items-center justify-between bg-card/50">
            <h2 className="text-base font-bold uppercase tracking-widest text-foreground/80">
              Schützen ({store.spieler.length}/6)
            </h2>
            <div className="flex gap-2">
              <TouchButton
                variant="outline"
                className="h-10 px-3 gap-2 text-sm"
                onClick={() => { setShowPortalPanel(!showPortalPanel); }}
              >
                <Download className="w-4 h-4" />
                Portal
              </TouchButton>
              <TouchButton
                variant="outline"
                className="h-10 px-3 gap-2 text-sm"
                onClick={addManualPlayer}
                disabled={store.spieler.length >= 6}
              >
                <UserPlus className="w-4 h-4" />
                Manuell
              </TouchButton>
            </div>
          </div>

          {/* Portal player picker panel */}
          {showPortalPanel && (
            <div className="border-b-2 border-primary/40 bg-primary/5 p-3 flex flex-col gap-2">
              <div className="flex items-center justify-between">
                <span className="text-xs font-bold text-primary uppercase tracking-wider">
                  Spieler aus Portal
                </span>
                <TouchButton
                  variant="outline"
                  className="h-8 px-3 gap-1 text-xs"
                  onClick={handleLaden}
                  disabled={store.portalLaden}
                >
                  {store.portalLaden
                    ? <Loader2 className="w-3 h-3 animate-spin" />
                    : <Download className="w-3 h-3" />
                  }
                  Laden
                </TouchButton>
              </div>

              {store.portalFehler && (
                <div className="flex items-center gap-2 text-xs text-red-400 bg-red-500/10 border border-red-500/30 rounded-lg px-3 py-2">
                  <AlertCircle className="w-3 h-3 shrink-0" />
                  {store.portalFehler}
                </div>
              )}

              {store.portalSpieler.length > 0 && (
                <div className="flex flex-col gap-1 max-h-40 overflow-y-auto pr-1">
                  {store.portalSpieler.map(ps => {
                    const added = isInGame(ps.id);
                    return (
                      <button
                        key={ps.id}
                        onClick={() => !added && addFromPortal(ps)}
                        disabled={added || store.spieler.length >= 6}
                        className={cn(
                          "flex items-center gap-3 px-3 py-2 rounded-lg border text-left transition-all",
                          added
                            ? "border-green-500/40 bg-green-500/10 text-green-400 cursor-default"
                            : store.spieler.length >= 6
                              ? "border-border/30 text-muted-foreground/50 cursor-not-allowed"
                              : "border-border hover:border-primary/60 hover:bg-primary/10 cursor-pointer",
                        )}
                      >
                        {added
                          ? <CheckCircle2 className="w-4 h-4 text-green-400 shrink-0" />
                          : <UserPlus className="w-4 h-4 text-muted-foreground shrink-0" />
                        }
                        <span className="font-bold text-sm flex-1">{ps.name}</span>
                        {ps.mitgliedNr && (
                          <span className="text-xs text-muted-foreground font-mono">{ps.mitgliedNr}</span>
                        )}
                      </button>
                    );
                  })}
                </div>
              )}

              {store.portalSpieler.length === 0 && !store.portalLaden && !store.portalFehler && (
                <div className="text-xs text-muted-foreground italic text-center py-2">
                  Dréckt "Laden" fir Spieler aus dem Portal ze lueden
                </div>
              )}
            </div>
          )}

          {/* Active game players */}
          <div className="flex-1 overflow-y-auto p-4 flex flex-col gap-3">
            {store.spieler.map((s) => (
              <div key={s.id} className="flex items-center gap-3 bg-card border-2 border-border p-3 rounded-xl">
                <div className="w-10 h-10 rounded-lg bg-primary/20 flex items-center justify-center font-black text-base text-primary shrink-0">
                  P{s.startPosten}
                </div>
                <input
                  type="text"
                  value={s.name}
                  onChange={(e) => store.updateSpielerName(s.id, e.target.value)}
                  className="flex-1 bg-background border-2 border-border rounded-lg h-12 px-3 text-base font-bold focus:border-primary focus:outline-none"
                />
                <TouchButton
                  onClick={() => removePlayer(s.id)}
                  variant="ghost"
                  className="w-10 h-10 p-0 text-destructive hover:bg-destructive/10 shrink-0"
                >
                  <Trash2 className="w-5 h-5" />
                </TouchButton>
              </div>
            ))}

            {store.spieler.length === 0 && (
              <div className="h-24 border-2 border-dashed border-border rounded-xl flex items-center justify-center text-muted-foreground font-bold">
                Keng Schützen
              </div>
            )}
          </div>

          {/* Posten assignment preview */}
          {store.spieler.length > 0 && (
            <div className="border-t-2 border-border bg-card/30 p-3">
              <div className="text-xs font-bold text-muted-foreground uppercase tracking-wider mb-2">
                Startposten (Taube 1)
              </div>
              <div className="flex gap-2 flex-wrap">
                {[1,2,3,4,5].map(pos => {
                  const here = store.spieler.filter(s => s.startPosten === pos);
                  return (
                    <div key={pos} className={cn(
                      "flex-1 min-w-[70px] rounded-lg border p-2 text-center",
                      here.length > 0 ? "border-primary/40 bg-primary/5" : "border-border/30 bg-background/30",
                    )}>
                      <div className="text-xs font-bold text-muted-foreground mb-1">P{pos}</div>
                      {here.map(s => (
                        <div key={s.id} className="text-xs font-bold truncate text-foreground">{s.name}</div>
                      ))}
                      {here.length === 0 && <div className="text-muted-foreground/40 text-sm">—</div>}
                    </div>
                  );
                })}
              </div>
            </div>
          )}
        </div>

        {/* ── Right: Mode + Settings ────────────────────────────────── */}
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

          {/* Schanzen aktiv/deaktiviert */}
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

          {/* Game info summary */}
          <div className="bg-card/50 border-2 border-border rounded-xl p-3">
            <h2 className="text-xs font-bold uppercase tracking-widest text-muted-foreground mb-2">
              Spillübersicht
            </h2>
            <div className="grid grid-cols-3 gap-2 text-center">
              <div className="bg-background rounded-lg p-2">
                <div className="text-xl font-black text-primary">{store.spieler.length}</div>
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
