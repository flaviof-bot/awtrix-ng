#!/usr/bin/env python3
"""Real-broker MQTT contract check; run only against a disposable broker.

Requires paho-mqtt==2.1.0. By default it starts awtrix-linux at 53x11 (the Galactic Unicorn's
panel) with a temporary data directory; run_mqtt_smoke.sh does that inside the build container on
a Podman network:
  python test/integration/mqtt_smoke.py --broker mqtt-smoke-broker
Against a real device (the bench), configure the device's broker and set mqttPrefix to
"mqttsmoke" and haDiscovery on first, then:
  python test/integration/mqtt_smoke.py --broker BROKER [--user U --password P] \
      --device http://DEVICE --buttons manual
All command writes are undone.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request

import paho.mqtt.client as mqtt

PREFIX = "mqttsmoke"
BUTTONS = ("left", "select", "right")


class Observer:
    def __init__(self, broker, port, user=None, password=None):
        self.messages = []
        self.condition = threading.Condition()
        self.ready = threading.Event()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        if user:
            self.client.username_pw_set(user, password)
        self.client.on_connect = self.on_connect
        self.client.on_subscribe = lambda *args: self.ready.set()
        self.client.on_message = self.on_message
        self.client.connect(broker, port, 10)
        self.client.loop_start()
        assert self.ready.wait(10), "broker subscription timed out"

    def on_connect(self, client, userdata, flags, reason, properties):
        assert reason == 0, reason
        client.subscribe([("homeassistant/#", 0), (PREFIX + "/#", 0)])

    def on_message(self, client, userdata, message):
        with self.condition:
            self.messages.append({"topic": message.topic,
                                  "payload": message.payload.decode(),
                                  "retain": message.retain})
            self.condition.notify_all()

    def wait(self, topic, payload=None, start=0, timeout=15):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for message in self.messages[start:]:
                    if message["topic"] == topic and (payload is None or message["payload"] == payload):
                        return message
                remaining = deadline - time.monotonic()
                assert remaining > 0, f"missing {topic}: {payload}"
                self.condition.wait(remaining)

    def wait_retained(self, topic, timeout=15):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for message in self.messages:
                    if message["topic"] == topic and message["retain"]:
                        return message
                remaining = deadline - time.monotonic()
                assert remaining > 0, f"no retained copy of {topic}"
                self.condition.wait(remaining)

    def command(self, suffix, payload):
        start = len(self.messages)
        topic = PREFIX + "/cmd/" + suffix
        self.client.publish(topic, payload).wait_for_publish(5)
        reply = self.wait(topic + "/result", start=start)
        assert json.loads(reply["payload"]) == {"ok": True}, reply
        assert not reply["retain"], reply
        return reply

    def close(self):
        self.client.disconnect()
        self.client.loop_stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--broker", required=True)
    parser.add_argument("--broker-port", type=int, default=1883)
    parser.add_argument("--user")
    parser.add_argument("--password")
    parser.add_argument("--program", default=".pio/linux/awtrix-linux")
    parser.add_argument("--device", help="base URL of a running device; nothing is started")
    parser.add_argument("--buttons", choices=("manual", "skip"),
                        help="manual: a person presses A, B and C; skip: no button input "
                             "(default: skip for awtrix-linux, manual with --device)")
    parser.add_argument("--report", default=".pio/mqtt-smoke-report.json")
    args = parser.parse_args()
    buttons = args.buttons or ("manual" if args.device else "skip")
    port = 18087
    base = args.device.rstrip("/") if args.device else f"http://127.0.0.1:{port}"
    report = {"target": base, "commands": [], "checks": [], "skipped": []}
    observer = Observer(args.broker, args.broker_port, args.user, args.password)
    sim = None

    def fresh_observer():
        return Observer(args.broker, args.broker_port, args.user, args.password)

    def http(path, body=None):
        request = urllib.request.Request(base + path,
            data=None if body is None else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=5) as response:
            return json.load(response)

    def wait_http():
        deadline = time.monotonic() + 30
        while True:
            try:
                return http("/api/v1/device")
            except (urllib.error.URLError, TimeoutError, ConnectionError):
                assert time.monotonic() < deadline, "device HTTP not ready"
                time.sleep(0.2)

    try:
        with tempfile.TemporaryDirectory(prefix="awtrix-mqtt-") as data, \
                tempfile.TemporaryFile(mode="w+") as log:
            def start_sim():
                return subprocess.Popen([str(Path(args.program).resolve()), "--width", "53",
                                         "--height", "11", "--port", str(port), "--data", data],
                                        stdout=log, stderr=subprocess.STDOUT)
            if not args.device:
                Path(data, "device.json").write_text(json.dumps({
                    "mqttEnabled": True, "mqttHost": args.broker, "mqttPort": args.broker_port,
                    "mqttUser": args.user or "", "mqttPass": args.password or "",
                    "mqttPrefix": PREFIX, "haDiscovery": True, "hostname": "mqtt-smoke-linux",
                    "statsInterval": 1, "scriptingEnabled": False}))
                sim = start_sim()
            device = wait_http()
            uid = device["uid"]
            discovery_topic = f"homeassistant/device/{uid}/config"
            if args.device:
                # The device is already online; a fresh subscription sees its retained state.
                observer.wait_retained(PREFIX + "/availability")
                observer.wait_retained(discovery_topic)
            else:
                observer.wait(PREFIX + "/availability", "online", timeout=30)

            discovery = observer.wait(discovery_topic)
            report["discovery"] = json.loads(discovery["payload"])
            doc = report["discovery"]
            assert doc["dev"]["mf"] == "Blueforcer"
            assert doc["dev"]["mdl"] == "AWTRIX NG"
            assert doc["cmps"]["prefix"]["name"] == "MQTT prefix"
            assert doc["cmps"]["prefix"]["stat_t"] == "~/state/prefix"
            for key, name in zip(("btnl", "btnm", "btnr"), BUTTONS):
                comp = doc["cmps"][key]
                assert comp["name"] == "Button " + name and comp["p"] == "binary_sensor"
                assert comp["stat_t"] == "~/state/buttons/" + name
                assert comp["pl_on"] == "1" and comp["pl_off"] == "0"
            report["checks"].append("Blueforcer, MQTT prefix and stock button discovery")

            before = http("/api/v1/device")["messageCount"]
            report["commands"].append(observer.command("notify", '{"name":"smoke-note","text":"MQTT smoke","hold":true}'))
            report["commands"].append(observer.command("apps/pushed/test", '{"text":"Pico MQTT"}'))
            assert http("/api/v1/device")["messageCount"] == before + 2
            screen_start = len(observer.messages)
            observer.command("screen/get", "")
            screen = observer.wait(PREFIX + "/state/screen", start=screen_start)
            assert not screen["retain"]
            report["checks"].append("notify/app results, inbound-only count and non-retained screen")

            if buttons == "manual":
                report["button_edges"] = []
                for name, key in zip(BUTTONS, "ABC"):
                    start = len(observer.messages)
                    print(f"press and release {key} ({name}) within 60 s", flush=True)
                    topic = PREFIX + "/state/buttons/" + name
                    press = observer.wait(topic, "1", start=start, timeout=60)
                    after = next(i for i, m in enumerate(observer.messages) if m is press) + 1
                    release = observer.wait(topic, "0", start=after, timeout=60)
                    assert not press["retain"] and not release["retain"]
                    report["button_edges"] += [press, release]
                report["checks"].append("left/select/right press and release edges")
            else:
                report["skipped"].append("button press and release edges: awtrix-linux takes "
                                         "buttons only from TC002 input devices (no HTTP or MQTT "
                                         "input); test_buttonedges covers the edge queue")

            # Retain is observed on a NEW subscription, not the live delivery.
            fresh = fresh_observer()
            try:
                topics = ["availability", "state/capabilities", "state/prefix", "state/device",
                          "state/settings", "state/audio", "state/apps/active"]
                retained = [fresh.wait_retained(PREFIX + "/" + topic) for topic in topics]
                retained.append(fresh.wait_retained(discovery_topic))
                assert all(message["retain"] for message in retained), retained
                time.sleep(0.5)
                # Edges are live only; the device clears any retained button state on connect.
                assert not any(m["topic"].endswith("/result") or m["topic"] == PREFIX + "/state/screen"
                               or (m["topic"].startswith(PREFIX + "/state/buttons/") and m["payload"])
                               for m in fresh.messages), fresh.messages
                report["retained_topics"] = [m["topic"] for m in retained]
            finally:
                fresh.close()
            report["checks"].append("state/discovery retained; replies, screen and button edges not retained")

            # Undo command writes even though a simulator store is disposable.
            observer.command("apps/pushed/test", "")
            observer.command("notify/dismiss", "")
            report["checks"].append("test app and notification removed")

            start = len(observer.messages)
            if sim is not None:
                sim.kill()  # no MQTT DISCONNECT: the broker must deliver the will
                sim.wait(timeout=5)
            else:
                # A reboot drops the TCP session without an MQTT DISCONNECT.
                observer.client.publish(PREFIX + "/cmd/device/reboot", "").wait_for_publish(5)
            report["will"] = observer.wait(PREFIX + "/availability", "offline", start=start, timeout=60)
            if sim is not None:
                fresh = fresh_observer()
                try:
                    assert fresh.wait(PREFIX + "/availability", "offline")["retain"]
                finally:
                    fresh.close()
            report["checks"].append("abrupt exit publishes the offline last will")

            start = len(observer.messages)
            if sim is not None:
                sim = start_sim()
            observer.wait(PREFIX + "/availability", "online", start=start, timeout=120)
            observer.wait(discovery_topic, start=start)
            observer.wait(PREFIX + "/state/prefix", PREFIX, start=start)
            report["checks"].append("restart reannounces online, discovery and prefix")
            if sim is not None:
                sim.terminate()
                sim.wait(timeout=5)
                sim = None
                log.seek(0)
                report["program_log"] = log.read()
        Path(args.report).parent.mkdir(parents=True, exist_ok=True)
        Path(args.report).write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
        for skipped in report["skipped"]:
            print("SKIP " + skipped)
        print(f"PASS MQTT integration: {len(report['checks'])} contract checks, "
              f"{len(report['skipped'])} skipped")
    finally:
        if sim is not None and sim.poll() is None:
            sim.kill()
            sim.wait(timeout=5)
        observer.close()


if __name__ == "__main__":
    main()
