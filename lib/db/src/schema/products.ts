import { boolean, date, index, integer, pgEnum, pgTable, serial, text, timestamp } from "drizzle-orm/pg-core";
import { createInsertSchema } from "drizzle-zod";
import { z } from "zod/v4";
import { spielerTable } from "./spieler";

export const productCategoryEnum = pgEnum("product_category", ["GAME_CREDIT", "AMMO_CAL12", "AMMO_CAL20", "FOOD", "DRINK"]);

/** Products with isSystem=true are the protected, built-in catalogue entries. */
export const productsTable = pgTable("products", {
  id: serial("id").primaryKey(),
  code: text("code").unique(),
  name: text("name").notNull(),
  category: productCategoryEnum("category").notNull(),
  isSystem: boolean("is_system").notNull().default(false),
  active: boolean("active").notNull().default(true),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow().$onUpdate(() => new Date()),
});

/** Price history is append-only; sales point to the row used at checkout. */
export const productPriceRevisionsTable = pgTable("product_price_revisions", {
  id: serial("id").primaryKey(),
  productId: integer("product_id").notNull().references(() => productsTable.id, { onDelete: "restrict" }),
  unitPriceCents: integer("unit_price_cents").notNull(),
  effectiveFrom: timestamp("effective_from", { withTimezone: true }).notNull().defaultNow(),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
}, (t) => [
  index("product_price_revisions_product_effective_idx").on(t.productId, t.effectiveFrom),
]);

/** Signed quantities support correction/reversal events and externalId makes sync idempotent. */
export const saleEventsTable = pgTable("sale_events", {
  id: serial("id").primaryKey(),
  externalId: text("external_id").notNull().unique(),
  spielerId: integer("spieler_id").notNull().references(() => spielerTable.id, { onDelete: "restrict" }),
  datum: date("datum", { mode: "string" }).notNull(),
  productId: integer("product_id").notNull().references(() => productsTable.id, { onDelete: "restrict" }),
  priceRevisionId: integer("price_revision_id").notNull().references(() => productPriceRevisionsTable.id, { onDelete: "restrict" }),
  /** Historical catalogue values: reports must survive later product edits. */
  productName: text("product_name").notNull(),
  productCategory: productCategoryEnum("product_category").notNull(),
  unitPriceCents: integer("unit_price_cents").notNull(),
  quantity: integer("quantity").notNull(),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
}, (t) => [
  index("sale_events_datum_idx").on(t.datum),
  index("sale_events_spieler_datum_idx").on(t.spielerId, t.datum),
  index("sale_events_product_datum_idx").on(t.productId, t.datum),
]);

export const insertProductSchema = createInsertSchema(productsTable).omit({ id: true, createdAt: true, updatedAt: true });
export const insertProductPriceRevisionSchema = createInsertSchema(productPriceRevisionsTable).omit({ id: true, createdAt: true });
export const insertSaleEventSchema = createInsertSchema(saleEventsTable).omit({ id: true, createdAt: true, unitPriceCents: true });
export type InsertProduct = z.infer<typeof insertProductSchema>;
export type Product = typeof productsTable.$inferSelect;
export type InsertProductPriceRevision = z.infer<typeof insertProductPriceRevisionSchema>;
export type ProductPriceRevision = typeof productPriceRevisionsTable.$inferSelect;
export type InsertSaleEvent = z.infer<typeof insertSaleEventSchema>;
export type SaleEvent = typeof saleEventsTable.$inferSelect;