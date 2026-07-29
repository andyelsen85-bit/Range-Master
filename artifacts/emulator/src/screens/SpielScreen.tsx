import React, { useState } from 'react';
import { useGameStore, getAktiverSpieler } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { Mic, MicOff, SkipForward, RotateCcw, XOctagon, Zap } from 'lucide-react';
import { cn } from '@/lib/utils';

export function SpielScreen() {
  const store = useGameStore();
  const aktiverSpieler = getAktiverSpieler(store);
  const aktuelleMaschine = store.sequenz[store.taubeIndex];
  const [confirmAbort, setConfirmAbort] = useState(false);

  return (
    <div className="flex h-full w-full bg-background flex-col">
      <div className="flex flex-1 overflow-hidden">
        
        {/* Sidebar Navigation */}
        <div className="w-[340px] border-r-2 border-border bg-card p-6 flex flex-col gap-6">
          
          <div className="border-4 border-primary bg-primary/10 rounded-2xl p-6 flex flex-col items-center justify-center aspect-square animate-pulse shadow-[0_0_30px_rgba(232,103,10,0.1)]">
            <div className="text-2xl font-bold text-primary mb-2">SCHANZ</div>
            <div className="text-[120px] font-black text-primary leading-none">{aktuelleMaschine}</div>
          </div>

          <div className="bg-background border-2 border-border rounded-xl p-4 text-center">
            <div className="text-lg text-muted-foreground font-bold uppercase mb-1">Runde {store.lauf}/5</div>
            <div className="text-3xl font-bold font-mono">Taube {store.taubeIndex + 1}/8</div>
          </div>

          <TouchButton 
            className="h-24 w-full gap-4 text-2xl"
            variant={store.mikrofon ? 'primary' : 'default'}
            onClick={() => store.toggleMikrofon()}
          >
            {store.mikrofon ? <Mic className="w-8 h-8" /> : <MicOff className="w-8 h-8" />}
            {store.mikrofon ? 'MIC ON' : 'MIC OFF'}
          </TouchButton>

          <div className="mt-auto grid grid-cols-2 gap-4">
            <TouchButton className="h-20 flex-col gap-2" onClick={() => store.wiederholenTaube()}>
              <RotateCcw className="w-6 h-6" />
              <span className="text-sm">Widderhuelen</span>
            </TouchButton>
            <TouchButton className="h-20 flex-col gap-2" onClick={() => store.ueberspringenTaube()}>
              <SkipForward className="w-6 h-6" />
              <span className="text-sm">Iwwerspringen</span>
            </TouchButton>
          </div>

          {confirmAbort ? (
            <div className="grid grid-cols-2 gap-4">
              <TouchButton variant="destructive" className="h-20" onClick={() => store.ofbriechenSpiel()}>JA</TouchButton>
              <TouchButton className="h-20" onClick={() => setConfirmAbort(false)}>NEIN</TouchButton>
            </div>
          ) : (
            <TouchButton variant="outline" className="h-20 text-destructive border-destructive gap-2" onClick={() => setConfirmAbort(true)}>
              <XOctagon className="w-6 h-6" />
              Ofbriechen
            </TouchButton>
          )}

        </div>

        {/* Main Content Area */}
        <div className="flex-1 flex flex-col relative">
          
          <div className="flex-1 p-8 flex flex-col gap-8">
            {/* Active Player Banner */}
            <div className="bg-card border-2 border-primary/50 rounded-2xl p-8 flex items-center justify-between shadow-2xl">
              <div className="flex flex-col">
                <span className="text-2xl text-primary font-bold tracking-widest uppercase mb-2">Aktueller Schütze</span>
                <span className="text-6xl font-black truncate max-w-[600px]">{aktiverSpieler.name}</span>
              </div>
              <div className="flex flex-col items-end">
                <span className="text-2xl text-primary font-bold tracking-widest uppercase mb-2">Posten</span>
                <span className="text-7xl font-mono font-black">{aktiverSpieler.posten}</span>
              </div>
            </div>

            {/* Score Entry Buttons */}
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

          {/* Bottom Status Bar / Scoreboard */}
          <div className="h-32 border-t-2 border-border bg-card px-8 flex items-center gap-6 overflow-x-auto">
            {store.spieler.map(s => (
              <div 
                key={s.id} 
                className={cn(
                  "flex-shrink-0 flex items-center gap-4 px-6 h-20 rounded-xl border-2 transition-colors",
                  s.id === aktiverSpieler.id ? "bg-primary/20 border-primary" : "bg-background border-border"
                )}
              >
                <div className={cn(
                  "w-12 h-12 rounded-lg flex items-center justify-center font-black text-xl",
                  s.id === aktiverSpieler.id ? "bg-primary text-black" : "bg-border text-muted-foreground"
                )}>
                  P{s.posten}
                </div>
                <div className="flex flex-col justify-center">
                  <div className="font-bold text-lg max-w-[120px] truncate">{s.name}</div>
                  <div className="font-mono text-xl font-bold text-primary">{s.punkte} <span className="text-sm text-muted-foreground">pts</span></div>
                </div>
              </div>
            ))}
          </div>

          {/* Giant Hardware Button Overlay (simulating physical trigger) */}
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
