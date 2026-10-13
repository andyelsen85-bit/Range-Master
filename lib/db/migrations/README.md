# DB Migrations

Die verbindliche Deployment-Migrationskette liegt unter `lib/db/drizzle/`
und ist in `lib/db/drizzle/meta/_journal.json` registriert. Der bestehende
Kubernetes-initContainer `run-migrations` führt `/app/dist/migrate.mjs` aus;
der neue API-Container startet erst nach dessen Erfolg. Keine DDL in `index.ts`.

Lokal: `pnpm migrate`. Nicht gegen eine beliebige bestehende Datenbank ohne
passendes Drizzle-Journal ausführen: die historischen Migrationen `0000..0008`
sind nicht pauschal idempotent. `0009` darf hingegen bereits manuell angewendet
worden sein; Drizzle kann sie anschließend gefahrlos erneut ausführen und
registrieren. `drizzle-kit push` ersetzt weder die Datenbereinigung noch die
Sicherheitsprüfung der Migration.

## Gameday: blockierte Migration auflösen

`0009_gameday_daily_close` behält höchstens ein nicht widerrufenes Backup pro
Terminal und Luxemburger Kalendertag. Geschützte Backups und das neueste Backup
eines Terminals werden nicht stillschweigend gelöscht.

**Abbruchfälle:**
- Mehrere Backups desselben Terminals/Tages besitzen geschützte
  Wiederherstellungsfreigaben: `Multiple restore-protected backups ...`.
- Ein älteres geschütztes Backup und das neueste Backup des Terminals teilen
  denselben Tag: der eindeutige Index `terminal_config_backups_daily_unique`
  kann nicht erstellt werden. Auch dies ist ein absichtlicher Sicherheitsabbruch.

Drizzle rollt die Migration zurück; `0009` wird nicht als erfolgreich
eingetragen. Der initContainer scheitert und der neue API-Server startet nicht.
Beim bestehenden RollingUpdate (eine Replik, standardmäßig
`maxUnavailable: 0`, `maxSurge: 1`) läuft der gesunde alte Server weiter.
Keinen Neustart oder eine Löschung des alten Pods erzwingen.

### 1. Konflikte und zugehörige Freigaben ermitteln

Diese reine Leseabfrage funktioniert auch vor `0009`, wenn `backup_date` wegen
des Rollbacks noch nicht existiert. Sie zeigt keine verschlüsselten Inhalte
oder Schlüssel. Sie zeigt alle doppelten Tage; relevant sind insbesondere
mehrere geschützte Backups oder ein geschütztes älteres plus das neueste Backup.

```sql
WITH dated AS (
  SELECT b.id, b.terminal_id, b.created_at,
         (b.created_at AT TIME ZONE 'Europe/Luxembourg')::date AS backup_day,
         b.id = (
           SELECT newest.id FROM terminal_config_backups newest
           WHERE newest.terminal_id = b.terminal_id AND newest.revoked_at IS NULL
           ORDER BY newest.created_at DESC, newest.id DESC LIMIT 1
         ) AS is_latest
  FROM terminal_config_backups b WHERE b.revoked_at IS NULL
), duplicate_days AS (
  SELECT terminal_id, backup_day FROM dated
  GROUP BY terminal_id, backup_day HAVING count(*) > 1
)
SELECT b.terminal_id, b.backup_day, b.id AS backup_id, b.created_at,
       b.is_latest, a.id AS authorization_id, a.target_terminal_id,
       a.used_at, a.expires_at
FROM dated b
JOIN duplicate_days d USING (terminal_id, backup_day)
LEFT JOIN terminal_restore_authorizations a ON a.backup_id = b.id
  AND a.revoked_at IS NULL AND (a.used_at IS NULL OR a.expires_at > now())
ORDER BY b.terminal_id, b.backup_day, b.created_at DESC, b.id DESC, a.id;
```

**Genau dieselbe Schutzregel wie in der Migration:** Eine nicht widerrufene
Freigabe schützt das Backup, wenn `used_at IS NULL` **oder** `expires_at > now()`.
Eine unbenutzte Freigabe bleibt damit auch nach ihrem Ablauf geschützt, bis sie
explizit widerrufen wird. Nicht nur auf `expires_at` prüfen.

### 2. Bewusst entscheiden, welches Backup erhalten bleibt

Pro betroffenem Terminal/Tag genau ein Backup auswählen. Gehört das neueste
Backup des Terminals zu diesem Tag, muss dieses erhalten bleiben; andernfalls
würde die Schutzregel für das neueste Backup den Index weiterhin blockieren.
Für **alle anderen Backups dieses Tages** müssen **sämtliche** noch schützenden
Freigaben aus Schritt 1 widerrufen werden — nicht nur eine davon.

