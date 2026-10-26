# Cloud Field anchored by orientation only, Hilt at the origin

Pixel positions in the Cloud Field come from the Dong's orientation alone: the Hilt is treated as fixed at the field's origin and each Pixel sits at its distance along the Blade's current direction. A 6-axis IMU cannot hold a position estimate (double-integrated acceleration drifts by metres within seconds), so walking with the Dong carries the clouds along; arm translation during a swing is small relative to a 1–2 m Blade. Translation without rotation is answered only by Push — a short-lived, spring-back displacement drawn as a Local Layer — never by a persistent position.

## Considered Options

- **Full position tracking** (integrated acceleration, zero-velocity updates) — rejected: drift is unbounded without an external reference, and a slipping field breaks the illusion worse than a field that moves with you.
- **Orientation only, no response to translation** — rejected: thrusts and slides feel dead; Push gives a believable reaction at no drift cost.
