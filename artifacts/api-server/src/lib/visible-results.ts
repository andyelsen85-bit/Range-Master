import { sql } from "drizzle-orm";
import { spieleTable,spielTeilnahmenTable,ergebnisseTable } from "@workspace/db";
// Display/statistics only. Operational ledgers deliberately use the base tables.
export const visibleSpiele=sql`${spieleTable.id} IN (SELECT id FROM v_spiele_visible)`;
export const visibleTeilnahmen=sql`${spielTeilnahmenTable.id} IN (SELECT id FROM v_teilnahmen_visible)`;
export const visibleErgebnisse=sql`${ergebnisseTable.id} IN (SELECT id FROM v_ergebnisse_visible)`;
