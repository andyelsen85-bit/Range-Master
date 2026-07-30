import React, { useState } from 'react';
import { useGameStore } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { SkipForward, RotateCcw, XOctagon, Zap, Layers } from 'lucide-react';
import { cn } from '@/lib/utils';

export function SpielScreen() {
  const store = useGameStore();
  const aktiverSpieler = store.getAktivenSpieler();
  const eintrag = store.sequenz[store.taubeIndex];
  const aktuelleMaschine = eintrag?.maschine ?? 'A';
  const istDoublette = aktuelleMaschine === 'H';
  const doubletteNr = eintrag?.doubletteNr;
  const [confirmAbort, setConfirmAbort] = useState(false);

  const taubenGesamt = store.sequenz.length;
  const aktuellePosten = aktiverSpieler
    ? ((aktiverSpieler.startPosten - 1 + store.taubeIndex) % store.spieler.length) + 1
    : 1;

  return (
    <div className="flex h-full w-full bg-background flex-col">
      <div className="flex flex-1 overflow-hidden">

        {/* Sidebar */}
        <div className="w-[340px] border-r-2 border-border bg-card p-6 flex flex-col gap-6">

          {/* Machine display */}
          <div className={cn(
            "border-4 rounded-2xl p-6 flex flex-col items-center justify-center aspect-square shadow-[0_0_30px_rgba(232,103,10,0.1)]",
            istDoublette
              ? "border-amber-500 bg-amber-500/10 animate-pulse"
              : "border-primary bg-primary/10 animate-pulse"
          )}>
            {istDoublette && (
              <div className="flex items-center gap-2 mb-2">
                <Layers className={cn("w-6 h-6", istDoublette ? "text-amber-400" : "text-primary")} />
                <span className={cn("text-lg font-bold uppercase tracking-widest", "text-amber-400")}>
                  Doublette {doubletteNr}/2
                </span>
              </div>
            )}
            {!istDoublette && (
              <div className="text-2xl font-bold text-primary mb-2 uppercase tracking-widest">Schanz</div>
            )}
            <div className={cn(
              "font-black leading-none",
              istDoublette ? "text-[100px] text-amber-400" : "text-[120px] text-primary"
            )}>{aktuelleMaschine}</div>
          </div>

          {/* Round / pigeon counter */}
          <div className="bg-background border-2 border-border rounded-xl p-4 text-center">
            <div className="text-lg text-muted-foreground font-bold uppercase mb-1">
              Lauf {store.lauf}/2
            </div>
            <div className="text-3xl font-bold font-mono">
              Taube {store.taubeIndex + 1}/{taubenGesamt}
            </div>
          </div>

          {/* Control buttons */}
          <div className="grid grid-cols-2 gap-4">
            <TouchButton className="h-20 flex-col gap-2" onClick={() => store.wiederholenTaube()}>
              <RotateCcw className="w-6 h-6" />
              <span className="text-sm">Widderhuelen</span>
            </TouchButton>
            <TouchButton className="h-20 flex-col gap-2" onClick={() => store.ueberspringenTaube()}>
              <SkipForward className="w-6 h-6" />
              <span className="text-sm">Iwwerspringen</span>
            </TouchButton>
          </div>

          {/* Abort */}
          {confirmAbort ? (
            <div className="grid grid-cols-2 gap-4">
              <TouchButton variant="destructive" className="h-20" onClick={() => { store.ofbriechenSpiel(); setConfirmAbort(false); }}>JA</TouchButton>
              <TouchButton className="h-20" onClick={() => setConfirmAbort(false)}>NEIN</TouchButton>
            </div>
          ) : (
            <TouchButton
              variant="outline"
              className="h-20 text-destructive border-destructive gap-2"
              onClick={() => setConfirmAbort(true)}
            >
              <XOctagon className="w-6 h-6" />
              Ofbriechen
            </TouchButton>
          )}

        </div>

        {/* Main Content */}
        <div className="flex-1 flex flex-col relative">

          <div className="flex-1 p-8 flex flex-col gap-8">
            {/* Active Player Banner */}
            <div className="bg-card border-2 border-primary/50 rounded-2xl p-8 flex items-center justify-between shadow-2xl">
              <div className="flex flex-col">
                <span className="text-2xl text-primary font-bold tracking-widest uppercase mb-2">Aktuellen Schütze</span>
                <span className="text-6xl font-black truncate max-w-[600px]">{aktiverSpieler?.name ?? '—'}</span>
              </div>
              <div className="flex flex-col items-end">
                <span className="text-2xl text-primary font-bold tracking-widest uppercase mb-2">Posten</span>
                <span className="text-7xl font-mono font-black">{aktuellePosten}</span>
              </div>
            </div>

            {/* Score Entry */}
            <div className="grid grid-cols-3 gap-6 flex-1">
              <TouchButton
                variant="success"
                className="flex-col gap-4 text-3xl shadow-[0_10px_0_rgb(22,163,74)] active:translate-y-2 active:shadow-[0_0px_0_rgb(22,163,74)] transition-all"
                onClick={() => store.eintragenErgebnis(true, false)}
              >
                <div className="font-black text-6xl">2</div>
                <div className="uppercase font-bold tracking-widest text-xl opacity-80">1. Schoss</div>
              </TouchButton>

              <TouchButton
                variant="warning"
                className="flex-col gap-4 text-3xl shadow-[0_10px_0_rgb(217,119,6)] active:translate-y-2 active:shadow-[0_0px_0_rgb(217,119,6)] transition-all"
                onClick={() => store.eintragenErgebnis(false, true)}
              >
                <div className="font-black text-6xl">1</div>
                <div className="uppercase font-bold tracking-widest text-xl opacity-80">2. Schoss</div>
              </TouchButton>

              <TouchButton
                variant="destructive"
                className="flex-col gap-4 text-3xl shadow-[0_10px_0_rgb(185,28,28)] active:translate-y-2 active:shadow-[0_0px_0_rgb(185,28,28)] transition-all"
                onClick={() => store.eintragenErgebnis(false, false)}
              >
                <div className="font-black text-6xl">0</div>
                <div className="uppercase font-bold tracking-widest text-xl opacity-80">Fehl</div>
              </TouchButton>
            </div>
          </div>

          {/* Bottom scoreboard */}
          <div className="h-32 border-t-2 border-border bg-card px-8 flex items-center gap-6 overflow-x-auto">
            {store.spieler.map(s => {
              const isActive = s.id === aktiverSpieler?.id;
              return (
                <div
                  key={s.id}
                  className={cn(
                    "flex-shrink-0 flex items-center gap-4 px-6 h-20 rounded-xl border-2 transition-colors",
                    isActive ? "bg-primary/20 border-primary" : "bg-background border-border"
                  )}
                >
                  <div className={cn(
                    "w-12 h-12 rounded-lg flex items-center justify-center font-black text-xl",
                    isActive ? "bg-primary text-black" : "bg-border text-muted-foreground"
                  )}>
                    P{((s.startPosten - 1 + store.taubeIndex) % store.spieler.length) + 1}
                  </div>
                  <div className="flex flex-col justify-center">
                    <div className="font-bold text-lg max-w-[120px] truncate">{s.name}</div>
                    <div className="font-mono text-xl font-bold text-primary">
                      {s.punkte} <span className="text-sm text-muted-foreground">pts</span>
                    </div>
                  </div>
                </div>
              );
            })}
          </div>

          {/* Taube Werfen button — simulates physical relay trigger */}
          <div className="absolute right-8 bottom-40">
            <TouchButton
              size="xl"
              className="rounded-full w-32 h-32 bg-primary/20 border-4 border-primary text-primary hover:bg-primary hover:text-black transition-all flex flex-col gap-2"
              onClick={() => store.werfenTaube()}
            >
              <Zap className="w-12 h-12" />
              <span className="text-xs uppercase font-bold tracking-widest">Werfen</span>
            </TouchButton>
          </div>

        </div>
      </div>
    </div>
  );
}
