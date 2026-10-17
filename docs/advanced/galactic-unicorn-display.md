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
Volume steps the master `volume` (0-100 %), which AudioRouter applies to every group, tones included; speaker wiring
comes in M5b. The webhook and A+C boot rescue are separate milestones.

| Input | GPIO | Function |
|---|---|---|
| A / B / C | 0 / 1 / 3 | Left / select / right |
| D | 6 | Unmapped |
| Sleep | 27 | Display power toggle |
| Volume up / down | 7 / 8 | NG volume |
| Brightness up / down | 21 / 26 | NG brightness |
| Light sensor | 28 (ADC2) | Automatic brightness |

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
LEAmDNS is polled every loop. These records match ESP32, but **HTTP and its
provisioning form are not wired yet** (M4b): joining the AP cannot save
credentials yet and a phone may report no Internet/no working portal.

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
are unchanged. Pico boot logs report this reason (`reset: ...`).

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

## UDP services

Both Pico builds include shared discovery (query `FIND_AWTRIXNG` on UDP 4210,
reply `host[:port]` on 4211) and Art-Net on UDP 6454 when `artnet` is enabled.
The Pico binding uses WiFiUDP behind the shared `UdpSocket`, so the protocol
code is the ESP32's. Art-Net takes over below the power and AP screens and
returns to apps five seconds after the last frame. Universes start at zero and
hold 170 RGB pixels each, continuing across the 53x11 canvas. Discovery
advertises the web port; the HTTP server itself comes in M4b.

Hardware acceptance: flash the UF2, check the SSID/AP MODE behavior above,
capture display and CYW43 PIO/SM logs plus `refresh advancing`, then check the
animated boot screen during a stored-credential join. Host tests and firmware
builds cannot establish physical Wi-Fi/display coexistence.

Host tests cannot establish physical refresh timing, orientation, or absence of
visible tearing; check those on the panel with the CI-built UF2.
