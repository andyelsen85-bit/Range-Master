import {db} from "@workspace/db";
import {sql} from "drizzle-orm";
type Executor=Pick<typeof db,"execute">;
export async function queueEligibility(executor:Executor,ids:number[]) {
 if(!ids.length)return [];
 return (await executor.execute(sql`SELECT * FROM v_queue_eligibility
  WHERE id IN(${sql.join(ids.map(id=>sql`${id}`),sql`,`)}) ORDER BY id`)).rows as
  {id:number;name:string;registered:boolean;available:number}[];
}
/** Caller holds portal-queue advisory lock. Never touches TAKEN/LOCKED entries. */
export async function cancelIneligibleQueue(tx:Executor) {
 await tx.execute(sql`UPDATE queue_entries e SET status='CANCELLED',cancel_reason='NOT_ELIGIBLE'
  FROM queue_games g WHERE e.queue_game_id=g.id AND e.status='OPEN' AND g.status='OPEN'
   AND NOT EXISTS(SELECT 1 FROM v_queue_eligibility v WHERE v.id=e.spieler_id AND v.registered AND v.available>=1)`);
}
