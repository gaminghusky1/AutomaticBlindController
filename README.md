# ESP32 Automatic Blinds

Single-shade ESP32 controller for a Somfy RTS shade using a CC1101 transceiver and a BH1750 light sensor.

## What this firmware does

- Stores one RTS remote address, one rolling code, one paired state, and one estimated shade position.
- Sends `up`, `down`, `my`, and `prog` RTS commands through the CC1101.
- Keeps WiFi, mDNS, NTP, radio, shade, and automation settings in ESP32 NVS so they survive reboot.
- Provides a small built-in web UI and JSON API.
- Runs light-based automatic open/close logic through `AutomaticBlindController`.

## Default access

- If WiFi is not configured or connection fails, the ESP32 opens a setup AP named `AutoBlinds-XXXXXX`.
- The local web UI is served from the firmware at `/`.
- After WiFi connects, mDNS is available at `http://AutoBlinds.local` unless the hostname is changed.

## API overview

- `GET /api/status`
- `POST /api/command` with `{ "command": "up" | "down" | "my" | "prog" }`
- `POST /api/pair`
- `POST /api/pair/confirm`
- `POST /api/shade`
- `GET|POST /api/radio`
- `POST /api/wifi`
- `POST /api/settings`
- `GET|POST /api/automation`
- `POST /api/reboot`

## Notes

The shade position is estimated from the commands sent by this ESP32 and the configured travel times. If the physical remote is used, this firmware does not currently receive/decode that remote and therefore cannot know the true shade position.
