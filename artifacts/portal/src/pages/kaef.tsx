import {
  getGetMyPurchasesQueryKey,
  useGetMyPurchases,
  type GetMyPurchases200,
  type PlayerPurchase,
} from "@workspace/api-client-react";
import { Badge } from "@/components/ui/badge";
import { Skeleton } from "@/components/ui/skeleton";
import { AlertCircle, ReceiptText, ShoppingBag } from "lucide-react";

type Purchase = PlayerPurchase & {
  totalCents: number;
};

type PurchasesResponse = Omit<GetMyPurchases200, "purchases"> & {
  purchases: Purchase[];
};

function formatMoney(cents: number) {
  if (!Number.isFinite(cents)) return "—";

  return new Intl.NumberFormat("lb-LU", {
    style: "currency",
    currency: "EUR",
  }).format(cents / 100);
}

function formatDate(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;

  return new Intl.DateTimeFormat("lb-LU", {
    day: "2-digit",
    month: "2-digit",
    year: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  }).format(date);
}

function purchaseDate(purchase: Purchase) {
  return purchase.createdAt || purchase.datum;
}

export default function Kaef() {
  const { data, isLoading, isError, error, refetch, isFetching } = useGetMyPurchases<PurchasesResponse>({
    query: {
      queryKey: getGetMyPurchasesQueryKey(),
      // The deployed endpoint includes totalCents; the generated client was
      // produced before that response field was added to its schema.
      select: (response) => response as PurchasesResponse,
    },
  });

  const purchases = data?.purchases ?? [];

  return (
    <div className="space-y-6 animate-in fade-in slide-in-from-bottom-4 duration-500">
      <header className="border-b border-border/50 pb-6">
        <div className="flex items-center gap-3">
          <div className="flex h-10 w-10 items-center justify-center rounded-lg bg-primary/10 text-primary">
            <ReceiptText size={21} aria-hidden="true" />
          </div>
          <div>
            <h1 className="text-3xl font-bold tracking-tight">Meine Einkäufe</h1>
            <p className="mt-1 text-sm font-medium text-muted-foreground">
              Ihre persönliche Einkaufshistorie.
            </p>
          </div>
        </div>
      </header>

      {isLoading ? (
        <div className="space-y-3" aria-label="Einkaufshistorie wird geladen" data-testid="status-purchases-loading">
          {[1, 2, 3].map((item) => (
            <Skeleton key={item} className="h-20 w-full rounded-xl bg-card border border-border/50" />
          ))}
        </div>
      ) : isError ? (
        <section className="rounded-xl border border-destructive/30 bg-destructive/5 p-8 text-center" aria-live="polite" data-testid="status-purchases-error">
          <AlertCircle className="mx-auto mb-3 text-destructive" size={32} aria-hidden="true" />
          <h2 className="font-bold text-foreground">Einkaufshistorie konnte nicht geladen werden</h2>
          <p className="mx-auto mt-2 max-w-md text-sm text-muted-foreground">
            {error instanceof Error ? error.message : "Bitte versuchen Sie es erneut."}
          </p>
          <button
            type="button"
            onClick={() => refetch()}
            disabled={isFetching}
            className="mt-5 rounded-lg bg-primary px-4 py-2.5 text-sm font-bold text-primary-foreground transition-colors hover:bg-primary/90 disabled:cursor-not-allowed disabled:opacity-50"
            data-testid="button-retry-purchases"
          >
            {isFetching ? "Wird geladen…" : "Erneut versuchen"}
          </button>
        </section>
      ) : purchases.length === 0 ? (
        <section className="rounded-xl border border-border/50 bg-card p-12 text-center" data-testid="status-purchases-empty">
          <ShoppingBag className="mx-auto mb-4 text-muted-foreground/50" size={36} aria-hidden="true" />
          <h2 className="font-bold text-foreground">Noch keine Einkäufe</h2>
          <p className="mt-2 text-sm text-muted-foreground">
            Ihre abgeschlossenen Einkäufe werden hier angezeigt.
          </p>
        </section>
      ) : (
        <>
          <div className="hidden overflow-x-auto rounded-xl border border-border/50 bg-card shadow-sm md:block">
            <table className="w-full text-left text-sm">
              <caption className="sr-only">Ihre persönliche Einkaufshistorie</caption>
              <thead className="bg-secondary/20 text-xs uppercase tracking-widest text-muted-foreground">
                <tr className="border-b border-border/50">
                  <th scope="col" className="px-5 py-3 font-bold">Datum</th>
                  <th scope="col" className="px-5 py-3 font-bold">Produkt</th>
                  <th scope="col" className="px-5 py-3 font-bold">Kategorie</th>
                   <th scope="col" className="px-5 py-3 text-right font-bold">Menge</th>
                  <th scope="col" className="px-5 py-3 text-right font-bold">Stéckpräis</th>
                  <th scope="col" className="px-5 py-3 text-right font-bold">Total</th>
                </tr>
              </thead>
              <tbody>
                {purchases.map((purchase) => (
                  <tr key={purchase.externalId} className="border-b border-border/30 transition-colors last:border-0 hover:bg-secondary/20" data-testid={`row-purchase-${purchase.externalId}`}>
                    <td className="whitespace-nowrap px-5 py-4 font-mono text-xs text-muted-foreground">{formatDate(purchaseDate(purchase))}</td>
                    <td className="px-5 py-4 font-bold text-foreground">{purchase.productName}</td>
                    <td className="px-5 py-4"><CategoryBadge category={purchase.category} /></td>
                    <td className="px-5 py-4 text-right font-mono font-bold">{purchase.quantity}</td>
                    <td className="px-5 py-4 text-right font-mono text-muted-foreground">{formatMoney(purchase.unitPriceCents)}</td>
                    <td className="px-5 py-4 text-right font-mono font-bold text-primary" data-testid={`text-purchase-total-${purchase.externalId}`}>{formatMoney(purchase.totalCents)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>

          <div className="space-y-3 md:hidden">
            {purchases.map((purchase) => (
              <article key={purchase.externalId} className="rounded-xl border border-border/50 bg-card p-4 shadow-sm" data-testid={`card-purchase-${purchase.externalId}`}>
                <div className="flex items-start justify-between gap-3">
                  <div>
                    <h2 className="font-bold text-foreground">{purchase.productName}</h2>
                    <p className="mt-1 font-mono text-xs text-muted-foreground">{formatDate(purchaseDate(purchase))}</p>
                  </div>
                  <CategoryBadge category={purchase.category} />
                </div>
                <dl className="mt-4 grid grid-cols-3 gap-3 border-t border-border/40 pt-3 text-sm">
                  <div>
                     <dt className="text-xs font-bold uppercase tracking-wide text-muted-foreground">Menge</dt>
                    <dd className="mt-1 font-mono font-bold">{purchase.quantity}</dd>
                  </div>
                  <div>
                    <dt className="text-xs font-bold uppercase tracking-wide text-muted-foreground">Stéckpräis</dt>
                    <dd className="mt-1 font-mono">{formatMoney(purchase.unitPriceCents)}</dd>
                  </div>
                  <div className="text-right">
                    <dt className="text-xs font-bold uppercase tracking-wide text-muted-foreground">Total</dt>
                    <dd className="mt-1 font-mono font-bold text-primary">{formatMoney(purchase.totalCents)}</dd>
                  </div>
                </dl>
              </article>
            ))}
          </div>
        </>
      )}
    </div>
  );
}

function CategoryBadge({ category }: { category: string }) {
  return <Badge variant="outline" className="whitespace-nowrap border-primary/20 bg-primary/10 font-mono text-[10px] text-primary">{category}</Badge>;
}