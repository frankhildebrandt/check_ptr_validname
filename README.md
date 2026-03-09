# check_ptr_validname

`check_ptr_validname` ist ein kleiner DNS-Check in C, der fuer eine IP-Adresse prueft, ob der PTR-Eintrag technisch und inhaltlich konsistent ist.

## Kompilieren

Voraussetzungen:
- C-Compiler (`cc`, `gcc` oder `clang`)
- `make`
- POSIX-Umgebung (Linux/macOS; Cross-Compile fuer Windows ist ueber Toolchain moeglich)

Build fuer das Host-System:

```sh
make
```

Build-Konfiguration anzeigen:

```sh
make print-config
```

Artefakt wird unter `build/<os>-<arch>/check_ptr_validname` erzeugt, z. B.:

```text
build/macos-arm64/check_ptr_validname
```

Cross-Compile-Beispiele:

```sh
make OS=linux ARCH=arm64 CROSS=aarch64-linux-gnu-
make OS=windows ARCH=x86_64 CROSS=x86_64-w64-mingw32-
```

Aufraeumen:

```sh
make clean
```

## Nutzung

Grundsyntax:

```sh
./build/<os>-<arch>/check_ptr_validname -i <ip-address>
```

Beispiel:

```sh
./build/macos-arm64/check_ptr_validname -i 8.8.8.8
```

Unterstuetzte Parameter:
- `-i`, `--ip` IP-Adresse (IPv4 oder IPv6)
- `-h`, `--help` Hilfe ausgeben

Exit-Codes (Nagios/Icinga-kompatibel):
- `0` = `OK`
- `1` = `WARNING` (aktuell nicht verwendet)
- `2` = `CRITICAL`
- `3` = `UNKNOWN`

## Checkmoeglichkeiten

Der Check fuehrt folgende Pruefungen aus:

1. Eingabevalidierung der IP (IPv4 oder IPv6).
2. Reverse-DNS-Lookup (`PTR`) fuer die IP.
3. Validierung des PTR-Hostnamens als RFC-naher FQDN:
   - Gesamte Laenge max. 253
   - Label-Laenge 1-63
   - nur `[A-Za-z0-9-]`
   - Label darf nicht mit `-` beginnen oder enden
4. Forward-DNS-Lookup (`A`/`AAAA`) des PTR-Hostnamens.
5. Forward-Confirm-Match: mindestens ein Forward-Ergebnis muss exakt zur geprueften IP passen.

Typische Ergebnisse:
- `OK`: PTR vorhanden, Hostname gueltig, Forward-Confirm passt.
- `CRITICAL`: kein PTR, ungueltiger Hostname, Forward-Lookup fehlschlaegt oder kein Rueck-Match auf die Ursprungs-IP.
- `UNKNOWN`: ungueltige Parameter oder ungueltige IP-Eingabe.

## Contributen

Empfohlener Workflow:

1. Fork/Branch erstellen.
2. Aenderung in `check_ptr_validname.c` oder `Makefile` umsetzen.
3. Neu bauen:

```sh
make clean && make
```

4. Manuell testen, z. B. mit bekannten IPs (mit und ohne PTR).
5. PR mit kurzer Beschreibung erstellen:
   - Was wurde geaendert?
   - Warum ist die Aenderung noetig?
   - Welche Testfaelle wurden lokal ausgefuehrt?

Code-Richtlinien:
- Klare und kurze Fehlermeldungen.
- Exit-Codes stabil halten (Monitoring-Kompatibilitaet).
- Neue Pruefungen so bauen, dass bestehendes Verhalten nicht regressiert.

## Potentielle Erweiterungen

Moegliche Verbesserungen, um den Check nuetzlicher zu machen:

- Optionaler Timeout-Parameter fuer DNS-Lookups.
- Option fuer expliziten Resolver (statt System-Resolver).
- Optionales `WARNING`, wenn nur Teilkonsistenz besteht (z. B. PTR vorhanden, aber Hostname grenzwertig).
- Performance-Data-Ausgabe fuer Monitoring (z. B. Lookup-Latenzen).
- JSON-Ausgabe fuer Automatisierung/Pipelines.
- IDN/Punycode-Validierung als optionale, erweiterte Hostname-Pruefung.
- Unit-Tests fuer Parser/Hostname-Validierung und Integrationstests mit Test-DNS-Zone.
