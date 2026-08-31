import { useEffect, useMemo, useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { Calendar } from "@/components/ui/calendar";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Skeleton } from "@/components/ui/skeleton";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter, DialogDescription } from "@/components/ui/dialog";
import { Badge } from "@/components/ui/badge";
import { Coins, AlertCircle, ShoppingCart, Activity, Target, Receipt, CheckCircle, Clock, CalendarRange, RotateCcw, CircleCheck, CircleDashed } from "lucide-react";
import { cn } from "@/lib/utils";
import { endOfMonth, format, parseISO, startOfMonth } from "date-fns";
import { de } from "date-fns/locale";

// ── Types ──────────────────────────────────────────────────────────────────────

interface DaySummaryPlayerBill {
  spielerId: number;
  spielerName: string;
  mitgliedNr: string | null;
  state: "OPEN" | "PENDING_NEUTRAL" | "PAID";
  payment: {
    externalId: string | null;
    paidAt: string;
    source: string;
    markedByAdmin: {
      adminId: number;
      adminName: string;
    } | null;
    markedByApiKey: {
      keyId: number;
      keyName: string;
    } | null;
  } | null;
  
  credit: {
    granted: number;
    used: number;
    remaining: number;
  };
  
  lines: {
    productId: number;
    productName: string;
    category: string;
    quantity: number;
    unitPriceCents: number;
    totalCents: number;
  }[];
  dayLines?: {
    productId: number;
    productName: string;
    category: string;
    quantity: number;
    unitPriceCents: number;
    totalCents: number;
  }[];

  categorySubtotals: Record<string, number>;
  dayCategorySubtotals?: Record<string, number>;
  totalCents: number;
  dayTotalCents?: number;
  openTotalCents?: number;
  games?: number;
  completedGames?: number;
  confirmedClays?: number;
  paidDays?: number;
}

interface DaySummaryProductTotal {
  productName: string;
  category: string;
  quantity: number;
  totalCents: number;
}

interface GameClayBreakdown {
  modus: "NORMAL" | "HARAKIRI" | "CUSTOM_1" | "CUSTOM_2" | "CUSTOM_3" | "CUSTOM_4";
  games: number;
  completedGames: number;
  playerParticipations: number;
  theoreticalClays: number;
  confirmedClays: number;
  unconfirmedClays: number;
}

interface DaySummary {
  datum?: string;
  from?: string;
  to?: string;
  generalTotalCents: number;
  uniquePlayers: number;
  paidPlayers: number;
  games: number;
  completedGames: number;
  theoreticalClays: number;
  confirmedClays: number;
  unconfirmedClays: number;
  gameBreakdown: GameClayBreakdown[];
  productTotals: Record<string, DaySummaryProductTotal>;
  players: DaySummaryPlayerBill[];
}

interface ActivityDays {
  from: string;
  to: string;
  days: string[];
}

// ── Helpers ────────────────────────────────────────────────────────────────────

function formatMoney(cents: number) {
  return new Intl.NumberFormat('lb-LU', { style: 'currency', currency: 'EUR' }).format(cents / 100);
}

