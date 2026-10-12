import { boolean, check, date, integer, jsonb, pgTable, serial, text, timestamp, uniqueIndex } from "drizzle-orm/pg-core";
import { sql } from "drizzle-orm";
import { spielerTable } from "./spieler";

export const ammoInventoryDayTable = pgTable("ammo_inventory_day", {
  id: serial("id").primaryKey(), datum: date("datum", { mode: "string" }).notNull(),
  caliber: integer("caliber").notNull(),
  openingCount: integer("opening_count"), openingAt: timestamp("opening_at", { withTimezone: true }),
  openingSourceTerminal: text("opening_source_terminal"), openingTakenOver: boolean("opening_taken_over").default(false),
  closingCount: integer("closing_count"), closingAt: timestamp("closing_at", { withTimezone: true }),
  closingSourceTerminal: text("closing_source_terminal"), closingNote: text("closing_note"),
}, t => [uniqueIndex("ammo_inventory_day_unique").on(t.datum, t.caliber),
  check("ammo_caliber_valid", sql`${t.caliber} IN (12,20)`),
  check("ammo_counts_valid", sql`(${t.openingCount} IS NULL OR ${t.openingCount} BETWEEN 0 AND 1000000) AND (${t.closingCount} IS NULL OR ${t.closingCount} BETWEEN 0 AND 1000000)`)]);
export const gamedayMaitreTable = pgTable("gameday_maitre", {
  id: serial("id").primaryKey(), datum: date("datum", { mode: "string" }).notNull(),
  spielerId: integer("spieler_id").notNull().references(() => spielerTable.id),
}, t => [uniqueIndex("gameday_maitre_unique").on(t.datum, t.spielerId)]);
// Device-event receipts also bind the immutable payload, preventing changed retries.
export const gamedayEventTable = pgTable("gameday_event", {
  externalId: text("external_id").primaryKey(), terminalId: text("terminal_id").notNull(),
  payloadHash: text("payload_hash").notNull(), createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
});
export const dailyReportSettingsTable = pgTable("daily_report_settings", {
  id: integer("id").primaryKey().default(1), enabled: boolean("enabled").notNull().default(false),
}, t => [check("daily_report_singleton", sql`${t.id}=1`)]);
export const dailyReportRecipientTable = pgTable("daily_report_recipient", {
  id: serial("id").primaryKey(), email: text("email").notNull().unique(),
  active: boolean("active").notNull().default(true), createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
});
export const dailyReportLogTable = pgTable("daily_report_log", {
  datum: date("datum", { mode: "string" }).primaryKey(), status: text("status").notNull(),
  sentAt: timestamp("sent_at", { withTimezone: true }), attempts: integer("attempts").notNull().default(0),
  error: text("error"), recipientsSnapshot: jsonb("recipients_snapshot"), dataHash: text("data_hash"),
  firstAttemptAt: timestamp("first_attempt_at", { withTimezone: true }).notNull().defaultNow(),
  lastAttemptAt: timestamp("last_attempt_at", { withTimezone: true }).notNull().defaultNow(),
});
export const publicPageSettingsTable = pgTable("public_page_settings", {
  id: integer("id").primaryKey().default(1), enabled: boolean("enabled").notNull().default(false),
  nameMode: text("name_mode").notNull().default("INITIAL"),
}, t => [check("public_page_singleton", sql`${t.id}=1`), check("public_name_mode_valid", sql`${t.nameMode} IN ('FULL','INITIAL')`)]);
