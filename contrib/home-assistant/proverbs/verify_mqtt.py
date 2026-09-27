"""Documented MQTT-contract smoke test, not a blueprint implementation.
Run in the official HA image on h1-net. --prepare writes only /lab fixtures.
"""
import argparse
import asyncio
import json
from pathlib import Path
import queue
import threading
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
    finally:
        client.disconnect()
        client.loop_stop()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    prepare() if args.prepare else smoke()
