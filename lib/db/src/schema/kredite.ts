import { pgTable, serial, text, integer, timestamp, date, pgEnum, index } from "drizzle-orm/pg-core";
import { createInsertSchema } from "drizzle-zod";
import { z } from "zod/v4";
import { spielerTable } from "./spieler";
import { productPriceRevisionsTable } from "./products";

export const kreditTypEnum = pgEnum("kredit_typ", ["GRANT", "USE"]);

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
  /** Terminal clock timestamp; null is retained for legacy rows. */
  occurredAt: timestamp("occurred_at", { withTimezone: true }),
  /** Immutable GAME_CREDIT price snapshot for USE events; null for legacy/grants. */
  priceRevisionId: integer("price_revision_id").references(() => productPriceRevisionsTable.id, { onDelete: "restrict" }),
  unitPriceCents: integer("unit_price_cents"),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
}, (t) => [
  index("kredit_events_datum_idx").on(t.datum),
]);

export const insertKreditEventSchema = createInsertSchema(kreditEventsTable).omit({ id: true, createdAt: true });
export type InsertKreditEvent = z.infer<typeof insertKreditEventSchema>;
export type KreditEvent = typeof kreditEventsTable.$inferSelect;
