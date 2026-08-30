import { useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useAuthStore } from "@/store/use-auth-store";
import { useToast } from "@/hooks/use-toast";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Skeleton } from "@/components/ui/skeleton";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter, DialogDescription } from "@/components/ui/dialog";
import { Badge } from "@/components/ui/badge";
import { Coins, FileText, CheckCircle2, AlertCircle, ShoppingCart, Info, Activity, Target, Receipt, CheckCircle, Clock } from "lucide-react";
import { cn } from "@/lib/utils";

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
  dayLines: {
    productId: number;
    productName: string;
    category: string;
    quantity: number;
    unitPriceCents: number;
    totalCents: number;
  }[];

  categorySubtotals: Record<string, number>;
  dayCategorySubtotals: Record<string, number>;
  totalCents: number;
  dayTotalCents: number;
}

interface DaySummaryProductTotal {
  productName: string;
  category: string;
  quantity: number;
  totalCents: number;
}

interface DaySummary {
  datum: string;
  generalTotalCents: number;
  games: number;
  completedGames: number;
  confirmedClays: number;
  productTotals: Record<string, DaySummaryProductTotal>;
  players: DaySummaryPlayerBill[];
}

// ── Helpers ────────────────────────────────────────────────────────────────────

function formatMoney(cents: number) {
  return new Intl.NumberFormat('lb-LU', { style: 'currency', currency: 'EUR' }).format(cents / 100);
}

