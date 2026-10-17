# Galactic Unicorn storage

The Pico W and Pico 2 W have no NVS partition. The Galactic Unicorn firmware keeps its
configuration and settings in LittleFS instead, with the same keys and types the ESP32 stores in
NVS, so the shared configuration code runs unchanged.

## Layout

| Path | What |
|---|---|
| `/NVS/awtrix-cfg.bin` | device configuration (`DeviceConfig`): Wi-Fi, MQTT, time, the fields of `/api/v1/system` |
| `/NVS/awtrix-ng.bin` | settings (`NvsSettings`): the blob behind `/api/v1/settings` |
| `/ICONS`, `/PALETTES`, `/MELODIES`, `/SCRIPTS` | assets, as on the ESP32. Created at boot |
| `/apploop.json`, `/config/` | app order and icon origins, as on the ESP32 |

Each namespace file is one snapshot: a header, the typed keys and a CRC-32. A namespace is
committed by writing `<name>.bin.tmp`, reading it back, and renaming it over the old file, so a
power cut leaves either the old or the new snapshot. A damaged or truncated snapshot is ignored
and the defaults load. A number wider than four bytes is rejected, never truncated.

## What differs from the ESP32

- The panel (53 × 11) and every pin are fixed. `/api/v1/system` does not offer the panel or pin
  fields, and a backup made on an ESP32 restores everything else: this board keeps its own pins
  and panel.
- A requested reboot saves pending settings first. A factory reset or a settings reset removes the
  namespace files and reboots without saving over them.
- Restored assets are staged next to their target and renamed once complete; every folder a
  nested entry such as `/ICONS/sub/smile.gif` needs is created.
- Scripts and radio are compiled out of these builds, so the radio station list is not stored.
- Custom palettes in `/PALETTES` are found by name, as on the ESP32.
