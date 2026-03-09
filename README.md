# check_ptr_validname

English documentation is the default.
German version: [`README.de.md`](README.de.md)

`check_ptr_validname` is a DNS check written in C that validates whether a PTR record and forward resolution are consistent for a given IP address.

## Build

Requirements:
- C compiler (`cc`, `gcc`, or `clang`)
- `make`
- POSIX environment (Linux/macOS; Windows cross-compilation via toolchain)

Build for the host system:

```sh
make
```

Show build configuration:

```sh
make print-config
```

The artifact is generated at `build/<os>-<arch>/check_ptr_validname`.

Cross-compilation examples:

```sh
make OS=linux ARCH=arm64 CROSS=aarch64-linux-gnu-
make OS=windows ARCH=x86_64 CROSS=x86_64-w64-mingw32-
```

Clean up:

```sh
make clean
```

## Usage

Basic syntax:

```sh
./build/<os>-<arch>/check_ptr_validname -i <ip-address> [options]
```

Example:

```sh
./build/macos-arm64/check_ptr_validname -i 8.8.8.8 --perfdata
```

Supported parameters:
- `-i`, `--ip <ip>`: IP address (IPv4 or IPv6), required
- `-r`, `--resolver <ip>`: Explicit DNS resolver (instead of auto-detecting from `/etc/resolv.conf`)
- `-t`, `--timeout-ms <ms>`: Timeout per DNS lookup in milliseconds (default: `2000`)
- `--warn-partial`: Return partial consistency as `WARNING` instead of `CRITICAL`
- `--perfdata`: Append monitoring perfdata
- `--json`: JSON output for automation/pipelines
- `--idn-check`: Enable extended IDN/punycode validation
- `-h`, `--help`: Show help

## Exit Codes (Nagios/Icinga compatible)

- `0` = `OK`
- `1` = `WARNING`
- `2` = `CRITICAL`
- `3` = `UNKNOWN`

## Validation Steps

1. Validate input IP (IPv4/IPv6).
2. Perform PTR lookup.
3. Validate hostname:
   - Strict: RFC-like FQDN rules (`[A-Za-z0-9-]`, label length 1-63, total length <= 253)
   - Optional: IDN/punycode checks (`--idn-check`) for `xn--` labels
4. Perform forward lookup (`A` for IPv4, `AAAA` for IPv6).
5. Confirm at least one forward IP exactly matches the input IP.

## Partial Consistency / WARNING

With `--warn-partial`, edge cases that are technically partially resolvable return `WARNING`, for example:
- PTR exists, hostname fails strict rules but passes relaxed validation.
- IDN/punycode checks are enabled and show suspicious results.

If forward lookup or forward-confirm-match fails, the result remains `CRITICAL`.

## Performance Data

With `--perfdata`, the first output line includes latency metrics:

- `ptr_lookup_ms`
- `forward_lookup_ms`
- `total_ms`

Example:

```text
OK - PTR present and consistent (8.8.8.8 -> dns.google) [Forward confirm match succeeded] | ptr_lookup_ms=4.768ms;;;; forward_lookup_ms=10.437ms;;;; total_ms=15.842ms;;;;
```

## JSON Output

With `--json`, output is machine-readable and includes:
- `state`, `code`, `message`, `detail`
- `ip`, `hostname`, `resolver`, `timeout_ms`
- `checks` (individual check results)
- `latency_ms` (lookup timings)

Example:

```json
{"state":"OK","code":0,"message":"PTR present and consistent","detail":"Forward confirm match succeeded","ip":"8.8.8.8","hostname":"dns.google","resolver":"192.168.171.133:53","timeout_ms":2000,"checks":{"ptr":true,"hostname":true,"idn":true,"forward":true,"forward_match":true,"partial":false},"latency_ms":{"ptr_lookup":7.182,"forward_lookup":46.210,"total":53.425}}
```

## Resolver Notes

- Without `--resolver`, the first `nameserver` from `/etc/resolv.conf` is used.
- With `--resolver`, DNS queries go explicitly to that resolver on `53/UDP`.

## GitHub Actions

This repository contains two workflows:

- `Deploy GitHub Pages` (`.github/workflows/pages.yml`)
  - Trigger: push to `main` or manual run
  - Builds the site with Astro (`src/pages/index.astro`)
  - Uses `PUBLIC_*` environment variables for repository/download links
  - Deploys the one-pager to GitHub Pages

- `Build Linux x86_64 Binary` (`.github/workflows/release-linux.yml`)
  - Trigger: tags `v*` or manual run
  - Builds `check_ptr_validname` for `linux/x86_64`
  - Uploads binary + SHA256 as workflow artifact
  - Automatically creates a GitHub Release for tag builds

Tag example for a release:

```sh
git tag v1.0.0
git push origin v1.0.0
```

## Test Website Locally (Astro)

```sh
npm ci
npm run dev
```
