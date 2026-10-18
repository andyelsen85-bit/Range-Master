import { pgTable, serial, text, integer, timestamp, date, pgEnum, index, check } from "drizzle-orm/pg-core";
import {sql} from "drizzle-orm";
import { createInsertSchema } from "drizzle-zod";
import { z } from "zod/v4";
import { spielerTable } from "./spieler";
import { productPriceRevisionsTable } from "./products";

export const kreditTypEnum = pgEnum("kredit_typ", ["GRANT", "USE", "VOID_USE"]);

/**
 * Day-scoped prepaid game credit events.
 * GRANT = operator added N credits for the day, USE = a game start consumed N.
 * externalId makes offline-queue pushes idempotent.
 */
export const kreditEventsTable = pgTable("kredit_events", {
  id: serial("id").primaryKey(),
  externalId: text("external_id").notNull().unique(),
  spielerId: integer("spieler_id").notNull().references(() => spielerTable.id),
  datum: date("datum").notNull(),
  typ: kreditTypEnum("typ").notNull(),
  anzahl: integer("anzahl").notNull(),
  /** Cancels exactly one immutable USE; nullable to allow legacy events. */
  voidsExternalId: text("voids_external_id").unique(),
  /** Terminal clock timestamp; null is retained for legacy rows. */
  occurredAt: timestamp("occurred_at", { withTimezone: true }),
  /** Immutable GAME_CREDIT price snapshot for USE events; null for legacy/grants. */
  priceRevisionId: integer("price_revision_id").references(() => productPriceRevisionsTable.id, { onDelete: "restrict" }),
  unitPriceCents: integer("unit_price_cents"),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
}, (t) => [
  index("kredit_events_datum_idx").on(t.datum),
  check("kredit_events_void_shape",sql`(
    (${t.typ}::text='VOID_USE' AND ${t.anzahl}=1 AND ${t.voidsExternalId} IS NOT NULL
     AND ${t.voidsExternalId}<>${t.externalId} AND ${t.occurredAt} IS NULL
     AND ${t.priceRevisionId} IS NULL AND ${t.unitPriceCents} IS NULL)
    OR (${t.typ}::text IN('GRANT','USE') AND ${t.voidsExternalId} IS NULL)
  )`),
]);

export const insertKreditEventSchema = createInsertSchema(kreditEventsTable).omit({ id: true, createdAt: true });
export type InsertKreditEvent = z.infer<typeof insertKreditEventSchema>;
export type KreditEvent = typeof kreditEventsTable.$inferSelect;
