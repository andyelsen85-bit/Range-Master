import { pgTable, serial, text, boolean, timestamp } from "drizzle-orm/pg-core";

export const apiKeysTable = pgTable("api_keys", {
  id: serial("id").primaryKey(),
  name: text("name").notNull(),
  key: text("key").notNull().unique(),
  type: text("type").notNull().$type<"EMULATOR" | "TERMINAL">(),
  active: boolean("active").notNull().default(true),
  createdAt: timestamp("created_at", { withTimezone: true }).notNull().defaultNow(),
});

export type ApiKey = typeof apiKeysTable.$inferSelect;
