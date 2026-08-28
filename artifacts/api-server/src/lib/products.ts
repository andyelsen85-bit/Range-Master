import { and, desc, eq, lte, sql } from "drizzle-orm";
import { db, productPriceRevisionsTable, productsTable, saleEventsTable } from "@workspace/db";

const systemProducts = [
  { code: "GAME_CREDIT", name: "Game credit", category: "GAME_CREDIT" as const },
  { code: "AMMO_CAL12", name: "12 calibre ammunition", category: "AMMO_CAL12" as const },
  { code: "AMMO_CAL20", name: "20 calibre ammunition", category: "AMMO_CAL20" as const },
] as const;

/** Safe to call from each catalogue endpoint; it never changes an existing row. */
export async function ensureSystemProducts(): Promise<void> {
  for (const product of systemProducts) {
    await db.insert(productsTable).values({ ...product, isSystem: true }).onConflictDoNothing({ target: productsTable.code });
  }
  const products = await db.select({ id: productsTable.id }).from(productsTable).where(eq(productsTable.isSystem, true));
  for (const product of products) {
    const [price] = await db.select({ id: productPriceRevisionsTable.id }).from(productPriceRevisionsTable)
      .where(eq(productPriceRevisionsTable.productId, product.id)).limit(1);
    if (!price) await db.insert(productPriceRevisionsTable).values({ productId: product.id, unitPriceCents: 0 });
  }
}

export async function catalogue(activeOnly = false) {
  await ensureSystemProducts();
  const products = await db.select().from(productsTable).where(activeOnly ? eq(productsTable.active, true) : undefined).orderBy(productsTable.id);
  return Promise.all(products.map(async (product) => {
    const [currentPrice] = await db.select().from(productPriceRevisionsTable)
      .where(and(eq(productPriceRevisionsTable.productId, product.id), lte(productPriceRevisionsTable.effectiveFrom, new Date())))
      .orderBy(desc(productPriceRevisionsTable.effectiveFrom), desc(productPriceRevisionsTable.id)).limit(1);
    return { ...product, currentPrice: currentPrice ?? null };
  }));
}

export async function daySalesReport(datum: string) {
  const sales = await db.select({
    productId: saleEventsTable.productId,
    productName: productsTable.name,
    quantity: sql<number>`COALESCE(SUM(${saleEventsTable.quantity}), 0)::int`,
    totalCents: sql<number>`COALESCE(SUM(${saleEventsTable.quantity} * ${saleEventsTable.unitPriceCents}), 0)::int`,
  }).from(saleEventsTable).innerJoin(productsTable, eq(saleEventsTable.productId, productsTable.id))
    .where(eq(saleEventsTable.datum, datum)).groupBy(saleEventsTable.productId, productsTable.name).orderBy(productsTable.name);
  return { datum, sales, totalCents: sales.reduce((sum, sale) => sum + Number(sale.totalCents), 0) };
}