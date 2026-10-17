---
only: [rp2040]
---

# Galactic Unicorn

AWTRIX NG runs on Pimoroni's **Galactic Unicorn**, a board with a fixed **53×11** RGB display,
with the same HTTP and MQTT `/api/v1` commands as every other AWTRIX NG clock. Pick the image for
the Pico fitted to your board:

| Hardware | PlatformIO environment | USB image | Hardware status |
| --- | --- | --- | --- |
| Pico W, RP2040, 264 KB SRAM, 2 MB flash | `galactic_unicorn` | `firmware-galactic-unicorn.uf2` | verified on PR 1 (1.2.1 base) |
| Pico 2 W, RP2350, 520 KB SRAM, 4 MB flash | `galactic_unicorn_2w` | `firmware-galactic-unicorn-2w.uf2` | verified on PR 1 (1.2.1 base) |

The hardware results on this page come from the earlier port of AWTRIX NG 1.2.1 to the board
(PR 1). This build carries the same features on AWTRIX NG 1.2.2; its own hardware results replace
the PR 1 notes once the board has been checked again.

Both images reserve **512 KiB for LittleFS**, separate from the firmware. Neither Pico has
PSRAM. There is no battery, and no I²C sensor is read (the Qw/ST connector is unused), so
battery, temperature and humidity values are left out; the light sensor on the board is used.
Install only one of the two UF2 images above: an image for another clock does not run here, and
the pin presets of other clocks do not apply.

## Flash over USB

1. Get the UF2 for your Pico. A build from source uses `pio run -e galactic_unicorn` or
   `pio run -e galactic_unicorn_2w` and writes `.pio/build/<environment>/firmware.uf2`. CI's
   `uf2-<environment>` artifact also holds a file called `firmware.uf2`, so keep track of which
   artifact you downloaded.
2. Disconnect power. Hold the Pico's **BOOTSEL** button while you connect a USB **data** cable to
   your computer, then release it. This is BOOTSEL on the Pico itself, not A/B/C/D or the board's
   Sleep key.
3. Copy the matching `.uf2` onto the boot drive that appears (`RPI-RP2` for the Pico W, `RP2350`
   for the Pico 2 W). Let the copy finish. The drive disappears and the board restarts by itself.
4. Later updates work the same way. A normal firmware UF2 keeps the settings and files in
   LittleFS as long as the flash layout stays the same; back them up first. Do not use a
   full-flash erase image unless you want to erase them.

The browser cannot install firmware on this board: `POST /update` answers `503 unavailable` and
points to BOOTSEL and the UF2. The web UI's **Check for updates** still names a newer release and
links its release notes, where the `.uf2` files are, but offers no **Download & install** and no
firmware upload. See [Updating firmware](../guides/updating.md).

## First start and Wi-Fi

The display shows the boot animation and, without saved Wi-Fi credentials, an animated
**AP MODE** page. Join the open **awtrixng-xxxxxx** network (`xxxxxx` is the end of the MAC
address), or the hostname you set. Keep the phone connected even though this setup network has no
internet.

Open **http://192.168.4.1/** if the captive portal does not open by itself. Pick or type your
2.4 GHz Wi-Fi name, enter its password and save. Press **Reboot** to join at once. Or disconnect
the phone: retries pause while a phone is connected. After that the setup network goes away for a
few seconds every 60 seconds while the Galactic Unicorn tries the saved network, and it restarts
once it has joined. Hold **B** for one second while the board starts to open setup mode without
erasing the saved credentials.

Back on your own network, open **http://awtrixng-xxxxxx.local/** (or your hostname and port). If
mDNS does not work on your network, take the address from your router's list of DHCP leases.
`/api/v1/device` reports the address and the connection state. MQTT and Home Assistant discovery
are set up in **System**, as on every AWTRIX NG clock. NTP and the time zone you set give the
clock its time once it has joined; there is no battery-backed clock.

