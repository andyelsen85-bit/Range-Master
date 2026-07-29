import { Router } from "express";
import { db, spielerTable, spielTeilnahmenTable, spieleTable } from "@workspace/db";
import { eq, gte, lt, and, inArray } from "drizzle-orm";
import { sql } from "drizzle-orm";

const router = Router();

// GET /api/rangliste
router.get("/", async (req, res) => {
  const { modus, jahr } = req.query as { modus?: string; jahr?: string };
  const y = jahr ? parseInt(jahr) : new Date().getFullYear();
  const yearStart = new Date(`${y}-01-01`);
  const yearEnd = new Date(`${y + 1}-01-01`);

  // Get all spiele in year (optionally by modus)
  let spieleQuery = db
    .select({ id: spieleTable.id, modus: spieleTable.modus })
    .from(spieleTable)
    .where(and(gte(spieleTable.datum, yearStart), lt(spieleTable.datum, yearEnd)));

  const spieleRows = await spieleQuery;

  let filteredSpielIds = spieleRows.map((s) => s.id);
  if (modus && modus !== "ALL") {
    filteredSpielIds = spieleRows.filter((s) => s.modus === modus).map((s) => s.id);
  }

  if (!filteredSpielIds.length) {
    return res.json({ rangliste: [] });
  }

  // Aggregate by spieler
  const teilnahmen = await db
    .select({
      spielerId: spielTeilnahmenTable.spielerId,
      punkte: spielTeilnahmenTable.punkte,
    })
    .from(spielTeilnahmenTable)
    .where(inArray(spielTeilnahmenTable.spielId, filteredSpielIds));

  const aggregated = new Map<number, { gesamtPunkte: number; anzahlLauefe: number; punkteListe: number[] }>();
  for (const t of teilnahmen) {
    if (!aggregated.has(t.spielerId)) {
      aggregated.set(t.spielerId, { gesamtPunkte: 0, anzahlLauefe: 0, punkteListe: [] });
    }
    const entry = aggregated.get(t.spielerId)!;
    entry.gesamtPunkte += t.punkte;
    entry.anzahlLauefe += 1;
    entry.punkteListe.push(t.punkte);
  }

  if (!aggregated.size) return res.json({ rangliste: [] });

  // Fetch player names
  const spielerIds = Array.from(aggregated.keys());
  const spielerRows = await db
    .select({ id: spielerTable.id, name: spielerTable.name })
    .from(spielerTable)
    .where(inArray(spielerTable.id, spielerIds));

  const nameMap = new Map(spielerRows.map((s) => [s.id, s.name]));

  const rangliste = Array.from(aggregated.entries())
    .map(([spielerId, data]) => ({
      spielerId,
      name: nameMap.get(spielerId) ?? "Unbekannt",
      gesamtPunkte: data.gesamtPunkte,
      anzahlLauefe: data.anzahlLauefe,
      durchschnitt: Math.round((data.gesamtPunkte / data.anzahlLauefe) * 10) / 10,
      bestPunkte: Math.max(...data.punkteListe),
    }))
    .sort((a, b) => b.durchschnitt - a.durchschnitt || b.gesamtPunkte - a.gesamtPunkte)
    .map((e, i) => ({ rang: i + 1, ...e }));

  return res.json({ rangliste });
});

export default router;
