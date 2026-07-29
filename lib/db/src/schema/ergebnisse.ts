import { pgTable, serial, integer, boolean } from "drizzle-orm/pg-core";
import { createInsertSchema } from "drizzle-zod";
import { z } from "zod/v4";
import { spielerTable } from "./spieler";
import { spieleTable } from "./spiele";
import { maschineEnum } from "./spiele";

export const ergebnisseTable = pgTable("ergebnisse", {
  id: serial("id").primaryKey(),
  spielId: integer("spiel_id").notNull().references(() => spieleTable.id, { onDelete: "cascade" }),
  spielerId: integer("spieler_id").notNull().references(() => spielerTable.id),
  lauf: integer("lauf").notNull(),
  taube: integer("taube").notNull(),
  maschine: maschineEnum("maschine").notNull(),
  posten: integer("posten").notNull(),
  schuss1: boolean("schuss1").notNull().default(false),
  schuss2: boolean("schuss2").notNull().default(false),
  punkte: integer("punkte").notNull().default(0),
  wiederholt: boolean("wiederholt").notNull().default(false),
});

export const insertErgebnisSchema = createInsertSchema(ergebnisseTable).omit({ id: true });
export type InsertErgebnis = z.infer<typeof insertErgebnisSchema>;
export type Ergebnis = typeof ergebnisseTable.$inferSelect;