function todayStr(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

function dateStr(date: Date): string {
  return format(date, "yyyy-MM-dd");
}

function periodLabel(from: string, to: string): string {
  if (from === to) return format(parseISO(from), "dd. MMMM yyyy", { locale: de });
  return `${format(parseISO(from), "dd. MMM yyyy", { locale: de })} – ${format(parseISO(to), "dd. MMM yyyy", { locale: de })}`;
}

function modusLabel(modus: GameClayBreakdown["modus"]): string {
  if (modus === "NORMAL") return "Normal";
  if (modus === "HARAKIRI") return "Harakiri";
  return `Custom ${modus.slice(-1)}`;
}

function StatCard({ label, value, sub, icon: Icon }: { label: string; value: number | string; sub?: string; icon: any }) {
  return (
    <div className="bg-card border border-border/50 rounded-xl p-5 shadow-sm relative overflow-hidden group">
      <div className="absolute -right-4 -top-4 opacity-5 pointer-events-none transition-transform group-hover:scale-110 group-hover:rotate-12 duration-500">
        <Icon size={120} />
      </div>
      <div className="relative z-10">
        <div className="flex items-center gap-2 text-muted-foreground mb-2">
          <Icon size={16} className="text-primary" />
          <p className="text-xs uppercase tracking-widest font-bold">{label}</p>
        </div>
        <p className="text-3xl font-bold font-mono">{value}</p>
        {sub && <p className="text-xs text-muted-foreground mt-1">{sub}</p>}
      </div>
    </div>
  );
}

export default function AdminOfrechnung() {
  const token = useAuthStore((s) => s.token);
  const { toast } = useToast();
  const qc = useQueryClient();
  
  const today = todayStr();
  const [from, setFrom] = useState(today);
  const [to, setTo] = useState(today);
  const [pendingStart, setPendingStart] = useState<string | null>(null);
  const [calendarMonth, setCalendarMonth] = useState(() => startOfMonth(parseISO(today)));
  const [filterState, setFilterState] = useState<"ALL" | "OPEN" | "PENDING_NEUTRAL" | "PAID">("ALL");
  const [selectedBill, setSelectedBill] = useState<DaySummaryPlayerBill | null>(null);
  const isSingleDay = from === to && pendingStart === null;
  const visibleMonthFrom = dateStr(startOfMonth(calendarMonth));
  const visibleMonthTo = dateStr(endOfMonth(calendarMonth));

  const { data, isLoading, error } = useQuery<DaySummary>({
    queryKey: ["admin-bill-summary", from, to],
    queryFn: async () => {
      const endpoint = from === to
        ? `/api/admin/bills/day-summary?datum=${from}`
        : `/api/admin/bills/period-summary?from=${from}&to=${to}`;
      const res = await fetch(endpoint, {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) {
        if (res.status === 404) {
          return {
            ...(from === to ? { datum: from } : { from, to }),
            generalTotalCents: 0,
            uniquePlayers: 0,
            paidPlayers: 0,
            games: 0,
            completedGames: 0,
            theoreticalClays: 0,
            confirmedClays: 0,
            unconfirmedClays: 0,
            gameBreakdown: [],
            productTotals: {},
            players: []
          };
        }
        const err = await res.json().catch(() => ({}));
        throw new Error(err.error || `HTTP ${res.status}`);
      }
      return await res.json();
    },
  });

  const { data: activityData, error: activityError } = useQuery<ActivityDays>({
    queryKey: ["admin-bill-activity-days", visibleMonthFrom, visibleMonthTo],
    queryFn: async () => {
      const res = await fetch(`/api/admin/bills/activity-days?from=${visibleMonthFrom}&to=${visibleMonthTo}`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) {
        const err = await res.json().catch(() => ({}));
        throw new Error(err.error || `HTTP ${res.status}`);
      }
      return await res.json();
    },
  });

  const activityDates = useMemo(() => (activityData?.days ?? []).map((value) => parseISO(value)), [activityData?.days]);

  useEffect(() => {
    setSelectedBill(null);
  }, [from, to]);

  function selectCalendarDay(selected: Date) {
    const selectedDate = dateStr(selected);
    if (pendingStart === null) {
      setPendingStart(selectedDate);
      setFrom(selectedDate);
      setTo(selectedDate);
      return;
    }
    if (selectedDate < pendingStart) {
      setFrom(selectedDate);
      setTo(pendingStart);
    } else {
      setFrom(pendingStart);
      setTo(selectedDate);
    }
    setPendingStart(null);
  }

  function updateFrom(value: string) {
    if (!value) return;
    setPendingStart(null);
    setFrom(value);
    if (value > to) setTo(value);
    setCalendarMonth(startOfMonth(parseISO(value)));
  }

  function updateTo(value: string) {
    if (!value) return;
    setPendingStart(null);
    setTo(value);
    if (value < from) setFrom(value);
    setCalendarMonth(startOfMonth(parseISO(value)));
  }

  function resetToday() {
    setPendingStart(null);
    setFrom(today);
    setTo(today);
    setCalendarMonth(startOfMonth(parseISO(today)));
  }

  const payMut = useMutation({
    mutationFn: async (spielerId: number) => {
      const res = await fetch(`/api/admin/bills/${spielerId}/paid`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${token}`
        },
        body: JSON.stringify({ datum: from }),
      });
      if (!res.ok) {
        const err = await res.json().catch(() => ({}));
        throw new Error(err.error || `HTTP ${res.status}`);
      }
      return await res.json();
    },
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["admin-bill-summary", from, to] });
      qc.invalidateQueries({ queryKey: ["admin-bill-activity-days"] });
      toast({ title: "Bezahlt", description: "Die Rechnung wurde als bezahlt markiert." });
      setSelectedBill(null);
    },
    onError: (e: any) => {
      toast({ title: "Fehler", description: e.message || "Unbekannter Fehler", variant: "destructive" });
    }
  });

  const filteredBills = (data?.players ?? []).filter(b => {
    if (filterState === "OPEN") return b.state === "OPEN";
    if (filterState === "PENDING_NEUTRAL") return b.state === "PENDING_NEUTRAL";
    if (filterState === "PAID") return b.state === "PAID";
    return true;
  });

  const summary = data ?? { generalTotalCents: 0, uniquePlayers: 0, paidPlayers: 0, games: 0, completedGames: 0, theoreticalClays: 0, confirmedClays: 0, unconfirmedClays: 0, gameBreakdown: [], productTotals: {}, players: [] };
  const products = Object.values(summary.productTotals || {}) as DaySummaryProductTotal[];
  const selectedDayLines = isSingleDay ? selectedBill?.dayLines ?? selectedBill?.lines ?? [] : selectedBill?.lines ?? [];
  const selectedDayCategories = isSingleDay ? selectedBill?.dayCategorySubtotals ?? selectedBill?.categorySubtotals ?? {} : selectedBill?.categorySubtotals ?? {};
  const selectedDayTotal = isSingleDay ? selectedBill?.dayTotalCents ?? selectedBill?.totalCents ?? 0 : selectedBill?.totalCents ?? 0;

  return (
    <div className="space-y-8 animate-in fade-in duration-500 pb-20">
      <header className="border-b border-border/50 pb-6 flex flex-col md:flex-row md:items-end justify-between gap-4">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Tagesabrechnung</h1>
          <p className="text-muted-foreground mt-2 text-sm font-medium">
            Vollständige Übersicht für einen Tag oder einen frei gewählten Zeitraum.
          </p>
        </div>
        <div className="text-right">
          <p className="text-xs uppercase tracking-widest font-bold text-muted-foreground">Ausgewählter Zeitraum</p>
          <p className="mt-1 text-sm font-bold">{periodLabel(from, to)}</p>
        </div>
      </header>

      <section className="grid grid-cols-1 lg:grid-cols-[auto_minmax(0,1fr)] gap-5">
        <div className="bg-card border border-border/50 rounded-xl shadow-sm overflow-hidden">
          <Calendar
            mode="range"
            month={calendarMonth}
            onMonthChange={setCalendarMonth}
            selected={{
              from: parseISO(from),
              to: pendingStart === null ? parseISO(to) : undefined,
            }}
            onDayClick={selectCalendarDay}
            locale={de}
            modifiers={{ activity: activityDates }}
            modifiersClassNames={{
              activity: "after:absolute after:bottom-1 after:left-1/2 after:z-20 after:h-1.5 after:w-1.5 after:-translate-x-1/2 after:rounded-full after:bg-emerald-500",
            }}
            className="w-full p-4 [--cell-size:2.6rem] sm:[--cell-size:3rem]"
            data-testid="billing-calendar"
          />
          <div className="border-t border-border/40 px-4 py-3 flex flex-wrap items-center gap-x-5 gap-y-2 text-xs text-muted-foreground">
            <span className="flex items-center gap-2">
              <span className="h-2 w-2 rounded-full bg-emerald-500" />
              Tag mit Aktivität
            </span>
            {activityError && (
              <span className="flex items-center gap-1.5 text-destructive">
                <AlertCircle size={13} />
                Aktivitätsmarkierungen konnten nicht geladen werden
              </span>
            )}
          </div>
        </div>

        <div className="bg-card border border-border/50 rounded-xl p-5 shadow-sm flex flex-col justify-between gap-6">
          <div>
            <div className="flex items-start justify-between gap-4">
              <div className="flex items-center gap-3">
                <div className="h-10 w-10 rounded-xl bg-primary/10 text-primary flex items-center justify-center">
                  <CalendarRange size={20} />
                </div>
                <div>
                  <h2 className="font-bold">Zeitraum auswählen</h2>
                  <p className="text-sm text-muted-foreground mt-0.5">
                    {pendingStart
                      ? "Startdatum gewählt – jetzt das Enddatum auswählen."
                      : "Erster Klick: Von-Datum. Zweiter Klick: Bis-Datum."}
                  </p>
                </div>
              </div>
              <button
                onClick={resetToday}
                className="h-9 px-3 rounded-lg border border-border/60 text-xs font-bold text-muted-foreground hover:text-foreground hover:bg-secondary/40 transition-colors flex items-center gap-2"
              >
                <RotateCcw size={14} />
                Heute
              </button>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-2 gap-4 mt-6">
              <label className="space-y-2">
                <span className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Von</span>
                <input
                  type="date"
                  value={from}
                  onChange={(event) => updateFrom(event.target.value)}
                  className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-bold focus:outline-none focus:ring-2 focus:ring-primary/40"
                  data-testid="date-picker-from"
                />
              </label>
              <label className="space-y-2">
                <span className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Bis</span>
                <input
                  type="date"
                  value={to}
                  onChange={(event) => updateTo(event.target.value)}
                  className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-bold focus:outline-none focus:ring-2 focus:ring-primary/40"
                  data-testid="date-picker-to"
                />
              </label>
            </div>
          </div>

          <div className={cn(
            "rounded-xl border px-4 py-3 text-sm",
            pendingStart
              ? "border-amber-500/30 bg-amber-500/10 text-amber-700 dark:text-amber-300"
              : "border-primary/20 bg-primary/5 text-foreground"
          )}>
            <p className="font-bold">
              {pendingStart ? `Von ${format(parseISO(pendingStart), "dd.MM.yyyy", { locale: de })}` : periodLabel(from, to)}
            </p>
            <p className="text-xs mt-1 opacity-75">
              {pendingStart
                ? "Wähle denselben Tag erneut für eine Einzeltagesansicht."
                : isSingleDay
                  ? "Einzeltagesansicht – Rechnungen können als bezahlt markiert werden."
                  : "Zeitraumansicht – Zahlungen werden nur in einer Einzeltagesansicht geändert."}
            </p>
          </div>
        </div>
      </section>

      {/* Summary Cards */}
      <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-6 gap-4">
         <StatCard label="Gesamtumsatz" value={formatMoney(summary.generalTotalCents)} sub={isSingleDay ? "Alle Produkte des Tages" : "Alle Produkte im Zeitraum"} icon={Coins} />
         <StatCard label="Spiele" value={summary.games} sub={`${summary.completedGames} abgeschlossen`} icon={Activity} />
         <StatCard label="Theoretisch" value={summary.theoreticalClays} sub="Resultate abgeschlossener Spiele" icon={Target} />
         <StatCard label="Real bestätigt" value={summary.confirmedClays} sub="Nur Auslösungen mit ACK" icon={CircleCheck} />
         <StatCard label="Differenz" value={summary.unconfirmedClays} sub="Ohne bestätigten ACK" icon={CircleDashed} />
         <StatCard label={isSingleDay ? "Rechnungen" : "Spieler"} value={summary.uniquePlayers} sub={`${summary.paidPlayers} vollständig bezahlt`} icon={Receipt} />
      </div>

      <section className="space-y-4">
        <div>
          <h2 className="text-xl font-bold tracking-tight">Tauben nach Spieltyp</h2>
          <p className="text-sm text-muted-foreground mt-1">
            Theoretisch basiert auf gespeicherten Resultaten. Real zählt ausschließlich vom Empfänger bestätigte ACK-Auslösungen.
          </p>
        </div>
        <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
          {isLoading ? (
            <div className="p-6 space-y-3">
              {[1, 2, 3].map((i) => <Skeleton key={i} className="h-10 w-full rounded-lg bg-secondary/30" />)}
            </div>
          ) : summary.gameBreakdown.length === 0 ? (
            <div className="p-10 text-center text-muted-foreground">
              <Target size={32} className="mx-auto mb-3 opacity-30" />
              <p className="text-sm font-medium">Keine Spiele im ausgewählten Zeitraum.</p>
            </div>
          ) : (
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Spieltyp</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Spiele</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Spieler-Einsätze</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Theoretisch</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Real</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Differenz</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {summary.gameBreakdown.map((entry) => (
                  <TableRow key={entry.modus} className="border-border/30 hover:bg-secondary/10">
                    <TableCell>
                      <p className="font-bold text-sm">{modusLabel(entry.modus)}</p>
                      {entry.completedGames !== entry.games && (
                        <p className="text-[10px] text-muted-foreground mt-0.5">{entry.completedGames} von {entry.games} abgeschlossen</p>
                      )}
                    </TableCell>
                    <TableCell className="text-right font-mono font-bold">{entry.games}</TableCell>
                    <TableCell className="text-right font-mono font-bold">{entry.playerParticipations}</TableCell>
                    <TableCell className="text-right font-mono font-bold">{entry.theoreticalClays}</TableCell>
                    <TableCell className="text-right font-mono font-bold text-emerald-600 dark:text-emerald-400">{entry.confirmedClays}</TableCell>
                    <TableCell className={cn(
                      "text-right font-mono font-black",
                      entry.unconfirmedClays > 0 ? "text-amber-600 dark:text-amber-400" : "text-muted-foreground"
                    )}>{entry.unconfirmedClays}</TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          )}
        </div>
        <p className="text-xs text-muted-foreground">
          Hinweis: Bei älteren Spielen kann „0 real bestätigt“ bedeuten, dass die ACK-Zählung damals noch nicht gespeichert wurde.
          Die Differenz ändert keine Rechnung automatisch.
        </p>
      </section>

      <div className="grid grid-cols-1 xl:grid-cols-3 gap-6">
        
        {/* Left Column: Player Bills */}
        <div className="xl:col-span-2 space-y-4">
          <div className="flex items-center justify-between">
            <h2 className="text-xl font-bold tracking-tight">{isSingleDay ? "Spielerrechnungen" : "Spieleraktivitäten"}</h2>
            <div className="flex gap-1 bg-secondary/20 rounded-xl p-1 border border-border/40">
              {(["ALL", "OPEN", "PENDING_NEUTRAL", "PAID"] as const).map(f => (
                <button
                  key={f}
                  onClick={() => setFilterState(f)}
                  className={cn(
                    "px-4 py-1.5 rounded-lg text-xs font-bold transition-colors",
                    filterState === f
                      ? "bg-card text-foreground shadow-sm border border-border/50"
                      : "text-muted-foreground hover:text-foreground"
                  )}
                >
                  {f === "ALL" ? "Alle" : f === "OPEN" ? "Offen" : f === "PENDING_NEUTRAL" ? "Neutral" : "Bezahlt"}
                </button>
              ))}
            </div>
          </div>

          <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
            {isLoading ? (
              <div className="p-6 space-y-3">
                {[1, 2, 3, 4].map((i) => <Skeleton key={i} className="h-16 w-full rounded-lg bg-secondary/30" />)}
              </div>
            ) : error ? (
              <div className="p-12 text-center text-destructive">
                <AlertCircle size={32} className="mx-auto mb-3 opacity-50" />
                <p className="text-sm font-medium">Fehler beim Laden: {(error as any).message}</p>
              </div>
            ) : filteredBills.length === 0 ? (
              <div className="p-12 text-center text-muted-foreground">
                <Receipt size={32} className="mx-auto mb-3 opacity-30" />
                <p className="text-sm font-medium">Keine Aktivitäten im ausgewählten Zeitraum gefunden.</p>
              </div>
            ) : (
              <div className="divide-y divide-border/30">
                {filteredBills.map((bill) => (
                  <div 
                    key={bill.spielerId}
                    onClick={() => setSelectedBill(bill)}
                    className="flex items-center justify-between p-4 hover:bg-secondary/20 transition-colors cursor-pointer group"
                    data-testid={`bill-row-${bill.spielerId}`}
                  >
                    <div className="flex items-center gap-4">
                      <div className={cn(
                        "w-10 h-10 rounded-full flex items-center justify-center border shadow-sm transition-colors",
                        bill.state === "PAID"
                          ? "bg-emerald-500/10 border-emerald-500/20 text-emerald-500" 
                          : bill.state === "PENDING_NEUTRAL"
                            ? "bg-blue-500/10 border-blue-500/20 text-blue-500"
                            : "bg-amber-500/10 border-amber-500/20 text-amber-500"
                      )}>
                        {bill.state === "PAID" ? <CheckCircle size={18} strokeWidth={2.5} /> : <Clock size={18} strokeWidth={2.5} />}
                      </div>
                      <div>
                        <p className="font-bold text-foreground text-sm group-hover:text-primary transition-colors">{bill.spielerName}</p>
                         <p className="text-xs text-muted-foreground font-mono mt-0.5">{bill.mitgliedNr || "Kein Mitglied"}</p>
                      </div>
                    </div>
                    
                    <div className="flex flex-col items-end gap-1">
                      <p className="font-mono font-black text-lg">{formatMoney(bill.totalCents)}</p>
                      {isSingleDay && bill.dayTotalCents !== undefined && bill.dayTotalCents !== bill.totalCents && (
                        <span className="text-[10px] font-mono text-muted-foreground">
                           Tag: {formatMoney(bill.dayTotalCents)}
                        </span>
                      )}
                      {!isSingleDay && bill.openTotalCents !== undefined && (
                        <span className="text-[10px] font-mono text-muted-foreground">
                          Davon offen: {formatMoney(bill.openTotalCents)}
                        </span>
                      )}
                      {bill.state === "PAID" ? (
                         <span className="text-[10px] uppercase tracking-widest font-bold text-emerald-500">Bezahlt</span>
                      ) : bill.state === "PENDING_NEUTRAL" ? (
                        <span className="text-[10px] uppercase tracking-widest font-bold text-blue-500">Neutral</span>
                      ) : (
                         <span className="text-[10px] uppercase tracking-widest font-bold text-amber-500">Offen</span>
                      )}
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>

        {/* Right Column: Products Summary */}
        <div className="space-y-4">
           <h2 className="text-xl font-bold tracking-tight">Verkaufte Produkte</h2>
          
          <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
            {isLoading ? (
              <div className="p-6 space-y-3">
                {[1, 2, 3].map((i) => <Skeleton key={i} className="h-10 w-full rounded-lg bg-secondary/30" />)}
              </div>
            ) : products.length === 0 ? (
              <div className="p-12 text-center text-muted-foreground">
                <ShoppingCart size={32} className="mx-auto mb-3 opacity-30" />
                <p className="text-sm font-medium">Keine Produkte verkauft.</p>
              </div>
            ) : (
              <Table>
                <TableHeader className="bg-secondary/20">
                  <TableRow className="border-border/50 hover:bg-transparent">
                    <TableHead className="text-xs uppercase tracking-widest font-bold">Produkt</TableHead>
                   <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Menge</TableHead>
                    <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Total</TableHead>
                  </TableRow>
                </TableHeader>
                <TableBody>
                  {products.map((p) => (
                    <TableRow key={p.productName} className="border-border/30 hover:bg-secondary/10">
                      <TableCell>
                        <p className="font-bold text-xs">{p.productName}</p>
                        <Badge variant="outline" className="text-[9px] font-mono mt-1 opacity-70 px-1 py-0 h-4">{p.category}</Badge>
                      </TableCell>
                      <TableCell className="text-right font-mono font-bold text-muted-foreground text-xs">{p.quantity}</TableCell>
                      <TableCell className="text-right font-mono font-bold text-primary text-xs">{formatMoney(p.totalCents)}</TableCell>
                    </TableRow>
                  ))}
                  <TableRow className="bg-secondary/20 border-border/50">
                    <TableCell colSpan={2} className="font-black text-xs uppercase tracking-widest">Total</TableCell>
                    <TableCell className="text-right font-mono font-black text-sm">{formatMoney(products.reduce((acc, p) => acc + p.totalCents, 0))}</TableCell>
                  </TableRow>
                </TableBody>
              </Table>
            )}
          </div>
        </div>
      </div>

      {/* Bill Detail Dialog */}
      <Dialog open={!!selectedBill} onOpenChange={(o) => !o && setSelectedBill(null)}>
        <DialogContent className="sm:max-w-2xl p-0 overflow-hidden gap-0">
          {selectedBill && (
            <>
              <div className={cn(
                "px-6 py-8 flex items-start justify-between border-b",
                selectedBill.state === "PAID" 
                  ? "bg-emerald-500/10 border-emerald-500/20" 
                  : selectedBill.state === "PENDING_NEUTRAL" 
                    ? "bg-blue-500/10 border-blue-500/20" 
                    : "bg-amber-500/10 border-amber-500/20"
              )}>
                <div>
                  <h2 className="text-2xl font-black">{selectedBill.spielerName}</h2>
                   <p className="font-mono text-sm opacity-70 mt-1">{selectedBill.mitgliedNr || "Gast"}</p>
                  
                  <div className="flex items-center gap-2 mt-4">
                    <Badge variant={selectedBill.state === "PAID" ? "default" : "secondary"} className={cn(
                      "font-bold px-3 py-1",
                      selectedBill.state === "PAID" 
                        ? "bg-emerald-500 hover:bg-emerald-600 text-white" 
                        : selectedBill.state === "PENDING_NEUTRAL" 
                          ? "bg-blue-500/20 text-blue-600 hover:bg-blue-500/30" 
                          : "bg-amber-500/20 text-amber-600 hover:bg-amber-500/30"
                    )}>
                       {selectedBill.state === "PAID" ? "BEZAHLT" : selectedBill.state === "PENDING_NEUTRAL" ? "NEUTRAL" : "OFFEN"}
                    </Badge>
                    {selectedBill.state === "PAID" && selectedBill.payment && (
                      <span className="text-xs font-mono opacity-60">
                        {new Date(selectedBill.payment.paidAt).toLocaleTimeString()} 
                        {selectedBill.payment.markedByAdmin 
                           ? ` – von ${selectedBill.payment.markedByAdmin.adminName}`
                           : selectedBill.payment.markedByApiKey
                             ? ` – per API (${selectedBill.payment.markedByApiKey.keyName})`
                             : " – System"
                        }
                      </span>
                    )}
                  </div>
                </div>
                <div className="text-right">
                   <p className="text-xs uppercase tracking-widest font-bold opacity-60 mb-1">
                     {isSingleDay ? "Aktuell offen" : "Aktivität im Zeitraum"}
                   </p>
                  <p className="text-4xl font-mono font-black">
                    {formatMoney(isSingleDay ? selectedBill.totalCents : selectedDayTotal)}
                  </p>
                </div>
              </div>

              <div className="p-6 bg-card max-h-[60vh] overflow-y-auto">
                {/* Credits Summary */}
                <div className="mb-6 grid grid-cols-2 md:grid-cols-4 gap-4 bg-secondary/20 rounded-xl p-4 border border-border/40">
                  <div>
                     <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Guthaben gekauft</p>
                    <p className="font-mono font-bold mt-1 text-lg">{selectedBill.credit.granted}</p>
                  </div>
                  <div>
                     <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Gespielt</p>
                    <p className="font-mono font-bold mt-1 text-lg">{selectedBill.credit.used}</p>
                  </div>
                  <div>
                     <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Rest</p>
                    <p className="font-mono font-black mt-1 text-lg text-primary">{selectedBill.credit.remaining}</p>
                  </div>
                  <div>
                     <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">
                       {isSingleDay ? "Tagessumme" : "Zeitraumsumme"}
                     </p>
                    <p className="font-mono font-black mt-1 text-lg">{formatMoney(selectedDayTotal)}</p>
                  </div>
                </div>

                {/* Subtotals by category */}
                <div className="mb-6">
                   <h4 className="text-xs uppercase tracking-widest font-bold text-muted-foreground mb-3">
                     {isSingleDay ? "Zusammenfassung des Tages" : "Zusammenfassung des Zeitraums"}
                   </h4>
                  <div className="flex flex-wrap gap-2">
                    {Object.entries(selectedDayCategories).map(([cat, cents]) => (
                      <div key={cat} className="flex items-center gap-2 bg-background border border-border/50 rounded-lg px-3 py-1.5 shadow-sm">
                        <Badge variant="outline" className="text-[9px] font-mono px-1 py-0">{cat}</Badge>
                        <span className="font-mono font-bold text-sm">{formatMoney(cents)}</span>
                      </div>
                    ))}
                  </div>
                </div>

                {/* Line Items */}
                <div>
                   <h4 className="text-xs uppercase tracking-widest font-bold text-muted-foreground mb-3">
                     {isSingleDay ? "Einkäufe des Tages" : "Aktivitäten im Zeitraum"}
                   </h4>
                  <div className="border border-border/50 rounded-xl overflow-hidden shadow-sm">
                    <Table>
                      <TableHeader className="bg-secondary/20">
                        <TableRow className="border-border/50 hover:bg-transparent">
                          <TableHead className="text-[10px] uppercase tracking-widest font-bold h-8 py-2">Positioun</TableHead>
                          <TableHead className="text-right text-[10px] uppercase tracking-widest font-bold h-8 py-2">Stéckpräis</TableHead>
                          <TableHead className="text-right text-[10px] uppercase tracking-widest font-bold h-8 py-2">Quantitéit</TableHead>
                          <TableHead className="text-right text-[10px] uppercase tracking-widest font-bold h-8 py-2">Total</TableHead>
                        </TableRow>
                      </TableHeader>
                      <TableBody>
                        {selectedDayLines.map((line, idx) => (
                          <TableRow key={idx} className="border-border/30 hover:bg-secondary/10">
                            <TableCell className="py-2.5">
                              <p className="font-bold text-sm">{line.productName}</p>
                              <p className="text-[10px] font-mono text-muted-foreground mt-0.5">{line.category}</p>
                            </TableCell>
                            <TableCell className="text-right font-mono text-muted-foreground py-2.5 text-xs">{formatMoney(line.unitPriceCents)}</TableCell>
                            <TableCell className="text-right font-mono font-bold py-2.5 text-xs">x{line.quantity}</TableCell>
                            <TableCell className="text-right font-mono font-bold text-primary py-2.5 text-sm">{formatMoney(line.totalCents)}</TableCell>
                          </TableRow>
                        ))}
                        {selectedDayLines.length === 0 && (
                          <TableRow>
                            <TableCell colSpan={4} className="py-6 text-center text-sm text-muted-foreground">
                               Keine abgerechneten Aktivitäten für den ausgewählten Zeitraum
                            </TableCell>
                          </TableRow>
                        )}
                      </TableBody>
                    </Table>
                  </div>
                </div>
              </div>

              <div className="p-4 border-t border-border/50 bg-secondary/10 flex justify-end gap-3">
                <button 
                  onClick={() => setSelectedBill(null)} 
                  className="px-6 py-2.5 text-sm font-bold text-muted-foreground hover:text-foreground hover:bg-secondary rounded-lg transition-colors"
                >
                  Schließen
                </button>
                {isSingleDay && selectedBill.state !== "PAID" && (
                  <button 
                    onClick={() => payMut.mutate(selectedBill.spielerId)}
                    disabled={payMut.isPending}
                    className="px-8 py-2.5 bg-emerald-500 hover:bg-emerald-600 text-white text-sm font-bold rounded-lg shadow-lg shadow-emerald-500/20 transition-all active:scale-95 disabled:opacity-50 flex items-center gap-2"
                    data-testid="button-mark-paid"
                  >
                     {payMut.isPending ? "Wird gespeichert..." : (
                      <>
                        <CheckCircle size={18} strokeWidth={2.5} />
                         Als bezahlt markieren
                      </>
                    )}
                  </button>
                )}
                {!isSingleDay && (
                  <p className="mr-auto self-center text-xs text-muted-foreground">
                    Zum Bezahlen bitte einen einzelnen Tag auswählen.
                  </p>
                )}
              </div>
            </>
          )}
        </DialogContent>
      </Dialog>
    </div>
  );
}