import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
export async function pruneBackups(today: string) {
  return db.transaction(async tx => {
    await tx.execute(sql`LOCK TABLE terminal_config_backups,terminal_restore_authorizations IN SHARE ROW EXCLUSIVE MODE`);
    const result = await tx.execute(sql`DELETE FROM terminal_config_backups b
      WHERE b.backup_date < ${today}::date - INTERVAL '30 days'
      AND NOT EXISTS(SELECT 1 FROM terminal_restore_authorizations a WHERE a.backup_id=b.id
        AND a.revoked_at IS NULL AND (a.used_at IS NULL OR a.expires_at>now()))
      AND b.id <> COALESCE((SELECT x.id FROM terminal_config_backups x
        WHERE x.terminal_id=b.terminal_id AND x.revoked_at IS NULL
        ORDER BY x.backup_date DESC,x.created_at DESC,x.id DESC LIMIT 1),-1) RETURNING b.id`);
    return result.rows.length;
  });
}
