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
