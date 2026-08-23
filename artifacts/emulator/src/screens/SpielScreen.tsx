import React, { useState } from 'react';
import { useGameStore, getCurrentPosten, isHarakiriModus, getEintragForPlayer, countH2Before } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { SkipForward, RotateCcw, XOctagon, Zap, Layers } from 'lucide-react';
import { cn } from '@/lib/utils';

/** Returns players assigned to each of the 5 posts for the current step */
function buildPositionMap(
  spieler: ReturnType<typeof useGameStore.getState>['spieler'],
  taubeIndex: number,
): Map<number, typeof spieler> {
  const map = new Map<number, typeof spieler>();
  for (let p = 1; p <= 5; p++) map.set(p, []);
  for (const s of spieler) {
    const pos = getCurrentPosten(s, taubeIndex, spieler.length);
    map.get(pos)!.push(s);
  }
  return map;
}

export function SpielScreen() {
  const store = useGameStore();
  const aktiverSpieler = store.getAktivenSpieler();
  const harakiri = isHarakiriModus(store.modus);
  const [confirmAbort, setConfirmAbort] = useState(false);
  const [quitError, setQuitError] = useState(false);

  // In Harakiri each post offsets into the sequenz — compute active player's post first,
  // then derive the eintrag from it. In Normal mode eintrag is just sequenz[taubeIndex].
  const rawPosten = aktiverSpieler
    ? getCurrentPosten(aktiverSpieler, store.taubeIndex, store.spieler.length)
    : 1;
  const eintrag = getEintragForPlayer(store.sequenz, store.taubeIndex, rawPosten, store.modus);
  const maschine = eintrag?.maschine ?? 'A';
  const istHDoublette = eintrag?.pairKind === 'h';
  const istCustomPair = eintrag?.pairKind === 'custom';
  const doubletteNr = eintrag?.doubletteNr;
  const isSecondPairResult = doubletteNr === 2;
  const machineLabel = istHDoublette
    ? `H${doubletteNr ?? 1}`
    : istCustomPair && doubletteNr === 1
      ? `${maschine}+${eintrag?.partner}`
      : maschine;

  // H2 post-stays-same-as-H1 adjustment — Normal mode only; Harakiri advances naturally
  const effectiveTaubeIdx =
    !harakiri && doubletteNr === 2 ? store.taubeIndex - 1 : store.taubeIndex;
  // H1+H2 = ONE physical position: subtract H2 entries before this slot
  const h2Offset = harakiri ? 0 : countH2Before(store.sequenz, effectiveTaubeIdx);
  const logicalTaubeIdx = effectiveTaubeIdx - h2Offset;
  const posMap = buildPositionMap(store.spieler, logicalTaubeIdx);
  const aktuellePosten = aktiverSpieler
    ? getCurrentPosten(aktiverSpieler, logicalTaubeIdx, store.spieler.length)
    : 1;

  return (
    <div className="flex h-full w-full bg-background">

      {/* ── Left Sidebar ──────────────────────────────────────────────────── */}
      <div className="w-[260px] border-r-2 border-border bg-card flex flex-col gap-0">

        {/* Machine display */}
        <div className={cn(
          "border-b-2 border-border flex flex-col items-center justify-center py-6",
          istHDoublette ? "bg-amber-950/30" : "bg-primary/5",
        )}>
          {istHDoublette && (
            <div className="flex items-center gap-1 mb-1">
              <Layers className="w-4 h-4 text-amber-400" />
              <span className="text-sm font-bold text-amber-400 tracking-widest">
                DOUBLETTE {doubletteNr}/2
              </span>
            </div>
          )}
          {!istHDoublette && (
            <div className="text-base font-bold text-primary/70 tracking-widest mb-1">
              {istCustomPair ? `DOUBLETTE ${doubletteNr}/2` : 'SCHANZ'}
            </div>
          )}
          <div className={cn(
            "font-black leading-none",
            istHDoublette ? "text-[96px] text-amber-400" : "text-[96px] text-primary",
          )}>
            {machineLabel}
          </div>
        </div>

        {/* Lauf / Taube counter */}
        <div className="border-b-2 border-border p-4 text-center bg-background/40">
          <div className="text-sm text-muted-foreground font-bold uppercase tracking-wider mb-1">
            Lauf {store.lauf} / 2
          </div>
          <div className="text-2xl font-bold font-mono">
            Taube {store.taubeIndex + 1} / {store.sequenz.length}
          </div>
          <div className="text-xs text-muted-foreground mt-1 font-mono">
            Schütze {store.spielerIndex + 1} / {store.spieler.length}
          </div>
        </div>

        {/* Taube Werfen */}
        <div className="border-b-2 border-border p-4">
          <TouchButton
            className="w-full h-16 gap-3 text-base"
            variant="primary"
            onClick={() => store.werfenTaube()}
            disabled={isSecondPairResult}
          >
            <Zap className="w-6 h-6" />
            <span className="font-bold tracking-widest">
              {isSecondPairResult
                ? '2. RESULTAT'
                : istHDoublette
                  ? 'WERFEN H DOUBLETTE'
                  : istCustomPair
                    ? `WERFEN ${maschine}+${eintrag?.partner}`
                    : 'WERFEN'}
            </span>
          </TouchButton>
        </div>

        {/* Control buttons */}
        <div className="p-4 flex flex-col gap-3">
          <TouchButton className="h-14 w-full gap-3" onClick={() => store.wiederholenTaube()}>
            <RotateCcw className="w-5 h-5" />
            <span className="font-bold">Widderhuelen</span>
          </TouchButton>
          <TouchButton className="h-14 w-full gap-3" onClick={() => store.ueberspringenTaube()}>
            <SkipForward className="w-5 h-5" />
            <span className="font-bold">Iwwerspringen</span>
          </TouchButton>
        </div>

        {/* Abort */}
        <div className="mt-auto p-4 border-t-2 border-border">
          {confirmAbort ? (
            <div className="grid grid-cols-2 gap-2">
              <p className="col-span-2 text-center text-xs font-semibold leading-relaxed text-muted-foreground">
                Game will not be saved. All player credits will be restored.
              </p>
              <TouchButton variant="destructive" className="h-14 text-base font-bold"
                onClick={() => {
                  if (store.ofbriechenSpiel()) {
                    setConfirmAbort(false);
                    setQuitError(false);
                  } else {
                    setQuitError(true);
                  }
                }}>
                YES, QUIT
              </TouchButton>
              <TouchButton className="h-14 text-base font-bold"
                onClick={() => setConfirmAbort(false)}>
                KEEP GAME
              </TouchButton>
              {quitError && (
                <p className="col-span-2 text-center text-xs font-semibold text-destructive">
                  Credits could not be restored. Sync or clear the queue, then try again.
                </p>
              )}
            </div>
          ) : (
            <TouchButton
              variant="outline"
              className="h-14 w-full text-destructive border-destructive gap-2"
              onClick={() => { setQuitError(false); setConfirmAbort(true); }}
            >
              <XOctagon className="w-5 h-5" />
              QUIT GAME
            </TouchButton>
          )}
        </div>
      </div>

      {/* ── Right Main ────────────────────────────────────────────────────── */}
      <div className="flex-1 flex flex-col">

        {/* ── 5 Positions Grid ── */}
        <div className="border-b-2 border-border bg-card/50 p-4">
          <div className="text-xs font-bold text-muted-foreground uppercase tracking-widest mb-3">
            {harakiri
              ? `Harakiri — Schritt ${store.taubeIndex + 1} / ${store.sequenz.length} · Lauf ${store.lauf}`
              : `Posten — Schanz ${machineLabel} · Lauf ${store.lauf}`}
          </div>
          <div className="grid grid-cols-5 gap-3">
            {[1, 2, 3, 4, 5].map(pos => {
              const playersHere = posMap.get(pos) ?? [];
              const hasActivePlayer = playersHere.some(s => s.id === aktiverSpieler?.id);
              const isNextPlayer = playersHere.some(s =>
                s.id === store.spieler[Math.min(store.spielerIndex + 1, store.spieler.length - 1)]?.id
              ) && !hasActivePlayer;
              // In Harakiri show which machine this post faces
              const posEintrag = harakiri
                ? getEintragForPlayer(store.sequenz, store.taubeIndex, pos, store.modus)
                : undefined;

              return (
                <div
                  key={pos}
                  className={cn(
                    "rounded-xl border-2 p-3 flex flex-col items-center gap-2 transition-all",
                    hasActivePlayer
                      ? "border-primary bg-primary/15 shadow-lg shadow-primary/10"
                      : playersHere.length > 0
                        ? "border-border/60 bg-background"
                        : "border-border/20 bg-background/30",
                  )}
                >
                  <div className="flex items-center gap-1.5 w-full justify-between">
                    <span className={cn(
                      "text-xs font-black uppercase tracking-widest",
                      hasActivePlayer ? "text-primary" : "text-muted-foreground",
                    )}>
                      Posten {pos}
                    </span>
                    {posEintrag && (
                      <span className={cn(
                        "text-xs font-black px-1.5 py-0.5 rounded font-mono",
                        posEintrag.maschine === 'H'
                          ? "bg-amber-500/20 text-amber-400"
                          : "bg-primary/20 text-primary",
                      )}>
                        {posEintrag.pairKind === 'h'
                          ? `H${posEintrag.doubletteNr}`
                          : posEintrag.pairKind === 'custom' && posEintrag.doubletteNr === 1
                            ? `${posEintrag.maschine}+${posEintrag.partner}`
                            : posEintrag.maschine}
                      </span>
                    )}
                  </div>

                  {playersHere.length === 0 ? (
                    <div className="text-muted-foreground/30 text-xl font-bold">—</div>
                  ) : (
                    <div className="w-full flex flex-col gap-1">
                      {playersHere.map(s => {
                        const isActive = s.id === aktiverSpieler?.id;
                        return (
                          <div key={s.id} className={cn(
                            "rounded-lg px-2 py-1 flex items-center justify-between gap-2",
                            isActive ? "bg-primary text-black" : "bg-border/30",
                          )}>
                            <span className="font-bold text-sm truncate max-w-[90px]">
                              {s.name}
                            </span>
                            <span className={cn(
                              "font-mono font-black text-sm shrink-0",
                              isActive ? "text-black" : "text-primary",
                            )}>
                              {s.punkte}p
                            </span>
                          </div>
                        );
                      })}
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        </div>

        {/* ── Active Shooter Banner ── */}
        <div className="px-6 py-4 border-b-2 border-primary/30 bg-primary/5 flex items-center gap-6">
          <div className="flex flex-col">
            <span className="text-sm text-primary font-bold tracking-widest uppercase">Aktuellen Schütze</span>
            <span className="text-4xl font-black truncate max-w-[480px]">
              {aktiverSpieler?.name ?? '—'}
            </span>
          </div>
          <div className="ml-auto flex flex-col items-end">
            <span className="text-sm text-primary font-bold tracking-widest uppercase">Posten</span>
            <span className="text-5xl font-mono font-black">{aktuellePosten}</span>
          </div>
          <div className="flex flex-col items-end border-l-2 border-border pl-6">
            <span className="text-sm text-muted-foreground font-bold uppercase">Punkte</span>
            <span className="text-5xl font-mono font-black text-primary">
              {aktiverSpieler?.punkte ?? 0}
            </span>
          </div>
        </div>

        {/* ── Score Entry Buttons — flex so they stretch to full remaining height ── */}
        <div className="flex-1 p-5 flex gap-5">
          {istHDoublette ? (
            /* ── H doublette: hit = 2 pts, miss = 0 pts — no 2nd shot ── */
            <>
              <TouchButton
                variant="success"
                className="flex-1 h-auto flex-col gap-3 shadow-[0_12px_0_rgb(22,163,74)] active:translate-y-3 active:shadow-none transition-all"
                onClick={() => store.eintragenErgebnis(true, false)}
              >
                <div className="font-black text-8xl">2</div>
                <div className="uppercase font-bold tracking-widest text-xl opacity-90">Getraff</div>
              </TouchButton>

              <TouchButton
                variant="destructive"
                className="flex-1 h-auto flex-col gap-3 shadow-[0_12px_0_rgb(185,28,28)] active:translate-y-3 active:shadow-none transition-all"
                onClick={() => store.eintragenErgebnis(false, false)}
              >
                <div className="font-black text-8xl">0</div>
                <div className="uppercase font-bold tracking-widest text-xl opacity-90">Fehl</div>
              </TouchButton>
            </>
          ) : (
            /* ── Single clay: 1st shot = 2 pts, 2nd shot = 1 pt, miss = 0 ── */
            <>
              <TouchButton
                variant="success"
                className="flex-1 h-auto flex-col gap-3 shadow-[0_12px_0_rgb(22,163,74)] active:translate-y-3 active:shadow-none transition-all"
                onClick={() => store.eintragenErgebnis(true, false)}
              >
                <div className="font-black text-8xl">2</div>
                <div className="uppercase font-bold tracking-widest text-xl opacity-90">1. Schoss</div>
              </TouchButton>

              <TouchButton
                variant="warning"
                className="flex-1 h-auto flex-col gap-3 shadow-[0_12px_0_rgb(217,119,6)] active:translate-y-3 active:shadow-none transition-all"
                onClick={() => store.eintragenErgebnis(false, true)}
              >
                <div className="font-black text-8xl">1</div>
                <div className="uppercase font-bold tracking-widest text-xl opacity-90">2. Schoss</div>
              </TouchButton>

              <TouchButton
                variant="destructive"
                className="flex-1 h-auto flex-col gap-3 shadow-[0_12px_0_rgb(185,28,28)] active:translate-y-3 active:shadow-none transition-all"
                onClick={() => store.eintragenErgebnis(false, false)}
              >
                <div className="font-black text-8xl">0</div>
                <div className="uppercase font-bold tracking-widest text-xl opacity-90">Fehl</div>
              </TouchButton>
            </>
          )}
        </div>

      </div>
    </div>
  );
}
