import { pgTable, serial, text, boolean, timestamp, uniqueIndex } from "drizzle-orm/pg-core";
import { sql } from "drizzle-orm";
import { createInsertSchema } from "drizzle-zod";
import { z } from "zod/v4";

export const spielerTable = pgTable("spieler", {
  id: serial("id").primaryKey(),
  name: text("name").notNull(),
  terminalExternalId: text("terminal_external_id"),
  email: text("email").unique(),
  mitgliedNr: text("mitglied_nr").unique(),
  aktiv: boolean("aktiv").notNull().default(true),
  deactivatedAt: timestamp("deactivated_at", { withTimezone: true }),
  portalAktiv: boolean("portal_aktiv").notNull().default(false),
  isAdmin: boolean("is_admin").notNull().default(false),
  isMaitreDeTir: boolean("is_maitre_de_tir").notNull().default(false),
  passwortHash: text("passwort_hash"),
  eingeladenAt: timestamp("eingeladen_at", { withTimezone: true }),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow().$onUpdate(() => new Date()),
}, (table) => [
  uniqueIndex("spieler_terminal_external_id_unique").on(table.terminalExternalId)
    .where(sql`${table.terminalExternalId} IS NOT NULL`),
]);

export const insertSpielerSchema = createInsertSchema(spielerTable).omit({ id: true, createdAt: true, updatedAt: true });
export type InsertSpieler = z.infer<typeof insertSpielerSchema>;
export type Spieler = typeof spielerTable.$inferSelect;
