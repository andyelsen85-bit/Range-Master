import { Router } from "express";
import { db, spielerTable, spielTeilnahmenTable, spieleTable } from "@workspace/db";
import { eq, gte, lt, and, inArray } from "drizzle-orm";

const router = Router();

// GET /api/rangliste
router.get("/", async (req, res) => {
  const { modus, jahr } = req.query as { modus?: string; jahr?: string };
  const y = jahr ? parseInt(jahr) : new Date().getFullYear();
  const yearStart = new Date(`${y}-01-01`);
  const yearEnd = new Date(`${y + 1}-01-01`);

  const spieleRows = await db
    .select({
      id: spieleTable.id,
      modus: spieleTable.modus,
      taubenProLauf: spieleTable.taubenProLauf,
      lauf: spieleTable.lauf,
    })
    .from(spieleTable)
    .where(and(gte(spieleTable.datum, yearStart), lt(spieleTable.datum, yearEnd)));

  let filteredSpiele = spieleRows;
  if (modus && modus !== "ALL") {
    filteredSpiele = spieleRows.filter((s) => s.modus === modus);
  }

  const filteredSpielIds = filteredSpiele.map((s) => s.id);

  if (!filteredSpielIds.length) {
    return res.json({ rangliste: [] });
  }

  // Build a map of spielId → maxPunkte for normalization.
  // maxPunkte = taubenProLauf (targets per lauf) × 2 (max points per target) × lauf (number of läufe)
  const maxPunkteMap = new Map<number, number>(
    filteredSpiele.map((s) => [s.id, s.taubenProLauf * 2 * s.lauf])
  );

  // Fetch raw per-lauf data including spielId so we can sum both Läufe per game
  const teilnahmen = await db
    .select({
      spielerId: spielTeilnahmenTable.spielerId,
      spielId: spielTeilnahmenTable.spielId,
      punkte: spielTeilnahmenTable.punkte,
    })
    .from(spielTeilnahmenTable)
    .where(inArray(spielTeilnahmenTable.spielId, filteredSpielIds));

  // Step 1: Sum both Läufe → per-game totals, including the max possible score
  const perGame = new Map<string, { spielerId: number; total: number; maxPunkte: number }>();
  for (const t of teilnahmen) {
    const k = `${t.spielerId}:${t.spielId}`;
    if (!perGame.has(k)) {
      perGame.set(k, {
        spielerId: t.spielerId,
        total: 0,
        maxPunkte: maxPunkteMap.get(t.spielId) ?? 36,
      });
    }
    perGame.get(k)!.total += t.punkte;
  }

  // Step 2: Aggregate per spieler
  const aggregated = new Map<
    number,
    {
      gesamtPunkte: number;
      anzahlSpiele: number;
      spielPunkteListe: number[];
      gesamtMaxPunkte: number;
    }
  >();
  for (const { spielerId, total, maxPunkte } of perGame.values()) {
    if (!aggregated.has(spielerId)) {
      aggregated.set(spielerId, { gesamtPunkte: 0, anzahlSpiele: 0, spielPunkteListe: [], gesamtMaxPunkte: 0 });
    }
    const entry = aggregated.get(spielerId)!;
    entry.gesamtPunkte += total;
    entry.anzahlSpiele++;
    entry.spielPunkteListe.push(total);
    entry.gesamtMaxPunkte += maxPunkte;
  }

  if (!aggregated.size) return res.json({ rangliste: [] });

  const spielerIds = Array.from(aggregated.keys());
  const spielerRows = await db
    .select({ id: spielerTable.id, name: spielerTable.name })
    .from(spielerTable)
    .where(inArray(spielerTable.id, spielerIds));
  const nameMap = new Map(spielerRows.map((s) => [s.id, s.name]));

  const rangliste = Array.from(aggregated.entries())
    .map(([spielerId, data]) => {
      const durchschnitt = Math.round((data.gesamtPunkte / data.anzahlSpiele) * 10) / 10;
      // Normalize to percentage so different formats are comparable:
      // sum of actual scores / sum of maximum possible scores × 100
      const durchschnittProzent =
        data.gesamtMaxPunkte > 0
          ? Math.round((data.gesamtPunkte / data.gesamtMaxPunkte) * 1000) / 10
          : 0;
      return {
        spielerId,
        name: nameMap.get(spielerId) ?? "Unbekannt",
        gesamtPunkte: data.gesamtPunkte,
        anzahlSpiele: data.anzahlSpiele,
        durchschnitt,
        durchschnittProzent,
        bestPunkte: Math.max(...data.spielPunkteListe),
      };
    })
    // Sort by normalized percentage so cross-format comparison is fair
    .sort((a, b) => b.durchschnittProzent - a.durchschnittProzent || b.gesamtPunkte - a.gesamtPunkte)
    .map((e, i) => ({ rang: i + 1, ...e }));

  return res.json({ rangliste });
});

export default router;
