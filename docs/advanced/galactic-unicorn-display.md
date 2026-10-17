# Galactic Unicorn display

The Pico W/Pico 2 W board drives the fixed 53x11 panel using the display half
of Pimoroni pico **v1.23.0** (`3440ab232cdc2b019bb8d16f67b0448502efd9dc`, MIT).
Audio and pico_graphics are not included. See `THIRD-PARTY-NOTICES.md`.

## Height and wiring

`panelHeight` defaults to **11**. **8** produces a 53x8 canvas letterboxed on
physical rows **1–8** (zero-based); rows **0, 9, 10** remain dark. Any other
height stored outside the API uses 11 and logs a warning at startup. Width is
always 53 with one panel. API and MQTT edits to width, chaining or wiring
are rejected with `422 validationFailed`. A backup restore keeps the board's
own panel and wiring. `mirror` and `rotate` flip the picture, and `rotate`
also swaps A and C.

Clock/data/latch/blank are GPIO 13/14/15/16, row selects GPIO 17–20. The
physical stream reverses both axes, matching Pimoroni's original driver.

## Refresh and color

One dynamically claimed PIO SM and two chained DMA channels continuously
refresh fourteen binary-weighted bit planes without CPU refresh interrupts.
PIO1 is preferred to leave PIO0 instruction memory for CYW43; allocation checks
both free SMs and program space, and can fall back to PIO0. The startup log
prints the actual display PIO/SM and DMA channels. The Pico variant's early
`init_cyw43_wifi` call is deferred with a linker wrapper until after display
startup. The radio log reports newly claimed PIO/SMs from the SDK allocation
bitmap, without depending on CYW43's private bus structure. After the Wi-Fi join
or AP startup, a bounded DMA-progress probe logs `refresh advancing` (or a
failure). This checks DMA movement, not panel wiring or visual quality; check on
the panel that the boot animation keeps moving while Wi-Fi connects.

Two aligned 9,240-byte buffers (18,480 bytes total) fit in static RAM. `show()`
packs the inactive buffer, publishes its aligned address, then waits until DMA
has begun reading it before reusing the old buffer. The control channel only
loads a new address at a full-frame boundary. The PIO FIFO preserves ordering
across that boundary. Calls must be serialized on core 0; the board is a single
device-lifetime instance. Each show may wait up to one refresh period; refresh
continues autonomously between calls, including while flash writes run.

Saturation, gamma, brightness, correction and tint follow NG ColorGrade's
ordering. A 14-bit LUT preserves panel precision instead of first quantizing
to an 8-bit intermediate canvas. Gamma is applied **once**, using the NG gamma
setting (default 1.9), replacing Pimoroni's fixed 2.2 curve. Brightness and color
balance scale linear light **after** gamma, unlike the upstream brightness
pre-scale. There is no second fixed gamma curve. The ADC2 light sensor feeds
NG's median filter, light curve and automatic brightness policy.

## Input pins

