import { pgTable, serial, text, integer, boolean, timestamp, pgEnum } from "drizzle-orm/pg-core";

export const smtpVerschluesselungEnum = pgEnum("smtp_verschluesselung", ["NONE", "STARTTLS", "SSL"]);

/**
 * SMTP mail server configuration (single row, id=1).
 * Password is write-only in the UI; API never returns it.
 */
export const smtpSettingsTable = pgTable("smtp_settings", {
  id: serial("id").primaryKey(),
  host: text("host").notNull().default(""),
  port: integer("port").notNull().default(587),
  username: text("username").notNull().default(""),
  passwort: text("passwort").notNull().default(""),
  fromAddress: text("from_address").notNull().default(""),
  verschluesselung: smtpVerschluesselungEnum("verschluesselung").notNull().default("STARTTLS"),
  ignoreTlsErrors: boolean("ignore_tls_errors").notNull().default(false),
  portalUrl: text("portal_url").notNull().default(""),
  updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow().$onUpdate(() => new Date()),
});

export type SmtpSettings = typeof smtpSettingsTable.$inferSelect;
