# Zwei Fernbedienungen mit ID '1': Wiederherstellung ohne Relais-Update

Diese Anleitung ändert **nur Gateway und beide Fernbedienungen**. Relais und
Terminal brauchen für diese Identitätskorrektur kein Firmware-Update.
Der Handy-Maschinentest ist eine separate Funktion mit eigenem Updateumfang.

## Sicherheitsgrenze

Beide Fernbedienungen haben möglicherweise identische AES-GCM-Nonces mit
dem bisherigen gemeinsamen Schlüssel gesendet. Die folgende Trennung verwendet
einen **frischen, ausschließlich für Fernbedienungen bestimmten Schlüssel**
und verhindert weitere solche Kollisionen. Sie macht vergangene Nonce-
Wiederverwendung nicht rückgängig. Den Relais-Schlüssel ebenfalls wechseln,
sobald alle Relais zugänglich sind. Nicht als vollständige rückwirkende
Sicherheitsbereinigung verstehen.

Vor Beginn beide Fernbedienungen außer Betrieb nehmen. Wurfmaschinen nach dem
Sicherheitsverfahren des Standes gegen unbeabsichtigte Auslösung sichern.
Für ACK-Tests den Fernbedienungsempfang an allen Terminals ausschalten; ein
Gateway-ACK benötigt kein aktives Terminal. Keine Schussprobe ohne freien,
abgesicherten Gefahrenbereich.

## Was unverändert bleibt

- `TM_PROTOCOL_KEY_BYTES`: bisheriger AES-Schlüssel für Gateway ↔ Relais.
- `TM_GATEWAY_AUTH_KEY`: vorhandener HMAC-Schlüssel für Terminal ↔ Gateway.
- Senderzähler, Gateway-Relaiszähler, WLAN-Konfiguration und Handy-PIN.
- Die private GPIO-Konfiguration jeder Fernbedienung.
- Sämtliche NVS-Daten: **kein erase-flash, kein NVS-Reset**.

## Schritte

1. Einen neuen zufälligen 128-Bit-AES-Schlüssel lokal erzeugen. Er muss
   verschieden vom bisherigen Relais-Schlüssel und bisher unbenutzt sein.
   Nicht in Chat, Git oder Diagnoseprotokolle kopieren.
2. In den **privaten, ignorierten** lokalen Konfigurationen des Gateways und
   **beider** Fernbedienungen dieselben neuen 16 Bytes eintragen:

   ```cpp
   #define TM_REMOTE_KEY_BYTES { /* genau 16 frische private Bytewerte */ }
   ```

   Die vorhandenen `TM_PROTOCOL_KEY_BYTES` bleiben auf allen drei Geräten
   **unverändert**. Das Beispiel ist kein verwendbarer Schlüssel.
3. Gateway mit diesem Stand bauen und normal flashen, ohne Löschen des Flash.
   Es verwendet den neuen Schlüssel ausschließlich für Fernbedienungen und
   quittiert **keine** Fernbedienungsframes mehr mit dem alten Relais-Schlüssel.
   Bestehende Relais-FIRE/ACK-Frames bleiben bytegleich und verwenden weiter
   den alten Schlüssel. Der Gateway hält Remote-Replay-Zähler pro Schlüssel
   separat; alte Zähler werden aufbewahrt, nicht gelöscht.
4. Erste Fernbedienung über `TrapMasterRemote1/TrapMasterRemote1.ino` (ID `'1'`)
   bauen und normal flashen. **Kein** Migrationsdefine auf dieser Fernbedienung.
   Ihr vorhandener Senderzähler bleibt bestehen.
5. Zweite Fernbedienung über `TrapMasterRemote2/TrapMasterRemote2.ino` (ID `'2'`)
   bauen. Ausschließlich in deren privater `TrapMasterRemote.local.h` zusätzlich:

   ```cpp
   #define TM_REMOTE_MIGRATE_FROM_ID '1'
   ```

   Normal flashen, ohne Flash/NVS zu löschen. Beim ersten Boot darf sie die
   gespeicherte ID nur ändern, wenn die alte ID tatsächlich `'1'` ist und der
   aktuelle Remote-Schlüssel vom gespeicherten bisherigen Schlüssel abweicht.
   Identität und Schlüsselfingerprint werden **als ein NVS-Blob atomar**
   gespeichert. Der vorhandene Senderzähler wird weder gelöscht noch reduziert.
6. Serial Monitor mit **115200 Baud** prüfen:

   ```text
   Remote identity migrated 1 -> 2 with new remote key; counter retained
   ```

   Weitere Starts mit derselben ID sind normal; die Meldung erscheint nur
   beim eigentlichen Wechsel. Danach das Migrationsdefine entfernen und
   optional nochmals normal flashen. Beide Fernbedienungen neu starten und
   nacheinander prüfen, dass der Gateway getrennte Sender 1 und 2 erkennt.
   **ACK ist nur Empfang, keine Bestätigung einer Wurfmaschinenauslösung.**

**Falls „ID gesperrt“ erscheint:** nicht weiter experimentieren, nicht löschen.
Im USB-Log steht `Remote identity blocked: reason=...`:

| Grund | Bedeutung |
|---|---|
| 2 | gespeicherte ID passt nicht und ist nicht ausdrücklich zur Migration freigegeben |
| 3 | der Remote-Schlüssel wurde gegenüber der gespeicherten Identität nicht erneuert |
| 4 | Fingerprint-/NVS-Speicherung fehlgeschlagen; keine Freigabe zum Senden |
| 5 | Identitätsdaten ungültig oder nicht eindeutig lesbar |

Ein Key-Fingerprint ist kein Schlüssel. Er dient nur dem sicheren Vergleich;
die Firmware protokolliert keine privaten Schlüsselwerte.

## Rückwärtskompatibilität und Rückbau

Ohne `TM_REMOTE_KEY_BYTES` bleibt das alte Radioverhalten erhalten. Diese
Kompatibilität ist **nicht** der Wiederherstellungsweg für doppelte IDs.
Mit separatem Schlüssel gibt es bewusst keinen automatischen Rückfall auf
den alten Schlüssel für Fernbedienungsnachrichten.

Nicht auf alte Fernbedienungsfirmware zurückgehen und keine gespeicherten
Identitäts-/Replay-Daten entfernen. Unterschiedliche Keys auf Gateway und
Fernbedienung ergeben „keine Antwort“; das ist absichtlich keine Aufforderung,
Zähler zurückzusetzen.
