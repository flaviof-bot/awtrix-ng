"""Run only in a throwaway official HA container with writable /config.

Creates an explicitly inert blueprint fixture, NOT the third-party blueprint.
Usage: python verify_config.py
"""
import asyncio
from pathlib import Path
import shutil
import subprocess

import yaml

ROOT = Path(__file__).parent
CONFIG = Path('/config')
INPUTS = '''awtrix_displays extra_prefixes toggle_helper notification_text my_icon
icon_mode text_case background_color text_color gradient_1 gradient_2 show_rainbow
notification_name play_alert_tone sound_name sound_rtttl hold_notification repeat
duration stack scroll scrollspeed effect'''.split()


def prepare():
    (CONFIG / 'packages').mkdir(parents=True, exist_ok=True)
    shutil.copy(ROOT / 'packages/proverbs.yaml', CONFIG / 'packages/proverbs.yaml')
    shutil.copy(ROOT / 'automation_example.yaml', CONFIG / 'automations.yaml')
    (CONFIG / 'configuration.yaml').write_text(
        'homeassistant:\n  packages: !include_dir_named packages\n'
        'automation: !include automations.yaml\n')
    stub = CONFIG / 'blueprints/automation/smarthomejunkie/awtrix_ng/awtrix_ng_create_notification.yaml'
    stub.parent.mkdir(parents=True, exist_ok=True)
    stub.write_text(yaml.safe_dump({
        'blueprint': {'name': 'H1 inert input-schema fixture', 'domain': 'automation',
                      'input': {name: {'default': None} for name in INPUTS}},
        'triggers': [], 'actions': [],
    }))
    (CONFIG / 'proverbs.txt').write_text('A rolling stone gathers no moss.\r\n')


async def templates():
    from homeassistant.core import HomeAssistant
    from homeassistant.helpers.template import Template
    from homeassistant.util import dt
    from datetime import timedelta
    hass = HomeAssistant(str(CONFIG))
    package = yaml.safe_load((ROOT / 'packages/proverbs.yaml').read_text())
    pacing = package['automation'][0]
    motion = pacing['actions'][1]['value_template']
    entity = pacing['variables']['motion_entity']
    assert not Template(motion, hass).async_render({'motion_entity': entity})
    for state, age, expected in [('on', 8000, True), ('off', 7190, True),
                                  ('off', 7210, False), ('unavailable', 0, False),
                                  ('unknown', 0, False)]:
        hass.states.async_set(entity, state, force_update=True)
        hass.states.get(entity).last_changed = dt.utcnow() - timedelta(seconds=age)
        assert Template(motion, hass).async_render({'motion_entity': entity}) is expected
    print('PASS motion: missing/on/recent-off/expired-off/unavailable/unknown')
    hass.states.async_set('input_number.proverb_interval_s', '96')
    duration = Template(pacing['actions'][0]['data']['duration'], hass)
    for _ in range(200):
        assert 96 <= duration.async_render() <= 111
    print('PASS interval: 200 samples in 96..111 seconds')
    sensors = package['template'][0]['sensor']
    for _ in range(200):
        text = Template(sensors[0]['state'], hass).async_render()
        bg = Template(sensors[1]['state'], hass).async_render()
        assert len(text) == 3 and text not in ([0, 0, 0], [0, 0, 255])
        assert bg in ([0, 0, 0], [0, 0, 255])
    print('PASS colours: 200 RGB samples, text exclusions and background palette')
    sensor = package['command_line'][0]['sensor']
    value = subprocess.check_output(sensor['command'], shell=True, text=True).strip()
    assert '\r' not in value and value == 'A rolling stone gathers no moss.'
    assert len(Template(sensor['value_template'], hass).async_render({'value': 'é' * 300})) == 250
    print('PASS command-line random line, CR stripping and 250 Unicode characters')
    await hass.async_stop()


if __name__ == '__main__':
    prepare()
    subprocess.run(['python', '-m', 'homeassistant', '--script', 'check_config', '-c', str(CONFIG)], check=True)
    print('PASS check_config (inert blueprint fixture only)', flush=True)
    asyncio.run(templates())
