import { db } from "@workspace/db";
import { sql } from "drizzle-orm";

/**
 * The authoritative day view. Amounts are calculated only from immutable sale
 * snapshots, while a payment is an immutable closure event rather than a
 * mutable flag on a player.
 */
export async function dayBillSummary(datum: string) {
  const result = await db.execute(sql`
    WITH activity AS (
      SELECT spieler_id FROM sale_events WHERE datum = ${datum}
      UNION SELECT spieler_id FROM kredit_events WHERE datum = ${datum}
      UNION SELECT st.spieler_id FROM spiel_teilnahmen st
        JOIN spiele g ON g.id = st.spiel_id
        WHERE (g.datum AT TIME ZONE 'UTC')::date = ${datum}
      UNION SELECT spieler_id FROM bill_payments WHERE datum = ${datum}
    ), lines AS (
      SELECT s.spieler_id, s.product_id, s.product_name, s.product_category category,
             s.price_revision_id, s.unit_price_cents,
             SUM(s.quantity)::int quantity,
             SUM(s.quantity * s.unit_price_cents)::int total_cents
      FROM sale_events s
      WHERE s.datum = ${datum}
      GROUP BY s.spieler_id, s.product_id, s.product_name, s.product_category, s.price_revision_id, s.unit_price_cents
    ), credit AS (
      SELECT spieler_id,
        COALESCE(SUM(CASE WHEN typ = 'GRANT' THEN anzahl ELSE 0 END),0)::int granted,
        COALESCE(SUM(CASE WHEN typ = 'USE' THEN anzahl ELSE 0 END),0)::int used
      FROM kredit_events WHERE datum = ${datum} GROUP BY spieler_id
    ), game_counts AS (
      SELECT st.spieler_id, COUNT(DISTINCT g.id)::int games,
        COUNT(DISTINCT g.id) FILTER (WHERE g.abgeschlossen)::int completed_games,
        COALESCE(SUM(g.confirmed_launches) FILTER (WHERE g.abgeschlossen),0)::int confirmed_clays
      FROM spiel_teilnahmen st JOIN spiele g ON g.id = st.spiel_id
      WHERE (g.datum AT TIME ZONE 'UTC')::date = ${datum} GROUP BY st.spieler_id
    )
    SELECT a.spieler_id, sp.name spieler_name, sp.mitglied_nr,
      COALESCE((SELECT jsonb_agg(jsonb_build_object(
        'productId', l.product_id, 'productName', l.product_name, 'category', l.category,
        'priceRevisionId', l.price_revision_id, 'unitPriceCents', l.unit_price_cents,
        'quantity', l.quantity, 'totalCents', l.total_cents) ORDER BY l.product_name, l.price_revision_id)
        FROM lines l WHERE l.spieler_id = a.spieler_id), '[]'::jsonb) lines,
      COALESCE((SELECT jsonb_object_agg(category, cents) FROM (
        SELECT category, SUM(total_cents)::int cents FROM lines WHERE spieler_id=a.spieler_id GROUP BY category
      ) categories), '{}'::jsonb) category_subtotals,
      COALESCE((SELECT SUM(total_cents) FROM lines WHERE spieler_id=a.spieler_id),0)::int total_cents,
      COALESCE(c.granted,0)::int credit_granted, COALESCE(c.used,0)::int credit_used,
      (COALESCE(c.granted,0)-COALESCE(c.used,0))::int credit_remaining,
      COALESCE(gc.games,0)::int games, COALESCE(gc.completed_games,0)::int completed_games,
      COALESCE(gc.confirmed_clays,0)::int confirmed_clays,
      bp.external_id payment_external_id, bp.paid_at, bp.source,
      admin.id marked_by_admin_id, admin.name marked_by_admin_name,
      key.id marked_by_api_key_id, key.name marked_by_api_key_name,
      CASE WHEN bp.id IS NOT NULL THEN 'PAID'
           WHEN COALESCE((SELECT SUM(total_cents) FROM lines WHERE spieler_id=a.spieler_id),0) = 0 THEN 'PENDING_NEUTRAL'
           ELSE 'OPEN' END bill_state
    FROM activity a JOIN spieler sp ON sp.id=a.spieler_id
    LEFT JOIN credit c ON c.spieler_id=a.spieler_id
    LEFT JOIN game_counts gc ON gc.spieler_id=a.spieler_id
    LEFT JOIN bill_payments bp ON bp.spieler_id=a.spieler_id AND bp.datum=${datum} AND bp.status='PAID'
    LEFT JOIN spieler admin ON admin.id=bp.marked_by_admin_id
    LEFT JOIN api_keys key ON key.id=bp.marked_by_api_key_id
    ORDER BY sp.name
  `);
  const players = (result.rows as any[]).map(row => ({
    spielerId: Number(row.spieler_id), spielerName: row.spieler_name, mitgliedNr: row.mitglied_nr,
    lines: row.lines, categorySubtotals: row.category_subtotals, totalCents: Number(row.total_cents),
    credit: { granted: Number(row.credit_granted), used: Number(row.credit_used), remaining: Number(row.credit_remaining) },
    games: Number(row.games), completedGames: Number(row.completed_games), confirmedClays: Number(row.confirmed_clays),
    state: row.bill_state, payment: row.payment_external_id ? {
      externalId: row.payment_external_id, paidAt: row.paid_at, source: row.source,
      markedByAdmin: row.marked_by_admin_id ? { id: Number(row.marked_by_admin_id), name: row.marked_by_admin_name } : null,
      markedByApiKey: row.marked_by_api_key_id ? { id: Number(row.marked_by_api_key_id), name: row.marked_by_api_key_name } : null,
    } : null,
  }));
  const productTotals: Record<string, {
    productId: number; productName: string; category: string; priceRevisionId: number;
    unitPriceCents: number; quantity: number; totalCents: number;
  }> = {};
  const categorySubtotals: Record<string, number> = {};
  for (const player of players) for (const line of player.lines) {
    const key = `${line.productId}:${line.priceRevisionId}:${line.unitPriceCents}`;
    const total = productTotals[key] ?? {
      productId: Number(line.productId), productName: line.productName, category: line.category,
      priceRevisionId: Number(line.priceRevisionId), unitPriceCents: Number(line.unitPriceCents),
      quantity: 0, totalCents: 0,
    };
    total.quantity += Number(line.quantity);
    total.totalCents += Number(line.totalCents);
    productTotals[key] = total;
    categorySubtotals[line.category] = (categorySubtotals[line.category] ?? 0) + Number(line.totalCents);
  }
  const [gameTotals] = (await db.execute(sql`
    SELECT COUNT(*)::int games,
      COUNT(*) FILTER (WHERE abgeschlossen)::int completed_games,
      COALESCE(SUM(confirmed_launches) FILTER (WHERE abgeschlossen), 0)::int confirmed_clays
    FROM spiele WHERE (datum AT TIME ZONE 'UTC')::date = ${datum}
  `)).rows as any[];
  return { datum, players, categorySubtotals, productTotals,
    generalTotalCents: players.reduce((n, p) => n + p.totalCents, 0),
    uniquePlayers: players.length, paidPlayers: players.filter(p => p.state === "PAID").length,
    games: Number(gameTotals?.games ?? 0), completedGames: Number(gameTotals?.completed_games ?? 0),
    confirmedClays: Number(gameTotals?.confirmed_clays ?? 0) };
}