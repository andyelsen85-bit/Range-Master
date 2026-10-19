import { pgTable,serial,text,timestamp,integer,jsonb,bigint,primaryKey,uniqueIndex,check } from "drizzle-orm/pg-core";
import { sql } from "drizzle-orm";
import { spieleTable,modusEnum } from "./spiele";
import { spielerTable } from "./spieler";
const at=(name:string)=>timestamp(name,{withTimezone:true});
export const kioskTokensTable=pgTable("kiosk_tokens",{
 id:serial("id").primaryKey(),name:text("name").notNull(),tokenHash:text("token_hash").notNull().unique(),
 createdAt:at("created_at").notNull().defaultNow(),createdBy:integer("created_by").notNull(),
 lastUsedAt:at("last_used_at"),lastUsedIpHash:text("last_used_ip_hash"),revokedAt:at("revoked_at"),
});
// Historical table above is retained for audit only; it has no runtime reader.
export const kioskDevicesTable=pgTable("kiosk_devices",{
 id:serial("id").primaryKey(),tokenHash:text("token_hash").notNull().unique(),name:text("name"),
 pinEpoch:integer("pin_epoch").notNull(),createdAt:at("created_at").notNull().defaultNow(),
 lastUsedAt:at("last_used_at").notNull().defaultNow(),lastRenewedAt:at("last_renewed_at").notNull().defaultNow(),
 lastUsedIpHash:text("last_used_ip_hash"),revokedAt:at("revoked_at"),
});
export const kioskPinLimitsTable=pgTable("kiosk_pin_limits",{
 key:text("key").primaryKey(),failures:integer("failures").notNull().default(0),
 strikes:integer("strikes").notNull().default(0),windowStartedAt:at("window_started_at").notNull().defaultNow(),
 lockedUntil:at("locked_until"),updatedAt:at("updated_at").notNull().defaultNow(),
});
export const spielerGruppenTable=pgTable("spieler_gruppen",{
 id:serial("id").primaryKey(),name:text("name").notNull(),createdAt:at("created_at").notNull().defaultNow(),
 updatedAt:at("updated_at").notNull().defaultNow(),deletedAt:at("deleted_at"),
});
export const spielerGruppenMitgliederTable=pgTable("spieler_gruppen_mitglieder",{
 gruppeId:integer("gruppe_id").notNull().references(()=>spielerGruppenTable.id,{onDelete:"cascade"}),
 spielerId:integer("spieler_id").notNull().references(()=>spielerTable.id),
},t=>[primaryKey({columns:[t.gruppeId,t.spielerId]})]);
export const queueGamesTable=pgTable("queue_games",{
 id:serial("id").primaryKey(),modus:modusEnum("modus").notNull().default("NORMAL"),position:integer("position").notNull(),
 status:text("status").notNull().default("OPEN"),createdAt:at("created_at").notNull().defaultNow(),
 lockedAt:at("locked_at"),startedAt:at("started_at"),terminalId:text("terminal_id"),startedExternalId:text("started_external_id"),
 startedSpielId:integer("started_spiel_id").references(()=>spieleTable.id,{onDelete:"set null"}),source:text("source").notNull().default("WEB"),
},t=>[check("queue_games_status_check",sql`${t.status} IN('OPEN','LOCKED','STARTED','DONE','CANCELLED')`),
 check("queue_games_source_check",sql`${t.source} IN('WEB','TERMINAL','ADMIN')`),
 uniqueIndex("queue_started_external").on(t.startedExternalId).where(sql`${t.startedExternalId} IS NOT NULL`)]);
export const queueEntriesTable=pgTable("queue_entries",{
 id:serial("id").primaryKey(),queueGameId:integer("queue_game_id").notNull().references(()=>queueGamesTable.id,{onDelete:"cascade"}),
 spielerId:integer("spieler_id").notNull().references(()=>spielerTable.id),
 groupId:integer("group_id").references(()=>spielerGruppenTable.id,{onDelete:"set null"}),modus:text("modus").notNull(),
 signupAt:at("signup_at").notNull().defaultNow(),signedByIpHash:text("signed_by_ip_hash"),status:text("status").notNull().default("OPEN"),
 cancelReason:text("cancel_reason"),spielId:integer("spiel_id").references(()=>spieleTable.id,{onDelete:"set null"}),
},t=>[check("queue_entries_status_check",sql`${t.status} IN('OPEN','TAKEN','DONE','CANCELLED')`),
 uniqueIndex("queue_one_active_player").on(t.spielerId).where(sql`${t.status} IN('OPEN','TAKEN')`)]);
export const queueAuditTable=pgTable("queue_audit",{
 id:serial("id").primaryKey(),at:at("at").notNull().defaultNow(),action:text("action").notNull(),spielerId:integer("spieler_id"),
 entryId:integer("entry_id"),groupId:integer("group_id"),ipHash:text("ip_hash"),tokenId:integer("token_id"),
 deviceId:integer("device_id").references(()=>kioskDevicesTable.id),
 details:jsonb("details").notNull().default(sql`'{}'::jsonb`),
});
export const spielSpielerHiddenTable=pgTable("spiel_spieler_hidden",{
 spielId:integer("spiel_id").notNull().references(()=>spieleTable.id,{onDelete:"cascade"}),
 spielerId:integer("spieler_id").notNull().references(()=>spielerTable.id),hiddenAt:at("hidden_at").notNull().defaultNow(),hiddenBy:integer("hidden_by").notNull(),
},t=>[primaryKey({columns:[t.spielId,t.spielerId]})]);
export const adminGameEditsTable=pgTable("admin_game_edits",{
 id:serial("id").primaryKey(),at:at("at").notNull().defaultNow(),adminUserId:integer("admin_user_id").notNull(),action:text("action").notNull(),
 spielId:integer("spiel_id").references(()=>spieleTable.id,{onDelete:"set null"}),spielerId:integer("spieler_id"),before:jsonb("before"),after:jsonb("after"),
});
export const liveGameStateTable=pgTable("live_game_state",{
 terminalId:text("terminal_id").primaryKey(),seq:bigint("seq",{mode:"number"}).notNull(),state:text("state").notNull(),
 payload:jsonb("payload").notNull(),updatedAt:at("updated_at").notNull().defaultNow(),
},t=>[check("live_game_state_seq_check",sql`${t.seq}>=0`),check("live_game_state_state_check",sql`${t.state} IN('RUNNING','FINISHED','ABORTED')`)]);
