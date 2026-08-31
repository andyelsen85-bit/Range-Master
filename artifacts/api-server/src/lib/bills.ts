import { db } from "@workspace/db";
import { sql } from "drizzle-orm";

type SqlExecutor = { execute: (query: any) => Promise<{ rows: unknown[] }> };

export function isBillableCreditEvent(type: "GRANT" | "USE"): boolean {
  return type === "USE";
}

export function isSettlementRedundant(status: { hasPayment: boolean; hasLaterBillableActivity: boolean }): boolean {
  return status.hasPayment && !status.hasLaterBillableActivity;
}

export function periodSettlementState(openDays: number, billableDays: number): "OPEN" | "PAID" | "PENDING_NEUTRAL" {
  if (openDays > 0) return "OPEN";
  if (billableDays > 0) return "PAID";
  return "PENDING_NEUTRAL";
}

/** Settlements are ordered by server receipt time, never a terminal clock. */
export function isActivityAfterPayment(createdAt: Date, paidAt: Date | null): boolean {
  return paidAt === null || createdAt.getTime() > paidAt.getTime();
}

/**
 * Billable activity is deliberately independent from credit grants. Game-credit
 * charges are created only when a credit is consumed and are priced at the
 * revision effective at the immutable occurrence timestamp (or createdAt for
 * legacy rows that predate occurrence snapshots).
 */
export async function billSettlementStatus(executor: SqlExecutor, spielerId: number, datum: string) {
  const result = await executor.execute(sql`
    WITH latest_payment AS (
      SELECT paid_at FROM bill_payments
      WHERE spieler_id = ${spielerId} AND datum = ${datum} AND status = 'PAID'
      ORDER BY paid_at DESC, id DESC LIMIT 1
    ), billable AS (
      SELECT s.created_at FROM sale_events s
      WHERE s.spieler_id = ${spielerId} AND s.datum = ${datum}
        AND s.product_category <> 'GAME_CREDIT'
      UNION ALL
      SELECT k.created_at FROM kredit_events k
      WHERE k.spieler_id = ${spielerId} AND k.datum = ${datum} AND k.typ = 'USE'
    )
    SELECT EXISTS(SELECT 1 FROM latest_payment) AS has_payment,
      EXISTS(SELECT 1 FROM billable
        WHERE created_at > COALESCE((SELECT paid_at FROM latest_payment), '-infinity'::timestamptz)
      ) AS has_later_billable_activity
  `);
  const row = result.rows[0] as { has_payment?: boolean; has_later_billable_activity?: boolean } | undefined;
  return {
    hasPayment: Boolean(row?.has_payment),
    hasLaterBillableActivity: Boolean(row?.has_later_billable_activity),
  };
}

/** Serialize payment decisions for one player/day, including terminal retries. */
export async function lockBillSettlement(executor: SqlExecutor, spielerId: number, datum: string): Promise<void> {
  await executor.execute(sql`SELECT pg_advisory_xact_lock(hashtextextended(${`bill:${spielerId}:${datum}`}, 0))`);
}

