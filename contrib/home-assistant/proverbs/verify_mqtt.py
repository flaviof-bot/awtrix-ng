"""Documented MQTT-contract smoke test, not a blueprint implementation, plus
an on-screen check of automation_direct.yaml's rendered payload.
Run in the official HA image on h1-net. --prepare writes only /lab fixtures.
"""
import argparse
import asyncio
import json
from pathlib import Path
import queue
import threading
import time
import urllib.request

import yaml

ROOT = Path(__file__).parent


def prepare():
    lab = Path('/lab')
    (lab / 'simdata').mkdir(parents=True, exist_ok=True)
    (lab / 'mosquitto.conf').write_text('listener 1883\nallow_anonymous true\npersistence false\n')
    (lab / 'simdata/device.json').write_text(json.dumps({
        'panelWidth': 53, 'panelHeight': 11, 'panels': 1,
        'mqttEnabled': True, 'mqttHost': 'h1-mqtt', 'mqttPort': 1883,
        'mqttPrefix': 'h1', 'haDiscovery': False,
    }))


async def colours():
    from homeassistant.core import HomeAssistant
    from homeassistant.helpers.template import Template
    hass = HomeAssistant('/tmp/h1-template')
    package = yaml.safe_load((ROOT / 'packages/proverbs.yaml').read_text())
    result = [Template(s['state'], hass).async_render() for s in package['template'][0]['sensor']]
    await hass.async_stop()
    return result


async def direct_payload(text, text_colour, background_colour):
    """Render automation_direct.yaml's topic and payload templates as HA would."""
    from homeassistant.core import HomeAssistant
    from homeassistant.helpers.template import Template
    hass = HomeAssistant('/tmp/h2-template')
    hass.states.async_set('sensor.proverb', text)
    hass.states.async_set('sensor.proverb_text_color', json.dumps(text_colour))
    hass.states.async_set('sensor.proverb_background_color', json.dumps(background_colour))
    automation = yaml.safe_load((ROOT / 'automation_direct.yaml').read_text())[0]
    data = automation['actions'][0]['data']
    variables = {'prefix': 'h1'}
    topic = Template(data['topic'], hass).async_render(variables, parse_result=False)
    payload = Template(data['payload'], hass).async_render(variables, parse_result=False)
    await hass.async_stop()
    return topic, payload


def lit_columns(screen, colour=None):
    """Columns holding a non-black pixel, or a pixel of exactly `colour` (0xRRGGBB)."""
    width, pixels = screen['width'], screen['pixels']
    return sorted({i % width for i, value in enumerate(pixels)
                   if (value != 0 if colour is None else value == colour)})


