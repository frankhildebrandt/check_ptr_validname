---
layout: ../../layouts/AboutLayout.astro
title: CLI Options
heading: CLI Options
subtitle: Flags, output modes, and practical invocation examples.
---

## Syntax

```sh
check_ptr_validname -i <ip-address> [options]
```

## Required option

- `-i`, `--ip <ip>`: target IPv4 or IPv6 address.

## Resolver and timeout

- `-r`, `--resolver <ip>`: use a specific resolver instead of the first `nameserver` in `/etc/resolv.conf`.
- `-t`, `--timeout-ms <ms>`: timeout per DNS lookup in milliseconds. Default: `2000`.

## Behavior flags

- `--warn-partial`: treat partial consistency as `WARNING` instead of `CRITICAL`.
- `--idn-check`: enable additional IDN and punycode validation.

## Output flags

- `--perfdata`: append latency metrics for monitoring systems.
- `--json`: emit machine-readable JSON output.
- `-h`, `--help`: print usage help.

## Examples

```sh
# Basic check
./build/linux-x86_64/check_ptr_validname -i 8.8.8.8

# Use a specific resolver and perfdata
./build/linux-x86_64/check_ptr_validname -i 8.8.8.8 -r 1.1.1.1 --perfdata

# Enable JSON for scripts
./build/linux-x86_64/check_ptr_validname -i 2001:4860:4860::8888 --json
```

## Exit codes

- `0`: OK
- `1`: WARNING
- `2`: CRITICAL
- `3`: UNKNOWN