/** The authoritative, incremental day view. */
export async function dayBillSummary(datum: string) {
  const result = await db.execute(sql`
    WITH latest_payments AS (
      SELECT DISTINCT ON (spieler_id) spieler_id, id, external_id, paid_at, source, marked_by_admin_id, marked_by_api_key_id
      FROM bill_payments WHERE datum = ${datum} AND status = 'PAID'
      ORDER BY spieler_id, paid_at DESC, id DESC
    ), activity AS (
      SELECT spieler_id FROM sale_events WHERE datum = ${datum}
      UNION SELECT spieler_id FROM kredit_events WHERE datum = ${datum}
      UNION SELECT st.spieler_id FROM spiel_teilnahmen st
        JOIN spiele g ON g.id = st.spiel_id WHERE (g.datum AT TIME ZONE 'UTC')::date = ${datum}
      UNION SELECT spieler_id FROM bill_payments WHERE datum = ${datum}
    ), billable_events AS (
      SELECT s.spieler_id, s.product_id, s.product_name, s.product_category AS category,
        s.price_revision_id, s.unit_price_cents, s.quantity, s.created_at
      FROM sale_events s WHERE s.datum = ${datum} AND s.product_category <> 'GAME_CREDIT'
      UNION ALL
      SELECT k.spieler_id, p.id, p.name, p.category, COALESCE(k.price_revision_id, pr.id),
        COALESCE(k.unit_price_cents, pr.unit_price_cents), k.anzahl, k.created_at
      FROM kredit_events k
      JOIN products p ON p.code = 'GAME_CREDIT'
      LEFT JOIN LATERAL (
        SELECT id, unit_price_cents FROM product_price_revisions
        WHERE product_id = p.id AND effective_from <= COALESCE(k.occurred_at, k.created_at)
        ORDER BY effective_from DESC, id DESC LIMIT 1
      ) pr ON true
      WHERE k.datum = ${datum} AND k.typ = 'USE'
    ), raw_lines AS (
      SELECT b.* FROM billable_events b LEFT JOIN latest_payments lp ON lp.spieler_id = b.spieler_id
      WHERE b.created_at > COALESCE(lp.paid_at, '-infinity'::timestamptz)
    ), lines AS (
      SELECT spieler_id, product_id, product_name, category, price_revision_id, unit_price_cents,
        SUM(quantity)::int quantity, SUM(quantity * unit_price_cents)::int total_cents
      FROM raw_lines GROUP BY spieler_id, product_id, product_name, category, price_revision_id, unit_price_cents
    ), full_day_lines AS (
      SELECT spieler_id, product_id, product_name, category, price_revision_id, unit_price_cents,
        SUM(quantity)::int quantity, SUM(quantity * unit_price_cents)::int total_cents
      FROM billable_events
      GROUP BY spieler_id, product_id, product_name, category, price_revision_id, unit_price_cents
    ), credit AS (
      SELECT spieler_id,
        COALESCE(SUM(CASE WHEN typ = 'GRANT' THEN anzahl ELSE 0 END), 0)::int granted,
        COALESCE(SUM(CASE WHEN typ = 'USE' THEN anzahl ELSE 0 END), 0)::int used
      FROM kredit_events WHERE datum = ${datum} GROUP BY spieler_id
    ), game_counts AS (
      SELECT st.spieler_id, COUNT(DISTINCT g.id)::int games,
        COUNT(DISTINCT g.id) FILTER (WHERE g.abgeschlossen)::int completed_games,
        COALESCE(SUM(g.confirmed_launches) FILTER (WHERE g.abgeschlossen), 0)::int confirmed_clays
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
        SELECT category, SUM(total_cents)::int cents FROM lines WHERE spieler_id = a.spieler_id GROUP BY category
      ) categories), '{}'::jsonb) category_subtotals,
      COALESCE((SELECT SUM(total_cents) FROM lines WHERE spieler_id = a.spieler_id), 0)::int total_cents,
      COALESCE((SELECT jsonb_agg(jsonb_build_object(
        'productId', f.product_id, 'productName', f.product_name, 'category', f.category,
        'priceRevisionId', f.price_revision_id, 'unitPriceCents', f.unit_price_cents,
        'quantity', f.quantity, 'totalCents', f.total_cents
      ) ORDER BY f.product_name, f.price_revision_id) FROM full_day_lines f
        WHERE f.spieler_id = a.spieler_id), '[]'::jsonb) day_lines,
      COALESCE((SELECT jsonb_object_agg(category, cents) FROM (
        SELECT category, SUM(total_cents)::int cents
        FROM full_day_lines
        WHERE spieler_id = a.spieler_id
        GROUP BY category
      ) categories), '{}'::jsonb) day_category_subtotals,
      COALESCE((SELECT SUM(total_cents) FROM full_day_lines
        WHERE spieler_id = a.spieler_id), 0)::int day_total_cents,
      COALESCE(c.granted, 0)::int credit_granted, COALESCE(c.used, 0)::int credit_used,
      (COALESCE(c.granted, 0) - COALESCE(c.used, 0))::int credit_remaining,
      COALESCE(gc.games, 0)::int games, COALESCE(gc.completed_games, 0)::int completed_games,
      COALESCE(gc.confirmed_clays, 0)::int confirmed_clays,
      lp.external_id payment_external_id, lp.paid_at, lp.source,
      admin.id marked_by_admin_id, admin.name marked_by_admin_name,
      key.id marked_by_api_key_id, key.name marked_by_api_key_name,
      CASE WHEN lp.id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM raw_lines rl WHERE rl.spieler_id = a.spieler_id) THEN 'PAID'
           WHEN COALESCE((SELECT SUM(total_cents) FROM lines WHERE spieler_id = a.spieler_id), 0) <> 0 THEN 'OPEN'
           ELSE 'PENDING_NEUTRAL' END bill_state
    FROM activity a JOIN spieler sp ON sp.id = a.spieler_id
    LEFT JOIN credit c ON c.spieler_id = a.spieler_id LEFT JOIN game_counts gc ON gc.spieler_id = a.spieler_id
    LEFT JOIN latest_payments lp ON lp.spieler_id = a.spieler_id
    LEFT JOIN spieler admin ON admin.id = lp.marked_by_admin_id LEFT JOIN api_keys key ON key.id = lp.marked_by_api_key_id
    ORDER BY sp.name, sp.id
  `);
  const rows = result.rows as any[];
  const players = rows.map(row => ({
    spielerId: Number(row.spieler_id), spielerName: row.spieler_name, mitgliedNr: row.mitglied_nr,
    lines: row.lines, categorySubtotals: row.category_subtotals, totalCents: Number(row.total_cents),
    dayLines: row.day_lines, dayCategorySubtotals: row.day_category_subtotals,
    dayTotalCents: Number(row.day_total_cents),
    credit: { granted: Number(row.credit_granted), used: Number(row.credit_used), remaining: Number(row.credit_remaining) },
    games: Number(row.games), completedGames: Number(row.completed_games), confirmedClays: Number(row.confirmed_clays),
    state: row.bill_state, payment: row.payment_external_id ? {
      externalId: row.payment_external_id, paidAt: row.paid_at, source: row.source,
      markedByAdmin: row.marked_by_admin_id ? { id: Number(row.marked_by_admin_id), name: row.marked_by_admin_name } : null,
      markedByApiKey: row.marked_by_api_key_id ? { id: Number(row.marked_by_api_key_id), name: row.marked_by_api_key_name } : null,
    } : null,
  }));
  const productTotals: Record<string, { productId: number; productName: string; category: string; priceRevisionId: number; unitPriceCents: number; quantity: number; totalCents: number }> = {};
  const categorySubtotals: Record<string, number> = {};
  const fullDayLines = players.flatMap((player: any) => player.dayLines ?? []);
  for (const line of fullDayLines) {
    const key = `${line.productId}:${line.priceRevisionId}:${line.unitPriceCents}`;
    const total = productTotals[key] ?? { productId: Number(line.productId), productName: line.productName, category: line.category, priceRevisionId: Number(line.priceRevisionId), unitPriceCents: Number(line.unitPriceCents), quantity: 0, totalCents: 0 };
    total.quantity += Number(line.quantity); total.totalCents += Number(line.totalCents); productTotals[key] = total;
    categorySubtotals[line.category] = (categorySubtotals[line.category] ?? 0) + Number(line.totalCents);
  }
  const [gameTotals] = (await db.execute(sql`
    SELECT COUNT(*)::int games, COUNT(*) FILTER (WHERE abgeschlossen)::int completed_games,
      COALESCE(SUM(confirmed_launches) FILTER (WHERE abgeschlossen), 0)::int confirmed_clays
    FROM spiele WHERE (datum AT TIME ZONE 'UTC')::date = ${datum}
  `)).rows as any[];
  return { datum, players, categorySubtotals, productTotals, generalTotalCents: fullDayLines.reduce((n: number, line: any) => n + Number(line.totalCents), 0),
    uniquePlayers: players.length, paidPlayers: players.filter(p => p.state === "PAID").length,
    games: Number(gameTotals?.games ?? 0), completedGames: Number(gameTotals?.completed_games ?? 0), confirmedClays: Number(gameTotals?.confirmed_clays ?? 0) };
}

