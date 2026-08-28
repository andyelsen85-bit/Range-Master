import React, { useEffect, useState } from 'react';
import { useGameStore, PortalSpieler } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { PlayerSearch } from '@/components/PlayerSearch';
import { ArrowLeft, Coins, Plus, RefreshCw, Trash2 } from 'lucide-react';
import { cn } from '@/lib/utils';

const ADD_AMOUNTS = [1, 2, 3, 4] as const;

export function KrediteScreen() {
  const store = useGameStore();
  // Incrementing this key forces PlayerSearch to remount after each selection
  const [searchKey, setSearchKey] = useState(0);

  // Pull latest server state once when the screen opens (best effort — offline OK)
  useEffect(() => {
    void store.ladeKredite();
    void store.ladeProdukte();
    void store.ladeVerkaeufe();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const nameFor = (id: number) =>
    store.portalSpieler.find(p => p.id === id)?.name ??
    store.spieler.find(s => s.id === id)?.name ??
    `Spiller #${id}`;

  // Today's players = anyone with a credit entry today
  const eintraege = Object.entries(store.kredite)
    .map(([id, stand]) => {
      const spielerId = Number(id);
      return {
        spielerId,
        name: nameFor(spielerId),
        gewaehrt: stand.gewaehrt,
        verbraucht: stand.verbraucht,
        rest: Math.max(0, stand.gewaehrt - stand.verbraucht),
        cal12: store.getProduktAnzahl(spielerId, 'cal-12'),
        cal20: store.getProduktAnzahl(spielerId, 'cal-20'),
        pending: store.pendingVerkaeufe.some(v => v.spielerId === spielerId),
      };
    })
    .sort((a, b) => a.name.localeCompare(b.name));

  const mitRest = eintraege.filter(e => e.rest > 0);
  const ohneRest = eintraege.filter(e => e.rest === 0);

  const registerPlayer = (ps: PortalSpieler) => {
    store.registerSpielerFuerTag(ps.id);
    setSearchKey(k => k + 1); // reset search so the same player can't be re-clicked
  };
  const refreshDay = () => {
    void store.ladeKredite();
    void store.ladeProdukte();
    void store.ladeVerkaeufe();
  };

  return (
    <div className="flex h-full w-full bg-background flex-col">
      {/* ── Header ─────────────────────────────────────────────────────────── */}
      <header className="h-16 border-b-2 border-border flex items-center px-6 bg-card gap-4 shrink-0 pr-20">
        <TouchButton onClick={() => store.setScreen('dashboard')} className="w-12 h-12 p-0">
          <ArrowLeft className="w-6 h-6" />
        </TouchButton>
        <h1 className="text-xl font-bold tracking-wider text-primary">SPILLER VUM DAG</h1>
        <span className="text-xs text-muted-foreground font-mono">{store.kreditDatum}</span>
        <div className="ml-auto flex items-center gap-3">
          {store.pendingKredite.length > 0 && (
            <span className="text-xs font-bold text-amber-400 bg-amber-500/10 border border-amber-500/30 rounded-lg px-3 py-1.5">
              {store.pendingKredite.length} net synchroniséiert
            </span>
          )}
          <TouchButton
            className="h-12 px-4 gap-2"
            onClick={refreshDay}
            disabled={store.krediteLaden || store.verkaeufeLaden}
          >
            <RefreshCw className={cn('w-4 h-4', (store.krediteLaden || store.verkaeufeLaden) && 'animate-spin')} />
            Aktualiséieren
          </TouchButton>
        </div>
      </header>

      <div className="flex flex-1 overflow-hidden flex-col lg:flex-row">
        {/* ── Left: player search (register only, no credits) ───────────────── */}
        <div className="lg:w-[380px] lg:border-r-2 border-b-2 lg:border-b-0 border-border flex flex-col">
          <div className="p-4 border-b-2 border-border bg-card/50 shrink-0">
            <h2 className="text-base font-bold uppercase tracking-widest text-foreground/80">
              Spiller dobäisetzen
            </h2>
            <p className="text-xs text-muted-foreground mt-0.5">
              Spiller sichen → erschéngt riets → Kreditter dobäisetzen
            </p>
          </div>

          <div className="p-4 flex flex-col gap-3 flex-1 overflow-y-auto">
            <PlayerSearch
              key={searchKey}
              onSelect={registerPlayer}
              placeholder="Spiller sichen…"
            />
          </div>
        </div>

        {/* ── Right: players of the day with credit buttons ─────────────────── */}
        <div className="flex-1 flex flex-col overflow-hidden">
          <div className="p-4 border-b-2 border-border bg-card/50 shrink-0 flex items-center gap-3">
            <Coins className="w-5 h-5 text-primary" />
            <h2 className="text-base font-bold uppercase tracking-widest text-foreground/80">
              Haut um Terrain
            </h2>
            <span className="text-sm text-muted-foreground">
              <span className="text-primary font-black">{mitRest.length}</span> mat Kreditter
              {ohneRest.length > 0 && <> · <span className="font-bold">{ohneRest.length}</span> op Null</>}
            </span>
          </div>

           <div className="flex-1 overflow-y-auto p-4 flex flex-col gap-3">
            {eintraege.length === 0 && (
              <div className="text-muted-foreground italic text-center py-12">
                Nach keng Spiller fir haut — lénks e Spiller sichen an dobäisetzen
              </div>
            )}

            {eintraege.map(e => (
              <div
                key={e.spielerId}
                className={cn(
                  'grid grid-cols-1 xl:grid-cols-[minmax(180px,1fr)_auto_auto_auto] items-center gap-3 border-2 rounded-xl p-3 transition-all',
                  e.rest > 0
                    ? 'border-primary/50 bg-primary/5'
                    : 'border-border/40 bg-background/40 opacity-80',
                )}
              >
                {/* Name + stats */}
                <div className="min-w-0">
                  <div className={cn(
                    'font-bold text-base truncate',
                    e.rest > 0 ? 'text-foreground' : 'text-muted-foreground',
                  )}>
                    {e.name}
                  </div>
                  <div className="text-xs text-muted-foreground mt-0.5 font-mono">
                    {e.gewaehrt} bezuelt · {e.verbraucht} gespillt
                    {e.rest === 0 && e.gewaehrt > 0 && (
                      <span className="text-amber-400 font-bold"> · opgebraucht</span>
                    )}
                  </div>
                  {e.pending && <div data-testid={`status-pending-${e.spielerId}`} className="text-[10px] font-bold text-amber-400 mt-1">Verkeef nach net synchroniséiert</div>}
                </div>

                <div className="flex flex-col gap-1">
                  <span className="text-[10px] font-bold uppercase text-primary">Credits: {e.rest}</span>
                  <div className="flex items-center gap-1.5 shrink-0">
                  {/* −1 refund */}
                  <TouchButton
                    variant="ghost"
                    className="h-11 w-11 p-0 border border-amber-500/40"
                    onClick={() => store.removeKredit(e.spielerId, 1)}
                    disabled={e.rest === 0}
                    style={e.rest === 0 ? { opacity: 0.3, pointerEvents: 'none' } : undefined}
                    title="1 Kredit zréckbezuelen"
                  >
                    <span className={cn('font-black text-base', e.rest > 0 ? 'text-amber-400' : 'text-muted-foreground')}>
                      −1
                    </span>
                  </TouchButton>

                  <div className="w-px h-8 bg-border/60 mx-0.5" />

                  {/* +1 +2 +3 +4 */}
                  {ADD_AMOUNTS.map(n => (
                    <TouchButton
                      key={n}
                      variant="outline"
                      className="h-11 px-3 font-bold text-sm gap-0.5"
                      onClick={() => store.addKredite(e.spielerId, n)}
                    >
                      <Plus className="w-3 h-3" />{n}
                    </TouchButton>
                  ))}

                  <div className="w-px h-8 bg-border/60 mx-0.5" />

                  {/* Delete row */}
                  <TouchButton
                    variant="ghost"
                    className="h-11 w-11 p-0 text-destructive hover:bg-destructive/10"
                    onClick={() => store.deleteKreditEntry(e.spielerId)}
                    title="Feeler korrigéieren — Spiller vum Dag läschen"
                  >
                    <Trash2 className="w-4 h-4" />
                  </TouchButton>
                </div>
                </div>

                {([
                  { id: 'cal-12', label: 'Cal. 12', quantity: e.cal12 },
                  { id: 'cal-20', label: 'Cal. 20', quantity: e.cal20 },
                ] as const).map(product => (
                  <div key={product.id} className="flex flex-col gap-1 min-w-[210px]">
                    <span className="text-[10px] font-bold uppercase text-foreground">{product.label}: <b data-testid={`text-quantity-${product.id}-${e.spielerId}`}>{product.quantity}</b></span>
                    <div className="flex gap-1">
                      <TouchButton variant="ghost" className="h-10 w-10 p-0 border border-amber-500/40" onClick={() => store.addVerkauf(e.spielerId, product.id, -1)} data-testid={`button-minus-${product.id}-${e.spielerId}`}>−1</TouchButton>
                      {ADD_AMOUNTS.map(n => <TouchButton key={n} variant="outline" className="h-10 px-2 font-bold" onClick={() => store.addVerkauf(e.spielerId, product.id, n)} data-testid={`button-add-${product.id}-${n}-${e.spielerId}`}><Plus className="w-3 h-3" />{n}</TouchButton>)}
                    </div>
                  </div>
                ))}
              </div>
            ))}
          </div>

          {/* Repay hint */}
          {mitRest.length > 0 && (
            <div className="border-t-2 border-border bg-card/50 px-4 py-2 text-xs text-muted-foreground shrink-0">
              Kreditter gëlle just haut — wat um Enn vum Dag iwwreg ass, gëtt zréckbezuelt
              {' '}({mitRest.reduce((s, e) => s + e.rest, 0)} Kreditter oppen)
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