All nine keys are active low and debounced for 35 ms. B held for 500 ms opens
the shared launcher; A/C step in it. Rotation and swapButtons follow ButtonRouter.
D remains unmapped. Sleep, brightness and volume use the engine command path.
Volume steps the master `volume` (0-100 %), which AudioRouter applies to every group, tones included
(see [Sound](#sound)). The A+C boot rescue is a separate milestone.

| Input | GPIO | Function |
|---|---|---|
| A / B / C | 0 / 1 / 3 | Left / select / right |
| D | 6 | Unmapped |
| Sleep | 27 | Display power toggle |
| Volume up / down | 7 / 8 | NG volume |
| Brightness up / down | 21 / 26 | NG brightness |
| Light sensor | 28 (ADC2) | Automatic brightness |

A, B and C also call the [`buttonCallback`](../reference/system.md) webhook on every press and
release, with the same body as an ESP32 (`left`, `middle`, `right`). Connecting and waiting for
the answer are each capped at 300 ms, so an unreachable listener only stalls the display briefly.

## Wi-Fi and setup

The shared `NetworkService` loads stored Wi-Fi credentials and uses the same
boot timeout (15 seconds by default), five-second reconnect checks and weak-signal
roam policy as ESP32. The boot join is tried up to three times before the Pico
falls back to the setup network: after an unclean reset the AP can still hold the
old session and break off the first join mid key exchange. A join is only
restarted once the previous one has had its full connect timeout. In provisioning
mode the Pico retries the saved network every **60 seconds**, not 30: its one
radio can only join on the AP's channel while the AP is up, so each retry stops
the AP and captive DNS for one station-only join (up to the join timeout) and
starts them again cleanly if it fails. The setup network disappearing for those
seconds is expected. Retries pause while a phone is attached. Static IP uses
Pico's different argument order and configures both DNS servers. Pico uses the
core's worldwide country default rather than ESP32's country/scan/sort APIs.

Lessons from real routers and mesh networks, kept on every join: radio power save
is turned off (ESP32 parity, `WIFI_PS_NONE`), and the CYW43 firmware's own
roaming is turned off (`roam_off=1`); roaming stays with the shared weak-signal
policy. The radio reports an AP that breaks off one join mid key exchange the
same way as a wrong password, so Wi-Fi `lastError` reads `badCredentials` only
when two joins in a row were refused. Every CYW43 event is logged at debug level,
and link-up logs the AP and RSSI.

On a fresh boot without credentials, expect the boot logo followed by the
animated rainbow **AP MODE** screen. A phone sees an open SSID
**awtrixng-xxxxxx**, where `xxxxxx` is the last three MAC bytes in lowercase hex
(or the configured hostname). DHCP and wildcard captive DNS use **192.168.4.1**.
Hold **B / SELECT** for one second at boot to force this mode without erasing
credentials (the 500 ms launcher hold only applies once the clock runs).
An unset address reads `0.0.0.0`, as on ESP32.

After a successful STA boot, mDNS publishes `<hostname>.local`, `_http._tcp`
and `_awtrixng._tcp` on the configured web port (default 80), with the same
`id` (lowercase MAC without colons), `name` and `type=awtrixng` TXT records.
LEAmDNS is polled every loop. These records match ESP32. The shared HTTP
server serves the embedded, gzip-compressed web UI and `/api/v1` on port 80
in AP mode, or `webPort` (default 80) after a station-mode boot.

### Provision from a phone

1. Power on without saved credentials, or hold **B / SELECT** for one second
   during boot. Join the open `awtrixng-xxxxxx` Wi-Fi network (or your saved
   hostname). Accept the phone's **stay connected without Internet** prompt.
2. Open the captive portal, or explicitly browse to **http://192.168.4.1/**.
   The setup form offers Wi-Fi scanning or manual SSID entry. Enter your
   network name and password and save the changes.
3. Use the form's **Reboot** action to join immediately. Alternatively disconnect
   the phone from the AP: every 60 seconds the Pico pauses the AP for a
   station-only join, and once it joins the saved network it reboots. Retries
   deliberately pause while a phone remains attached, so do not wait on the
   open portal.
4. Rejoin your normal Wi-Fi and open **http://awtrixng-xxxxxx.local/** (or
   `http://<configured-hostname>.local:<webPort>/`). If mDNS is unavailable,
   use the address shown in the router's DHCP lease list or USB serial log.
5. Verify the API with `curl -i http://awtrixng-xxxxxx.local/api/v1/device`.
   Expect HTTP 200 and JSON containing `uid`, `soc` (`rp2040`), `ipAddress`,
   `freeHeapBytes` and `resetReason`. Add the configured port and Basic auth if
   you enabled them.

### HTTP port notes

The shared API enforces the same 8 KiB ordinary-body limit (413), JSON content
type checks (415), settings validation (422) and provisioning-mode admission as
on ESP32. The Pico WebServer uses its `HTTPServer&` request hooks, including
`canRaw`; larger bodies stream into the fixed arena instead of an unbounded
String. File and melody listings use LittleFS's File API rather than an ESP VFS
mount, and file hashes use BearSSL's SHA-256. Scripting, MP3 and radio routes
keep the shared 503 `unavailable` policy. `/update` answers 503 `unavailable`
with an instruction to flash a **UF2 over USB using BOOTSEL**: browser OTA is not
supported. `updateImage` names the UF2 for this board
(`firmware-galactic-unicorn.uf2` or `firmware-galactic-unicorn-2w.uf2`); use it,
not an ESP32 `.bin` image.

Device heap facts use `rp2040.getFreeHeap()`. `minFreeHeapBytes` is the minimum
sampled by device-state requests, not an allocator-wide low-water mark.
`largestFreeBlockBytes` is the free block at the top of the heap: newlib cannot
list the holes further down, so the true largest block can only be larger.
PSRAM facts are omitted. `scriptHeapPool` is `unavailable` with a budget of 0.
The body-copy guard checks total free heap on the Pico, not the largest block.

arduino-pico's lwIP has five TCP pcbs. The listener runs at its clients' TCP
priority, so a SYN that finds the pool full is dropped (the browser retries)
instead of aborting a live client, and lwIP's `accept(NULL, ERR_MEM)` for that
SYN never reaches `WiFiServer`. The firmware links `_printf_float`; without it
every float in the JSON API renders empty and the web UI falls back to
provisioning mode.

Uploading, deleting or restoring an asset drops the cached icons and palettes,
as on ESP32, and a `mirror` or `rotate` change applies live. A restored backup
keeps the board's pins, panel and Wi-Fi; nested icon folders
(`/ICONS/sub/smile.gif`) restore. At boot a pin map or panel size stored by
another build is replaced by the board's own, so it cannot make every later
`PUT /api/v1/system` fail.

## Time, reset and sleep

The configured POSIX `tz` and `ntpServer` feed the core's asynchronous SNTP
client. It restarts only when either changes, or on a disconnected-to-connected
transition (matching ESP32 reconnect behavior). DNS/NTP retries are asynchronous.
The same `SystemPageClock` as ESP32 converts UTC to local time, including DST;
years before 2020 remain unset, so until the first sync the clock and date show
their unset state. No battery-backed clock is assumed. Effect noise is seeded
from Pico SDK `get_rand_32()` hardware entropy at boot.

Reset reporting uses the framework's best-effort cause: power-on → `poweron`,
watchdog → `watchdog`, reboot → `software`, RUN pin/debug → `external`,
brownout → `brownout` when distinguishable, otherwise `unknown`. ESP32 values
are unchanged. Pico boot logs (`reset: ...`) and `/api/v1/device` report this
reason.

The RP2040 hardware watchdog is armed once the radio is up, with its ~8 s maximum. A Pico that
hangs or hard-faults reboots itself after about 8 seconds, like an ESP32 after a panic, and
`/api/v1/device` then reports `resetReason: "watchdog"`. The reason is read once at boot, before
the watchdog is armed, so a requested reboot or a flash still reads `software`. Boot's Wi-Fi join,
sleep and the HTTP server feed it while they wait:

- a request is parsed only once its header, and a body of up to 2 KB, has fully arrived; a client
  that has not sent them within 5 s is dropped, and a header over 2 KB is answered
  `431 Request Header Fields Too Large` and the connection is closed;
- larger bodies (uploads, restores and script sources, refused ones included) feed it as each
  piece arrives;
- every response is written only as fast as the client takes it; a client that takes nothing for
  5 s is given up on and its connection closed.

A body over 2 KB sent slower than about 180 bytes per second can still hold the device past the
watchdog, which then restarts it; ordinary slow networks are far faster. A factory reset disarms
the watchdog before it removes the settings files.

The Pico has **no ESP32-style deep sleep with GPIO wake**; `POST
/api/v1/device/sleep` is **emulated**. Like a reboot it runs after the display
power animation. Sleep then blanks the panel, enables CYW43 aggressive power
saving, and waits with short yields. The CPU, RAM and panel refresh hardware
remain powered; this is not a low-microamp shutdown. Application and network
handling pause. The requested duration ends sleep; a press of the Sleep key
(GPIO27) also wakes it. A key held on entry must be released before a new press
wakes it. Wake restarts the application like ESP32 deep-sleep wake, but reports
`software`, not `deepSleep`. The ordinary Sleep key display toggle remains
separate and does not enter this timed sleep. Check timer wake and a
release/new GPIO27 press on physical hardware.

## Sound

The Unicorn has an I2S amplifier and speaker instead of a buzzer, and plays the same RTTTL
melodies a buzzer does. It is the router's tone sink: a notification's or `POST /api/v1/audio/play`'s
`"sound": "name"` plays the saved melody `/MELODIES/name.txt`, and `"sound": {"rtttl": "..."}`
plays the notes in the request. The melodies are rendered as 24 kHz, 16-bit square waves with the
buzzer's pitch and note and rest lengths, and a 6 ms gap between notes, and streamed through
arduino-pico's I2S (data GPIO 9, bit clock GPIO 10, word clock GPIO 11) with non-blocking writes,
so sound never holds up the display or the watchdog. The master `volume`, the group volumes and
the volume keys scale the output, including a melody that is playing.

The amplifier enable (GPIO 22) is held low from boot, and whenever nothing plays or the heard
volume is 0. If no PIO state machine or DMA channel is left for I2S, the amplifier stays muted and
`/api/v1/capabilities` reports `rtttl` false. Otherwise it reports `rtttl` true, and `mp3`,
`track`, `radio`, `song`, `speech`, `effect`, `url` and `clip` false: no MP3, DFPlayer track,
radio or PCM sound. Listen on the hardware for the melodies, the volume keys and the idle mute.

## MQTT and Home Assistant

Both Pico builds link the ESP32's MQTT service, so the
[settings, topics, retain flags and Home Assistant discovery](../reference/mqtt.md) are the
same. Restart after changing broker settings, as on an ESP32. Discovery keeps manufacturer
`Blueforcer`, model `AWTRIX NG` and the stock entity names, including the MQTT prefix and the
left/select/right button binary sensors; the device is named `AWTRIX NG` unless a hostname is
set. The Unicorn adds the light sensor entities, and no battery or environment sensor.

The Pico's network stack resolves DNS and `.local` mDNS names only with blocking calls. Each
lookup may pause the loop for up to one second; if a `.local` lookup fails, DNS on the bare label
is tried too, for at most two seconds together. An ESP32 resolves in a background task instead.
TCP connect and write use a 300 ms timeout, the MQTT CONNACK wait is two seconds, and this bounded
work feeds the eight-second hardware watchdog. Failed connections keep the shared retry backoff.

## UDP services

Both Pico builds include shared discovery (query `FIND_AWTRIXNG` on UDP 4210,
reply `host[:port]` on 4211) and Art-Net on UDP 6454 when `artnet` is enabled.
The Pico binding uses WiFiUDP behind the shared `UdpSocket`, so the protocol
code is the ESP32's. Art-Net takes over below the power and AP screens and
returns to apps five seconds after the last frame. Universes start at zero and
hold 170 RGB pixels each, continuing across the 53x11 canvas. Discovery
advertises the shared HTTP server's web port.

Hardware acceptance: flash the UF2, check the SSID/AP MODE behavior above,
capture display and CYW43 PIO/SM logs plus `refresh advancing`, then check the
animated boot screen during a stored-credential join. Host tests and firmware
builds cannot establish physical Wi-Fi/display coexistence.

Host tests cannot establish physical refresh timing, orientation, or absence of
visible tearing; check those on the panel with the CI-built UF2.
