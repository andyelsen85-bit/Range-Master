import { pgTable, serial, text, integer, timestamp, pgEnum, index } from "drizzle-orm/pg-core";
import { spielerTable } from "./spieler";

export const spielerUpdateTypEnum = pgEnum("spieler_update_typ", ["UPDATE", "PASSWORT_RESET"]);
export const emailStatusEnum = pgEnum("email_status", ["NONE", "PENDING", "SENT", "FAILED"]);

/**
 * Player changes pushed from the terminal via sync.
 * externalId makes offline-queue pushes idempotent; emailStatus tracks
 * outbound invitation / password-reset emails triggered by the change.
 */
export const spielerUpdatesTable = pgTable("spieler_updates", {
  id: serial("id").primaryKey(),
  externalId: text("external_id").notNull().unique(),
  spielerId: integer("spieler_id").notNull().references(() => spielerTable.id),
  typ: spielerUpdateTypEnum("typ").notNull(),
  emailStatus: emailStatusEnum("email_status").notNull().default("NONE"),
  emailError: text("email_error"),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
}, (t) => [
  index("spieler_updates_spieler_idx").on(t.spielerId),
]);

export type SpielerUpdate = typeof spielerUpdatesTable.$inferSelect;
