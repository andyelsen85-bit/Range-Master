import { useState } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { useToast } from "@/hooks/use-toast";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Skeleton } from "@/components/ui/skeleton";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter, DialogDescription } from "@/components/ui/dialog";
import { Badge } from "@/components/ui/badge";
import { Pencil, Plus, Trash2, Tag, CheckCircle2, XCircle, Store, Archive } from "lucide-react";
import { cn } from "@/lib/utils";

import {
  useListAdminProducts,
  useGetAdminDaySales,
  useCreateAdminProduct,
  useUpdateAdminProduct,
  useDeleteAdminProduct,
  useCreateAdminProductPrice,
  getListAdminProductsQueryKey,
  getGetAdminDaySalesQueryKey
} from "@workspace/api-client-react";
import { Product, ProductCategory, FlexibleProductInputCategory } from "@workspace/api-client-react";

function formatMoney(cents: number) {
  return new Intl.NumberFormat('lb-LU', { style: 'currency', currency: 'EUR' }).format(cents / 100);
}

function todayStr(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

type TabId = "katalog" | "verkeef";

const TABS: { id: TabId; label: string }[] = [
  { id: "katalog", label: "Katalog" },
   { id: "verkeef", label: "Tagesverkäufe" },
];

export default function AdminVerkeef() {
  const [activeTab, setActiveTab] = useState<TabId>("katalog");

  return (
    <div className="space-y-6 animate-in fade-in duration-500">
      <header className="border-b border-border/50 pb-6">
        <h1 className="text-3xl font-bold tracking-tight">Katalog & Verkäufe</h1>
        <p className="text-muted-foreground mt-2 text-sm font-medium">
          Produkte verwalten und die Tagesverkäufe überprüfen.
        </p>
      </header>

      <div className="flex gap-1 bg-secondary/20 rounded-xl p-1 w-fit border border-border/40">
        {TABS.map((t) => (
          <button
            key={t.id}
            onClick={() => setActiveTab(t.id)}
            className={cn(
              "px-5 py-2 rounded-lg text-sm font-bold transition-colors",
              activeTab === t.id
                ? "bg-card text-foreground shadow-sm border border-border/50"
                : "text-muted-foreground hover:text-foreground"
            )}
          >
            {t.label}
          </button>
        ))}
      </div>

      {activeTab === "katalog" && <TabKatalog />}
      {activeTab === "verkeef" && <TabVerkeef />}
    </div>
  );
}

// ── Tab: Katalog ─────────────────────────────────────────────────────────────

function TabKatalog() {
  const qc = useQueryClient();
  const { toast } = useToast();
  
  const { data, isLoading } = useListAdminProducts();
  const products = data?.products ?? [];

  const createMut = useCreateAdminProduct({
    mutation: {
      onSuccess: () => {
        qc.invalidateQueries({ queryKey: getListAdminProductsQueryKey() });
        setAddOpen(false);
        setAddForm({ name: "", category: "FOOD", unitPriceString: "0.00", active: true });
        toast({ title: "Produkt erstellt" });
      },
      onError: (e) => toast({ title: "Fehler", description: e.data?.error || e.message || "Unbekannter Fehler", variant: "destructive" }),
    }
  });

  const updateMut = useUpdateAdminProduct({
    mutation: {
      onSuccess: () => {
        qc.invalidateQueries({ queryKey: getListAdminProductsQueryKey() });
        setEditProduct(null);
        toast({ title: "Produkt aktualisiert" });
      },
      onError: (e) => toast({ title: "Fehler", description: e.data?.error || e.message || "Unbekannter Fehler", variant: "destructive" }),
    }
  });

  const deleteMut = useDeleteAdminProduct({
    mutation: {
      onSuccess: () => {
        qc.invalidateQueries({ queryKey: getListAdminProductsQueryKey() });
        setDeleteProduct(null);
        toast({ title: "Produkt gelöscht" });
      },
      onError: (e) => toast({ title: "Fehler", description: e.data?.error || e.message || "Unbekannter Fehler", variant: "destructive" }),
    }
  });

  const priceMut = useCreateAdminProductPrice({
    mutation: {
      onSuccess: () => {
        qc.invalidateQueries({ queryKey: getListAdminProductsQueryKey() });
        setPriceProduct(null);
        toast({ title: "Preis aktualisiert" });
      },
      onError: (e) => toast({ title: "Fehler", description: e.data?.error || e.message || "Unbekannter Fehler", variant: "destructive" }),
    }
  });

  const [addOpen, setAddOpen] = useState(false);
  const [addForm, setAddForm] = useState({ name: "", category: "FOOD" as FlexibleProductInputCategory, unitPriceString: "0.00", active: true });

  const [editProduct, setEditProduct] = useState<Product | null>(null);
  const [editForm, setEditForm] = useState({ name: "", category: "FOOD" as FlexibleProductInputCategory, active: true });

  const [deleteProduct, setDeleteProduct] = useState<Product | null>(null);

  const [priceProduct, setPriceProduct] = useState<Product | null>(null);
  const [newPriceString, setNewPriceString] = useState("0.00"); // as string to prevent cursor jump

  const openEdit = (p: Product) => {
    setEditForm({ name: p.name, category: (p.category === "FOOD" || p.category === "DRINK" ? p.category : "FOOD"), active: p.active });
    setEditProduct(p);
  };

  const openPrice = (p: Product) => {
    setNewPriceString((p.currentPrice?.unitPriceCents ?? 0) > 0 ? (p.currentPrice!.unitPriceCents / 100).toFixed(2) : "0.00");
    setPriceProduct(p);
  };

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <p className="text-sm text-muted-foreground font-medium">
          Preise und Kategorien des Katalogs mit der Möglichkeit, Speisen und Getränke hinzuzufügen.
        </p>
        <button
          onClick={() => setAddOpen(true)}
          className="flex items-center gap-2 px-4 py-2.5 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors shrink-0"
        >
          <Plus size={16} /> Neues Produkt
        </button>
      </div>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-3">
            {[1, 2, 3, 4].map((i) => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}
          </div>
        ) : (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                   <TableHead className="text-xs uppercase tracking-widest font-bold">Name</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Code</TableHead>
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Kategorie</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Aktiv</TableHead>
                   <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Preis</TableHead>
                  <TableHead className="w-24"></TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {products.map((p) => (
                  <TableRow key={p.id} className={cn("border-border/30 hover:bg-secondary/20 transition-colors", !p.active && "opacity-60")}>
                    <TableCell className="font-bold text-foreground">
                      <div className="flex items-center gap-2">
                         {p.isSystem && <span title="Systemprodukt"><Archive size={14} className="text-primary/60" /></span>}
                        {p.name}
                      </div>
                    </TableCell>
                    <TableCell className="text-muted-foreground text-sm font-mono">{p.code || <span className="opacity-30">–</span>}</TableCell>
                    <TableCell>
                      <Badge variant="outline" className={cn(
                        "text-[10px] font-mono",
                        p.category === "GAME_CREDIT" && "bg-primary/10 text-primary border-primary/20",
                        (p.category === "AMMO_CAL12" || p.category === "AMMO_CAL20") && "bg-amber-500/10 text-amber-500 border-amber-500/20",
                        p.category === "FOOD" && "bg-emerald-500/10 text-emerald-500 border-emerald-500/20",
                        p.category === "DRINK" && "bg-blue-500/10 text-blue-500 border-blue-500/20",
                      )}>
                        {p.category}
                      </Badge>
                    </TableCell>
                    <TableCell className="text-center">
                      {p.active ? <CheckCircle2 size={16} className="text-green-500 mx-auto" /> : <XCircle size={16} className="text-muted-foreground/40 mx-auto" />}
                    </TableCell>
                    <TableCell className="text-right font-mono font-bold">
                      {p.currentPrice ? formatMoney(p.currentPrice.unitPriceCents) : <span className="text-muted-foreground/40">–</span>}
                    </TableCell>
                    <TableCell>
                      <div className="flex items-center justify-end gap-1">
                         <button onClick={() => openPrice(p)} title="Preis ändern" className="p-1.5 rounded-lg hover:bg-secondary text-muted-foreground/60 hover:text-foreground transition-colors"><Tag size={14} /></button>
                         <button onClick={() => openEdit(p)} title="Bearbeiten" className="p-1.5 rounded-lg hover:bg-secondary text-muted-foreground/60 hover:text-foreground transition-colors"><Pencil size={14} /></button>
                         {!p.isSystem && (
                           <button onClick={() => setDeleteProduct(p)} title="Löschen" className="p-1.5 rounded-lg hover:bg-destructive/20 text-muted-foreground/60 hover:text-destructive transition-colors"><Trash2 size={14} /></button>
                        )}
                      </div>
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </div>
        )}
      </div>

      {/* Add Dialog */}
      <Dialog open={addOpen} onOpenChange={setAddOpen}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Neues Produkt</DialogTitle>
            <DialogDescription>Neues Produkt hinzufügen (Speise oder Getränk).</DialogDescription>
          </DialogHeader>
          <div className="space-y-4 py-2">
            <div className="space-y-1.5">
               <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Name *</label>
              <input value={addForm.name} onChange={(e) => setAddForm(f => ({ ...f, name: e.target.value }))} className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors" placeholder="Z.b. Cola" />
            </div>
            <div className="space-y-1.5">
              <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Kategorie</label>
              <select value={addForm.category} onChange={(e) => setAddForm(f => ({ ...f, category: e.target.value as FlexibleProductInputCategory }))} className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors">
                 <option value="FOOD">Speisen</option>
                 <option value="DRINK">Getränke</option>
              </select>
            </div>
            <div className="space-y-1.5">
               <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Preis (€)</label>
              <input type="number" step="0.01" min="0" value={addForm.unitPriceString} onChange={(e) => setAddForm(f => ({ ...f, unitPriceString: e.target.value }))} className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors" />
            </div>
            <div className="pt-2">
              <button type="button" onClick={() => setAddForm(f => ({ ...f, active: !f.active }))} className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">
                <div className={`w-9 h-5 rounded-full transition-colors relative ${addForm.active ? "bg-primary" : "bg-secondary border border-border/60"}`}>
                  <span className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-all ${addForm.active ? "left-4.5" : "left-0.5"}`} />
                </div>
                Aktiv
              </button>
            </div>
          </div>
          <DialogFooter>
             <button onClick={() => setAddOpen(false)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button onClick={() => createMut.mutate({ data: { name: addForm.name, category: addForm.category, active: addForm.active, unitPriceCents: Math.round(Number(addForm.unitPriceString || 0) * 100) } })} disabled={!addForm.name || createMut.isPending} className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50">
              {createMut.isPending ? "Erstellen…" : "Erstellen"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* Edit Dialog */}
      <Dialog open={!!editProduct} onOpenChange={(o) => !o && setEditProduct(null)}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Produkt bearbeiten</DialogTitle>
            <DialogDescription>{editProduct?.name}</DialogDescription>
          </DialogHeader>
          <div className="space-y-4 py-2">
            <div className="space-y-1.5">
               <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Name</label>
              <input value={editForm.name} onChange={(e) => setEditForm(f => ({ ...f, name: e.target.value }))} disabled={editProduct?.isSystem} className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 transition-colors disabled:opacity-50" />
            </div>
            {!editProduct?.isSystem && (
              <div className="space-y-1.5">
                <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Kategorie</label>
                <select value={editForm.category} onChange={(e) => setEditForm(f => ({ ...f, category: e.target.value as FlexibleProductInputCategory }))} className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 transition-colors">
                   <option value="FOOD">Speisen</option>
                   <option value="DRINK">Getränke</option>
                </select>
              </div>
            )}
            <div className="pt-2">
              <button type="button" onClick={() => setEditForm(f => ({ ...f, active: !f.active }))} className="flex items-center gap-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">
                <div className={`w-9 h-5 rounded-full transition-colors relative ${editForm.active ? "bg-primary" : "bg-secondary border border-border/60"}`}>
                  <span className={`absolute top-0.5 w-4 h-4 rounded-full bg-white shadow transition-all ${editForm.active ? "left-4.5" : "left-0.5"}`} />
                </div>
                Aktiv
              </button>
            </div>
          </div>
          <DialogFooter>
             <button onClick={() => setEditProduct(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button onClick={() => editProduct && updateMut.mutate({ id: editProduct.id, data: editForm })} disabled={!editForm.name || updateMut.isPending} className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50">
               {updateMut.isPending ? "Wird gespeichert…" : "Speichern"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* Delete Dialog */}
      <Dialog open={!!deleteProduct} onOpenChange={(o) => !o && setDeleteProduct(null)}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Produkt löschen</DialogTitle>
            <DialogDescription>
              Möchten Sie <strong>{deleteProduct?.name}</strong> wirklich löschen? Dies kann nicht rückgängig gemacht werden.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
             <button onClick={() => setDeleteProduct(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button onClick={() => deleteProduct && deleteMut.mutate({ id: deleteProduct.id })} disabled={deleteMut.isPending} className="px-4 py-2 bg-destructive hover:bg-destructive/90 text-destructive-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50">
               {deleteMut.isPending ? "Wird gelöscht…" : "Endgültig löschen"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* Price Dialog */}
      <Dialog open={!!priceProduct} onOpenChange={(o) => !o && setPriceProduct(null)}>
        <DialogContent className="sm:max-w-md">
          <DialogHeader>
            <DialogTitle>Neuen Preis festlegen</DialogTitle>
            <DialogDescription>Fügt eine neue Preisrevision für <strong>{priceProduct?.name}</strong> hinzu.</DialogDescription>
          </DialogHeader>
          <div className="space-y-4 py-2">
            <div className="space-y-1.5">
               <label className="text-xs font-bold uppercase tracking-widest text-muted-foreground">Preis (€)</label>
              <input type="number" step="0.01" min="0" value={newPriceString} onChange={(e) => setNewPriceString(e.target.value)} className="w-full bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 focus:border-primary/50 transition-colors" />
            </div>
          </div>
          <DialogFooter>
             <button onClick={() => setPriceProduct(null)} className="px-4 py-2 text-sm font-semibold text-muted-foreground hover:text-foreground transition-colors">Abbrechen</button>
            <button onClick={() => priceProduct && priceMut.mutate({ id: priceProduct.id, data: { unitPriceCents: Math.round(Number(newPriceString || 0) * 100) } })} disabled={priceMut.isPending} className="px-4 py-2 bg-primary hover:bg-primary/90 text-primary-foreground text-sm font-bold rounded-lg transition-colors disabled:opacity-50">
               {priceMut.isPending ? "Wird gespeichert…" : "Speichern"}
            </button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}

// ── Tab: Dagesverkeef ────────────────────────────────────────────────────────

function TabVerkeef() {
  const [datum, setDatum] = useState(todayStr());

  const { data, isLoading } = useGetAdminDaySales(
    { datum },
    { query: { enabled: !!datum, queryKey: getGetAdminDaySalesQueryKey({ datum }) } }
  );

  const sales = data?.sales ?? [];
  const total = data?.totalCents ?? 0;

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between flex-wrap gap-4">
        <p className="text-sm text-muted-foreground font-medium">
          Die Gesamtverkäufe des ausgewählten Tages, einschließlich Guthaben, Munition und Bar.
        </p>
        <input
          type="date"
          value={datum}
          onChange={(e) => e.target.value && setDatum(e.target.value)}
          className="bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 transition-colors"
        />
      </div>

      <div className="grid grid-cols-1">
        <div className="bg-card border border-border/50 rounded-xl p-5 shadow-sm">
          <p className="text-xs uppercase tracking-widest font-bold text-muted-foreground">Gesamtumsatz ({datum})</p>
          <p className="text-3xl font-bold mt-1 font-mono text-primary">{formatMoney(total)}</p>
        </div>
      </div>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-3">
            {[1, 2, 3].map((i) => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}
          </div>
        ) : sales.length === 0 ? (
          <div className="p-12 text-center text-muted-foreground">
            <Store size={32} className="mx-auto mb-3 opacity-30" />
            <p className="text-sm font-medium">Keine Verkäufe für den {datum}.</p>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Produkt</TableHead>
                   <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Menge</TableHead>
                  <TableHead className="text-right text-xs uppercase tracking-widest font-bold">Total</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {sales.map((s, i) => (
                  <TableRow key={i} className="border-border/30 hover:bg-secondary/20 transition-colors">
                    <TableCell className="font-bold text-foreground">{s.productName || `Produkt #${s.productId}`}</TableCell>
                    <TableCell className="text-right font-mono font-bold text-muted-foreground">{s.quantity}</TableCell>
                    <TableCell className="text-right font-mono font-bold">{formatMoney(s.totalCents)}</TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </div>
        )}
      </div>
    </div>
  );
}
