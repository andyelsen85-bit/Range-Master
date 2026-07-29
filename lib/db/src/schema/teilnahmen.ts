import { pgTable, serial, integer, unique } from "drizzle-orm/pg-core";
import { createInsertSchema } from "drizzle-zod";
import { z } from "zod/v4";
import { spielerTable } from "./spieler";
import { spieleTable } from "./spiele";

export const spielTeilnahmenTable = pgTable("spiel_teilnahmen", {
  id: serial("id").primaryKey(),
  spielId: integer("spiel_id").notNull().references(() => spieleTable.id, { onDelete: "cascade" }),
  spielerId: integer("spieler_id").notNull().references(() => spielerTable.id),
  startPosten: integer("start_posten").notNull(),
  punkte: integer("punkte").notNull().default(0),
  lauf: integer("lauf").notNull(),
}, (t) => [
  unique().on(t.spielId, t.spielerId, t.lauf),
]);

export const insertSpielTeilnahmeSchema = createInsertSchema(spielTeilnahmenTable).omit({ id: true });
export type InsertSpielTeilnahme = z.infer<typeof insertSpielTeilnahmeSchema>;
export type SpielTeilnahme = typeof spielTeilnahmenTable.$inferSelect;
