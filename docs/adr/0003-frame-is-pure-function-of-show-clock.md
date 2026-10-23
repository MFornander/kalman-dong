# A frame depends only on Show Clock, orientation and settings

The synced part of every frame is a pure function of the Show Clock, the Dong's orientation, and its settings (Palette, Girth, Clear Sky, brightness). No hidden state may accumulate in it — no random walks, no lingering effects. This makes later multi-Dong sync (as in LightSwarm) a matter of sharing the Show Clock only: identical inputs render identically on every Dong, and frames can be golden-tested on the host. Anything that depends on one Dong's own motion history, such as Push, must be a Local Layer drawn on top and is never synced.

## Consequences

- Cloud drift and evolution must be expressed as functions of time (e.g. noise sampled at a time-dependent coordinate), not as stepped simulations.
- The Show Clock must not wrap during a session (LightSwarm's 32-bit µs mesh time wrapped every ~71 minutes and made effects jump).
- Heading still differs per Dong (no magnetometer), so two synced Dongs share cloud timing and colour, not exact cloud placement.
