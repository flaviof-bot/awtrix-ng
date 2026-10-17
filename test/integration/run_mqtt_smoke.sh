#!/bin/sh
# Run from the playground checkout after building awtrix-linux (ci-local.sh linux-build).
# No host ports exposed. AWTRIX_BUILD_IMAGE and PIO_HOME_VOLUME select the build image.
set -eu

# Reuse this entry point inside the ephemeral build container, without inline shell code.
if [ "${1:-}" = "--inside-container" ]; then
    python -m pip install --disable-pip-version-check --quiet paho-mqtt==2.1.0
    exec python test/integration/mqtt_smoke.py --broker mqtt-smoke-broker
fi

cd "$(dirname "$0")/../.."
image=${AWTRIX_BUILD_IMAGE:-localhost/awtrix-build:ng122}
volume=${PIO_HOME_VOLUME:-pio-home-ng122}
test -x .pio/linux/awtrix-linux
network_created=false
broker_started=false
cleanup() {
    status=$?
    trap - EXIT
    if [ "$broker_started" = true ]; then
        podman stop mqtt-smoke-broker || :
    fi
    if [ "$network_created" = true ]; then
        podman network rm mqtt-smoke || :
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

podman network create mqtt-smoke
network_created=true
podman run --detach --rm --cpus 1 --name mqtt-smoke-broker --network mqtt-smoke \
    -v "$PWD/test/integration/mosquitto.conf:/mosquitto/config/mosquitto.conf:ro,Z" \
    docker.io/library/eclipse-mosquitto:2
broker_started=true

# Probe readiness instead of racing the listener startup.
attempt=0
until podman exec mqtt-smoke-broker mosquitto_pub -h 127.0.0.1 -t smoke/ready -m ready; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 30 ]; then
        podman logs mqtt-smoke-broker
        exit 1
    fi
    sleep 1
done

podman run --rm --cpus 4 --network mqtt-smoke --entrypoint "" \
    -v "$volume:/root/.platformio" -v "$PWD:/w:Z" -w /w \
    "$image" sh test/integration/run_mqtt_smoke.sh --inside-container
