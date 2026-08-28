import { index, integer, pgTable, serial, text, timestamp } from "drizzle-orm/pg-core";
import { apiKeysTable } from "./api-keys";
import { spielerTable } from "./spieler";

/**
 * Encrypted terminal configuration snapshots. The application stores no
 * plaintext configuration payload here; encryption is performed by the API.
 */
export const terminalConfigBackupsTable = pgTable("terminal_config_backups", {
  id: serial("id").primaryKey(),
  terminalId: text("terminal_id").notNull(),
  apiKeyId: integer("api_key_id").references(() => apiKeysTable.id, { onDelete: "set null" }),
  schemaVersion: integer("schema_version").notNull(),
  firmwareVersion: text("firmware_version"),
  ciphertext: text("ciphertext").notNull(),
  iv: text("iv").notNull(),
  authTag: text("auth_tag").notNull(),
  checksum: text("checksum").notNull(),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow(),
  lastRestoredAt: timestamp("last_restored_at", { withTimezone: true }),
  revokedAt: timestamp("revoked_at", { withTimezone: true }),
}, (t) => [
  index("terminal_config_backups_terminal_idx").on(t.terminalId, t.updatedAt),
]);

/** A short-lived, one-time approval created by an authenticated administrator. */
export const terminalRestoreAuthorizationsTable = pgTable("terminal_restore_authorizations", {
  id: serial("id").primaryKey(),
  backupId: integer("backup_id").notNull().references(() => terminalConfigBackupsTable.id, { onDelete: "cascade" }),
  targetTerminalId: text("target_terminal_id").notNull(),
  targetApiKeyId: integer("target_api_key_id").notNull().references(() => apiKeysTable.id, { onDelete: "cascade" }),
  codeHash: text("code_hash").notNull().unique(),
  expiresAt: timestamp("expires_at", { withTimezone: true }).notNull(),
  createdBy: integer("created_by").references(() => spielerTable.id, { onDelete: "set null" }),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
  usedAt: timestamp("used_at", { withTimezone: true }),
  revokedAt: timestamp("revoked_at", { withTimezone: true }),
}, (t) => [
  index("terminal_restore_auth_code_idx").on(t.codeHash),
  index("terminal_restore_auth_expiry_idx").on(t.expiresAt),
]);

export type TerminalConfigBackup = typeof terminalConfigBackupsTable.$inferSelect;
export type TerminalRestoreAuthorization = typeof terminalRestoreAuthorizationsTable.$inferSelect;