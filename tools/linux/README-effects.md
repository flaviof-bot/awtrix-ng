# Effect survey on the Linux app

Run in the build container from the repository root after the Linux build
(`tools/container/ci-local.sh linux`, which leaves `.pio/linux/awtrix-linux`):

```console
python tools/linux/effect_survey.py --size 32x8 --size 53x11 --size 32x16 --size 52x16 --out .pio/effect-survey
```

For each size the tool starts its own `awtrix-linux --width W --height H` with a temporary data
directory and a loopback port (through `tests/runtime.py`), lists the effects from
`/api/v1/capabilities`, holds an empty-text notification with each effect and samples
`/api/v1/display/screen`. It writes one scaled PNG strip per effect, `all.png` with one row per
effect in capability order, and `summary.md` / `summary.json` with the lit and changing rows.
PNG encoding uses only Python's standard library. The app is stopped and the data removed even on
failure.

Defaults: 12 frames, 250 ms apart, after 500 ms settling, 4x nearest-neighbour scaling.
`--frames`, `--interval`, `--settle`, `--scale`, `--binary` and `--webui` are configurable. Rows
are zero-based; lit and changing rows are unions over the sampled frames. These are wall-clock
observations, not a whole-cycle guarantee or golden snapshots: sparse effects and periodic
sampling can omit rows or repeat frames. Frame-exact coverage and determinism are the host tests'
job (`test/test_tall_effects`, `test/test_effects`).

At 52x16 the Linux app has a page zoom, as the TC002 does, so a notification's effect is drawn at
double size on the 26x8 grid; the other sizes have none and use every pixel.

## Tall panels

1.2.2 already sizes every effect to the canvas: the swirls paint every pixel, and BrickBreaker,
PingPong and Snake are simulations on a field of the canvas size (`SimulatedEffects.h`,
`fx::PixelGrid`). `test/test_tall_effects` pins that at widths 32 and 53 and every height from 8
to 16: the swirls fill the canvas and drift in opposite directions; BrickBreaker's bricks are 3
LEDs with a 1-LED gap, one brick goes per hit, the ball never stands on a brick and leaves the
paddle at 45 degrees from its middle or shallow (2 across per 1 up) from its edges; the snake
eats, grows, stays connected without overlapping itself, mostly closes on the apple and restarts;
both PingPong paddles are drawn on the outer columns and both return the ball. Determinism and
frame-buffer guard sentinels cover widths 4 to 80.
