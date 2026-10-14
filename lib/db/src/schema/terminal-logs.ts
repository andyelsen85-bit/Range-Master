import { sql } from "drizzle-orm";
import { bigint, bigserial, date, index, jsonb, pgTable, primaryKey, text, timestamp, unique, uniqueIndex } from "drizzle-orm/pg-core";

export const terminalLogTable = pgTable("terminal_log", {
  id: bigserial("id", { mode: "number" }).primaryKey(),
  terminalId: text("terminal_id").notNull(), entryId: text("entry_id").notNull(),
  ts: timestamp("ts", { withTimezone: true }).notNull(),
  type: text("type").notNull(), level: text("level").notNull(), msg: text("msg").notNull(),
  data: jsonb("data").notNull().default(sql`'{}'::jsonb`),
  receivedAt: timestamp("received_at", { withTimezone: true }).notNull().defaultNow(),
  gatewayId: text("gateway_id"), gatewaySeq: bigint("gateway_seq", { mode: "number" }),
}, t => [unique("terminal_log_terminal_entry_unique").on(t.terminalId,t.entryId),
  uniqueIndex("terminal_log_gateway_seq_unique").on(t.gatewayId,t.gatewaySeq).where(sql`${t.gatewayId} IS NOT NULL`),
  index("terminal_log_ts_idx").on(t.ts), index("terminal_log_terminal_ts_idx").on(t.terminalId,t.ts)]);

export const terminalGameManifestTable = pgTable("terminal_game_manifest", {
  terminalId: text("terminal_id").notNull(), datum: date("datum", { mode: "string" }).notNull(),
  externalIds: jsonb("external_ids").notNull().default(sql`'[]'::jsonb`),
  updatedAt: timestamp("updated_at", { withTimezone: true }).notNull().defaultNow(),
}, t => [primaryKey({ columns: [t.terminalId,t.datum] })]);
