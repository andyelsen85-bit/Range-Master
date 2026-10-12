import { createHash } from "node:crypto";
import { db, ammoInventoryDayTable, gamedayMaitreTable, gamedayEventTable, spielerTable } from "@workspace/db";
import { and, asc, desc, eq, inArray, lt, sql } from "drizzle-orm";
import { daySchema } from "./local-day";
import { z } from "zod";

export const inventoryInput = z.object({
  externalId: z.string().min(8).max(100), terminalId: z.string().regex(/^[A-Za-z0-9_-]{1,64}$/),
  datum: daySchema, c12: z.number().int().min(0).max(1_000_000), c20: z.number().int().min(0).max(1_000_000),
  takenOver: z.boolean().default(false), note: z.string().max(240).default(""),
});
export const gamedayInput = inventoryInput.pick({ externalId: true, terminalId: true, datum: true }).extend({
  maitreIds: z.array(z.number().int().positive()).min(1).max(4).refine(a => new Set(a).size === a.length),
});
export class DayConflict extends Error {}
export async function inventoryState(datum: string) {
  const today = await db.select().from(ammoInventoryDayTable).where(eq(ammoInventoryDayTable.datum, datum));
  const previous = await db.select().from(ammoInventoryDayTable).where(lt(ammoInventoryDayTable.datum, datum)).orderBy(desc(ammoInventoryDayTable.datum), asc(ammoInventoryDayTable.caliber)).limit(2);
  const maitres = await db.select({ id: spielerTable.id, name: spielerTable.name }).from(spielerTable).where(and(eq(spielerTable.aktiv, true), eq(spielerTable.isMaitreDeTir, true))).orderBy(asc(spielerTable.name));
  const selected = await db.select({ id: gamedayMaitreTable.spielerId }).from(gamedayMaitreTable).where(eq(gamedayMaitreTable.datum, datum));
  const phase = (which: "opening" | "closing") => {
    const a = today.find(r => r.caliber === 12), b = today.find(r => r.caliber === 20);
    if (a?.[`${which}Count`] == null || b?.[`${which}Count`] == null) return null;
    return { c12: a[`${which}Count`], c20: b[`${which}Count`], at: a[`${which}At`],
      terminal: a[`${which}SourceTerminal`], takenOver: a.openingTakenOver, note: a.closingNote };
  };
  const last12 = previous.find(r => r.caliber === 12), last20 = previous.find(r => r.caliber === 20);
  const confirmed = last12?.closingCount != null && last20?.closingCount != null && last12.datum === last20.datum;
  return { maitres, lastClosing: last12 && last20 && last12.datum === last20.datum ? {
    datum: last12.datum, c12: confirmed ? last12.closingCount : last12.openingCount,
    c20: confirmed ? last20.closingCount : last20.openingCount, fromOpening: !confirmed,
  } : null, today: { opening: phase("opening"), closing: phase("closing"), maitres: selected.map(r => r.id) } };
}
export async function applyDayEvent(kind: "opening" | "closing" | "gameday", input: any) {
  const body = (kind === "gameday" ? gamedayInput : inventoryInput).parse(input);
  const hash = createHash("sha256").update(JSON.stringify({ kind, ...body })).digest("hex");
  return db.transaction(async tx => {
    await tx.execute(sql`SELECT pg_advisory_xact_lock(hashtextextended(${`inventory-event:${body.externalId}`},0))`);
    await tx.execute(sql`SELECT pg_advisory_xact_lock(hashtextextended(${`inventory:${body.datum}`},0))`);
    const [previous] = await tx.select().from(gamedayEventTable).where(eq(gamedayEventTable.externalId, body.externalId));
    if (previous) {
      if (previous.payloadHash !== hash || previous.terminalId !== body.terminalId) throw new DayConflict("Event-ID mit verändertem Inhalt");
      return { success: true, duplicate: true };
    }
    if (kind === "gameday") {
      const ids = (body as z.infer<typeof gamedayInput>).maitreIds;
      const flagged = await tx.select().from(spielerTable).where(and(inArray(spielerTable.id, ids), eq(spielerTable.aktiv, true), eq(spielerTable.isMaitreDeTir, true)));
      if (flagged.length !== ids.length) throw new DayConflict("Maître de Tir nicht mehr aktiv oder berechtigt");
      await tx.delete(gamedayMaitreTable).where(eq(gamedayMaitreTable.datum, body.datum));
      await tx.insert(gamedayMaitreTable).values(ids.map(spielerId => ({ datum: body.datum, spielerId })));
    } else {
      const data = body as z.infer<typeof inventoryInput>;
      if (kind === "opening") {
        const selected = await tx.select().from(gamedayMaitreTable).where(eq(gamedayMaitreTable.datum, data.datum));
        if (!selected.length) throw new DayConflict("Maître de Tir für Tagesbeginn fehlt");
      }
      for (const caliber of [12, 20]) {
        const values = kind === "opening" ? { openingCount: caliber === 12 ? data.c12 : data.c20,
          openingAt: new Date(), openingSourceTerminal: data.terminalId, openingTakenOver: data.takenOver }
          : { closingCount: caliber === 12 ? data.c12 : data.c20, closingAt: new Date(), closingSourceTerminal: data.terminalId, closingNote: data.note };
        await tx.insert(ammoInventoryDayTable).values({ datum: data.datum, caliber, ...values })
          .onConflictDoUpdate({ target: [ammoInventoryDayTable.datum, ammoInventoryDayTable.caliber], set: values });
      }
    }
    await tx.insert(gamedayEventTable).values({ externalId: body.externalId, terminalId: body.terminalId, payloadHash: hash });
    return { success: true, duplicate: false };
  });
}
