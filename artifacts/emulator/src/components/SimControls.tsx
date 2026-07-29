import React, { useState } from 'react';
import { useGameStore, getAktiverSpieler } from '@/store/gameStore';
import { Settings2, Cpu, Activity, X } from 'lucide-react';
import { cn } from '@/lib/utils';

export function SimControls() {
  const [open, setOpen] = useState(false);
  const store = useGameStore();

  if (!open) {
    return (
      <button 
        onClick={() => setOpen(true)}
        className="fixed top-4 right-4 z-50 bg-black/80 border border-primary text-primary p-3 rounded-lg shadow-2xl hover:bg-primary hover:text-black transition-colors"
      >
        <Cpu className="w-6 h-6" />
      </button>
    );
  }

  const aktiverSpieler = store.screen === 'spiel' ? getAktiverSpieler(store) : null;

  return (
    <div className="fixed top-4 right-4 z-50 w-80 bg-black/90 border border-primary rounded-xl shadow-2xl p-4 font-mono text-sm overflow-hidden flex flex-col gap-4">
      <div className="flex items-center justify-between border-b border-primary/30 pb-2">
        <div className="flex items-center gap-2 text-primary font-bold">
          <Activity className="w-4 h-4" />
          EMULATOR DEVTOOLS
        </div>
        <button onClick={() => setOpen(false)} className="text-muted-foreground hover:text-white">
          <X className="w-5 h-5" />
        </button>
      </div>

      <div className="flex flex-col gap-2">
        <div className="flex justify-between">
          <span className="text-muted-foreground">Screen:</span>
          <span className="text-white">{store.screen}</span>
        </div>
        <div className="flex justify-between">
          <span className="text-muted-foreground">Modus:</span>
          <span className="text-white">{store.modus}</span>
        </div>
        
        {store.screen === 'spiel' && (
          <>
            <div className="flex justify-between">
              <span className="text-muted-foreground">Lauf / Taube:</span>
              <span className="text-white">{store.lauf} / {store.taubeIndex + 1}</span>
            </div>
            <div className="flex justify-between">
              <span className="text-muted-foreground">Maschine:</span>
              <span className="text-primary font-bold">{store.sequenz[store.taubeIndex]}</span>
            </div>
            <div className="flex justify-between">
              <span className="text-muted-foreground">Aktiver P.:</span>
              <span className="text-white">{aktiverSpieler?.name} (P{aktiverSpieler?.posten})</span>
            </div>
          </>
        )}
      </div>

      <div className="border-t border-primary/30 pt-4 flex flex-col gap-2">
        <span className="text-muted-foreground font-bold text-xs uppercase mb-1">Quick Actions</span>
        <button 
          onClick={() => store.setScreen('dashboard')}
          className="w-full bg-primary/20 text-primary border border-primary/50 py-2 rounded hover:bg-primary hover:text-black transition-colors"
        >
          Reset to Dashboard
        </button>
        <button 
          onClick={() => {
            if (store.screen === 'spiel') {
               // sim hardware fire
               store.werfenTaube();
            }
          }}
          className={cn(
            "w-full py-2 rounded border transition-colors",
            store.screen === 'spiel' 
              ? "bg-amber-500/20 text-amber-500 border-amber-500/50 hover:bg-amber-500 hover:text-black" 
              : "bg-background text-muted-foreground border-border cursor-not-allowed"
          )}
        >
          Hardware: WURF_TRIGGER
        </button>
      </div>
    </div>
  );
}
