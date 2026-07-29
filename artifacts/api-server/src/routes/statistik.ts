import { Router } from "express";
import { db, ergebnisseTable, spielTeilnahmenTable, spieleTable } from "@workspace/db";
import { eq } from "drizzle-orm";
import { authenticate } from "./auth";
import { desc } from "drizzle-orm";

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
      gesamtLauefe: 0,
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

  const teilnahmen = await db
    .select({ punkte: spielTeilnahmenTable.punkte })
    .from(spielTeilnahmenTable)
    .where(eq(spielTeilnahmenTable.spielerId, spielerId));

  const punkte = teilnahmen.map((t) => t.punkte);

  return res.json({
    spielerId,
    gesamtLauefe: punkte.length,
    durchschnitt: punkte.length
      ? Math.round((punkte.reduce((a, b) => a + b, 0) / punkte.length) * 10) / 10
      : 0,
    bestPunkte: punkte.length ? Math.max(...punkte) : 0,
    trefferquote,
    maschinen,
  });
});

// GET /api/statistik/:spielerId/verlauf
router.get("/:spielerId/verlauf", authenticate, async (req, res) => {
  const spielerId = Number(req.params.spielerId);
  const limit = Number(req.query.limit ?? 20);

  const rows = await db
    .select({
      punkte: spielTeilnahmenTable.punkte,
      datum: spieleTable.datum,
      modus: spieleTable.modus,
    })
    .from(spielTeilnahmenTable)
    .innerJoin(spieleTable, eq(spielTeilnahmenTable.spielId, spieleTable.id))
    .where(eq(spielTeilnahmenTable.spielerId, spielerId))
    .orderBy(desc(spieleTable.datum))
    .limit(limit);

  const verlauf = rows.reverse().map((r) => ({
    datum: r.datum.toISOString().slice(0, 10),
    punkte: r.punkte,
    modus: r.modus,
  }));

  return res.json({ verlauf });
});

export default router;
