import { Router } from "express";
import { db, spielerTable, ergebnisseTable, spieleTable } from "@workspace/db";
import { eq } from "drizzle-orm";
import { authenticate } from "./auth";

const router = Router();

// GET /api/spieler
router.get("/", authenticate, async (_req, res) => {
  const rows = await db
    .select({
      id: spielerTable.id,
      name: spielerTable.name,
      email: spielerTable.email,
      mitgliedNr: spielerTable.mitgliedNr,
      portalAktiv: spielerTable.portalAktiv,
      createdAt: spielerTable.createdAt,
    })
    .from(spielerTable)
    .orderBy(spielerTable.name);
  return res.json({ spieler: rows });
});

// GET /api/spieler/:id
router.get("/:id", authenticate, async (req, res) => {
  const id = Number(req.params.id);
  const rows = await db
    .select({
      id: spielerTable.id,
      name: spielerTable.name,
      email: spielerTable.email,
      mitgliedNr: spielerTable.mitgliedNr,
      portalAktiv: spielerTable.portalAktiv,
      createdAt: spielerTable.createdAt,
    })
    .from(spielerTable)
    .where(eq(spielerTable.id, id))
    .limit(1);
  if (!rows[0]) return res.status(404).json({ error: "Nicht gefunden" });
  return res.json(rows[0]);
});

// GET /api/spieler/:id/ergebnisse
router.get("/:id/ergebnisse", authenticate, async (req, res) => {
  const spielerId = Number(req.params.id);
  const rows = await db
    .select({
      id: ergebnisseTable.id,
      spielId: ergebnisseTable.spielId,
      lauf: ergebnisseTable.lauf,
      taube: ergebnisseTable.taube,
      maschine: ergebnisseTable.maschine,
      posten: ergebnisseTable.posten,
      schuss1: ergebnisseTable.schuss1,
      schuss2: ergebnisseTable.schuss2,
      punkte: ergebnisseTable.punkte,
      wiederholt: ergebnisseTable.wiederholt,
      datum: spieleTable.datum,
      modus: spieleTable.modus,
    })
    .from(ergebnisseTable)
    .innerJoin(spieleTable, eq(ergebnisseTable.spielId, spieleTable.id))
    .where(eq(ergebnisseTable.spielerId, spielerId))
    .orderBy(spieleTable.datum);

  const ergebnisse = rows.map((r) => ({
    id: r.id,
    spielId: r.spielId,
    lauf: r.lauf,
    taube: r.taube,
    maschine: r.maschine,
    posten: r.posten,
    schuss1: r.schuss1,
    schuss2: r.schuss2,
    punkte: r.punkte,
    wiederholt: r.wiederholt,
    spiel: { datum: r.datum, modus: r.modus },
  }));

  return res.json({ ergebnisse });
});

export default router;
