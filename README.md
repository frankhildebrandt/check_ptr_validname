# check_ptr_validname

DNS PTR validation check written in C for monitoring systems.

This repository now uses the [Naid Astro template](https://github.com/studiolumina/naid) as the base for the GitHub Pages site.

## What this check does

For a target IP address, the check verifies:

1. PTR lookup success.
2. Hostname validity (strict rules, optional IDN checks).
3. Forward lookup success.
4. Forward-confirm match (resolved IP includes the original input IP).

Exit codes are Nagios/Icinga-compatible:

- `0` OK
- `1` WARNING
- `2` CRITICAL
- `3` UNKNOWN

## Build the binary

Requirements:

- C compiler (`cc`, `gcc`, or `clang`)
- `make`

Build native target:

```sh
make
```

Build Linux x86_64 explicitly:

```sh
make OS=linux ARCH=x86_64
```

## Run from CLI

```sh
./build/linux-x86_64/check_ptr_validname -i 8.8.8.8 --perfdata
```

## Documentation

Detailed English documentation is available in both repository docs and GitHub Pages routes:

- Code documentation: `docs/code.md` and `/docs/code`
- CLI options: `docs/cli-options.md` and `/docs/cli-options`
- Integrations (Icinga2, Nagios, CLI): `docs/integrations.md` and `/docs/integrations`

## Astro website (Naid-based)

Install and run locally:

```sh
npm ci
npm run dev
```

Build static site:

```sh
npm run build
```

## GitHub Actions

### Deploy GitHub Pages

Workflow: `.github/workflows/pages.yml`

- Trigger: push to `main` or manual dispatch
- Builds Astro site and deploys `dist/` to GitHub Pages
- Injects repository download links via `PUBLIC_*` environment variables

### Build Linux x86_64 Binary

Workflow: `.github/workflows/release-linux.yml`

- Trigger: tags matching `v*` or manual dispatch
- Builds Linux x86_64 binary
- Uploads binary and SHA256 as workflow artifacts
- Creates/updates GitHub release assets for tag builds

Release example:

```sh
git tag v1.0.0
git push origin v1.0.0
```
