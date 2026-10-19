import { db } from "@workspace/db";
import { sql, type SQL } from "drizzle-orm";
import { z } from "zod";
import { daySchema } from "./local-day";

export const purgeInput = z.discriminatedUnion("mode", [
  z.object({ mode:z.literal("day"), datum:daySchema, confirmation:z.literal("PURGE_DAY") }),
  z.object({ mode:z.literal("all"), confirmation:z.literal("PURGE_ALL") }),
]);
export type PurgeCounts = Record<
  "players"|"games"|"participations"|"results"|"credits"|"sales"|"billPayments"|
  "playerUpdates"|"queueGames"|"queueEntries"|"gamedayPlayers"|"gamedayMaitre"|
  "groupMembers"|"hiddenPlayers"|"liveGames"|"inventoryDays"|"dailyReports"|
  "terminalManifests"|"gamedayEvents"|"gamedayPlayerEvents"|"auditsDetached"|
  "restoreAuthorizationsDetached", number>;

const activityTables = [
  "spieler","spiele","admin_game_edits","bill_payments","sale_events","kredit_events",
  "spieler_updates","ergebnisse","spiel_teilnahmen","queue_games","queue_entries",
  "gameday_players","gameday_maitre","spieler_gruppen_mitglieder","spiel_spieler_hidden",
  "live_game_state","ammo_inventory_day","daily_report_log","terminal_game_manifest",
  "gameday_event","gameday_player_events","terminal_restore_authorizations",
];

