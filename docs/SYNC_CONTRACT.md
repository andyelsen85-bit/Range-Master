# Sync-Vertrag und Produktionsprüfung

`GET /api/health` (nach Ingress-Rewrite auch `/health`) liefert den eingebauten
Git-Commit, Build-Zeitpunkt, den Hinweis auf lokale Änderungen und die tatsächlich
registrierten Sync-Routen einschließlich HTTP-Methode. Die bestehenden
`/healthz`-Probes bleiben unverändert.

Die Docker-Build-Pipeline übergibt `BUILD_COMMIT=${github.sha}`. Dadurch stammt
die Versionsangabe aus dem gebauten Bundle, nicht aus einer später geänderten
Laufzeitvariable. Ein unbekannter/ungebündelter Build ist kein erfolgreicher
Versionsnachweis. Im Admin-Bereich steht „Portal-Version und Sync-Schnittstellen“.

## Vor Veröffentlichung

```
pnpm --filter @workspace/api-server test:sync-contract
```

Der Test erstellt ausschließlich eine eigene temporäre PostgreSQL-Datenbank,
migriert sie und prüft jede `/api/sync/`-Adresse aus dem Firmware-Client mit
gültigem Test-Schlüssel gegen Express. Beide Ingress-Formen werden getestet.
Ein absichtlich entferntes `/kredite/state` dient als negative Kontrolle.
Die GitHub-Image-Builds hängen von diesem Test ab.

## Nach dem Rollout

Den aktiven **Produktions-Terminal-Schlüssel** als Workspace-Secret
`PRODUCTION_TERMINAL_API_KEY` hinterlegen, niemals in Chat, Argumenten oder Git.

```
node scripts/check-production-sync.mjs
```

Der Test prüft `https://rangemaster.hostzone.lu`, erwartet standardmäßig den
aktuellen Git-HEAD und bricht bei fehlender Route, falschem Build oder fehlendem
Baseline-Nachweis ab. `SYNC_PROBE_ORIGIN`, `EXPECTED_BUILD_COMMIT` und
`SYNC_PROBE_DATUM` sind optionale, nicht geheime Parameter.
Die POSTs auf `/kredite` und `/spiele` haben absichtlich ungültige `{}`-Bodies
und müssen 400 liefern; es werden keine Ereignisse oder Spiele erzeugt.
`/kredite/state` wird mit gültigem, rein lesendem Body geprüft. Pro-Spieler-Daten
und Schlüssel werden nicht ausgegeben. Ein 401 ist **kein** erfolgreicher
Routennachweis.

Die Repository-Pipeline baut und veröffentlicht Container-Images, startet aber
keinen Kubernetes-Rollout. Erst nach erfolgreicher Image-Erstellung müssen der
API-initContainer und der API-Server dasselbe neue Commit-Image verwenden; die
bestehende Drizzle-Migration läuft ausschließlich im initContainer. Das Portal
muss ebenfalls das neue Image für die Versionsanzeige verwenden.
Kein Datenbank-DDL im API-Start und kein manuelles Einspielen von Migrationen.
Ein laufender oder erfolgreicher Image-Build allein beweist keinen Rollout.

## Verhalten bei altem Portal

404/405 auf einer Sync-Schnittstelle sind Vertragsfehler:

- Ein HTTP-Versuch, kein dreifacher Netzwerk-Retry.
- ERROR im ESP-Log und Terminal-Protokoll, Kategorie `diagnostic`, inklusive
  Endpoint/Status; wiederholte gleiche Fehler werden höchstens stündlich geloggt.
- Ein klarer erster Fehler im Sync-Status, z. B.
  `Portal-Version veraltet: /sync/kredite/state fehlt`.
- Zehn Minuten Pause je Methode/Endpoint; andere Endpoints bleiben nutzbar.
  Ein neu erkannter Portal-Commit hebt Vertrags-Pausen sofort auf.
- Bei fehlendem `/kredite/state` wird der eigentliche Credit-Upload gar nicht
  ausgeführt. Seine persistierte Outbox bleibt erhalten, inklusive
  Lieferunsicherheit und unveränderlicher Ereignis-IDs.

Der Terminal prüft die Build-Metadaten maximal einmal pro Minute und protokolliert
einen neu erkannten Commit. Alte Portale ohne `/health` erhalten eine begrenzte
Warnung; das verhindert nicht pauschal alle anderen Sync-Funktionen.
Baseline und ACK-Nachweise werden weiterhin aus einem gemeinsamen Portal-Snapshot
übernommen. Nur noch nicht enthaltene lokale Ereignisse werden darübergelegt.
Stornierungen bleiben `VOID_USE` zu einem vorhandenen `USE`, niemals ein neuer
`GRANT`. Historische falsche Ausgleichs-GRANTs werden nicht automatisch gelöscht
oder neu erzeugt; dafür bleibt die vorhandene manuelle Prüfung zuständig.
