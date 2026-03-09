# check_ptr_validname

`check_ptr_validname` ist ein DNS-Check in C, der fuer eine IP-Adresse prueft, ob PTR-Eintrag und Forward-Aufloesung konsistent sind.

## Kompilieren

Voraussetzungen:
- C-Compiler (`cc`, `gcc` oder `clang`)
- `make`
- POSIX-Umgebung (Linux/macOS; Cross-Compile fuer Windows ueber Toolchain)

Build fuer das Host-System:

```sh
make
```

Build-Konfiguration anzeigen:

```sh
make print-config
```

Artefakt wird unter `build/<os>-<arch>/check_ptr_validname` erzeugt.

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
./build/<os>-<arch>/check_ptr_validname -i <ip-address> [options]
```

Beispiel:

```sh
./build/macos-arm64/check_ptr_validname -i 8.8.8.8 --perfdata
```

Unterstuetzte Parameter:
- `-i`, `--ip <ip>`: IP-Adresse (IPv4 oder IPv6), Pflicht
- `-r`, `--resolver <ip>`: Expliziter DNS-Resolver (statt automatisch aus `/etc/resolv.conf`)
- `-t`, `--timeout-ms <ms>`: Timeout pro DNS-Lookup in Millisekunden (Default: `2000`)
- `--warn-partial`: Teilkonsistenz als `WARNING` statt `CRITICAL` behandeln
- `--perfdata`: Monitoring-Perfdata anhaengen
- `--json`: JSON-Ausgabe fuer Automatisierung/Pipelines
- `--idn-check`: Erweiterte IDN/Punycode-Validierung aktivieren
- `-h`, `--help`: Hilfe anzeigen

## Exit-Codes (Nagios/Icinga-kompatibel)

- `0` = `OK`
- `1` = `WARNING`
- `2` = `CRITICAL`
- `3` = `UNKNOWN`

## Was wird geprueft

1. Eingabevalidierung der IP (IPv4/IPv6).
2. PTR-Lookup per DNS-Query.
3. Hostname-Validierung:
   - Strikt: RFC-nahe FQDN-Regeln (`[A-Za-z0-9-]`, Label 1-63, Gesamtlaenge <= 253)
   - Optional: IDN/Punycode-Check (`--idn-check`) fuer `xn--` Labels
4. Forward-Lookup (`A` bei IPv4, `AAAA` bei IPv6).
5. Forward-Confirm-Match: mindestens eine Forward-IP muss exakt zur Eingabe-IP passen.

## Teilkonsistenz / WARNING

Mit `--warn-partial` werden grenzwertige, aber technisch teilweise aufloesbare Faelle als `WARNING` zurueckgegeben, z. B.:
- PTR vorhanden, Hostname verletzt strikte Regeln, besteht aber die relaxte Pruefung.
- IDN/Punycode-Check ist aktiv und meldet Auffaelligkeiten.

Wenn Forward-Lookup oder Forward-Confirm-Match fehlschlaegt, bleibt das Ergebnis `CRITICAL`.

## Performance-Data

Mit `--perfdata` wird die erste Ausgabezeile um Latenzen erweitert:

- `ptr_lookup_ms`
- `forward_lookup_ms`
- `total_ms`

Beispiel:

```text
OK - PTR vorhanden und konsistent (8.8.8.8 -> dns.google) [Forward-Confirm-Match erfolgreich] | ptr_lookup_ms=4.768ms;;;; forward_lookup_ms=10.437ms;;;; total_ms=15.842ms;;;;
```

## JSON-Ausgabe

Mit `--json` wird eine maschinenlesbare Ausgabe geliefert, inklusive:
- `state`, `code`, `message`, `detail`
- `ip`, `hostname`, `resolver`, `timeout_ms`
- `checks` (Einzelergebnisse)
- `latency_ms` (Lookup-Zeiten)

Beispiel:

```json
{"state":"OK","code":0,"message":"PTR vorhanden und konsistent","detail":"Forward-Confirm-Match erfolgreich","ip":"8.8.8.8","hostname":"dns.google","resolver":"192.168.171.133:53","timeout_ms":2000,"checks":{"ptr":true,"hostname":true,"idn":true,"forward":true,"forward_match":true,"partial":false},"latency_ms":{"ptr_lookup":7.182,"forward_lookup":46.210,"total":53.425}}
```

## Hinweise zur Resolver-Wahl

- Ohne `--resolver` wird der erste `nameserver` aus `/etc/resolv.conf` verwendet.
- Mit `--resolver` wird explizit gegen diesen Resolver auf Port `53/UDP` gefragt.

## Weitere Ideen

- Option `--resolver-port`, um non-standard DNS-Ports (z. B. Lab-Setups) direkt zu unterstuetzen.
- TCP-Fallback bei `TC`-Flag (truncated DNS-Antwort), damit grosse Antworten robuster verarbeitet werden.
- DNSSEC-Flags und AD-Bit als zusaetzliche Vertrauenspruefung im Output.
- `--strict-cname-chain`: CNAME-Ketten explizit verfolgen und im Ergebnis dokumentieren.
- Batch-Modus (mehrere IPs aus Datei/stdin) fuer schnelle Massenpruefung.
- Optionales Structured Logging (`ndjson`) fuer Observability-Pipelines.

## GitHub Actions

Dieses Repo enthaelt zwei Workflows:

- `Deploy GitHub Pages` (`.github/workflows/pages.yml`)
  - Trigger: Push auf `main` oder manuell
  - Baut die Website mit Astro (`src/pages/index.astro`)
  - Verwendet `PUBLIC_*` Umgebungsvariablen fuer Repo-/Download-Links
  - Deployt die 1-Pager-Doku nach GitHub Pages

- `Build Linux x86_64 Binary` (`.github/workflows/release-linux.yml`)
  - Trigger: Tags `v*` oder manuell
  - Baut `check_ptr_validname` fuer `linux/x86_64`
  - Laedt Binary + SHA256 als Workflow-Artefakt hoch
  - Erstellt bei Tag-Build automatisch ein GitHub Release mit Assets

Tag-Beispiel fuer ein Release:

```sh
git tag v1.0.0
git push origin v1.0.0
```

## Website lokal testen (Astro)

```sh
npm ci
npm run dev
```
