# Implementierungs-/PR-Beschreibung: Queue, Kiosk, Adminspiele, Live

## Lieferumfang

Ein gemeinsamer Stand aus Migration `0012`, Backend, OpenAPI/Orval, Portal,
Terminal-Firmware und Tests. Bestehender Init-Container bleibt der einzige
Deployment-DDL-Pfad. Keine Änderung der Partitionen oder des Spieljournalformats.
Bedienungs-/Einrichtungsanleitung: [queue-and-kiosk.md](queue-and-kiosk.md).

## Klassifikation der Spiele-/Teilnahme-/Ergebnisabfragen

Die drei SQL-Views `v_spiele_visible`, `v_teilnahmen_visible`,
`v_ergebnisse_visible` bilden die zentrale Sichtbarkeitsdefinition. Ein Spiel
ohne sichtbare Teilnehmer ist nicht sichtbar. Drizzle-Helfer kapseln dieselbe
View-Mitgliedschaft für vorhandene ORM-Abfragen.

| Bereich / Quelle | Klassifikation und Datenquelle |
|---|---|
| `routes/rangliste.ts`, `routes/rangliste-compute.ts` | Anzeige: nur sichtbare Spiele/Teilnahmen; bestes Einzelspiel, nicht Tagessumme. |
| `routes/statistik.ts`, `lib/player-stats.ts` | Anzeige: alle Statistik-, Verlauf- und Breakdown-Abfragen über sichtbare Daten. Tagesdruck und Portal verwenden dieselbe Statistikfunktion. |
| `routes/spieler.ts` Ergebnisse | Anzeige: zentrale Sichtbarkeitsbedingung auf Ergebnissen. |
| `routes/admin.ts` Spielerlisten/-profile, Ergebnisstatistiken | Anzeige: sichtbare Teilnahmen. |
| `routes/admin.ts` Kredite, Taubenverbrauch, Purge-/Wartungsoperationen | Operativ: Rohdaten/Ledger; kein Sichtbarkeitsfilter. Bestehende Purge-Regeln bleiben bestehen. |
| `lib/game-scorecard.ts` normale Portal-/Public-/Druckscorecards | Anzeige: sichtbare Spiele/Teilnehmer; eigene Ansicht serverseitig anonymisiert. |
| `lib/game-scorecard.ts` explizite Adminansicht | Verwaltungsansicht: Rohdaten einschließlich versteckter Teilnehmer und voller Namen, nur Admin. |
| `routes/gameday.ts` öffentliches letztes Spiel / eigene Scorecardberechtigung | Anzeige: sichtbare Auswahl beziehungsweise sichtbare Teilnahme. |
| `routes/kiosk-queue.ts` letzte Ergebnisse, Drucktage, Druck und Top 15 | Anzeige: sichtbare Views bzw. gemeinsame Statistik-/Scorecard-/Ranking-Funktionen. Spielerpicker/Gruppen lesen aktive Stammdaten, keine Ergebnisaggregate. |
| `routes/managed-games.ts` Liste, Detail, Hide-/Edit-/Audittransaktionen | Verwaltung: bewusst Rohdaten, Adminschutz; Korrektur und Audit atomar. Keine finanziellen Schreiboperationen. |
| `routes/sync.ts` Import, Duplikatprüfung, Historien-/Manifestabgleich | Operativ: alle unveränderten Originaldaten zur vollständigen Synchronisierung/Journalbestätigung. Hide/Unhide wird nie durch Sync zurückgesetzt; bearbeitete Spiele werden mit `EDITED_BY_ADMIN` bestätigt. |
| `routes/terminal-logs.ts` Journal-/Abgleichdaten | Operativ: Rohdaten, keine Filterung durch Statistik-Sichtbarkeit. |
| `lib/bills.ts` | Operativ: unveränderliche Kredit-/Verkaufs-/Abschlussledger und vollständige Spieldaten. |
| `lib/daily-report.ts`, `lib/daily-scheduler.ts` | Operativ: vollständige Spiele, bestätigte Starts, Zähler und Tagesberichte. Hide/Edit beeinflusst weder Bericht noch Berichthash. |
| `lib/queue.ts` Abschlussabgleich | Operative Zustandsverwaltung: vollständige importierte Spiele/Teilnahmen, damit auch versteckte Spiele ihre Anmeldung zuverlässig schließen. |
| `lib/live-game.ts` | Technische, getrennte Momentaufnahme; liest nur das Vorhandensein eines bereits importierten Spiels als Sperre gegen verspätete Live-Pakete. Schreibt weder Spiele noch Ergebnisse/Ledger. |
| `lib/visible-results.ts` | Zentrale View-Mitgliedschaft, keine eigene abweichende Sichtbarkeitslogik. |

