import { Router } from "express";
import { db, ergebnisseTable, spielTeilnahmenTable, spieleTable } from "@workspace/db";
import { eq } from "drizzle-orm";
import { authenticate } from "./auth";
import { desc, sql } from "drizzle-orm";

const router = Router();

// GET /api/statistik/:spielerId
router.get("/:spielerId", authenticate, async (req, res) => {
  const spielerId = Number(req.params.spielerId);

  const ergebnisse = await db
    .select({
      maschine: ergebnisseTable.maschine,
      schuss1: ergebnisseTable.schuss1,
      schuss2: ergebnisseTable.schuss2,
    })
    .from(ergebnisseTable)
    .where(eq(ergebnisseTable.spielerId, spielerId));

  if (!ergebnisse.length) {
    return res.json({
      spielerId,
      gesamtSpiele: 0,
      durchschnitt: 0,
      bestPunkte: 0,
      trefferquote: 0,
      maschinen: {},
    });
  }

  const treffer = ergebnisse.filter((e) => e.schuss1 || e.schuss2).length;
  const trefferquote = Math.round((treffer / ergebnisse.length) * 1000) / 10;

  const maschinen: Record<string, { versuche: number; treffer: number; quote: number }> = {};
  for (const e of ergebnisse) {
    if (!maschinen[e.maschine]) maschinen[e.maschine] = { versuche: 0, treffer: 0, quote: 0 };
    maschinen[e.maschine].versuche++;
    if (e.schuss1 || e.schuss2) maschinen[e.maschine].treffer++;
  }
  for (const m of Object.keys(maschinen)) {
    maschinen[m].quote = Math.round((maschinen[m].treffer / maschinen[m].versuche) * 1000) / 10;
  }

  // Group teilnahmen by spiel → sum both Läufe per game → per-game totals
  const teilnahmen = await db
    .select({ spielId: spielTeilnahmenTable.spielId, punkte: spielTeilnahmenTable.punkte })
    .from(spielTeilnahmenTable)
    .where(eq(spielTeilnahmenTable.spielerId, spielerId));

  const gameMap = new Map<number, number>();
  for (const t of teilnahmen) {
    gameMap.set(t.spielId, (gameMap.get(t.spielId) ?? 0) + t.punkte);
  }
  const gamePunkte = Array.from(gameMap.values());

  return res.json({
    spielerId,
    gesamtSpiele: gamePunkte.length,
    durchschnitt: gamePunkte.length
      ? Math.round((gamePunkte.reduce((a, b) => a + b, 0) / gamePunkte.length) * 10) / 10
      : 0,
    bestPunkte: gamePunkte.length ? Math.max(...gamePunkte) : 0,
    trefferquote,
    maschinen,
  });
});

// GET /api/statistik/:spielerId/modus-breakdown
router.get("/:spielerId/modus-breakdown", authenticate, async (req, res) => {
  const spielerId = Number(req.params.spielerId);

  // Fetch all teilnahmen with their game's modus and max possible score
  const rows = await db
    .select({
      spielId: spielTeilnahmenTable.spielId,
      punkte: spielTeilnahmenTable.punkte,
      modus: spieleTable.modus,
      taubenProLauf: spieleTable.taubenProLauf,
      lauf: spieleTable.lauf,
    })
    .from(spielTeilnahmenTable)
    .innerJoin(spieleTable, eq(spielTeilnahmenTable.spielId, spieleTable.id))
    .where(eq(spielTeilnahmenTable.spielerId, spielerId));

  if (!rows.length) {
    return res.json({ breakdown: [] });
  }

  // Step 1: Sum both Läufe per game → per-game totals
  const perGame = new Map<number, { modus: string; total: number; maxPunkte: number }>();
  for (const r of rows) {
    if (!perGame.has(r.spielId)) {
      perGame.set(r.spielId, {
        modus: r.modus,
        total: 0,
        maxPunkte: r.taubenProLauf * 2 * r.lauf,
      });
    }
    perGame.get(r.spielId)!.total += r.punkte;
  }

  // Step 2: Aggregate per modus (same normalization as rangliste)
  const perModus = new Map<
    string,
    { gesamtPunkte: number; gesamtMaxPunkte: number; anzahlSpiele: number }
  >();
  for (const { modus, total, maxPunkte } of perGame.values()) {
    if (!perModus.has(modus)) {
      perModus.set(modus, { gesamtPunkte: 0, gesamtMaxPunkte: 0, anzahlSpiele: 0 });
    }
    const entry = perModus.get(modus)!;
    entry.gesamtPunkte += total;
    entry.gesamtMaxPunkte += maxPunkte;
    entry.anzahlSpiele++;
  }

  const breakdown = Array.from(perModus.entries()).map(([modus, data]) => ({
    modus,
    anzahlSpiele: data.anzahlSpiele,
    durchschnitt: Math.round((data.gesamtPunkte / data.anzahlSpiele) * 10) / 10,
    durchschnittProzent:
      data.gesamtMaxPunkte > 0
        ? Math.round((data.gesamtPunkte / data.gesamtMaxPunkte) * 1000) / 10
        : 0,
  }));

  return res.json({ breakdown });
});

// GET /api/statistik/:spielerId/verlauf
router.get("/:spielerId/verlauf", authenticate, async (req, res) => {
  const spielerId = Number(req.params.spielerId);
  const limit = Number(req.query.limit ?? 20);

  // Sum both Läufe per game → one entry per game
  const rows = await db
    .select({
      spielId: spielTeilnahmenTable.spielId,
      punkte: sql<number>`cast(sum(${spielTeilnahmenTable.punkte}) as int)`,
      datum: spieleTable.datum,
      modus: spieleTable.modus,
      taubenProLauf: spieleTable.taubenProLauf,
      lauf: spieleTable.lauf,
    })
    .from(spielTeilnahmenTable)
    .innerJoin(spieleTable, eq(spielTeilnahmenTable.spielId, spieleTable.id))
    .where(eq(spielTeilnahmenTable.spielerId, spielerId))
    .groupBy(
      spielTeilnahmenTable.spielId,
      spieleTable.datum,
      spieleTable.modus,
      spieleTable.taubenProLauf,
      spieleTable.lauf,
    )
    .orderBy(desc(spieleTable.datum))
    .limit(limit);

  const verlauf = rows.reverse().map((r) => ({
    datum: r.datum.toISOString().slice(0, 10),
    punkte: r.punkte,
    modus: r.modus,
    maxPunkte: r.taubenProLauf * 2 * r.lauf,
  }));

  return res.json({ verlauf });
});

export default router;
