# Punktesummen über mehrere Läufe

`0015_round_participations` läuft automatisch beim Deployment über die
Drizzle-Migration im bestehenden API-initContainer. Nicht manuell einspielen.
Es gibt keine DDL beim Start des API-Servers und keine Änderung am Tabellenschema.

Die Migration ergänzt Teilnahmen pro Spieler und Lauf aus den Ergebnissen,
berechnet deren Punkte ohne wiederholte Versuche und übernimmt den Posten der
ersten Taube des jeweiligen Laufs. Fehlende/noch nicht geschossene Läufe bekommen
0 Punkte. Spiele ganz ohne Ergebnisdaten behalten ihre bisherigen Summen; aus
einer alten Gesamtsumme lassen sich keine verlässlichen Laufwerte rekonstruieren.
Die bestehenden Schutzregeln für deaktivierte Spieler bleiben wirksam.

Die Ausführung ist atomar und idempotent. Schreibzugriffe auf Ergebnisse und
Teilnahmen warten während der Korrektur; lesende Zugriffe bleiben möglich.
Ausgeblendete Spiele und Spieler bleiben ausgeblendet. Spielstatus,
`confirmed_launches`, Statistikausschlüsse, Admin-Korrekturen, Tagesabschluss,
Credits, Preise, Verkäufe, Zahlungen und Zähler werden nicht geändert.

## Nachweis und Produktionsanzahl

Der initContainer protokolliert:

```
[migrate] ROUND_TOTAL_REPAIR games=… players=… player_games=… round_rows=…
```

Jeder korrigierte Spieler eines Spiels erhält einen dauerhaften
`admin_game_edits`-Eintrag mit Aktion `REPAIR_ROUND_TOTALS`, System-Akteur
`admin_user_id=0`, ursprünglichen/neuen Laufwerten und Gesamtsummen.
Der Eintrag ist auch in der vorhandenen Admin-Spiel-Detailansicht enthalten.
Unveränderte Spiele erhalten keinen Eintrag, auch nicht bei erneuter Ausführung.

Nach dem Deployment lässt sich der tatsächliche Produktionsumfang mit diesen
**ausschließlich lesenden** Abfragen feststellen (nicht die Migration manuell
ausführen):

```sql
SELECT count(DISTINCT spiel_id) AS repaired_games,
       count(DISTINCT spieler_id) AS repaired_players,
       count(*) AS repaired_player_games,
       count(DISTINCT spiel_id) FILTER
         (WHERE before->'total' IS DISTINCT FROM after->'total') AS changed_total_games
FROM admin_game_edits WHERE action='REPAIR_ROUND_TOTALS';

SELECT a.spiel_id, g.external_id, g.datum, g.modus, g.hidden_at,
       a.spieler_id, a.before->>'total' AS old_total,
       a.after->>'total' AS new_total, a.before->'rounds' AS old_rounds,
       a.after->'rounds' AS new_rounds
FROM admin_game_edits a JOIN spiele g ON g.id=a.spiel_id
WHERE a.action='REPAIR_ROUND_TOTALS'
ORDER BY g.datum, a.spiel_id, a.spieler_id;
```

`repaired_games` enthält auch reine Startposten-Korrekturen;
`changed_total_games` zählt ausschließlich Spiele mit geänderter Gesamtsumme.
Ohne Zugriff auf die externe Produktionsdatenbank ist diese Zahl nicht aus
Entwicklungsdaten oder Test-Fixtures ableitbar.

## Terminal-Kompatibilität

Das vorhandene binäre `PendingGame`/`FinishedGame`-Format bleibt unverändert:
7052 Bytes pro FinishedGame bzw. 7072 Bytes pro Journal-Record.
Erst beim Serialisieren werden aus dem Ergebnis-Ledger bis zu
`MAX_SPIELER * 2` Teilnahmen. Ein Download faltet die Laufwerte pro Spieler
wieder zu sechs Gesamtsummen zusammen, statt nach sechs Laufzeilen abzubrechen.
Die Scorecard verwendet bei vorhandenem Ledger dessen vollständige Laufwerte.
Aggregatsummen sind nur der Rückfall für alte Spiele ohne Ergebnisse.

Der Server normalisiert sowohl ältere Terminal-Payloads (eine letzte Teilnahme)
als auch neue (eine je Lauf) vor der unveränderlichen Replay-Prüfung.
Die Reihenfolge der Ergebniszeilen beeinflusst die Normalisierung nicht.
