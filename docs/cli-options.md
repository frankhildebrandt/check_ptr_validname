# CLI Options

## Usage

```sh
check_ptr_validname -i <ip-address> [options]
```

## Options

- `-i, --ip <ip>`: Target IP address (required).
- `-r, --resolver <ip>`: Explicit resolver instead of `/etc/resolv.conf`.
- `-t, --timeout-ms <ms>`: Timeout per DNS lookup (default: `2000`).
- `--warn-partial`: Return partial consistency as WARNING.
- `--perfdata`: Include perfdata metrics.
- `--json`: Emit JSON output.
- `--idn-check`: Enable additional IDN/punycode checks.
- `-h, --help`: Show help.
