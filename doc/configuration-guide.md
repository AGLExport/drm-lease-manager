# DRM Lease Manager Configuration Guide

## 1. Selecting a configuration file

Pass a TOML configuration file to the daemon with `-c` or `--config`:

```sh
drm-lease-manager --config /etc/drm-lease-manager.toml /dev/dri/card0
```

The default path is `/etc/drm-lease-manager.toml`. If the DRM device is omitted,
the first available DRM device capable of modesetting is selected.

## 2. Basic syntax

Put one or more `[[lease]]` tables at the top level. Each lease requires a
string `name` and should normally have a string-array `connectors`. All string
values must be quoted.

```toml
[[lease]]
name = "front-display"
connectors = ["HDMI-A-1"]

[[lease]]
name = "rear-display"
connectors = ["DP-1", "DP-2"]
```

Connector names are DRM connector names. Common forms include `HDMI-A-1`,
`DP-1`, `eDP-1`, and `LVDS-1`. Check the names available on the target device
before writing the configuration.

A lease can contain multiple connectors. Do not design multiple leases to share
the same DRM resources.

## 3. Per-connector options

Create a top-level table with the same name as a string in the `connectors`
array to add options for that connector:

```toml
[[lease]]
name = "mixed-display"
connectors = ["HDMI-A-1", "DP-1"]

[DP-1]
optional = true
planes = [31, 42, 43]
```

### `optional`

This is a Boolean and defaults to `false`.

- `false`: if the connector is missing, creation of that lease fails.
- `true`: a missing connector is logged as a warning and omitted from that lease.

### `planes`

This is an integer array. It restricts the connector's lease candidates to the
listed DRM plane IDs:

```toml
[DP-1]
planes = [31, 42, 43]
```

When `planes` is omitted, all DRM planes on the device are candidates for the
connector. An explicit `planes` list also permits shared planes that can be used
by multiple CRTCs. Plane IDs are numeric DRM kernel resource IDs, not connector
names.

## 4. Defaults and important behavior

When no configuration is supplied, or when parsing produces zero configurations,
the daemon creates one lease per detected DRM connector, up to the number of
available CRTCs. Default lease names use this pattern:

```text
<DRM device minor number>-<connector name>
```

For example, `LVDS-1` on `/dev/dri/card0` becomes `card0-LVDS-1`.

TOML syntax errors, a missing `lease` array, a missing required `name`, a
non-string connector value, or a non-integer plane value are rejected by the
parser. In the implementation, `parse_config()` returns 0 for these errors, and
the startup path falls back to the default configuration. Always check the
startup log so that an unintended default configuration is not used.

A `lease` without `connectors` can be parsed, but has no target objects and is
not useful in practice. Specify at least one connector for every lease.

## 5. Minimal example

```toml
[[lease]]
name = "instrument-cluster"
connectors = ["eDP-1"]
```

## 6. Allowing hardware to be absent

This example includes `DP-1` only when the external display is present:

```toml
[[lease]]
name = "vehicle-displays"
connectors = ["eDP-1", "DP-1"]

[DP-1]
optional = true
```

`eDP-1` remains mandatory, so the lease cannot be created if it is missing.

## 7. Pinning plane IDs

To avoid automatic plane selection, specify IDs observed on the target device:

```toml
[[lease]]
name = "restricted-output"
connectors = ["HDMI-A-1"]

[HDMI-A-1]
planes = [10, 11]
```

If a listed ID does not exist, the lease cannot be created. Plane IDs depend on
the hardware and driver, so do not copy this setting unchanged to another system.

## 8. Related command-line options

These options change daemon behavior independently of the configuration file:

| Option | Meaning |
| --- | --- |
| `-c`, `--config PATH` | Select the configuration file |
| `-t`, `--lease-transfer` | Transfer a lease from an existing client to a new client |
| `-k`, `--keep-on-crash` | Keep a lease open when a client crashes |
| `-v`, `--verbose` | Enable debug logging |

Restart the daemon after changing the configuration. Use `-v` to inspect
connector names, lease names, and errors in the startup log.