function todayStr(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
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
  
  const [datum, setDatum] = useState(todayStr());
  const [filterState, setFilterState] = useState<"ALL" | "OPEN" | "PENDING_NEUTRAL" | "PAID">("ALL");
  const [selectedBill, setSelectedBill] = useState<DaySummaryPlayerBill | null>(null);

  const { data, isLoading, error } = useQuery<DaySummary>({
    queryKey: ["admin-day-summary", datum],
    queryFn: async () => {
      const res = await fetch(`/api/admin/bills/day-summary?datum=${datum}`, {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (!res.ok) {
        if (res.status === 404) {
          return {
            datum,
            generalTotalCents: 0,
            games: 0,
            completedGames: 0,
            confirmedClays: 0,
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

  const payMut = useMutation({
    mutationFn: async (spielerId: number) => {
      const res = await fetch(`/api/admin/bills/${spielerId}/paid`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${token}`
        },
        body: JSON.stringify({ datum }),
      });
      if (!res.ok) {
        const err = await res.json().catch(() => ({}));
        throw new Error(err.error || `HTTP ${res.status}`);
      }
      return await res.json();
    },
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["admin-day-summary", datum] });
      toast({ title: "Bezuelt", description: "D'Rechnung gouf als bezuelt markéiert." });
      setSelectedBill(null);
    },
    onError: (e: any) => {
      toast({ title: "Feeler", description: e.message || "Onbekannte Feeler", variant: "destructive" });
    }
  });

  const filteredBills = (data?.players ?? []).filter(b => {
    if (filterState === "OPEN") return b.state === "OPEN";
    if (filterState === "PENDING_NEUTRAL") return b.state === "PENDING_NEUTRAL";
    if (filterState === "PAID") return b.state === "PAID";
    return true;
  });

  const summary = data ?? { generalTotalCents: 0, games: 0, completedGames: 0, confirmedClays: 0, productTotals: {} };
  const products = Object.values(summary.productTotals || {}) as DaySummaryProductTotal[];
  const selectedDayLines = selectedBill?.dayLines ?? selectedBill?.lines ?? [];
  const selectedDayCategories = selectedBill?.dayCategorySubtotals ?? selectedBill?.categorySubtotals ?? {};
  const selectedDayTotal = selectedBill?.dayTotalCents ?? selectedBill?.totalCents ?? 0;

  return (
    <div className="space-y-8 animate-in fade-in duration-500 pb-20">
      <header className="border-b border-border/50 pb-6 flex flex-col md:flex-row md:items-end justify-between gap-4">
        <div>
          <h1 className="text-3xl font-bold tracking-tight">Dagesofrechnung</h1>
          <p className="text-muted-foreground mt-2 text-sm font-medium">
            Komplett Iwwersiicht vun den Deeg, Rechnungen pro Spiller a Bezuelungen.
          </p>
        </div>
        <div className="flex items-center gap-3 bg-secondary/20 p-1.5 rounded-xl border border-border/40 w-fit">
          <div className="px-3">
            <Clock size={16} className="text-muted-foreground" />
          </div>
          <input
            type="date"
            value={datum}
            onChange={(e) => e.target.value && setDatum(e.target.value)}
            className="bg-card border border-border/60 rounded-lg px-4 py-2 text-sm font-bold focus:outline-none focus:ring-2 focus:ring-primary/40 transition-colors"
            data-testid="date-picker-summary"
          />
        </div>
      </header>

      {/* Summary Cards */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
        <StatCard label="Total Ëmsaz" value={formatMoney(summary.generalTotalCents)} sub="All Produkter" icon={Coins} />
        <StatCard label="Spiller (Games)" value={summary.games} sub={`${summary.completedGames} ofgeschloss`} icon={Activity} />
        <StatCard label="Tauben" value={summary.confirmedClays} sub="Ausgeléist (A-G: 1, H: 2)" icon={Target} />
        <StatCard label="Rechnungen" value={data?.players.length ?? 0} sub={`${data?.players.filter(b => b.state === "PAID").length ?? 0} bezuelt`} icon={Receipt} />
      </div>

      <div className="grid grid-cols-1 xl:grid-cols-3 gap-6">
        
        {/* Left Column: Player Bills */}
        <div className="xl:col-span-2 space-y-4">
          <div className="flex items-center justify-between">
            <h2 className="text-xl font-bold tracking-tight">Spiller Rechnungen</h2>
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
                  {f === "ALL" ? "All" : f === "OPEN" ? "Oppen" : f === "PENDING_NEUTRAL" ? "Neutral" : "Bezuelt"}
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
                <p className="text-sm font-medium">Feeler beim Lueden: {(error as any).message}</p>
              </div>
            ) : filteredBills.length === 0 ? (
              <div className="p-12 text-center text-muted-foreground">
                <Receipt size={32} className="mx-auto mb-3 opacity-30" />
                <p className="text-sm font-medium">Keng Rechnungen fonnt.</p>
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
                        <p className="text-xs text-muted-foreground font-mono mt-0.5">{bill.mitgliedNr || "Keen Member"}</p>
                      </div>
                    </div>
                    
                    <div className="flex flex-col items-end gap-1">
                      <p className="font-mono font-black text-lg">{formatMoney(bill.totalCents)}</p>
                      {bill.dayTotalCents !== bill.totalCents && (
                        <span className="text-[10px] font-mono text-muted-foreground">
                          Dag: {formatMoney(bill.dayTotalCents)}
                        </span>
                      )}
                      {bill.state === "PAID" ? (
                        <span className="text-[10px] uppercase tracking-widest font-bold text-emerald-500">Bezuelt</span>
                      ) : bill.state === "PENDING_NEUTRAL" ? (
                        <span className="text-[10px] uppercase tracking-widest font-bold text-blue-500">Neutral</span>
                      ) : (
                        <span className="text-[10px] uppercase tracking-widest font-bold text-amber-500">Oppen</span>
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
          <h2 className="text-xl font-bold tracking-tight">Verkaaften Produkter</h2>
          
          <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
            {isLoading ? (
              <div className="p-6 space-y-3">
                {[1, 2, 3].map((i) => <Skeleton key={i} className="h-10 w-full rounded-lg bg-secondary/30" />)}
              </div>
            ) : products.length === 0 ? (
              <div className="p-12 text-center text-muted-foreground">
                <ShoppingCart size={32} className="mx-auto mb-3 opacity-30" />
                <p className="text-sm font-medium">Keng Produkter verkaaft.</p>
              </div>
            ) : (
              <Table>
                <TableHeader className="bg-secondary/20">
                  <TableRow className="border-border/50 hover:bg-transparent">
                    <TableHead className="text-xs uppercase tracking-widest font-bold">Produkt</TableHead>
                    <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Qty</TableHead>
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
                  <p className="font-mono text-sm opacity-70 mt-1">{selectedBill.mitgliedNr || "Gaascht"}</p>
                  
                  <div className="flex items-center gap-2 mt-4">
                    <Badge variant={selectedBill.state === "PAID" ? "default" : "secondary"} className={cn(
                      "font-bold px-3 py-1",
                      selectedBill.state === "PAID" 
                        ? "bg-emerald-500 hover:bg-emerald-600 text-white" 
                        : selectedBill.state === "PENDING_NEUTRAL" 
                          ? "bg-blue-500/20 text-blue-600 hover:bg-blue-500/30" 
                          : "bg-amber-500/20 text-amber-600 hover:bg-amber-500/30"
                    )}>
                      {selectedBill.state === "PAID" ? "BEZUELT" : selectedBill.state === "PENDING_NEUTRAL" ? "NEUTRAL" : "OPPEN"}
                    </Badge>
                    {selectedBill.state === "PAID" && selectedBill.payment && (
                      <span className="text-xs font-mono opacity-60">
                        {new Date(selectedBill.payment.paidAt).toLocaleTimeString()} 
                        {selectedBill.payment.markedByAdmin 
                          ? ` - vun ${selectedBill.payment.markedByAdmin.adminName}` 
                          : selectedBill.payment.markedByApiKey 
                            ? ` - per API (${selectedBill.payment.markedByApiKey.keyName})`
                            : " - system"
                        }
                      </span>
                    )}
                  </div>
                </div>
                <div className="text-right">
                  <p className="text-xs uppercase tracking-widest font-bold opacity-60 mb-1">Aktuell oppen</p>
                  <p className="text-4xl font-mono font-black">{formatMoney(selectedBill.totalCents)}</p>
                </div>
              </div>

              <div className="p-6 bg-card max-h-[60vh] overflow-y-auto">
                {/* Credits Summary */}
                <div className="mb-6 grid grid-cols-2 md:grid-cols-4 gap-4 bg-secondary/20 rounded-xl p-4 border border-border/40">
                  <div>
                    <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Kreditter Kaaft</p>
                    <p className="font-mono font-bold mt-1 text-lg">{selectedBill.credit.granted}</p>
                  </div>
                  <div>
                    <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Gespillt</p>
                    <p className="font-mono font-bold mt-1 text-lg">{selectedBill.credit.used}</p>
                  </div>
                  <div>
                    <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Rescht</p>
                    <p className="font-mono font-black mt-1 text-lg text-primary">{selectedBill.credit.remaining}</p>
                  </div>
                  <div>
                    <p className="text-[10px] uppercase tracking-widest font-bold text-muted-foreground">Dagestotal</p>
                    <p className="font-mono font-black mt-1 text-lg">{formatMoney(selectedDayTotal)}</p>
                  </div>
                </div>

                {/* Subtotals by category */}
                <div className="mb-6">
                  <h4 className="text-xs uppercase tracking-widest font-bold text-muted-foreground mb-3">Zesummefaassung vum Dag</h4>
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
                  <h4 className="text-xs uppercase tracking-widest font-bold text-muted-foreground mb-3">Kaf vum Dag</h4>
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
                              Keng verrechent Aktivitéit fir dësen Dag
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
                  Zoumaachen
                </button>
                {selectedBill.state !== "PAID" && (
                  <button 
                    onClick={() => payMut.mutate(selectedBill.spielerId)}
                    disabled={payMut.isPending}
                    className="px-8 py-2.5 bg-emerald-500 hover:bg-emerald-600 text-white text-sm font-bold rounded-lg shadow-lg shadow-emerald-500/20 transition-all active:scale-95 disabled:opacity-50 flex items-center gap-2"
                    data-testid="button-mark-paid"
                  >
                    {payMut.isPending ? "Späicheren..." : (
                      <>
                        <CheckCircle size={18} strokeWidth={2.5} />
                        Als Bezuelt markéieren
                      </>
                    )}
                  </button>
                )}
              </div>
            </>
          )}
        </DialogContent>
      </Dialog>
    </div>
  );
}