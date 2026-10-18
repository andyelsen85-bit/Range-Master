# Aktive und noch nicht übertragene Spiele

Diese Änderung betrifft das echte ESP32-P4-Terminal, nicht den Emulator.
NVS und FAT werden beim Firmware-Update weder gelöscht noch neu formatiert.
Partitionslayout und FinishedGame-/Journalformat bleiben unverändert.

## Dateinamen und Prüfung

Alle FAT-Dateipfade werden mit `store/fat_name.h` erstellt und auf 8.3 geprüft.
Beispiele: `ACTIVE0.BIN` / `ACTIVE0.TMP`, `J000.REC` / `J000.PRT` /
`J000.BAK`, `TMCACHE/ROSTER.BIN` / `ROSTER.TMP` / `ROSTER.BAK`.
FAT vergleicht Groß-/Kleinschreibung unabhängig; bestehende gültige Dateien
bleiben zugänglich. Lange unbekannte Altdateien werden nicht still gelöscht.

Beim Boot prüft das Terminal alle registrierten Namensmuster auf dem echten
Volume mit eigenen reservierten Scratch-Dateien: Erstellen, Schreiben, fsync,
Schließen, Umbenennen, Lesen und Löschen. Echte Slots werden dabei nie
überschrieben. Ein abgebrochener Test darf nur Scratch-Dateien mit exakt
passender Testmarkierung entfernen. Fehler nennen Muster, Schritt und errno
im Terminal-Protokoll und in „Offene Spiele“. Nach einer Bereinigung oder
Reparatur wird die Prüfung erneut ausgeführt.

## Bedienung

Unter **Einstellungen > System > OFFENE SPIELE**:

- Aktives Spiel: Modus, Startzeit, Lauf/Taube, Namen und Punkte.
  **Fortsetzen** verändert weder Position noch Wertungen und löst keine
  Maschine automatisch aus. **Beenden und speichern** benutzt den normalen
  Abschluss-/Journalpfad mit den bisher eingetragenen Wertungen.
  **Verwerfen** verlangt zwei Bestätigungen und benutzt den bestehenden
  Kredit-Stornopfad, niemals eine neue GRANT-Gutschrift.
- Fertige lokale Spiele: Namen, Zeitpunkt, Punkte, lokaler Status und letzter
  Portal-Abgleich. **Jetzt senden** startet den normalen Sync-Worker.
  **Mit Portal abgleichen** entfernt nur vom Portal ausdrücklich bestätigte
  IDs. Ein Export allein ist keine Portalbestätigung.
- Ein fertiges unbestätigtes Spiel darf nur mit dem separaten Speicher-Admin-PIN,
  unveränderter vollständiger JSON-Sicherung und zwei Bestätigungen verworfen
  werden. Der ERROR-Audit enthält seine externe Spiel-ID.

Ein fehlgeschriebener Abschluss-Checkpoint allein sperrt kein neues Spiel.
Das beendete Spiel erhält zusätzlich eine kleine NVS-Abschlussmarkierung;
die letzten vier IDs verhindern Wiederaufnahme alter aktiver Slots.
Abschluss-Checkpoints werden im Hintergrund nach 1/5/30 Sekunden erneut
versucht. Ein tatsächlich noch aktives Spiel oder ein nicht verfügbares
fertiges Spielarchiv wird nicht ungeprüft überschrieben.

Speicherbereinigung nennt offene Kredit-, Verkaufs- und Zahlungsereignisse
mit Anzahl. **Jetzt synchronisieren und dann reparieren** synchronisiert
zunächst und öffnet danach die neu geprüfte Bestätigung. Es formatiert nicht
automatisch. Eine Formatierung bleibt ausschließlich dem ausdrücklich
bestätigten, nachweislich sicheren Reparaturpfad vorbehalten.

## Prüfung am echten Terminal — noch erforderlich

1. Firmware per USB aktualisieren, niemals `erase-flash`. Wenn möglich vorher
   alle Spiele synchronisieren. Keine NVS-/FAT-Löschung durchführen.
2. Boot-Protokoll sichern: alle FAT-Namensmuster müssen den Test bestehen.
3. Harakiri mit zwei Spielern starten und mehrere Wertungen eintragen:
   nach jeder Wertung bleibt der Checkpoint fehlerfrei.
4. Mitten im Spiel aus-/einschalten. „Offene Spiele“ muss dieselben Namen,
   Punkte, Lauf und Taube zeigen. Fortsetzen darf keine Maschine auslösen.
5. Verwerfen doppelt bestätigen: neues Spiel sofort starten; kein GRANT und
   keine neue Ergebniszeile für das verworfene Spiel.
6. Ein Spiel beenden bzw. teilweise „Beenden und speichern“: normales
   Ergebnis, Journal und Sync prüfen; danach neues Spiel starten.
7. Erst ohne offene Daten „Speicher bereinigen“ bestätigen. Nach dem nächsten
   erfolgreichen vollständigen Sync müssen in TMCACHE wieder ROSTER.BIN,
   PRODUCTS.BIN, HISTORY.BIN, CREDITS.BIN, SALES.BIN, BILLS.BIN und META.BIN
   vorhanden sein. Pfadtest, Protokoll und Portalbestätigungen kontrollieren.

Hosttests und ein nativer Build ersetzen diese Hardwareprüfung nicht.
