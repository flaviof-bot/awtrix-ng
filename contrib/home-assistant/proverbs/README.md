# Home Assistant proverbs for AWTRIX NG

Replaces the scheduling side of `proverbs.sh`; the separately installed third-party
**AWTRIX NG Create Notification** blueprint owns sending/dismissing notifications.
No copy or reimplementation of that blueprint is included here.

## Install

1. Enable the MQTT integration and configure your AWTRIX NG display on the same
   broker. Install the notification blueprint yourself at
   `smarthomejunkie/awtrix_ng/awtrix_ng_create_notification.yaml`.
2. Copy `packages/proverbs.yaml` to `/config/packages/proverbs.yaml`. Merge this
   into your existing `configuration.yaml` (do not duplicate `homeassistant:`):

   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   automation: !include automations.yaml
   ```

3. Download the source list from a shell that can write HA's config directory:

   ```sh
   curl --fail --location https://raw.githubusercontent.com/alltom/proverb/master/proverbs.txt --output /config/proverbs.txt
   ```

   The command-line integration needs `shuf` and `tr` (available in the official
   HA container). The sensor picks one random line, strips CR, and truncates to
   250 Unicode characters. Its yearly scan interval disables practical periodic
   polling; HA still performs its initial read. Subsequent reads are requested by
   the pacing automation. Download again manually to refresh the source list.
4. In the package's ONE marked `motion_entity` variable, confirm or replace the
   supplied office motion entity. Missing/unknown/unavailable entities fail closed.
   Motion is allowed when on, or when its last state change was less than two
   hours ago. HA startup can reset `last_changed`; this intentionally follows the
   requested last-changed rule, not persistent motion history.
5. Append the list item in `automation_example.yaml` to `automations.yaml`, or
   instantiate the installed blueprint and set equivalent inputs. Replace
   `REPLACE_WITH_AWTRIX_DEVICE_ID` with the HA device ID (not an entity ID), OR
   use `awtrix_displays: []` and `extra_prefixes: [your_mqtt_prefix]`.
   The colours are templates returning RGB lists, not literal template strings
   to send over MQTT. Confirm your installed blueprint supports templated colour
   inputs; its selectors/implementation are not distributed or inspected here.
6. Run HA's configuration check, then restart HA to load helpers and sensors.
   Confirm the generated entity IDs match those in the example (rename existing
   conflicting entities first). Watch the pacing automation's trace and the
   blueprint automation's trace. The interval helper starts at 96 seconds; each
   restart adds a uniformly random 0–15 seconds. The timer is restarted even if
   there is no motion. Restarting HA starts a fresh timer and checks motion once.

## Behaviour and parity

- A trigger-based pair of template sensors redraws colours for every selected
  proverb, even if the same line is selected twice. Text is randomly selected
  from red, green, yellow, magenta, cyan and white: never black or blue.
  Background is black with probability 6/7, pure blue with probability 1/7.
- Colour sensor states contain JSON-compatible RGB lists. The example parses
  them back into lists. Pacing waits for both colour sensors before toggling.
- The toggle goes off (dismiss), waits one second, then on (notify). This keeps
  independent blueprint runs ordered in normal operation. The blueprint owns
  MQTT delivery; a config check cannot guarantee delivery timing or connectivity.
- Notifications use name `proverb`, no hold, one repeat, no stacking, scrolling
  at speed 100, no icon, no gradient/rainbow, no effect, no sound, duration 0.
  AWTRIX has no text outline; the old script's outline cannot be reproduced.
- This package does not stop an already scrolling notification when motion
  expires; it simply stops selecting new proverbs until motion qualifies again.
- The package does not publish MQTT directly. Do not run the old service and this
  automation together or they will compete for the display.

## Retire the shell loop

After testing HA, on the machine that actually owns the old system service:

```sh
sudo systemctl disable --now proverbs.service
systemctl is-active proverbs.service
systemctl is-enabled proverbs.service
```

Expect `inactive` and `disabled` (these queries may return nonzero). For a user
service use `systemctl --user` instead, without sudo. Keep the old script/unit as
rollback initially; stop the HA pacing automation and cancel `timer.proverb_pacing`
before re-enabling the service. Do not disable an unrelated unit on the HA host.

## Verification limits

A throwaway Home Assistant configuration can validate the package and blueprint
input wiring with a hand-written no-op stub declaring only the documented input
names. Such a stub is NOT the copyrighted blueprint and does not test its
selectors, template evaluation, or MQTT actions. Final verification with the
user-installed blueprint and real device remains a human installation step.
The simulator smoke test exercises the documented notification JSON contract
and the empty-payload dismissal, independently of that blueprint.
