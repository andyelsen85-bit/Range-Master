# Handy-Modus (echte Wireless Stick V3)

## Bedienung

1. Beide **physischen Schlüsselschalter sperren**, dann **Skip drei Sekunden**
   gedrückt halten. Die Fernbedienung öffnet `TrapMaster-Remote-1`
   (die letzte Ziffer ist ihre Sender-ID). Dieselbe Geste schaltet wieder aus.
   Die Geste funktioniert auch mit der Tastenmatrix; keine neue GPIO-Belegung.
2. Handy mit diesem **offenen WLAN ohne Internet** verbinden. Captive Portal
   öffnen, andernfalls `http://192.168.4.1/` im Browser aufrufen.
3. Beim ersten Mal eine vierstellige PIN zweimal eingeben. Es gibt keine
   voreingestellte PIN. Danach mit der PIN anmelden.
4. Unter **Steuerung** Schuss und Punkte unabhängig freigeben:
   Start / Wiederholen / Skip und 0 / 1 / 2 Punkte.
   Die Handy-Freigaben sind **unabhängig von den physischen Schlüsseln**.
   Physische Tasten bleiben ausschließlich durch ihre Schlüsselschalter
   freigegeben. Vorhandene Spielregeln gelten auch für das Handy.
5. **Maschinentest** ist eine eigene Seite: vollständige Terminal-ID
   `ESP32-P4-XXXXXXXXXXXX` aus **Einstellungen → Maschinen** am gewünschten
   Terminal eingeben, Testfreigabe
   aktivieren, Maschine A–H wählen und die Sicherheitsabfrage bestätigen.
   Das Terminal muss eingeschaltet und mit seinem Gateway verbunden sein,
   der Fernbedienungsempfang muss eingeschaltet sein. Es darf kein aktives
   (auch kein pausiertes) Spiel geben. Deaktivierte Maschinen werden abgelehnt.
   Der Test verwendet die bestehende Maschinen-Testauslösung des Terminals;
   er ändert keine Punkte, Kredite oder Spielstatistiken.

**Funk-ACK bedeutet nur Gateway-Empfang, niemals bestätigte Maschinenauslösung.**
Ein falsches/offline Terminal oder eine deaktivierte Maschine kann deshalb
trotz Gateway-ACK nicht auslösen. Es gibt bewusst keine automatische Auswahl
aller Terminals und keine gleichzeitige Sammelauslösung.

## PIN, Sitzungen und Abschaltung

- PIN ändern: Seite **PIN**, aktuelle PIN und neue PIN zweimal eingeben.
  Anschließend neu anmelden; alle alten Handy-Freigaben sind ungültig.
- PIN vergessen: **beide Schlüssel gesperrt halten**, beim Einschalten Skip
  gedrückt halten und **zehn Sekunden** nicht loslassen. Das löscht nur die
  Handy-PIN und deren Fehlversuchszähler, niemals LoRa-Schlüssel, Sender-ID
  oder Radio-Zähler. Ein ULP-Aufwachen ist kein PIN-Reset-Neustart.
- Drei falsche Versuche sperren für 30 Sekunden, danach jeweils Verdopplung
  bis maximal 15 Minuten. Der globale Fehlversuchszähler bleibt im NVS;
  Neustart setzt die Sperre nicht zurück. Zehn aufeinanderfolgende Fehler
  schalten das WLAN ab. Nur die physische Drei-Sekunden-Geste schaltet es
  wieder ein; die bestehende Sperrzeit bleibt erhalten.
- Nur eine RAM-Sitzung gleichzeitig; neue Anmeldung ersetzt die alte.
  Cookies: 128 Zufallsbits, HttpOnly, SameSite=Strict. Sitzungen und alle
  Handy-Freigaben enden nach zehn Minuten ohne erfolgreiche Bedienung.
  Statusabfragen zählen nicht als Bedienung.
- Ohne verbundene Station nach zwei Minuten aus, sonst nach zehn Minuten
  ohne erfolgreiche Bedienung. DNS/HTTP und WLAN werden komplett gestoppt.
  Danach gilt wieder der bisherige LoRa-/Schlafbetrieb. Beim normalen Boot
  bleibt WLAN aus; kein zusätzlicher periodischer CPU-Wakeup.