Dashboard-/Profil-/Resultater-Komponenten konsumieren die oben genannten
Statistik-/Ranking-/Ergebnisendpunkte und führen keine eigenen SQL-Abfragen aus.

## Gezielte Prüfung der Admin-Spielverwaltung

Der Admin-Menüpunkt **Spiele verwalten** verweist auf `/admin/spiele` und
verwendet `ListChecks`. **Spiele-Abgleich** behält ausschließlich
`GitCompareArrows` als Icon; die irrtümliche zusätzliche Objekteigenschaft wurde
entfernt. Nicht-Administratoren erhalten keinen dieser Menüpunkte.

Reproduzierbarer, isolierter PostgreSQL-Test:

```sh
pnpm --filter @workspace/api-server exec node --import tsx/esm --test ../../lib/db/tests/admin-games.test.mjs
```

Der Test erzeugt eine eigene temporäre Datenbank, führt sämtliche registrierten
Migrationen einschließlich `0012` zweimal aus und prüft unveränderte
Migrationseinträge beim zweiten Durchlauf. Drei Spiele/vier Spieler decken
Datums-, Modus-, Spieler-, Text- und Versteckt-Filter, Hide/Unhide auf beiden
Ebenen, Scorecards/Druck, Statistik/Verlauf, letzte-Spiel-Auswahl einschließlich
vollständig ausgeblendeter Teilnehmer, Schusskorrektur, Audit und erneuten Sync
ab. Alle Admin-Endpunkte müssen Nicht-Administratoren mit 403 abweisen.
Nach jeder Änderung werden die echten Tagesberichte einschließlich Hash,
Finanz-/Inventarledger sowie rohe Spiel-/Taubenzähler mit dem Ausgangszustand
verglichen. Nur die temporäre Testdatenbank wird anschließend entfernt.

Der gezielte Browserlauf im Entwicklungsportal bestätigte zusätzlich den
Menüklick, alle Filter, ausgegraute Hidden-Zustände, beide Hide/Unhide-Ebenen,
Schusskorrektur mit Neuberechnung und Audit nach Neuladen sowie die exakten
Bestätigungstexte aus Abschnitt 6.3. Ein normaler Spieler sieht keine Adminlinks
und erhält 403 bei Listen- und Änderungszugriff. Sämtliche Browserfixtures wurden
entfernt; bestehende Spiele, Gruppen und Einstellungen blieben unangetastet.
Beim Bearbeiten der Datumsfelder traten zwei vorübergehende HTTP-400-Meldungen auf;
der fertig gesetzte Datumsfilter und der weitere Ablauf funktionierten.
Portal-Typecheck und vier bestehende Komponententests waren ebenfalls erfolgreich.

**Produktionsstatus bei der Prüfung am 18.10.2026:** Der im Kubernetes-Ingress
konfigurierte externe Portalhost ist erreichbar. Sein ausgeliefertes JavaScript
enthält die Adminrouten, Korrekturansicht und den vorgeschriebenen
Bestätigungstext, aber noch nicht den neuen Menüeintrag. Der vorhandene
Admin-Anmeldeversuch wurde mit 401 abgewiesen. Daher sind Produktionsjournal
und angemeldete Produktionsabläufe ausdrücklich **nicht bestätigt**; weder
Produktionsspiele noch Produktionsschema wurden verändert. Entwicklungstests
sind kein Nachweis einer bereits erfolgten Kubernetes-Auslieferung.

