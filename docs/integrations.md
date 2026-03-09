# Integrations

## Icinga2

Use a custom `CheckCommand` and an apply rule to attach the check to hosts with `host.vars.ptr_checks` entries.

## Nagios

Define a `command` that points to the plugin binary and bind it in service definitions via `check_command` arguments.

## General CLI usage

The binary can be used standalone for diagnostics and integrated into scripts with `--json` output.
