import { pgTable, serial, text, boolean, timestamp, integer, pgEnum } from "drizzle-orm/pg-core";
import { createInsertSchema } from "drizzle-zod";
import { z } from "zod/v4";

export const modusEnum = pgEnum("modus", [
  "NORMAL",
  "HARAKIRI",
  "HARAKIRI_DELAYED",
  "HARAKIRI_FULL",
  "CUSTOM_1",
  "CUSTOM_2",
  "CUSTOM_3",
]);

export const maschineEnum = pgEnum("maschine", ["A", "B", "C", "D", "E", "F", "G", "H"]);

export const spieleTable = pgTable("spiele", {
  id: serial("id").primaryKey(),
  externalId: text("external_id").unique(),
  datum: timestamp("datum", { withTimezone: true }).notNull(),
  modus: modusEnum("modus").notNull().default("NORMAL"),
  lauf: integer("lauf").notNull(),
  taubenProLauf: integer("tauben_pro_lauf").notNull().default(9),
  abgeschlossen: boolean("abgeschlossen").notNull().default(false),
  syncedAt: timestamp("synced_at", { withTimezone: true }),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
});

export const insertSpielSchema = createInsertSchema(spieleTable).omit({ id: true, createdAt: true });
export type InsertSpiel = z.infer<typeof insertSpielSchema>;
export type Spiel = typeof spieleTable.$inferSelect;
