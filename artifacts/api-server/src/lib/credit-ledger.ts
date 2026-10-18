import { sql } from "drizzle-orm";
import { kreditEventsTable } from "@workspace/db";

/** The sole monetary visibility rule. Raw events remain immutable for audit. */
export const effectiveCreditEvent=sql<boolean>`${kreditEventsTable.id} IN (SELECT id FROM v_effective_kredit_events)`;

export function creditFingerprint(event: {
  spielerId:number;datum:string;typ:string;anzahl:number;voidsExternalId?:string|null;
  occurredAt?:Date|string|null;priceRevisionId?:number|null;unitPriceCents?:number|null;
}) {
 return JSON.stringify([event.spielerId,event.datum,event.typ,event.anzahl,event.voidsExternalId??null,
  event.occurredAt?new Date(event.occurredAt).toISOString():null,event.priceRevisionId??null,event.unitPriceCents??null]);
}