/** Dates with any authoritative activity, used to decorate the portal calendar. */
export async function activityDays(from: string, to: string) {
  const result = await db.execute(sql`
    SELECT activity_date::text AS activity_date
    FROM (
      SELECT datum AS activity_date
      FROM sale_events
      WHERE datum BETWEEN ${from} AND ${to}
      UNION
      SELECT datum AS activity_date
      FROM kredit_events
      WHERE datum BETWEEN ${from} AND ${to}
      UNION
      SELECT (datum AT TIME ZONE 'UTC')::date AS activity_date
      FROM spiele
      WHERE (datum AT TIME ZONE 'UTC')::date BETWEEN ${from} AND ${to}
      UNION
      SELECT datum AS activity_date
      FROM bill_payments
      WHERE datum BETWEEN ${from} AND ${to}
    ) days
    ORDER BY activity_date
  `);
  return { from, to, days: result.rows.map((row: any) => String(row.activity_date)) };
}

/** Inclusive period report. Billable totals are full activity; open totals retain payment context. */
export async function periodBillSummary(from: string, to: string) {
  const result = await db.execute(sql`
    WITH latest_payments AS (
      SELECT DISTINCT ON (spieler_id, datum) spieler_id, datum, paid_at
      FROM bill_payments
      WHERE datum BETWEEN ${from} AND ${to} AND status = 'PAID'
      ORDER BY spieler_id, datum, paid_at DESC, id DESC
    ), activity AS (
      SELECT spieler_id FROM sale_events WHERE datum BETWEEN ${from} AND ${to}
      UNION SELECT spieler_id FROM kredit_events WHERE datum BETWEEN ${from} AND ${to}
      UNION SELECT st.spieler_id FROM spiel_teilnahmen st
        JOIN spiele g ON g.id = st.spiel_id
        WHERE (g.datum AT TIME ZONE 'UTC')::date BETWEEN ${from} AND ${to}
      UNION SELECT spieler_id FROM bill_payments WHERE datum BETWEEN ${from} AND ${to}
    ), billable_events AS (
      SELECT s.spieler_id, s.datum, s.product_id, s.product_name, s.product_category AS category,
        s.price_revision_id, s.unit_price_cents, s.quantity, s.created_at
      FROM sale_events s
      WHERE s.datum BETWEEN ${from} AND ${to} AND s.product_category <> 'GAME_CREDIT'
      UNION ALL
      SELECT k.spieler_id, k.datum, p.id, p.name, p.category,
        COALESCE(k.price_revision_id, pr.id),
        COALESCE(k.unit_price_cents, pr.unit_price_cents), k.anzahl, k.created_at
      FROM kredit_events k
      JOIN products p ON p.code = 'GAME_CREDIT'
      LEFT JOIN LATERAL (
        SELECT id, unit_price_cents FROM product_price_revisions
        WHERE product_id = p.id AND effective_from <= COALESCE(k.occurred_at, k.created_at)
        ORDER BY effective_from DESC, id DESC LIMIT 1
      ) pr ON true
      WHERE k.datum BETWEEN ${from} AND ${to} AND k.typ = 'USE'
    ), open_events AS (
      SELECT b.*
      FROM billable_events b
      LEFT JOIN latest_payments lp ON lp.spieler_id = b.spieler_id AND lp.datum = b.datum
      WHERE b.created_at > COALESCE(lp.paid_at, '-infinity'::timestamptz)
    ), full_lines AS (
      SELECT spieler_id, product_id, product_name, category, price_revision_id, unit_price_cents,
        SUM(quantity)::int quantity, SUM(quantity * unit_price_cents)::int total_cents
      FROM billable_events
      GROUP BY spieler_id, product_id, product_name, category, price_revision_id, unit_price_cents
    ), open_totals AS (
      SELECT spieler_id, SUM(quantity * unit_price_cents)::int total_cents
      FROM open_events
      GROUP BY spieler_id
    ), day_states AS (
      SELECT b.spieler_id, b.datum,
        SUM(b.quantity * b.unit_price_cents)::int full_total_cents,
        SUM(CASE
          WHEN b.created_at > COALESCE(lp.paid_at, '-infinity'::timestamptz)
          THEN b.quantity * b.unit_price_cents ELSE 0
        END)::int open_total_cents
      FROM billable_events b
      LEFT JOIN latest_payments lp ON lp.spieler_id = b.spieler_id AND lp.datum = b.datum
      GROUP BY b.spieler_id, b.datum
    ), credit AS (
      SELECT spieler_id,
        COALESCE(SUM(CASE WHEN typ = 'GRANT' THEN anzahl ELSE 0 END), 0)::int granted,
        COALESCE(SUM(CASE WHEN typ = 'USE' THEN anzahl ELSE 0 END), 0)::int used
      FROM kredit_events
      WHERE datum BETWEEN ${from} AND ${to}
      GROUP BY spieler_id
    ), game_counts AS (
      SELECT st.spieler_id, COUNT(DISTINCT g.id)::int games,
        COUNT(DISTINCT g.id) FILTER (WHERE g.abgeschlossen)::int completed_games,
        COALESCE(SUM(g.confirmed_launches) FILTER (WHERE g.abgeschlossen), 0)::int confirmed_clays
      FROM spiel_teilnahmen st JOIN spiele g ON g.id = st.spiel_id
      WHERE (g.datum AT TIME ZONE 'UTC')::date BETWEEN ${from} AND ${to}
      GROUP BY st.spieler_id
    )
    SELECT a.spieler_id, sp.name spieler_name, sp.mitglied_nr,
      COALESCE((SELECT jsonb_agg(jsonb_build_object(
        'productId', l.product_id, 'productName', l.product_name, 'category', l.category,
        'priceRevisionId', l.price_revision_id, 'unitPriceCents', l.unit_price_cents,
        'quantity', l.quantity, 'totalCents', l.total_cents) ORDER BY l.product_name, l.price_revision_id)
        FROM full_lines l WHERE l.spieler_id = a.spieler_id), '[]'::jsonb) lines,
      COALESCE((SELECT jsonb_object_agg(category, cents) FROM (
        SELECT category, SUM(total_cents)::int cents FROM full_lines WHERE spieler_id = a.spieler_id GROUP BY category
      ) categories), '{}'::jsonb) category_subtotals,
      COALESCE((SELECT SUM(total_cents) FROM full_lines WHERE spieler_id = a.spieler_id), 0)::int total_cents,
      COALESCE(ot.total_cents, 0)::int open_total_cents,
      COALESCE(c.granted, 0)::int credit_granted, COALESCE(c.used, 0)::int credit_used,
      (COALESCE(c.granted, 0) - COALESCE(c.used, 0))::int credit_remaining,
      COALESCE(gc.games, 0)::int games, COALESCE(gc.completed_games, 0)::int completed_games,
      COALESCE(gc.confirmed_clays, 0)::int confirmed_clays,
      COALESCE((SELECT COUNT(*) FROM latest_payments lp WHERE lp.spieler_id = a.spieler_id), 0)::int paid_days,
      COALESCE((SELECT COUNT(*) FROM day_states ds
        WHERE ds.spieler_id = a.spieler_id AND ds.open_total_cents <> 0), 0)::int open_days,
      COALESCE((SELECT COUNT(*) FROM day_states ds
        WHERE ds.spieler_id = a.spieler_id AND ds.full_total_cents <> 0), 0)::int billable_days
    FROM activity a
    JOIN spieler sp ON sp.id = a.spieler_id
    LEFT JOIN open_totals ot ON ot.spieler_id = a.spieler_id
    LEFT JOIN credit c ON c.spieler_id = a.spieler_id
    LEFT JOIN game_counts gc ON gc.spieler_id = a.spieler_id
    ORDER BY sp.name, sp.id
  `);
  const rows = result.rows as any[];
  const players = rows.map(row => {
    const totalCents = Number(row.total_cents);
    const openTotalCents = Number(row.open_total_cents);
    return {
      spielerId: Number(row.spieler_id),
      spielerName: row.spieler_name,
      mitgliedNr: row.mitglied_nr,
      lines: row.lines,
      categorySubtotals: row.category_subtotals,
      totalCents,
      openTotalCents,
      credit: {
        granted: Number(row.credit_granted),
        used: Number(row.credit_used),
        remaining: Number(row.credit_remaining),
      },
      games: Number(row.games),
      completedGames: Number(row.completed_games),
      confirmedClays: Number(row.confirmed_clays),
      paidDays: Number(row.paid_days),
      state: periodSettlementState(Number(row.open_days), Number(row.billable_days)),
    };
  });
  const productTotals: Record<string, { productId: number; productName: string; category: string; priceRevisionId: number; unitPriceCents: number; quantity: number; totalCents: number }> = {};
  const categorySubtotals: Record<string, number> = {};
  for (const line of players.flatMap((player: any) => player.lines ?? [])) {
    const key = `${line.productId}:${line.priceRevisionId}:${line.unitPriceCents}`;
    const total = productTotals[key] ?? {
      productId: Number(line.productId),
      productName: line.productName,
      category: line.category,
      priceRevisionId: Number(line.priceRevisionId),
      unitPriceCents: Number(line.unitPriceCents),
      quantity: 0,
      totalCents: 0,
    };
    total.quantity += Number(line.quantity);
    total.totalCents += Number(line.totalCents);
    productTotals[key] = total;
    categorySubtotals[line.category] = (categorySubtotals[line.category] ?? 0) + Number(line.totalCents);
  }
  const [gameTotals] = (await db.execute(sql`
    SELECT COUNT(*)::int games, COUNT(*) FILTER (WHERE abgeschlossen)::int completed_games,
      COALESCE(SUM(confirmed_launches) FILTER (WHERE abgeschlossen), 0)::int confirmed_clays
    FROM spiele
    WHERE (datum AT TIME ZONE 'UTC')::date BETWEEN ${from} AND ${to}
  `)).rows as any[];
  return {
    from,
    to,
    players,
    categorySubtotals,
    productTotals,
    generalTotalCents: players.reduce((sum: number, player: any) => sum + player.totalCents, 0),
    uniquePlayers: players.length,
    paidPlayers: players.filter((player: any) => player.state === "PAID").length,
    games: Number(gameTotals?.games ?? 0),
    completedGames: Number(gameTotals?.completed_games ?? 0),
    confirmedClays: Number(gameTotals?.confirmed_clays ?? 0),
  };
}