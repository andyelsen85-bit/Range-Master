# Stand-PC-Zugang: PIN und Gerätefreigaben

## Ausrollen

API und Portal gehören zusammen in dieselbe Veröffentlichung. Nach erfolgreichem
GitHub-Image-Build im Cluster ausführen:

```sh
kubectl apply -k k8s/overlays/production
kubectl -n rangemaster rollout restart deployment/api-server deployment/portal
kubectl -n rangemaster rollout status deployment/api-server
kubectl -n rangemaster rollout status deployment/portal
curl -fsS https://rangemaster.hostzone.lu/api/health
```

Die Images einschließlich `run-migrations` und `run-seed` werden mit
`imagePullPolicy: Always` bezogen. Erst neu starten, wenn der gewünschte Build
vollständig veröffentlicht wurde. Für reproduzierbare Releases alternativ alle
API-/API-Init-Images und das Portal auf denselben veröffentlichten SHA-Tag setzen.
Der Health-Commit muss dem freizugebenden Git-Commit entsprechen.

Der bestehende Init-Container führt `0016_kiosk_device_pin` automatisch aus.
Keine manuelle SQL-Ausführung und kein DDL im API-Start. Die Migration widerruft
historische Zugangsdaten; deren Tabelle und bestehende Audit-Historie bleiben
erhalten, werden aber nicht mehr für Anmeldungen gelesen. Aktuelle Audit-Ereignisse
verwenden eine eigene `device_id`, nie die alte `token_id`.

`PUBLIC_HOST` wird im Produktions-Overlay auf den Ingress-Host gesetzt. Die API
vertraut genau einem Reverse-Proxy-Hop für die Client-IP. Der API-Service darf
nicht direkt aus dem Internet erreichbar sein; bei zusätzlichen Proxys muss die
Ingress-Konfiguration die tatsächliche Client-IP korrekt weitergeben. Keine
ungeprüften Client-Header als vertrauenswürdige Proxy-Kette übernehmen.

## Einmalig am Stand-PC

1. Im Portal als Administrator **Mailserver & Live-Seite → Öffentliche Seite →
   Stand-PC Zugang** öffnen und eine PIN mit 6–32 Ziffern setzen/bestätigen.
2. Im normalen, persistenten Browserprofil des Stand-PCs
   `https://rangemaster.hostzone.lu/live` öffnen und PIN + **OK** eingeben.
3. Browser schließen und mit demselben Profil erneut öffnen. Die Seite muss ohne
   erneute PIN erreichbar sein. Kein Inkognito-, Gast- oder flüchtiges Kioskprofil
   und keine automatische Cookie-Löschung verwenden.

Einzelwiderruf sperrt ein Gerät. PIN-Wechsel und **Alle abmelden** sperren alle
bisherigen Geräte, nicht die Portal-Admin-Sitzung. 60 Sekunden Leerlauf setzen nur
die Auswahl zurück. Terminal-API-Schlüssel sind von dieser Änderung unabhängig.

## Produktionsnachweis

Eine echte Stand-PC-Entsperrung erzeugt das strukturierte Ereignis
`KIOSK_UNLOCKED` mit `origin`, `host`, `forwardedHost` und Geräte-ID. Diese Werte
am echten Gerät prüfen; Entwicklungs-Preview-Header sind kein Produktionsnachweis.
Keine Cookie-Werte oder PINs in Tickets/Screenshots kopieren.

Die API muss `rm_kiosk` mit HttpOnly, Secure, SameSite=Lax, Path=/ und
Max-Age=31536000 senden. Ein zwischengeschalteter Proxy darf die Attribute nicht
abschwächen. Tägliche Verlängerung ist vom 30-Sekunden-Nutzungsstempel getrennt,
damit auch dauerhaft aktive Geräte eine gleitende Laufzeit behalten.

Die Integrationsprüfung `pnpm --filter @workspace/api-server test:kiosk` läuft in
CI vor dem Image-Build. Sie verwendet eine isolierte PostgreSQL-Datenbank und
prüft Vorwärts-/Wiederholungsmigration, Hash-/Cookie-Eigenschaften, PIN-Sperren,
Epochen/Widerrufe, Origin-Prüfung und fachliche Fehlercodes. Echte Stand-PC-Header
und Browser-Neustarts am Stand bleiben zusätzlich vor Ort zu bestätigen.

Bei der Entwicklungsprüfung war der Cookie direkt an Express `SameSite=Lax`,
im vorgeschalteten Preview jedoch `SameSite=None`. Die konkrete Zwischenstelle
wurde nicht bestimmt; die API-Konfiguration wurde nicht abgeschwächt. Eine neue
Browser-Context-Instanz mit gespeicherten Cookies blieb entsperrt; ein echter
Browser-Neustart mit persistentem Profil war mit dem Testwerkzeug nicht verfügbar.
Beides ersetzt ausdrücklich nicht den oben beschriebenen Test am echten Stand-PC.