Für den Produktionsnachweis muss der bestehende Init-Container erfolgreich
durchgelaufen sein. Auf der tatsächlichen Produktionsdatenbank ist anschließend
unter anderem diese reine Leseabfrage möglich:

```sql
SELECT hash, created_at
FROM drizzle.__drizzle_migrations
WHERE created_at = 1792310400000; -- Journalzeitpunkt von 0012
SELECT to_regclass('public.admin_game_edits'),
       to_regclass('public.spiel_spieler_hidden'),
       to_regclass('public.v_spiele_visible'),
       to_regclass('public.v_teilnahmen_visible'),
       to_regclass('public.v_ergebnisse_visible');
```

Keine Migration von Hand einspielen und kein DDL beim API-Start ergänzen:
Auslieferung weiterhin ausschließlich über den vorhandenen Init-Container.

## Abweichungen und widersprüchliche Vorgaben

1. **4-KiB-Live-Nutzlast:** Ein vollständiger Stand für sechs Spieler und bis zu
   200 bereits journalisierten Ergebnissen überschreitet 4 KiB nachweisbar.
   Die Grenze ist deshalb **64 KiB**, mit festen Obergrenzen für Spieler,
   Ergebnisse, Namen und HTTP-Puffer; kein Abschneiden/Teil-JSON.
2. **FINISHED und „BEENDET“-Zwischenanzeige:** Der verlangte Endpunkt liefert bei
   FINISHED, ABORTED und nach 120 Sekunden gleichermaßen `null`. Daraus lässt sich
   kein verlässlicher „BEENDET“-Übergang ableiten. `null` entfernt das laufende
   Panel; ein alter RUNNING-Stand wird nicht weiter eingeblendet. Das letzte
   synchronisierte Spiel erscheint über den getrennten Last-Game-Abruf.
3. **Persistente Sequenz gegenüber „nichts in NVS/FAT“:** Es werden nur zwei kleine
   skalare Metadatenwerte (Sequenz/Startzeit) mit dem vorhandenen Zustand gehalten.
   Live-Nutzdaten selbst bleiben ausschließlich in RAM, ohne Live-Outbox.
4. **HTTP-Verbindungswiederverwendung:** Der Hauptsync besitzt seinen HTTP-Kanal
   exklusiv. Der optionale Sender benutzt deshalb einen separaten kurzlebigen
   Zwei-Sekunden-Kanal, statt Hauptsync oder FIRE zu blockieren.
5. **Journalkapazität:** `MAX_ERGEBNISSE` bleibt aus Kompatibilitätsgründen bei 200.
   Eine Erhöhung würde die binäre Größe vorhandener, möglicherweise unsynchronisierter
   Spieljournaldatensätze verändern und benötigt eine eigene versionierte Migration.
   Der Backend-Vertrag kann größere Stände bis 384 aufnehmen; die Firmware überträgt
   ihre vorhandenen Ergebnisse vollständig, erfindet aber keine zusätzlichen Slots.
6. **Reservierung nach Neustart:** Optionaler `terminalId`-Parameter am Queue-GET
   erlaubt zusätzlich zu OPEN-Spielen ausschließlich die eigene LOCKED-Reservierung.
   Fremde reservierte Spiele werden nicht zur Übernahme angeboten.
7. **Unbekannte lokale Spieler:** Sichtbare Verweigerung statt automatischem Import,
   wie in der Spezifikation ausdrücklich zugelassen.

Nicht stillschweigend entschieden: Gruppen werden standardmäßig **nicht geteilt**;
Drucktage sind standardmäßig **alle sichtbaren Tage**, nicht nur die letzten 30.
Beide bleiben als offene Produktentscheidungen gekennzeichnet.

## Prüfung