/** Only call after authentication, admin authorization and explicit confirmation. */
export async function purgeAdminData(input:z.infer<typeof purgeInput>, adminId:number) {
  return db.transaction(async tx => {
    // Include every referencing table, including newly added/custom FKs. An
    // unexpected dependent row must block and roll back, never be deleted blindly.
    const references=(await tx.execute(sql`
      SELECT DISTINCT n.nspname,c.relname FROM pg_constraint f
      JOIN pg_class c ON c.oid=f.conrelid JOIN pg_namespace n ON n.oid=c.relnamespace
      WHERE f.contype='f' AND f.confrelid IN ('spiele'::regclass,'spieler'::regclass)
      ORDER BY n.nspname,c.relname`)).rows as {nspname:string;relname:string}[];
    const locks=activityTables.map(name=>({schema:"public",name}));
    for(const ref of references)
      if(!locks.some(l=>l.schema===ref.nspname&&l.name===ref.relname))
        locks.push({schema:ref.nspname,name:ref.relname});
    await tx.execute(sql`LOCK TABLE ${sql.join(locks.map(l=>
      sql`${sql.identifier(l.schema)}.${sql.identifier(l.name)}`),sql`, `)} IN ACCESS EXCLUSIVE MODE`);

    const all=input.mode==="all",day=input.mode==="day"?input.datum:null;
    const date=(column:SQL)=>all?sql`true`:sql`${column}=${day}::date`;
    const timestamp=(column:SQL)=>date(sql`(${column} AT TIME ZONE 'Europe/Luxembourg')::date`);
    const games=sql`SELECT g.id FROM spiele g WHERE ${timestamp(sql`g.datum`)}`;
    const gameExternalIds=sql`SELECT g.external_id FROM spiele g WHERE ${timestamp(sql`g.datum`)}`;
    const queues=sql`SELECT q.id FROM queue_games q WHERE ${all?sql`true`:sql`
      ${timestamp(sql`COALESCE(q.started_at,q.created_at)`)} OR q.started_spiel_id IN (${games})`}`;
    const nonAdmins=sql`SELECT id FROM spieler WHERE NOT is_admin`;
    const remove=async(table:string,where:SQL=sql`true`):Promise<number>=>{
      const result=await tx.execute(sql`WITH gone AS (
        DELETE FROM ${sql.identifier(table)} WHERE ${where} RETURNING 1)
        SELECT count(*)::int AS n FROM gone`);
      return Number(result.rows[0].n);
    };
    const counts={} as PurgeCounts;
    // Defensive backfill also covers audits written by an older server during
    // a rolling deployment after the migration's one-time backfill.
    const detached=await tx.execute(sql`
      UPDATE admin_game_edits a SET
        before=jsonb_build_object('game',jsonb_build_object('id',g.id,'externalId',g.external_id,'date',g.datum))
          || CASE WHEN jsonb_typeof(a.before)='object' THEN a.before ELSE jsonb_build_object('state',a.before) END,
        after=jsonb_build_object('game',jsonb_build_object('id',g.id,'externalId',g.external_id,'date',g.datum))
          || CASE WHEN jsonb_typeof(a.after)='object' THEN a.after ELSE jsonb_build_object('state',a.after) END,
        spiel_id=NULL
      FROM spiele g WHERE a.spiel_id=g.id AND g.id IN (${games}) RETURNING a.id`);
    counts.auditsDetached=detached.rows.length;
    counts.liveGames=await remove("live_game_state",all?sql`true`:sql`
      payload->>'spielExternalId' IN (${gameExternalIds}) OR
      ${timestamp(sql`CASE WHEN payload->>'startedAt' ~ '^[0-9]{4}-[0-9]{2}-[0-9]{2}T'
        THEN (payload->>'startedAt')::timestamptz ELSE updated_at END`)}`);
    // Delete entries first; queue selection still sees the referenced games.
    counts.queueEntries=await remove("queue_entries",all?sql`true`:sql`
      queue_game_id IN (${queues}) OR spiel_id IN (${games}) OR ${timestamp(sql`signup_at`)}`);
    counts.queueGames=await remove("queue_games",sql`id IN (${queues})`);
    counts.gamedayPlayers=await remove("gameday_players",date(sql`datum`));
    counts.gamedayMaitre=await remove("gameday_maitre",date(sql`datum`));
    counts.gamedayPlayerEvents=await remove("gameday_player_events",all?sql`true`:sql`payload->>'datum'=${day}`);
    // Legacy closing receipts have no business date. Receipt time is NOT the
    // shooting date (offline sync can arrive later). Keep these in day mode:
    // guessing could remove another day's dedupe protection. Global reset
    // explicitly clears them with terminals disconnected.
    counts.gamedayEvents=all?await remove("gameday_event"):0;
    counts.inventoryDays=await remove("ammo_inventory_day",date(sql`datum`));
    counts.dailyReports=await remove("daily_report_log",date(sql`datum`));
    counts.terminalManifests=await remove("terminal_game_manifest",date(sql`datum`));
    counts.billPayments=await remove("bill_payments",date(sql`datum`));
    counts.sales=await remove("sale_events",date(sql`datum`));
    counts.credits=await remove("kredit_events",date(sql`datum`));
    counts.hiddenPlayers=await remove("spiel_spieler_hidden",sql`spiel_id IN (${games})`);
    counts.results=await remove("ergebnisse",sql`spiel_id IN (${games})`);
    counts.participations=await remove("spiel_teilnahmen",sql`spiel_id IN (${games})`);
    counts.games=await remove("spiele",sql`id IN (${games})`);
    // Membership and profile-update history are not owned by a shooting day.
    // Preserve them for day mode; remove dependencies before global player reset.
    counts.groupMembers=all?await remove("spieler_gruppen_mitglieder",sql`spieler_id IN (${nonAdmins})`):0;
    counts.playerUpdates=all?await remove("spieler_updates"):0;
    counts.restoreAuthorizationsDetached=0;
    if(all){
      const changed=await tx.execute(sql`UPDATE terminal_restore_authorizations SET created_by=NULL
        WHERE created_by IN (${nonAdmins}) RETURNING id`);
      counts.restoreAuthorizationsDetached=changed.rows.length;
    }
    counts.players=all?await remove("spieler",sql`NOT is_admin`):0;
    const snapshot={mode:input.mode,datum:day,counts};
    const [audit]=(await tx.execute(sql`
      INSERT INTO admin_game_edits(admin_user_id,action,before,after)
      VALUES(${adminId},'PURGE',NULL,${JSON.stringify(snapshot)}::jsonb) RETURNING id`)).rows as {id:number}[];
    return {mode:input.mode,datum:day??undefined,counts,auditId:audit.id};
  });
}

/** Drizzle wraps PostgreSQL errors in .cause. Never expose raw SQL or parameters. */
export function purgeDatabaseError(error:unknown) {
  let current:any=error,detail:any={},seen=new Set();
  while(current&&typeof current==="object"&&!seen.has(current)){
    seen.add(current);
    if(current.code||current.constraint||current.table)detail=current;
    current=current.cause;
  }
  const table=typeof detail.table==="string"?detail.table:null;
  const constraint=typeof detail.constraint==="string"?detail.constraint:null;
  const blocked=["23503","23514","40P01","55P03","40001"].includes(detail.code);
  const context=[table&&`Tabelle „${table}“`,constraint&&`Prüfregel „${constraint}“`].filter(Boolean).join(", ");
  return {
    status:blocked?409:500,
    body:{error:`Bereinigung ${blocked?"blockiert":"fehlgeschlagen"}${context?`: ${context}`:""}. Es wurde nichts gelöscht. Bitte die Protokollmeldung prüfen und erneut versuchen.`,
      code:blocked?"PURGE_BLOCKED":"PURGE_FAILED",table,constraint},
  };
}