- OLED zeigt „Handy AN“, IP und physische Schlüsselzustände. Serial meldet
  `handy mode on` / `handy mode off (reason)`, niemals PIN oder Sitzungstoken.

**Sicherheitsgrenze:** Offenes WLAN und HTTP sind ausdrücklich gewünscht,
aber nicht vertraulich. Ein Funkbeobachter kann HTTP-PIN/Sitzung mitlesen.
Nur im kontrollierten lokalen Umfeld verwenden, danach abschalten. Die PIN
ersetzt weder Sicherheitsbereich noch mechanische Sicherung/Not-Aus.

## Umsetzung und kompatibles Update

Für die sechs normalen Tasten bleibt das vorhandene verschlüsselte
BTN-Protokoll unverändert: derselbe Sender, derselbe persistierte Zähler,
dieselben gespeicherten Frames und 150-ms-ACK-/Retry-Regeln. Handy und
physische Tasten haben genau einen Radio-Sendepfad. Während einer laufenden
Anfrage werden weitere Tasten nicht vorgemerkt. Sperren einer Handy-Gruppe,
Abmeldung oder WLAN-Aus stoppen weitere Handy-Retries, aber machen eine
bereits gesendete Auslösung nicht rückgängig.

Maschinentests benötigen **Remote, Gateway und Terminal mit diesem Update**.
Dafür gibt es einen getrennten, authentifizierten LoRa-Testframe:
Sender-ID, Maschinenkommando A–H, dauerhafter Zähler und sechs Bytes
P4-eFuse-MAC als Ziel; AES-GCM schützt auch das Ziel. Legacy-Relais-FIRE/ACK
bleiben unverändert; die Relais müssen nicht aktualisiert werden. Alte
Gateways verwerfen neue Testframes; keine Umdeutung in Spieltasten.
Gateway persistiert auch Test-Zähler **vor** ACK, Duplikate erzeugen keine
zweite Aktion. Die bestehende Event-HMAC bindet zusätzlich das Testziel;
Client-/Server-Nonces, Alter, Event-ID und 3-Sekunden-Grenze bleiben bestehen.
Nur das exakt adressierte Terminal nimmt den Test an.

HTTP/DNS/PBKDF2 laufen in einer eigenen niedrig priorisierten Task auf Core 0.
Nur Arduino `loop()` verwendet Radio/SPI/OLED und den Senderzähler.
PBKDF2-HMAC-SHA256 (100.000 Iterationen, zufälliges 16-Byte-Salz) und NVS
laufen nicht in der Radio-IRQ/ACK-Schleife. Nur Salz und Hash werden gespeichert,
kein Klartext-PIN. Defekte PIN-Daten sperren die Anmeldung, statt Setup freizugeben.
AP-only, maximal zwei Stationen, kein WLAN-Client/Router.

## Prüfung

```sh
python3 artifacts/lora-remote/tests/test_phone_policy.py
python3 artifacts/lora-remote/tests/test_phone_http.py
python3 artifacts/lora-remote/tests/test_remote.py
python3 artifacts/lora-common/tests/test_button_protocol.py
python3 artifacts/lora-gateway/tests/test_remote_events.py
python3 artifacts/firmware/tests/test_button_events.py
python3 artifacts/firmware/tests/test_remote_actions.py
```

Native Tests benötigen C++17 und mbedTLS (`pkg-config mbedcrypto`).
Echte S3-Builds müssen beide Schlafmodi prüfen; P4-Terminal und Gateway
gemeinsam bauen. Bench-Builds dürfen keine privaten lokalen Header übernehmen.

Vor Anschluss einer Wurfmaschine: reale Hardware mit getrennten Relais
prüfen — Android/iOS-Captive-Portal, parallele physische/Handy-Bedienung,
langsamer Browser/PIN-Prüfung während LoRa-ACK, 10-Minuten-/2-Minuten-Abschaltung,
Stromaufnahme und ULP-Wakeup. Bei zwei Terminals am Gateway darf nur die
eingetragene Terminal-ID einen Test erhalten. Native Tests und ein erfolgreicher
Build beweisen keine Funklatenz oder Stromaufnahme auf der echten Platine.
