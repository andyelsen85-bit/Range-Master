# Rangemaster: Kiosk, Warteschlange und eigene Ergebnisse

## Inbetriebnahme

1. API und Portal zusammen ausrollen. Die vorhandene API-Init-Container-Migration
   führt `0012_queue_kiosk_live.sql` automatisch aus. Keine SQL-Datei von Hand
   einspielen und kein `drizzle-kit push` im Serverstart verwenden.
2. Als Administrator **Mailserver & Live-Seite** öffnen. Live-Seite und bei Bedarf
   Warteschlange, Gruppen und Top 15 einschalten. Erlaubte Spielmodi und maximale
   Zahl der wartenden Spiele festlegen; neue Installationen erlauben zunächst Normal.
3. Unter **Kiosk-Zugänge** einen Zugang pro Stand-PC anlegen. Die vollständige URL
   und der geheime Token werden nur einmal angezeigt. Die URL auf dem betreffenden
   PC öffnen. `/live` speichert den Token lokal als `rangemaster-kiosk` und entfernt
   ihn sofort aus der Adresszeile. Ein Browserprofil pro Stand-PC verwenden.
4. Optional Chromium/Chrome im Kioskmodus starten:

   ```sh
   chromium --kiosk --no-first-run 'https://<eigener-portal-host>/live'
   ```

   Das Profil zuvor einmal mit der im Portal erzeugten geheimen URL einrichten.
   Tokens nicht in gemeinsam genutzte Startskripte, Screenshots oder Tickets kopieren.
   Für zuverlässigen Betrieb Standby/Bildschirmsperre am Stand-PC konfigurieren.

Die Seite ist für 1920 × 1080 und Touchbedienung ausgelegt. Links stehen laufendes
und letztes Spiel, rechts die Warteschlange. Laufendes Spiel, letztes Spiel und
Warteschlange werden unabhängig alle 3, 10 und 5 Sekunden abgefragt. Such-/Gruppen-
Hilfsdaten aktualisieren sich alle 10 Sekunden. Nach 60 Sekunden ohne Bedienung
setzt sich nur die Bedienoberfläche zurück; bestehende Anmeldungen bleiben erhalten.
Ohne laufendes Spiel kann optional die Top-15-Rangliste erscheinen.

## Anmeldung und Gruppen

- Spieler suchen, Modus auswählen und anmelden. Ein Spieler kann nur eine aktive
  Anmeldung haben, auch wenn sein Spiel bereits am Terminal läuft.
- Einzelspieler werden in das erste offene Spiel desselben Modus mit freiem Platz
  aufgenommen. Daher können sie eine noch nicht passende Gruppe überholen.
- Gruppen enthalten 2–6 Spieler; maximal 20 gespeicherte Gruppen. Eine Gruppe kommt
  vollständig in dasselbe Spiel und wird **nicht aufgeteilt**. Die Auswahl und
  Bearbeitung ist nur bei freigeschalteter Gruppenfunktion verfügbar.
- Pro Spiel sind höchstens sechs Spieler, insgesamt höchstens 40 aktive Anmeldungen
  erlaubt. Der konfigurierte Spiele-Höchstwert gilt zusätzlich.
- Abmelden ist nur bei `OPEN` möglich. `LOCKED` ist am Terminal reserviert; nach
  `STARTED` verschwindet das Spiel von der Webseite. Nach dem abgeschlossenen
  Spielsync werden seine Anmeldungen abgeschlossen.
- Ungestartete Anmeldungen verfallen nach zwölf Stunden. Nicht gestartete
  Reservierungen werden nach 15 Minuten freigegeben. Queue-Audit: 90 Tage.

## Terminal

Die neue Firmware zeigt bei aktivierter Warteschlange auf **Spiel aufsetzen** und
**Spieler des Tages** die Schaltfläche **Warteschlange (n)**. Offline ist sie
deaktiviert. Abruf erfolgt nach einem Sync und auf Anforderung; unveränderte
Versionswerte bauen die Anzeige nicht neu auf.

Übernehmen ist immer freiwillig. Eine vorhandene Aufstellung kann ersetzt oder
ergänzt werden. Ergänzen erhält bereits belegte Startpositionen. Ein Spieler, der
lokal fehlt oder inaktiv ist, wird mit **Spieler noch nicht synchronisiert**
abgelehnt: zuerst synchronisieren, dann erneut übernehmen. Es gibt keinen
automatischen Spielstart und keine automatische Kreditbelastung durch die Queue.

Die Reservierung sowie unveränderliche STARTED-/RELEASE-Ereignisse werden in zwei
kleinen wechselnden, prüfsummengeschützten Dateien gespeichert. Zusammen benötigen
sie höchstens zwei FAT-Cluster; die Datensätze sind jeweils kleiner als 1 KiB.
Nach Stromausfall wird der letzte gültige Stand geladen. Verlorene Bestätigungen
führen zur idempotenten Wiederholung desselben Ereignisses. Ist der Speicher
beschädigt oder die begrenzte Ereigniswarteschlange voll, wird die betreffende
Queue-Aktion sichtbar verweigert; die manuelle Spielaufstellung bleibt verfügbar.