## Buttons

The keys are active low and debounced for 35 ms. The extra keys act once per press, not
repeatedly while held.

| Key | Action |
| --- | --- |
| A | Previous app; steps back in the menu |
| B | Select. Hold it for half a second to open the menu, where A and C step through the entries |
| C | Next app; steps forward in the menu |
| D | Not used |
| Brightness + / − | Step the brightness and switch to manual brightness |
| Volume + / − | Step the master `volume` (0–100 %), which every sound follows |
| Sleep | Switch the display off and on; this is not the timed sleep below |

Navigation follows the shared rotate, swap and `blockNavigation` settings. A, B and C send the
same button events and [`buttonCallback`](../reference/system.md) webhook bodies as every other
AWTRIX NG clock (`left`, `middle`, `right`, press and release). The pins are listed in
[Fixed wiring](#fixed-wiring).

The speaker on the board plays RTTTL melodies: `sound` with the name of a saved melody, or
inline notes. `GET /api/v1/capabilities` reports `rtttl`; MP3, radio and DFPlayer tracks are not
available. See [Sound](../guides/sounds.md).

## Fixed wiring

Every pin is part of the board. A change to the pin map, or switching on the DFPlayer, is refused
with `400 invalidPinConfig` and the message “pin assignments are fixed”. `pinMatrix: -1` stands
for the built-in display, not a display that is switched off.

| What | GPIO |
| --- | --- |
| Display clock / data / latch / blank | 13 / 14 / 15 / 16 |
| Row select | 17–20 |
| A / B / C / D | 0 / 1 / 3 / 6 |
| Sleep | 27 |
| Volume + / − | 7 / 8 |
| Brightness + / − | 21 / 26 |
| Light sensor | 28 (12-bit ADC) |
| Qw/ST connector (I²C SDA / SCL) | 4 / 5, unused: no I²C sensor is read, so `pinI2cSda` and `pinI2cScl` are `-1` |
| Speaker bit clock / word clock / data / amplifier enable | 10 / 11 / 9 / 22 |

`gpio` in `GET /api/v1/capabilities` reports `soc: "rp2040"` for both Picos and `fixed: true`;
`updateImage` in `GET /api/v1/device` tells the two images apart. The `matrix` list there is
empty, because the display has no data pin to choose, and the `rtc` list is empty, because the
Sleep key ends a timed sleep (see [Sleep](#sleep)).

## Which features each build has

Always read **`GET /api/v1/capabilities`** instead of guessing from the chip.

### Scripting

Both images run Berry scripts and report `scripting: true`, so the web UI shows **Run scripts**
and the script pages. Scripts share one memory with Wi-Fi, HTTP, MQTT and the display, so the
budget depends on the chip:

| | Pico W (RP2040) | Pico 2 W (RP2350) |
| --- | --- | --- |
| Script memory budget (`scriptHeapBudgetBytes`) | 48 KB | 96 KB |
| What the script engine needs with no script installed (PR 1, 1.2.1 base) | about 19 KB of free memory | about 17 KB of free memory |
| Largest script that installed (PR 1, 1.2.1 base) | about 7 KB of source | about 28 KB of source |

The last two rows were measured on PR 1 (1.2.1 base); 1.2.2's script engine is larger, so read
`freeHeapBytes` and `scriptHeapBudgetBytes` on your own board. On the Pico 2 W a larger script
fails with `out of memory` while plenty of memory is still free: the 96 KB budget is the limit
there, not the memory Wi-Fi needs.

On the Pico W a failed install of a large script can leave the memory in pieces, so later
installs are refused with `507` although enough memory is free in total. Restart to recover.
Script `http.*` requests, and Modbus reads, which use the same path, return `false`. Timers,
drawing, script icons, storage, buttons, sound and MQTT work as on every other AWTRIX NG clock.

If a script takes the clock down at every start, hold **A and C** together for three seconds
while you switch it on. The display shows `NOSCR` and AWTRIX NG starts without running scripts
(see [troubleshooting](../troubleshooting/troubleshooting.md#scripts-eat-the-memory-and-awtrix-never-comes-up)).

`scriptingEnabled` switches the script engine off from the next start, as on every clock; stored
scripts stay listed and editable. An image built with `-D AWTRIX_FEATURE_SCRIPTING=0` still
**accepts and stores `scriptingEnabled`**, but there the setting does nothing: `scripting` is
false, the script routes answer `503 unavailable`, and the web UI hides **Run scripts** and the
script pages and says they are not available on this board.

### Left out of both images

| Feature | Why, and what happens |
| --- | --- |
| MP3 and internet radio | The board plays melodies through a tone output, not a decoder; their capability flags are false and their routes answer `503 unavailable`. |
| Outbound TLS | There is no secure client on the Pico; use a trusted local network and plain-HTTP addresses. |
| Firmware update from the browser | There is no second firmware slot in the Pico's flash layout; update over USB with a UF2. |

HTTP Basic authentication is not encryption. Do not expose the board directly to the internet.
Routes for a missing feature answer with the shared error codes, not with a second API.

## Sleep

`POST /api/v1/device/sleep` switches the display off, turns on the radio's power saving and waits
for `durationMs`, or for a new press of **Sleep** (GPIO27). A key already held when the sleep
starts must be released first. The CPU, the memory and the display refresh stay powered and
requests are not answered, so the board saves little power: this is an emulated sleep, not a
deep sleep. Waking restarts AWTRIX NG, and `resetReason` then reads `software`. The Sleep key's
ordinary display toggle keeps apps and the network running. The hardware watchdog still guards a
hung firmware and reports `watchdog` after the restart.

## Eleven rows and the tall-display rule

`panelHeight` is **11** by default, and the width is always **53**. Height **8** is a
compatibility mode shown on rows 1–8, with the rows above and below dark. The API refuses any
other height, and any change to the width or the wiring settings, with `422 validationFailed`; a
height stored some other way falls back to 11 at start with a warning. A restored backup keeps
the board's own pins and display size.

The shared tall-display rule draws ordinary text, the main 8×8 icon and the built-in apps in an
eight-row band that starts at row `floor((H - 8) / 2)`, which is row 1 at height 11.
Backgrounds, effects, overlays, transitions and charts use the whole display; the progress bar is
on its last row. Draw commands and icons placed at a position use absolute coordinates. GIFs keep
their own size. See [Limits](../reference/limits.md#tall-displays-content-band-and-full-canvas).

## Verification scope

These results are from PR 1 (1.2.1 base); this build has not been checked on the board again yet.

Verified on PR 1 (1.2.1 base), **Pico W**: display, light sensor and frame rate, the HTTP smoke
test (`tools/pico/smoke.sh`, no failures), 80 requests at once, watchdog recovery, MQTT and Home
Assistant discovery (21 entities), Matrix light control, notifications, A/B/C events, brightness,
volume, Sleep, and hearing the `alert` melody. The button webhook was tested only on the host.

Verified on PR 1 (1.2.1 base), **Pico 2 W**: display at 42 fps, `mirror` and `rotate`,
notifications, light sensor, Wi-Fi join, mDNS and NTP, the HTTP smoke test (every API check
passes; the 80-request burst depends on the Wi-Fi link), watchdog recovery
(`resetReason: "watchdog"`), settings kept across firmware UF2 updates, the scripting figures
above, MQTT with Home Assistant discovery (21 entities, Matrix light control, notifications),
A/B/C events, brightness, volume, Sleep, inline and saved melodies, the A+C `NOSCR` rescue, and
Wi-Fi setup from a phone through the setup network.

The [display driver](galactic-unicorn-display.md) and [storage](pico-persistence.md) pages
describe how the board's firmware works inside.
