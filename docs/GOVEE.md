# Govee (`import govee`)

LAN control for Govee smart lights, including **H70B1** Curtain Lights (520 LED, RGBIC).

Uses the [Govee LAN API](https://app-h5.govee.com/user-manual/wlan-guide) over UDP (ports 4001–4003). No cloud API key required.

## Requirements

- Govee device with **LAN Control** enabled in the Govee Home app
- Same LAN as the host running Shakti
- UDP multicast to `239.255.255.250:4001` and bind to port **4002** (only one LAN integration per machine)

## Quick start

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/govee_demo.ie
```

Pin a device IP (skip multicast scan):

```bash
export SHAKTI_GOVEE_IP=192.168.1.42
./shakti examples/govee_demo.ie
```

## API

| Function | Description |
|----------|-------------|
| `govee.model()` | Default SKU (`H70B1`, overridable via `SHAKTI_GOVEE_SKU`) |
| `govee.scan([timeout_ms])` | Multicast discovery; returns list of device dicts |
| `govee.device()` | Resolve target from `SHAKTI_GOVEE_IP` or first matching scan hit |
| `govee.turn(on)` | Power on/off |
| `govee.brightness(0..100)` | Set brightness |
| `govee.color(r, g, b)` | Set RGB color |
| `govee.color_temp(kelvin)` | Set color temperature |
| `govee.status()` | Query `devStatus` |
| `govee.cmd_turn(on)` / `cmd_brightness(n)` / `cmd_color(r,g,b[,kelvin])` | Build wire JSON (offline-safe) |
| `govee.lan_send(ip, json)` | Send raw LAN command |
| `govee.api_error(resp)` | Return error string from response dict, or `0` |

## Environment

| Variable | Purpose |
|----------|---------|
| `SHAKTI_GOVEE_IP` | Device IPv4 address |
| `SHAKTI_GOVEE_SKU` | SKU prefix filter (default `H70B1`) |
| `SHAKTI_GOVEE_SKIP_LAN` | Set to `1` to skip UDP (offline tests) |

## Example

```ie
import govee

govee.turn(True)
govee.brightness(60)
govee.color(255, 128, 0)
print(govee.status())
```

See also [examples/govee_demo.ie](../examples/govee_demo.ie).