Vor dem Widerruf mit dem verantwortlichen Administrator und gegebenenfalls dem
Ersatzterminal-Betreiber bestätigen, dass diese Wiederherstellungen nicht mehr
benötigt werden. Ein Widerruf macht die betroffenen Wiederherstellungscodes
unbrauchbar. Keinesfalls Freigaben anderer Tage/Terminals pauschal widerrufen.
Das gewählte Backup und seine Freigaben unverändert lassen.

### 3. Nur die bestätigten Freigaben widerrufen und erneut prüfen

In einer administrativen DB-Sitzung ausführen. Die benannten SQL-Parameter
stehen für die zuvor geprüften Werte; je nach Client als gebundene Parameter
übergeben oder ausdrücklich durch die bestätigten Werte ersetzen:

```sql
BEGIN;
LOCK TABLE terminal_config_backups, terminal_restore_authorizations
  IN SHARE ROW EXCLUSIVE MODE;
UPDATE terminal_restore_authorizations a
SET revoked_at = now()
FROM terminal_config_backups b
WHERE a.backup_id = b.id
  AND b.revoked_at IS NULL
  AND b.terminal_id = :terminal_id
  AND (b.created_at AT TIME ZONE 'Europe/Luxembourg')::date = :backup_day
  AND b.id <> :keep_backup_id
  AND a.id IN (:confirmed_authorization_ids)
  AND a.revoked_at IS NULL
  AND (a.used_at IS NULL OR a.expires_at > now())
RETURNING a.id, a.backup_id;
-- Vor COMMIT prüfen: nur die ausdrücklich bestätigten IDs wurden geändert.
COMMIT;
```

Schritt 1 erneut ausführen und sicherstellen, dass nur das ausgewählte Backup
noch geschützt ist. **Keine Backups manuell löschen und keine Sicherheitsprüfung
oder den eindeutigen Index entfernen.** Anschließend den blockierten Rollout
wiederholen beziehungsweise den initContainer erneut versuchen lassen. Die
Migration entfernt die nun ungeschützten redundanten Backups selbst und
trägt `0009` erst nach vollständigem Erfolg im Drizzle-Journal ein.

## Lokaler Regressionstest

`pnpm --filter @workspace/db test:migrations` erstellt ausschließlich eigene,
leere Testdatenbanken und entfernt sie danach wieder. Die Entwicklungsdatenbank
und ihre Daten bleiben unverändert. Der lokale DB-Benutzer benötigt dafür
`CREATEDB`. Der Test prüft die gesamte Kette, Wiederholung, bereits angewendete
Gameday-SQL, Schutzabbrüche samt Rollback und den Abgleich mit dem Drizzle-Schema.
Der API-Build muss zuvor `artifacts/api-server/dist/migrate.mjs` erzeugt haben;
dieser tatsächliche initContainer-Einstiegspunkt wird ebenfalls getestet.

---

## modus enum – CUSTOM_4 (applied 2026-07-31)

**Why:** The `modus` PostgreSQL enum was missing `CUSTOM_4` and contained two removed values
(`HARAKIRI_DELAYED`, `HARAKIRI_FULL`).  The sync route already validates against the new set,
so any `CUSTOM_4` game arriving before the DB migration would cause an insert failure.

**What was changed in code:**
- `lib/db/src/schema/spiele.ts` – removed `HARAKIRI_DELAYED` and `HARAKIRI_FULL`; added `CUSTOM_4`

**SQL to apply (idempotent, additive – safe on a live DB):**
```sql
ALTER TYPE modus ADD VALUE IF NOT EXISTS 'CUSTOM_4';
```

> **Note:** Postgres `ALTER TYPE … ADD VALUE` commits immediately and cannot be run inside a
> transaction block.  Run it directly via `psql` or a plain client connection before deploying
> the new API image.

**Removed values (`HARAKIRI_DELAYED`, `HARAKIRI_FULL`):**
Existing rows that still carry these values will continue to read back correctly — Postgres
retains enum labels that are stored in live rows even after the Drizzle schema no longer
lists them.  No `UPDATE` or backfill is required unless you want to normalise old data.
If a future migration needs to drop those labels, do so only after confirming no rows reference
them:
```sql
SELECT COUNT(*) FROM spiele WHERE modus IN ('HARAKIRI_DELAYED', 'HARAKIRI_FULL');
```
