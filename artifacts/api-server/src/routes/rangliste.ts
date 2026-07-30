import { Router } from "express";
import { db, spielerTable, spielTeilnahmenTable, spieleTable } from "@workspace/db";
import { eq, gte, lt, and, inArray } from "drizzle-orm";
import { computeRangliste } from "./rangliste-compute.js";

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

  const teilnahmen = await db
    .select({
      spielerId: spielTeilnahmenTable.spielerId,
      spielId: spielTeilnahmenTable.spielId,
      punkte: spielTeilnahmenTable.punkte,
    })
    .from(spielTeilnahmenTable)
    .where(inArray(spielTeilnahmenTable.spielId, filteredSpielIds));

  const spielerIds = Array.from(
    new Set(teilnahmen.map((t) => t.spielerId)),
  );

  if (!spielerIds.length) return res.json({ rangliste: [] });

  const spielerRows = await db
    .select({ id: spielerTable.id, name: spielerTable.name })
    .from(spielerTable)
    .where(inArray(spielerTable.id, spielerIds));

  const rangliste = computeRangliste(filteredSpiele, teilnahmen, spielerRows);

  return res.json({ rangliste });
});

export default router;
