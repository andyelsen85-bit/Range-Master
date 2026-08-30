import { Router } from "express";
import { db, spielerTable, ergebnisseTable, spieleTable } from "@workspace/db";
import { eq, sql } from "drizzle-orm";
import { authenticate } from "./auth";

const router = Router();

export function playerPurchaseScope(user: { id: number }): number {
  return user.id;
}

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

// GET /api/spieler/me/purchases — the authenticated player's immutable purchase history.
router.get("/me/purchases", authenticate, async (req, res) => {
  const userId = playerPurchaseScope((req as any).user);
  const result = await db.execute(sql`
    SELECT * FROM (
      SELECT 'SALE'::text AS type, s.id, s.external_id AS "externalId", s.datum,
        s.created_at AS "createdAt", s.product_id AS "productId", s.product_name AS "productName",
        s.product_category AS category, s.price_revision_id AS "priceRevisionId",
        s.unit_price_cents AS "unitPriceCents", s.quantity,
        (s.quantity * s.unit_price_cents)::int AS "totalCents"
      FROM sale_events s WHERE s.spieler_id = ${userId}
      UNION ALL
      SELECT 'GAME_CREDIT_USE'::text AS type, k.id, k.external_id AS "externalId", k.datum,
        COALESCE(k.occurred_at, k.created_at) AS "createdAt", p.id AS "productId", p.name AS "productName",
        p.category, COALESCE(k.price_revision_id, pr.id) AS "priceRevisionId",
        COALESCE(k.unit_price_cents, pr.unit_price_cents) AS "unitPriceCents", k.anzahl AS quantity,
        (k.anzahl * COALESCE(k.unit_price_cents, pr.unit_price_cents))::int AS "totalCents"
      FROM kredit_events k JOIN products p ON p.code = 'GAME_CREDIT'
      LEFT JOIN LATERAL (
        SELECT id, unit_price_cents FROM product_price_revisions
        WHERE product_id = p.id AND effective_from <= COALESCE(k.occurred_at, k.created_at)
        ORDER BY effective_from DESC, id DESC LIMIT 1
      ) pr ON true
      WHERE k.spieler_id = ${userId} AND k.typ = 'USE'
    ) purchases ORDER BY "createdAt" DESC, id DESC
  `);
  return res.json({ purchases: result.rows });
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
