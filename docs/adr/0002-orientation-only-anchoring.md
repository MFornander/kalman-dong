# Cloud Field anchored by orientation, with Travel from deliberate strokes

Pixel positions in the Cloud Field come from the Dong's orientation plus its Travel: the Hilt starts at the field's origin and each Pixel sits at its distance along the Blade's current direction. A 6-axis IMU cannot hold a position estimate (double-integrated acceleration drifts by metres within seconds), so the Dong never tracks position continuously. Instead, each deliberate stroke of the Hilt without rotation is measured on its own, from the moment it starts until the Hilt comes to a stop, and its level part is kept as Travel. Errors stay within one short stroke instead of growing without bound. Everything not kept (up and down, small jiggles, strokes cut short by a swing or lasting over 2 s) is shown as Push, a short-lived displacement that springs back. Walking with the Dong still carries the clouds along, and Re-level clears Travel.

## Considered Options

- **Full position tracking** (integrated acceleration with zero-velocity updates at every pause): rejected. Drift is unbounded without an external reference, and a slipping field breaks the illusion worse than a field that moves with you.
- **Orientation only, with Push springing every move back** (the first version): replaced. A thrust or slide looked alive, but the clouds always returned to the same place, so moving the Hilt never felt like moving through the field.
- **Strokes kept as Travel, gated off while rotating** (chosen):
  - Translation is measured only while the Dong turns slower than 1.5 rad/s. A hard swing makes the IMU in the Hilt feel strong forces as it circles the wrist, which would otherwise read as large moves.
  - Only the level part is kept, so the bob of walking or a dip doesn't wander the field up and down.
  - Strokes are kept at 60% of their measured size. That hides integration error, and the move still feels like passing through fog.
  - On the recorded hand sweeps and swings (`test/data/`), no Travel builds up at all.

## Revisions

- 2026-10-26: Travel added; Push now carries only the moves Travel doesn't keep. Previously all translation sprang back.
