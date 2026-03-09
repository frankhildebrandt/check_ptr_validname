# Code Documentation

The implementation is intentionally compact in one C source file: `check_ptr_validname.c`.

## Runtime architecture

1. Parse CLI options into `struct options`.
2. Validate and normalize input IP.
3. Resolve PTR and validate hostname quality.
4. Resolve forward records and verify that at least one record matches the input IP.
5. Emit Nagios-compatible status and optional perfdata/JSON.

## Output model

- Text mode for monitoring systems.
- JSON mode for automation (`--json`).
- Exit codes: `0` OK, `1` WARNING, `2` CRITICAL, `3` UNKNOWN.
