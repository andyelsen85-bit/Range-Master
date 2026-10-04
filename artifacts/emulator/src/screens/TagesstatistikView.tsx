import { useEffect, useState } from 'react';
import { useGameStore } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { RefreshCw, WifiOff, AlertTriangle, Clock } from 'lucide-react';
import { cn } from '@/lib/utils';

const eur = (c: number) => `${(c / 100).toFixed(2).replace('.', ',')} €`;
const num = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? String(v) : 'nicht verfügbar');

function Kpi({ label, value, sub, tone, testId }: { label: string; value: string; sub?: string; tone?: string; testId?: string }) {
  const missing = value === 'nicht verfügbar';
  return (
    <div className="bg-card border-2 border-border rounded-xl p-4 flex flex-col gap-1 min-w-0">
      <div className="text-xs font-bold uppercase tracking-widest text-muted-foreground">{label}</div>
      <div data-testid={testId} className={cn('font-mono font-black leading-tight', missing ? 'text-base text-muted-foreground pt-2' : 'text-4xl', !missing && tone)}>{value}</div>
      {sub && <div className="text-sm text-muted-foreground">{sub}</div>}
    </div>
  );
}

export function TagesstatistikView() {
  const store = useGameStore();
  const s = store.daySummary;
  const [online, setOnline] = useState(typeof navigator === 'undefined' ? true : navigator.onLine);

  useEffect(() => {
    const on = () => setOnline(true), off = () => setOnline(false);
    window.addEventListener('online', on); window.addEventListener('offline', off);
    return () => { window.removeEventListener('online', on); window.removeEventListener('offline', off); };
  }, []);

  const refresh = () => { void store.ladeDaySummary(); };

  const configured = !!store.apiUrl && !!store.apiKey;
  const stale = !!s && s.datum !== store.verkaufDatum;

  const pendingByProduct: Record<string, number> = {};
  const unknownPrice = new Set<string>();
  s?.players?.forEach(p => p.lines?.forEach(l => {
    if (l.pending) pendingByProduct[l.productName] = (pendingByProduct[l.productName] ?? 0) + 1;
    if (typeof l.unitPriceCents !== 'number' || !Number.isFinite(l.unitPriceCents)) unknownPrice.add(l.productName);
  }));
  const products = s?.productTotals ? Object.entries(s.productTotals) : null;
  const cats = s?.categorySubtotals ? Object.entries(s.categorySubtotals) : null;
  const hasPending = Object.keys(pendingByProduct).length > 0;

  return (
    <div className="flex flex-col gap-4 max-w-5xl">
      <div className="flex items-end justify-between gap-4 border-b border-border pb-3">
        <div>
          <h2 className="text-2xl font-black text-foreground">Tagesabrechnung</h2>
          <p className="text-sm text-muted-foreground">Übersicht für {store.verkaufDatum}</p>
        </div>
        <TouchButton className="h-12 px-5 gap-2" variant="outline" onClick={refresh} disabled={store.daySummaryLaden || !store.apiUrl || !store.apiKey}>
          <RefreshCw className={cn('w-5 h-5', store.daySummaryLaden && 'animate-spin')} />
          Aktualisieren
        </TouchButton>
      </div>

      {!online && (
        <div className="flex items-center gap-2 p-3 rounded-lg border-2 border-amber-500/50 bg-amber-500/10 text-sm font-bold">
          <WifiOff className="w-5 h-5 shrink-0" /> Offline – angezeigt wird der zuletzt gespeicherte Stand{s ? '' : ', es liegt keiner vor'}.
        </div>
      )}
      {!configured && (
        <div className="flex items-center gap-2 p-3 rounded-lg border-2 border-border text-sm">
          <AlertTriangle className="w-5 h-5 shrink-0" /> Portal nicht eingerichtet (API-Adresse oder Schlüssel fehlt). Aktualisieren ist nicht möglich.
        </div>
      )}
      {store.daySummaryError && configured && (
        <div className="flex items-center gap-2 p-3 rounded-lg border-2 border-destructive/50 bg-destructive/10 text-sm font-bold">
          <AlertTriangle className="w-5 h-5 shrink-0" /> Aktualisierung fehlgeschlagen: {store.daySummaryError} {s && 'Der zuletzt gespeicherte Stand bleibt sichtbar.'}
        </div>
      )}
      {stale && (
        <div className="flex items-center gap-2 p-3 rounded-lg border-2 border-amber-500/50 bg-amber-500/10 text-sm font-bold">
          <Clock className="w-5 h-5 shrink-0" /> Gespeicherter Stand stammt vom {s!.datum}, nicht von heute.
        </div>
      )}

      {!s ? (
        store.daySummaryLaden ? (
          <div className="grid grid-cols-3 gap-3 animate-pulse">
            {[0, 1, 2, 3, 4, 5].map(i => <div key={i} className="h-28 rounded-xl bg-card border-2 border-border" />)}
          </div>
        ) : (
          <div className="p-10 border-2 border-dashed border-border rounded-xl text-center text-muted-foreground text-lg">
            Keine Tagesabrechnung verfügbar. Zum Laden „Aktualisieren“ tippen.
          </div>
        )
      ) : (
        <>
          <div className={cn('grid grid-cols-3 gap-3', store.daySummaryLaden && 'opacity-70')}>
            <Kpi label="Gesamtumsatz" value={typeof s.generalTotalCents === 'number' ? eur(s.generalTotalCents) : 'nicht verfügbar'} sub="Alle Produkte des Tages" tone="text-primary" />
            <Kpi label="Spieler" value={num(s.uniquePlayers)} sub="Mit Aktivität an diesem Tag" />
            <Kpi label="Spiele" value={num(s.games)} sub="Alle gespeicherten Spiele" />
            <Kpi label="Abgeschlossen" value={num(s.completedGames)} sub="Vollständig beendete Spiele" />
            <Kpi label="Tauben bestätigt" value={num(s.confirmedClays)} sub="Nur Auslösungen mit Bestätigung" tone="text-primary" testId="confirmed-clays" />
            <Kpi label="Rechnungen bezahlt" value={num(s.paidPlayers)} sub="Vollständig bezahlte Spieler" tone="text-green-500" />
          </div>
          <p className="text-xs text-muted-foreground">Bestätigte Tauben zählen nur ACK-Auslösungen. Bei älteren Spielen fehlt diese Zählung möglicherweise.</p>

          <div className="grid grid-cols-2 gap-3">
            <section className="bg-card border-2 border-border rounded-xl p-4 flex flex-col gap-2">
              <h3 className="text-sm font-bold uppercase tracking-widest text-muted-foreground">Umsatz nach Kategorie</h3>
              {!cats ? <p className="text-muted-foreground">Nicht verfügbar</p> : cats.length === 0 ? <p className="text-muted-foreground py-3">Keine Umsätze heute.</p> :
                cats.map(([cat, c]) => (
                  <div key={cat} className="flex justify-between items-center p-3 bg-background border border-border rounded-lg">
                    <span className="font-bold uppercase tracking-wider text-sm text-muted-foreground">{cat}</span>
                    <span className="font-mono font-black text-lg">{eur(c)}</span>
                  </div>
                ))}
              <div className="flex justify-between items-center p-3 bg-primary/10 border-2 border-primary/30 rounded-lg">
                <span className="font-bold uppercase text-primary">Gesamt</span>
                <span className="font-mono font-black text-primary text-xl">{typeof s.generalTotalCents === 'number' ? eur(s.generalTotalCents) : 'nicht verfügbar'}</span>
              </div>
            </section>

            <section className="bg-card border-2 border-border rounded-xl p-4 flex flex-col gap-2">
              <h3 className="text-sm font-bold uppercase tracking-widest text-muted-foreground">Verkaufte Produkte</h3>
              {!products ? <p className="text-muted-foreground">Nicht verfügbar</p> : products.length === 0 ? <p className="text-muted-foreground py-3">Keine Produkte verkauft.</p> :
                products.map(([name, c]) => (
                  <div key={name} className="flex justify-between items-center gap-2 p-3 bg-background border border-border rounded-lg">
                    <div className="min-w-0">
                      <div className="font-bold truncate">{name}</div>
                      {pendingByProduct[name] && <div className="text-xs text-amber-500 font-bold">Ausstehend: {pendingByProduct[name]} Position(en) noch nicht synchronisiert</div>}
                      {unknownPrice.has(name) && <div className="text-xs text-amber-500 font-bold">Preis unbekannt</div>}
                    </div>
                    <span className="font-mono font-black text-lg shrink-0">{unknownPrice.has(name) ? 'Abgleich' : eur(c)}</span>
                  </div>
                ))}
              {hasPending && <p className="text-xs text-muted-foreground">Ausstehende Positionen sind lokal erfasst und noch nicht Teil der Portal-Abrechnung.</p>}
            </section>
          </div>
        </>
      )}
    </div>
  );
}
