---
layout: ../../layouts/AboutLayout.astro
title: Integrations
heading: Integrations
subtitle: Copy-paste examples for Icinga2, Nagios, and direct CLI usage.
---

## Icinga2 `CheckCommand`

```text
object CheckCommand "check_ptr_validname" {
  import "plugin-check-command"

  command = [ PluginDir + "/check_ptr_validname" ]

  arguments += {
    "-i" = "$check_ptr_ip$"
    "-r" = "$check_ptr_resolver$"
    "--timeout-ms" = "$check_ptr_timeout_ms$"
    "--warn-partial" = {
      set_if = "$check_ptr_warn_partial$"
    }
    "--perfdata" = {
      set_if = "$check_ptr_perfdata$"
    }
    "--idn-check" = {
      set_if = "$check_ptr_idn_check$"
    }
  }

  vars.check_ptr_timeout_ms = 2000
  vars.check_ptr_perfdata = true
}
```

## Icinga2 apply rule

```text
apply Service "ptr-validname-" for (ip => cfg in host.vars.ptr_checks) {
  import "generic-service"

  check_command = "check_ptr_validname"
  vars.check_ptr_ip = ip
  vars += cfg

  assign where host.vars.ptr_checks
}
```

## Nagios command and service

```text
define command {
  command_name    check_ptr_validname
  command_line    /usr/lib/nagios/plugins/check_ptr_validname -i $ARG1$ --perfdata
}

define service {
  use                     generic-service
  host_name               dns-target-host
  service_description     PTR Validname 8.8.8.8
  check_command           check_ptr_validname!8.8.8.8
}
```

## General CLI usage

```sh
# Manual validation
./check_ptr_validname -i 8.8.8.8 --perfdata

# Explicit resolver
./check_ptr_validname -i 1.1.1.1 -r 8.8.8.8 --warn-partial

# JSON output for automation
./check_ptr_validname -i 8.8.4.4 --json | jq .
```