def smoke():
    import paho.mqtt.client as mqtt
    source = 'https://raw.githubusercontent.com/alltom/proverb/master/proverbs.txt'
    with urllib.request.urlopen(source, timeout=30) as response:
        lines = response.read().decode().splitlines()
    # Quote a genuine source proverb containing an apostrophe. The source has
    # no single line containing BOTH double quotes and an apostrophe.
    original = next(line for line in lines if "'" in line)
    text = '"' + original + '"'
    assert len(text) <= 250 and '"' in text and "'" in text
    print('Source proverb:', original, flush=True)
    text_colour, background_colour = asyncio.run(colours())
    example = yaml.safe_load((ROOT / 'automation_example.yaml').read_text())[0]['use_blueprint']['input']
    payload = {
        'name': example['notification_name'], 'text': text, 'icon': example['my_icon'],
        'backgroundColor': background_colour, 'textColor': text_colour, 'palette': None,
        'textCase': example['text_case'], 'iconMode': example['icon_mode'],
        'hold': example['hold_notification'], 'stack': example['stack'],
        'repeat': example['repeat'], 'durationMs': example['duration'] * 1000,
        'scroll': {'speed': example['scrollspeed']}, 'effect': example['effect'],
        'sound': example['sound_name'], 'soundRtttl': example['sound_rtttl'],
    }
    messages = queue.Queue()
    subscribed = threading.Event()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id='h1-contract-test')
    client.on_connect = lambda c, u, f, rc, p: c.subscribe('h1/#')
    client.on_subscribe = lambda *args: subscribed.set()
    client.on_message = lambda c, u, m: messages.put((m.topic, m.payload.decode()))
    client.connect('h1-mqtt', 1883, 30)
    client.loop_start()

    def receive(topic):
        while True:
            actual, body = messages.get(timeout=30)
            if actual == topic:
                print(actual, body, flush=True)
                return body

    try:
        assert subscribed.wait(10), 'No SUBACK'
        assert receive('h1/availability') == 'online'
        client.publish('h1/cmd/screen/get', '').wait_for_publish()
        screen = json.loads(receive('h1/state/screen'))
        assert (screen['width'], screen['height']) == (53, 11)
        body = json.dumps(payload, ensure_ascii=False)
        print('PUBLISH h1/cmd/notify', body, flush=True)
        client.publish('h1/cmd/notify', body).wait_for_publish()
        assert json.loads(receive('h1/cmd/notify/result')) == {'ok': True}
        print('PUBLISH h1/cmd/notify/dismiss/proverb <empty>', flush=True)
        client.publish('h1/cmd/notify/dismiss/proverb', '').wait_for_publish()
        assert json.loads(receive('h1/cmd/notify/dismiss/proverb/result')) == {'ok': True}
        print('PASS 53x11 notification and empty-payload dismissal', flush=True)

        def screen_get():
            client.publish('h1/cmd/screen/get', '').wait_for_publish()
            while True:
                actual, body = messages.get(timeout=30)
                if actual == 'h1/state/screen':
                    return json.loads(body)

        def publish_direct(sample, colour):
            topic, body = asyncio.run(direct_payload(sample, colour, [0, 0, 0]))
            assert topic == 'h1/cmd/notify', topic
            parsed = json.loads(body)
            assert parsed['font'] == 'large' and parsed['repeat'] == 1 and parsed['stack'] is True
            assert parsed['scroll'] == {'speed': 100, 'entry': 'offscreen', 'whenFits': 'scroll'}
            assert parsed['text'] == sample and parsed['textColor'] == colour
            print('PUBLISH', topic, body, flush=True)
            client.publish(topic, body).wait_for_publish()
            assert json.loads(receive('h1/cmd/notify/result')) == {'ok': True}
            return time.monotonic()

        # Direct automation: the text enters at the right edge and moves left.
        publish_direct(text, [255, 255, 255])
        first = lit_columns(screen_get())
        print('DIRECT first frame lit columns:', first, flush=True)
        assert first and min(first) >= 45, first
        assert not [x for x in first if x <= 40], first
        time.sleep(1)
        later = lit_columns(screen_get())
        print('DIRECT +1 s lit columns:', later, flush=True)
        assert later and min(later) < min(first), (first, later)
        print('PASS direct payload accepted; entered at the right edge, then moved left', flush=True)
        client.publish('h1/cmd/notify/dismiss/proverb', '').wait_for_publish()
        assert json.loads(receive('h1/cmd/notify/dismiss/proverb/result')) == {'ok': True}

        # repeat 1 ends only after the whole text has left, well past appDurationMs (7 s).
        long_text = ' '.join(line for line in lines if len(line) > 40)[:90]
        green = [0, 255, 0]
        start = publish_direct(long_text, green)
        last_seen, last_columns = None, None
        while time.monotonic() - start < 60:
            columns = lit_columns(screen_get(), 0x00FF00)
            now = time.monotonic() - start
            if columns:
                last_seen, last_columns = now, columns
            elif last_seen is not None:
                break
            time.sleep(0.25)
        print(f'REPEAT last green frame at {last_seen:.2f} s, columns {last_columns}', flush=True)
        assert last_seen is not None and last_seen > 8, last_seen
        assert max(last_columns) <= 12, last_columns
        assert not lit_columns(screen_get(), 0x00FF00)
        print(f'PASS repeat 1: {len(long_text)} chars scrolled {last_seen:.1f} s (> 7 s) '
              'and ended only after the last column cleared', flush=True)
    finally:
        client.disconnect()
        client.loop_stop()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    prepare() if args.prepare else smoke()
