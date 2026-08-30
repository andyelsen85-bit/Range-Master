import { date, index, integer, pgEnum, pgTable, serial, text, timestamp } from "drizzle-orm/pg-core";
import { spielerTable } from "./spieler";
import { apiKeysTable } from "./api-keys";

export const billPaymentStatusEnum = pgEnum("bill_payment_status", ["PAID"]);

/** Immutable audit trail for bill settlements. A player may settle again after new activity. */
export const billPaymentsTable = pgTable("bill_payments", {
  id: serial("id").primaryKey(),
  externalId: text("external_id").notNull().unique(),
  spielerId: integer("spieler_id").notNull().references(() => spielerTable.id, { onDelete: "restrict" }),
  datum: date("datum", { mode: "string" }).notNull(),
  status: billPaymentStatusEnum("status").notNull().default("PAID"),
  terminalId: text("terminal_id"),
  source: text("source").notNull().default("TERMINAL"),
  paidAt: timestamp("paid_at", { withTimezone: true }).notNull().defaultNow(),
  markedByAdminId: integer("marked_by_admin_id").references(() => spielerTable.id, { onDelete: "restrict" }),
  markedByApiKeyId: integer("marked_by_api_key_id").references(() => apiKeysTable.id, { onDelete: "restrict" }),
}, (t) => [
  index("bill_payments_datum_idx").on(t.datum),
]);