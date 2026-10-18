# Spielabbruch: VOID_USE statt kompensierendem GRANT

## Deployment

`0014_void_credit_use` läuft automatisch beim Deployment über die Drizzle-Migration
im bestehenden initContainer. Keine DDL im API-Start und kein manuelles Produktions-SQL.
Zuerst Portal/API mitsamt Migration ausrollen, **danach** die Terminal-Firmware.
Aktive Spiele vor dem Firmware-Upgrade regulär beenden oder stornieren und
synchronisieren; nicht mitten in einem laufenden Spiel aktualisieren.
Die neue Firmware benötigt `POST /api/sync/kredite/state`; sie akzeptiert keine
alte Antwort ohne konsistente Ereignisbestätigungen.

Vor Aktivierung auch ältere noch ausstehende Migrationen prüfen. Insbesondere
`0013_gameday_queue_eligibility` aktiviert die bereits vorbereitete
Tagesanmeldungsprüfung. Fehlende Tagesanmeldungen können bestehende offene
Warteschlangeneinträge beim nächsten Abgleich ungültig machen. Die Kreditmigration
erteilt keine Anmeldungen und schreibt bestehende Warteschlangeneinträge nicht um.

## Ledger und Abrechnung

Ein Storno referenziert eine einzelne `USE` desselben Spielers und Tags:

```json
{"externalId":"kre-void-unique","typ":"VOID_USE","voidsExternalId":"kre-original-use","spielerId":42,"datum":"2026-10-18","anzahl":1}
```

Keine Preis-/Belegfelder bei `VOID_USE`. Empfang vor USE, im selben Batch und
wiederholte Zustellung sind zulässig. Auch eine weitere Storno-ID für denselben
USE wird ohne zweite Wirkung bestätigt. Abweichende Bindung und geänderte
Wiederholungen werden abgewiesen; DB-Trigger schützt auch andere Schreibpfade.

Alle finanziellen Lesepfade verwenden `v_effective_kredit_events` direkt oder die
gemeinsame Sichtbarkeitsbedingung: gewährte Kredite bleiben unverändert, stornierte
USE fehlen in Verbrauch, verfügbarer Menge, Tages-/Jahresübersicht, Käufen,
Tagesabschluss, Tages-/Periodenabrechnung und Queue-Guthabenprüfung.
Rohdaten bleiben für Audit und Wiederholungsprüfung bestehen.
Spieljournal, Ergebnisse, Schusszähler, Verkäufe und Zahlungsbelege werden nicht umgeschrieben.

## Terminal und Neustart

Nie gesendete USE werden entfernt. Für möglicherweise empfangene USE bleibt der
originale Datensatz zur Wiederholung erhalten und ein dauerhaftes VOID wird
angefügt. Ein gesetztes `mayBeAccepted` wird nach fehlgeschlagenem ACK oder Neustart
nicht zurückgenommen. Bereits bestätigte aktive USE-Belege bleiben bis Ende oder
Abbruch erhalten, insbesondere für das ursprüngliche Datum bei Mitternachtswechsel.
Die Kapazität wird vor allen Spieleränderungen geprüft; Spielstart reserviert
zusätzliche Storno-Plätze.

NVS enthält Count, Datensätze und zuletzt stornierte Spiel-ID in **einem**
versionierten Blob. Ältere Outbox-Layouts werden verlustfrei gelesen. Unbekannte
oder unlesbare Outbox bleibt unverändert gespeichert; neue Spiele und Zahlung
werden blockiert. Kein NVS-Erase, keine FAT-Formatierung, keine Änderung an
FinishedGame, Journal-Record oder Partitionen.
Die Storno-ID verhindert Wiederaufnahme nach Stromausfall zwischen Outbox-Commit
und Active-Checkpoint-Tombstone. Ein alter Count-Schlüssel ist für das neue
selbstbeschreibende Format nicht maßgeblich.

Credit-Pull liefert Beträge und die angefragten bekannten/entwerteten/noch
abrechenbaren USE-IDs aus **derselben Repeatable-Read-Transaktion**. Noch nicht
berücksichtigte lokale Ereignisse werden genau einmal überlagert. Auch ein
bereits bezahlter USE wird nicht nochmals von einem Null-Baseline abgezogen.
Ein gecachter Rechnungsbetrag ist während offener Kreditereignisse nicht
autoritativ/zahlbar; erst nach Annahme und erneutem Rechnungsabruf gilt die neue
Portalabrechnung.

Vor einem Firmware-Downgrade alle neuen Storno-Ereignisse synchronisieren.
Alte Firmware versteht das neue Outbox-Format nicht; ein Downgrade mit offener
Outbox ist nicht freigegeben.

## Frühere kompensierende GRANTs

Keine automatische Datenkorrektur: ein echter GRANT ist nicht sicher von einer
früheren Erstattung zu unterscheiden. Unter **Guthaben → Alt-Stornierungen prüfen**
erscheinen Hinweise: USE und anschließender GRANT innerhalb von 30 Minuten,
gleicher Spieler/Tag, kein gespeichertes Spiel an diesem Tag. Auch ausgeblendete
Spiele zählen für diesen Gegencheck. Die Liste ist ein Hinweis, kein Beweis.
Nach menschlicher Prüfung ausschließlich die normale vorhandene Guthaben-Korrektur
verwenden. Alle ursprünglichen Zeilen bleiben erhalten.
API: `GET /api/admin/kredite/legacy-cancel-candidates?from=YYYY-MM-DD&to=YYYY-MM-DD`,
optional `minutes=1..1440`, maximal 500 Zeilen pro Abruf.

## Reproduzierbare Softwareprüfungen

```sh
pnpm --filter @workspace/api-server test:credits
pnpm --filter @workspace/api-server test:isolated
python artifacts/firmware/tests/test_credit_cancellation.py
python artifacts/firmware/tests/test_harakiri_players.py
python artifacts/firmware/tests/test_sync_batch.py
python artifacts/firmware/tests/test_game_journal.py
```

Die PostgreSQL-Tests verwenden ausschließlich neue temporäre Datenbanken und
prüfen frische sowie wiederholte Migrationen, HTTP-Sync, beide Ankunftsreihenfolgen,
Idempotenz, Bindungsfehler, drei gemischte Spielerzustände, Abrechnung/Berichte und
unveränderte historische GRANTs. Host-Tests kompilieren die echten Store-Funktionen,
Outbox-Decodierung und Active-Checkpoint-Logik.
Ein echter Netzabbruch/Stromausfall auf physischen Terminals bleibt als
Hardwareabnahme erforderlich; Host-Tests und Target-Build sind kein Hardwarebeleg.