Der Live-Sender ist ein separater, niedrig priorisierter Zusatzkanal. Er sendet
nur den neuesten RAM-Stand, höchstens einmal je 1,5 Sekunden, mit zwei Sekunden
HTTP-Zeitlimit. Bei Offlinebetrieb, Hauptsync oder FIRE-Belegung wird übersprungen.
Es gibt keine Live-Nutzdaten-Outbox und keine Wiederholung alter Zwischenstände.
Ein abgeschlossener Spielsync entfernt den Live-Stand ebenfalls.

**Kein `erase-flash` oder Löschen von NVS erforderlich.** Die
Partitionstabelle und das bestehende Spieljournalformat bleiben unverändert.
Die gesonderte Aktion **Speicher reparieren** darf bei verwaisten Clustern nur
nach Nachweis, dass keine offenen Spiele/Queue-Ereignisse oder ungeprüften Daten
existieren, ein leeres Arbeitsvolumen neu formatieren. Vor einem
Firmwarewechsel wie üblich alle Spiele synchronisieren; die vorhandenen
USB-/OTA-Flashanweisungen und Hinweise zum Partitionstabellenwechsel beachten.

## Eigene Ergebnisse drucken

Spieler wählen → **Meine Ergebnisse drucken** → Tag → Vorschau → **Drucken**.
Die Tagesauswahl enthält alle Tage mit sichtbaren Ergebnissen, standardmäßig heute.
Die Vorschau bietet A4 und **Bon (80 mm)**. Drucker, Papierformat und Skalierung
im Browserdialog passend zum Gerät wählen; stilles Drucken ist nicht vorausgesetzt.
Browser-Kopf-/Fußzeilen bei Bedarf ausschalten.

Scorecards verwenden dieselbe Darstellung wie die Portal-Ergebnisse. Tagesstatistik:
Spiele, Schüsse, Treffer, Trefferquote, bestes Spiel, Durchschnitt, Maschinen und
Taubennummern; bei mehreren Modi auch die Modusübersicht. Balken haben lesbare
Zahlen und ein schwarzweiß druckbares Muster. Andere Teilnehmer werden bereits
auf dem Server als „Spieler 2“, „Spieler 3“ usw. anonymisiert; ihre IDs fehlen.

## Spiele administrieren

**Spiele verwalten** ist nur für Administratoren erreichbar: 50 Einträge je Seite,
Filter nach Datum, Modus, Spieler, Sichtbarkeit und Text. Im Detail lassen sich
ganze Spiele oder einzelne Teilnehmer verstecken/einblenden und Schüsse korrigieren.
Vorher/Nachher wird mit der Änderung in derselben Transaktion protokolliert;
Aufbewahrung 365 Tage. Punkte werden mit derselben Funktion wie beim Sync berechnet.

**Dies betrifft nur Statistik und Anzeige.** Tagesabschluss, Spielzähler, Tauben,
`confirmed_launches`, Kredite, Rechnungen, Verkäufe, Inventar und Terminaljournal
bleiben unverändert. Es wird kein Spiel gelöscht. Versteckte Teilnehmer fehlen
auch in den Scorecards anderer Spieler. Administratoren sehen weiterhin alles.
Ein erneuter Terminalsync überschreibt keine Korrektur und blendet nichts ein.

## Sicherheit und Fehlerbehebung

- Alle `/api/public/*`-Aufrufe verlangen `X-Kiosk-Token`, auch Ergebnisse und Druck.
  Token: 32 Zufallsbytes; gespeichert wird nur SHA-256. Metadaten zeigen niemals
  den Token. Widerrufen beendet den Zugang; Erneuern widerruft den bisherigen.
- Live-Seite aus oder kein aktiver Datenbank-Token: **404**. Ansonsten fehlender/
  falscher Token: **401**. Browser löscht bei 401/403 seinen Kiosk-Token und zeigt
  die Zugangshinweise. Das meldet keinen gleichzeitig angemeldeten Administrator ab.
- Same-Origin, keine öffentliche CORS-Freigabe, `Cache-Control: no-store`.
  Pro Token/IP: 60 Lesezugriffe, 10 Schreibzugriffe, zusätzlich sechs Druckabrufe
  pro Minute. Bei 429 kurz warten, nicht einen neuen Zugang erzeugen.
- Terminal-Queue und Live-Uploads benötigen einen aktiven API-Schlüssel vom Typ
  `TERMINAL`; Reservierungen sind zusätzlich an Schlüssel **und Terminal-ID** gebunden.
- Live-Stände älter als 120 Sekunden oder mit FINISHED/ABORTED erscheinen nicht.
  Verwaiste Live-Daten werden nach 24 Stunden bereinigt.
- Bei „Zugang erforderlich“ neuen Kioskzugang anlegen/erneuern und die einmalige URL
  auf diesem Browserprofil öffnen. Bei „Spieler noch nicht synchronisiert“ Terminal
  synchronisieren. Bei Queue-Konflikt erneut abrufen; manuelles Aufstellen bleibt möglich.

Offene Produktentscheidungen und technische Abweichungen stehen in
[queue-kiosk-implementation.md](queue-kiosk-implementation.md).
