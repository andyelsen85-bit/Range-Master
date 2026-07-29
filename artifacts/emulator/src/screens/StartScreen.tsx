import React from 'react';
import { useGameStore } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { ArrowLeft, Play, UserPlus, Trash2 } from 'lucide-react';
import { cn } from '@/lib/utils';

export function StartScreen() {
  const store = useGameStore();

  const addPlayer = () => {
    if (store.spieler.length >= 5) return;
    store.setSpieler([
      ...store.spieler,
      { id: Date.now(), name: `Schütze ${store.spieler.length + 1}`, punkte: 0, posten: store.spieler.length + 1 }
    ]);
  };

  const removePlayer = (id: number) => {
    store.setSpieler(store.spieler.filter(s => s.id !== id).map((s, i) => ({ ...s, posten: i + 1 })));
  };

  return (
    <div className="flex h-full w-full bg-background flex-col">
      <header className="h-20 border-b-2 border-border flex items-center px-6 bg-card gap-6">
        <TouchButton onClick={() => store.setScreen('dashboard')} className="w-16 h-16 p-0">
          <ArrowLeft className="w-8 h-8" />
        </TouchButton>
        <h1 className="text-2xl font-bold tracking-wider text-primary">SPIEL EINRICHTEN</h1>
      </header>

      <div className="flex flex-1 overflow-hidden p-8 gap-8">
        {/* Players Column */}
        <div className="w-1/2 flex flex-col gap-6">
          <div className="flex items-center justify-between">
            <h2 className="text-2xl font-bold uppercase tracking-widest text-foreground/80">Schützen ({store.spieler.length}/5)</h2>
            <TouchButton onClick={addPlayer} disabled={store.spieler.length >= 5} variant="outline" className="h-12 px-4 gap-2">
              <UserPlus className="w-6 h-6" /> Add
            </TouchButton>
          </div>
          
          <div className="flex flex-col gap-4 overflow-y-auto pr-2">
            {store.spieler.map((s, i) => (
              <div key={s.id} className="flex items-center gap-4 bg-card border-2 border-border p-4 rounded-xl">
                <div className="w-12 h-12 rounded-lg bg-border flex items-center justify-center font-bold text-xl text-muted-foreground">
                  P{s.posten}
                </div>
                <input 
                  type="text"
                  value={s.name}
                  onChange={(e) => store.updateSpielerName(s.id, e.target.value)}
                  className="flex-1 bg-background border-2 border-border rounded-lg h-14 px-4 text-xl font-bold focus:border-primary focus:outline-none"
                />
                <TouchButton onClick={() => removePlayer(s.id)} variant="ghost" className="w-14 h-14 p-0 text-destructive hover:bg-destructive/10 hover:text-destructive">
                  <Trash2 className="w-7 h-7" />
                </TouchButton>
              </div>
            ))}
            {store.spieler.length === 0 && (
              <div className="h-32 border-2 border-dashed border-border rounded-xl flex items-center justify-center text-xl text-muted-foreground font-bold">
                Keine Schützen
              </div>
            )}
          </div>
        </div>

        {/* Settings Column */}
        <div className="flex-1 flex flex-col gap-8">
          <div className="bg-card border-2 border-border p-8 rounded-xl flex flex-col gap-6">
            <h2 className="text-2xl font-bold uppercase tracking-widest text-foreground/80">Modus</h2>
            <div className="grid grid-cols-3 gap-4">
              {(['NORMAL', 'HARAKIRI', 'CUSTOM_1'] as const).map(m => (
                <TouchButton 
                  key={m}
                  variant={store.modus === m ? 'primary' : 'default'}
                  className="h-20 text-xl"
                  onClick={() => store.setModus(m)}
                >
                  {m}
                </TouchButton>
              ))}
            </div>
          </div>

          <div className="bg-card border-2 border-border p-8 rounded-xl flex flex-col gap-6">
            <div className="flex justify-between items-end">
              <h2 className="text-2xl font-bold uppercase tracking-widest text-foreground/80">Doublette Versatz</h2>
              <span className="text-3xl font-mono font-bold text-primary">{store.doubletteVersatz.toFixed(1)}s</span>
            </div>
            
            <input 
              type="range" 
              min="0" max="3" step="0.1" 
              value={store.doubletteVersatz}
              onChange={(e) => store.setDoubletteVersatz(parseFloat(e.target.value))}
              className="w-full h-4 bg-background rounded-lg appearance-none cursor-pointer accent-primary"
            />
            <div className="flex justify-between text-muted-foreground font-bold font-mono">
              <span>0.0s</span>
              <span>1.5s</span>
              <span>3.0s</span>
            </div>
          </div>

          <div className="mt-auto">
            <TouchButton 
              size="xl" 
              variant="primary" 
              className="w-full gap-4 h-28 text-3xl shadow-2xl"
              onClick={() => store.startSpiel()}
              disabled={store.spieler.length === 0}
            >
              <Play className="w-10 h-10" />
              SPILL STARTEN
            </TouchButton>
          </div>
        </div>
      </div>
    </div>
  );
}
