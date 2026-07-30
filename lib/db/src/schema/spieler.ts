import { pgTable, serial, text, boolean, timestamp } from "drizzle-orm/pg-core";
import { createInsertSchema } from "drizzle-zod";
import { z } from "zod/v4";

export const spielerTable = pgTable("spieler", {
  id: serial("id").primaryKey(),
  name: text("name").notNull(),
  email: text("email").unique(),
  mitgliedNr: text("mitglied_nr").unique(),
  portalAktiv: boolean("portal_aktiv").notNull().default(false),
  isAdmin: boolean("is_admin").notNull().default(false),
  passwortHash: text("passwort_hash"),
  eingeladenAt: timestamp("eingeladen_at", { withTimezone: true }),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow().$onUpdate(() => new Date()),
});

export const insertSpielerSchema = createInsertSchema(spielerTable).omit({ id: true, createdAt: true, updatedAt: true });
export type InsertSpieler = z.infer<typeof insertSpielerSchema>;
export type Spieler = typeof spielerTable.$inferSelect;