```sh
pnpm --filter @workspace/api-spec run codegen
pnpm --filter @workspace/api-server run typecheck
pnpm --filter @workspace/api-server test
pnpm --filter @workspace/api-server test:queue
pnpm --filter @workspace/portal run typecheck
pnpm --filter @workspace/portal test
python artifacts/firmware/tests/test_portal_runtime.py
python artifacts/firmware/tests/test_harakiri_players.py
python artifacts/firmware/tests/test_harakiri_custom.py
python artifacts/firmware/tests/test_remote_actions.py
# Mit eingerichteter ESP-IDF-5.3.5-/ESP32-P4-Umgebung:
idf.py -C artifacts/firmware build
python artifacts/firmware/tests/test_partitions.py <build-verzeichnis>
```

`test:queue` benötigt PostgreSQL-CREATEDB-Rechte. Es erzeugt ausschließlich eine
eindeutige temporäre Testdatenbank, spielt 0000–0012 ein, führt den Migrator erneut
aus, vergleicht Schema-/Migrationsspalten und entfernt nur diese Testdatenbank.
Es verändert keine Entwicklungs-/Produktionsdaten.

Abgedeckt: parallele Doppelanmeldung und Gruppen ohne Aufteilung, First-Fit,
exakte Take-Einträge, Terminalbesitz, Release-/STARTED-Wiederholung, verspätetes
STARTED nach abgeschlossenem Spielsync, Live-Sequenz/Staleness/Isolation,
Tokenrotation/-widerruf und Ratenlimit, keine fremden Namen/IDs in eigenen
Scorecards, Druckstatistikgleichheit, drei Spiele/vier Spieler mit Hide/Unhide
gegen gefilterte Rankingfixture, unveränderte operative Berichte einschließlich
Hash, Admin-Korrekturen und konfliktfreier erneuter Sync.

Host-Firmwaretest: realer JSON-Parser mit ungültigen/duplizierten Daten,
Aufstellungszusammenführung, echte Zwei-Slot-Dateischreib-/Leseoperationen,
Prüfsumme, beschädigter letzter Slot, unveränderliche Ereigniswiederholung,
Live-JSON-Escaping/-Grenzen und Coalescing/Offline-/Sync-/FIRE-Sperren.

Ergebnis des abschließenden Softwarelaufs: 38 API-Tests, vier Portal-Komponententests,
PostgreSQL-Integration und die aufgeführten Firmware-Hosttests erfolgreich;
API-/Portal-Typechecks erfolgreich. ESP32-P4/IDF 5.3.5: vollständiger nativer Build
erfolgreich, App **2.613.536 Bytes**, **728.800 Bytes Reserve je OTA-Slot**.
Partitions-/Bootplan-Test bestätigt unveränderte geschützte Bereiche und `ota_0`.

Ein Playwright-Durchlauf prüfte am echten laufenden Portal 1920×1080:
Tokenübernahme/URL-Bereinigung, Zugang ohne Token, Touch-Tastatur, Anmeldung,
Gruppen, Abmeldung, Tagesdruck, stabilen DOM-/Auswahlzustand bei Aktualisierungen,
60-Sekunden-Rücksetzung, Adminfilter, Hide/Unhide, Schusskorrektur/Audit, Top 15
und Tokenwiderruf. Screenshots: Splitansicht, Tastatur, Gruppeneditor, Druck,
Zugangsseite, Adminliste, korrigiertes Detail, Top 15 und Tokenverwaltung.
Testeigene Fixtures wurden bereinigt; gleichzeitig von Benutzern verwendete
Gruppen/Anmeldungen wurden ausdrücklich nicht gelöscht.

**Noch nicht auf physischer Hardware bewiesen:** echte Stromunterbrechung zwischen
Take/STARTED/ACK, Rückkehr aus Offlinebetrieb, Touchbedienung auf dem Terminal,
FIRE-Latenz unter Funk-/TLS-Last und echter A4-/Bondruck. Native Kompilierung und
Hosttests ersetzen diese Geräteprüfungen nicht. Es wurde nichts geflasht/gelöscht.
