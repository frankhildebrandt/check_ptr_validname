---
layout: ../../layouts/AboutLayout.astro
title: Code Documentation
heading: Code Documentation
subtitle: Runtime flow, validation stages, and output decisions.
---

`check_ptr_validname.c` is intentionally compact: one source file, one binary, no runtime framework overhead.

## Execution flow

1. Parse command line options into `struct options`.
2. Validate the input IP and detect the address family.
3. Determine the resolver from `--resolver` or `/etc/resolv.conf`.
4. Perform the PTR lookup and capture latency.
5. Validate the returned hostname with strict rules and optional IDN checks.
6. Perform forward lookup and require a forward-confirm match.
7. Emit Nagios-compatible text output or JSON output.

## Important structs

- `struct options` holds runtime settings such as timeout, resolver, JSON mode, and warning policy.
- `struct check_result` aggregates state, messages, hostname data, booleans for each validation stage, and latency metrics.

## Validation model

- IP parsing supports IPv4 and IPv6.
- Hostname validation is strict by default and can optionally inspect punycode labels.
- Partial consistency can be downgraded to `WARNING` with `--warn-partial`.
- Missing forward-confirm match remains a hard failure.

## Output behavior

- Plain output is designed for Nagios and Icinga2.
- `--perfdata` appends timing metrics.
- `--json` emits a structured payload for scripts and automation.

## Build targets

```sh
make
make OS=linux ARCH=x86_64
make OS=windows ARCH=x86_64 CROSS=x86_64-w64-mingw32-
```
