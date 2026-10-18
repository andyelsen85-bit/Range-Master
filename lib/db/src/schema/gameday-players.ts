import {pgTable,text,date,integer,timestamp,jsonb,primaryKey} from "drizzle-orm/pg-core";
import {spielerTable} from "./spieler";
const at=(name:string)=>timestamp(name,{withTimezone:true});
export const gamedayPlayersTable=pgTable("gameday_players",{
 datum:date("datum",{mode:"string"}).notNull(),spielerId:integer("spieler_id").notNull().references(()=>spielerTable.id),
 addedAt:at("added_at").notNull(),removedAt:at("removed_at"),terminalId:text("terminal_id").notNull(),
 occurredAt:at("occurred_at").notNull(),externalId:text("external_id").notNull(),
},t=>[primaryKey({columns:[t.datum,t.spielerId]})]);
export const gamedayPlayerEventsTable=pgTable("gameday_player_events",{
 externalId:text("external_id").primaryKey(),payloadHash:text("payload_hash").notNull(),
 payload:jsonb("payload").notNull(),receivedAt:at("received_at").notNull().defaultNow(),
});
