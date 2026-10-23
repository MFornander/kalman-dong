# Own SK9822 driver on SPI2 DMA instead of FastLED

Pixels are sent by our own ~150-line driver ([src/sk9822_spi.cpp](../../src/sk9822_spi.cpp)) using ESP-IDF's SPI master with DMA on SPI2, not FastLED. On the real board FastLED 3.10.8's ESP32-S3 clocked-LED driver (LCD_CAM) crashed the chip with an interrupt watchdog timeout from inside its own ISR, and 3.10.8 offers no supported way to opt out of it; 3.9.20 works but bit-bangs, costing ~3.5 ms of CPU per frame and capping us near 125 fps. Our driver encodes a frame and queues it, so the transfer overlaps the next frame's render (show() costs ~0.3 ms), and it took the board to the 240 fps cap.

We used FastLED only to clock bytes out — noise, palettes and power limiting were already ours (ADR 0001) — so dropping it removed a large, fast-moving dependency for little code.

## Consequences

- The display (Seeed_GFX) owns SPI3; LEDs own SPI2. Don't add another SPI user without checking which host it takes.
- Two Strands share SPI2: a pre-transfer callback re-routes the SPI data and clock signals to each Strand's pins in turn and parks the others low. Up to `kMaxStrands` Strands, each at most 32 KB per frame (~8000 Pixels).
- Only SK9822/APA102-style clocked Pixels are supported; the global 5-bit brightness is always full (PowerGovernor scales colors instead).
