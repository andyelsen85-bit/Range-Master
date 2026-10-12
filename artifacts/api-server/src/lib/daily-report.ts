import { createHash } from "node:crypto";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { dayBillSummary } from "./bills";
import { daySchema } from "./local-day";
import { canonicalJson } from "./sync-manifest";

export async function buildDailyReport(datum: string) {
  daySchema.parse(datum);
  const summary = await dayBillSummary(datum);
  const inventory = (await db.execute(sql`SELECT c.caliber,i.opening_count,i.closing_count
    FROM (VALUES(12),(20)) c(caliber) LEFT JOIN ammo_inventory_day i ON i.caliber=c.caliber AND i.datum=${datum}
    ORDER BY c.caliber`)).rows as any[];
  const sold = (await db.execute(sql`SELECT product_category,COALESCE(sum(quantity),0)::int quantity FROM sale_events
    WHERE datum=${datum} AND product_category IN('AMMO_CAL12','AMMO_CAL20') GROUP BY product_category`)).rows as any[];
  const ammunition = [12,20].map((caliber, index) => {
    const row = inventory[index], units = Number(sold.find(r => r.product_category === `AMMO_CAL${caliber}`)?.quantity ?? 0);
    const opening = row?.opening_count == null ? null : Number(row.opening_count);
    const closing = row?.closing_count == null ? null : Number(row.closing_count);
    return { caliber, unit: "Verkaufseinheiten", sold: units, opening, closing,
      difference: opening === null || closing === null ? null : opening - units - closing };
  });
  const maitres = (await db.execute(sql`SELECT s.id,s.name FROM gameday_maitre m JOIN spieler s ON s.id=m.spieler_id WHERE m.datum=${datum} ORDER BY s.name,s.id`)).rows as { id: number; name: string }[];
  const [{ exists }] = (await db.execute(sql`SELECT (EXISTS(SELECT 1 FROM sale_events WHERE datum=${datum}) OR
    EXISTS(SELECT 1 FROM bill_payments WHERE datum=${datum}) OR EXISTS(SELECT 1 FROM spiele WHERE abgeschlossen
      AND (datum AT TIME ZONE 'Europe/Luxembourg')::date=${datum}::date)) AS exists`)).rows as { exists: boolean }[];
  const report = { ...summary, ammunition, maitres, exists: Boolean(exists),
    openTotalCents: summary.players.reduce((n, p) => n + (p.state === "OPEN" ? p.totalCents : 0), 0) };
  const dataHash = createHash("sha256").update(canonicalJson(report)).digest("hex");
  return { ...report, dataHash };
}
const escape = (v: unknown) => String(v ?? "").replace(/[&<>"']/g, c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;" }[c]!));
export function dailyReportMessage(report: Awaited<ReturnType<typeof buildDailyReport>>, portalUrl: string, sentAt = new Date()) {
  const datum = report.datum.split("-").reverse().join(".");
  const money = (n: number) => (n / 100).toLocaleString("de-LU", { style: "currency", currency: "EUR" });
  const lines = [`Range-Master Tagesabrechnung ${datum}`, `Maître de Tir: ${report.maitres.map(m => m.name).join(", ") || "nicht erfasst"}`,
    `Gesamtumsatz: ${money(report.generalTotalCents)}`, `Spieler: ${report.uniquePlayers}`, `Spiele: ${report.games}`,
    `abgeschlossen: ${report.completedGames}`, `Tauben bestätigt: ${report.confirmedClays}`, `Rechnungen bezahlt: ${report.paidPlayers}`,
    `offene Beträge: ${money(report.openTotalCents)}`, "Umsatz nach Kategorie"];
  for (const [category, cents] of Object.entries(report.categorySubtotals)) lines.push(`${category}: ${money(cents)}`);
  lines.push("Verkaufte Produkte (Menge / Preis / Betrag)");
  for (const p of Object.values(report.productTotals)) lines.push(`${p.productName}: ${p.quantity} / ${money(p.unitPriceCents)} / ${money(p.totalCents)}`);
  lines.push("Munition (Verkaufseinheiten)");
  for (const a of report.ammunition) lines.push(`Kaliber ${a.caliber}: verkauft ${a.sold}; Tagesbeginn ${a.opening ?? "nicht erfasst"}; Tagesschluss ${a.closing ?? "nicht erfasst"}; Differenz ${a.difference ?? "nicht erfasst"}`);
  lines.push("Differenz ist informativ: Abgang ohne Verkauf bzw. unregistrierter Zugang.", `Portal: ${portalUrl}`, `Versand: ${sentAt.toLocaleString("de-LU",{ timeZone:"Europe/Luxembourg" })}`);
  let link="";
  try { const url=new URL(portalUrl);if(["https:","http:"].includes(url.protocol))link=`<a href="${escape(url.href)}">Portal öffnen</a>`; } catch { /* An unconfigured URL is not a link. */ }
  return { subject: `Range-Master Tagesabrechnung ${datum}`, text: lines.join("\n"),
    html: `<html lang="de"><body>${lines.map((line, i) => i === 0 ? `<h1>${escape(line)}</h1>` : line.startsWith("Portal: ")&&link?`<p>${link}</p>`:`<p>${escape(line)}</p>`).join("")}</body></html>` };
}
