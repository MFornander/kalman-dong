# A "Yo Dong" wake word

Research note, 2026-10-27. Question: what would it take to give the Dong's
ESP32-S3 firmware its own always-on wake word, "Yo Dong", without paying
Espressif for a custom WakeNet model? Every claim links to its source.
Anything I worked out myself rather than read is marked *(inferred)*.

Our numbers (established before this note): MultiNet7 (`mn7_en`) measures
~27 ms of compute per 32 ms chunk on the board (worst ~51 ms), about 85% of
core 0 while it runs. It is fed raw mic audio directly, with no AFE and no
WakeNet (`src/voice.cpp`).

## Summary

- **Recommended: get "Yo Dong" as a free WakeNet model from Espressif's
  community program, and prototype with "Hi ESP" now.**
  - Espressif trains community-requested wake words with its TTS pipeline,
    free of charge. The models ship in ESP-SR under its licence and are "free
    for commercial use" ([sr-88]).
  - To qualify, a request needs a link to an ongoing project (ours is public)
    or more than 5 upvotes ([sr-88]).
  - Recent English requests took 2–6 weeks: "Hey Nova" was asked for
    2026-07-27 and done 2026-09-07 ([sr-88-nova-req], [sr-88-wn10]); "Hey
    Budi" was asked for 2026-09-11 and delivered as "Hey Buddy" on 2026-09-24
    ([sr-88-budi-req], [sr-88-buddy]). Espressif says the schedule is "highly
    uncertain" ([sr-88-schedule]).
  - The model would be public, not exclusive to us ([sr-list]).
- **WakeNet is cheap, and its plumbing is already in our build.**
  - WakeNet9 costs 3.0 ms per 32 ms frame (2-channel) on an ESP32-S3,
    with 16 KB internal RAM and 324 KB PSRAM ([sr-bench]).
  - New requests are now delivered as WakeNet10 ([sr-88-wn10]). It costs
    7.1 ms per frame (3-channel) and 523 KB PSRAM ([sr-bench]).
  - Our `srmodels.bin` already contains `wn9_hiesp` ("Hi ESP") next to
    `mn7_en`. So the always-on stage can be built and measured today, with no
    new model *(inferred from the shipped file's contents)*.
  - Our MultiNet runs about 2.5× slower than Espressif's table says (27 vs
    11 ms per frame, [sr-bench]). If WakeNet scales the same way, budget
    ~25% of core 0 for WN9 and ~55% for WN10 until measured *(inferred)*.
- **Plan B, fully self-service: train a microWakeWord model.**
  - Code is Apache-2.0 ([mww]). Training uses only synthetic Piper TTS clips
    plus downloaded negative data ([mww]).
  - Models are 52–62 KB with a 22–30 KB tensor arena ([mww-models]).
  - On a plain ESP32, one inference takes 3.7 ms (internal RAM) or 13.3 ms
    (PSRAM). It runs ~33 times a second, which is 12% or 44% of a core
    ([esphome-pr20529]).
  - The catch is quality. The author warns that the stock notebook's model
    "will most likely not be usable" without experimentation ([mww-nb]).
    The training data is non-commercial ([mww-nb], [mww-hf]).
- **Don't use MultiNet as the wake word.** Espressif says command recognition
  "must be used with WakeNet" ([sr-mn]). Always-on, it would hold ~85% of
  core 0 forever. Espressif also publishes no false-accept rate for it.
- **Other routes are out.**
  - openWakeWord takes "several seconds" per 80 ms frame on an ESP32-S3
    ([oww]).
  - Porcupine has no ESP32 port ([pv-docs], [pv-mcu]), and Picovoice has no
    personal plan ([pv-faq]).
  - Edge Impulse's free plan works but means building our own dataset
    ([ei-pricing]).
- **"Yo Dong" is short for a wake word.** It is 2 syllables and about 5
  phonemes (Y OW D AO NG) *(inferred)*. Picovoice advises against fewer
  than 6 phonemes ([pv-choose]). Espressif's wake words "usually" have 3–6
  "symbols", for example "Hi Leixin" ([sr-custom]). Expect more false
  accepts on "you don't", "yo dawg" or "so long" *(inferred)*. Keep a
  3-syllable fallback in mind, such as "Okay Dong" (6 phonemes)
  *(inferred)*.

## Option 1: Espressif WakeNet

### Paid customization (what we're avoiding)

- **Price.** A one-time fee, with no per-device licence. Amounts are not
  published; ask sales@espressif.com ([sr-custom]).
- **Three methods** ([sr-custom]):
  1. TTS samples only;
  2. Espressif's crowdsourced recordings (~500 speakers) plus TTS;
  3. a corpus we supply of "at least 20,000 qualified" recordings. The spec
     is more than 500 speakers, including at least 100 children; 15
     utterances each at 1 m and 3 m; recorded in a room under 40 dB.
  Training takes 2–3 weeks once the corpus is ready.
- **Limits.** Up to 5 wake words per WakeNet model ([sr-custom], [sr-wn]).

### Free community models (the recommended route)

- **The offer** ([sr-88]):
  - Espressif trains requested wake words "by using only TTS samples".
  - It claims TTS Pipeline V2 reaches "95–98% accuracy compared to models
    trained by human samples".
  - English is supported.
- **Eligibility** ([sr-88]), since 2024-08-01: attach an ongoing project
  link and overview, *or* get more than 5 upvotes.
- **Terms.**
  - Requests fall under a Wake Word Submission Agreement ([sr-agreement]).
    We warrant the phrase infringes no one's rights, and we indemnify
    Espressif.
  - Espressif says it is "for testing and some open-source projects". Product
    work should go to sales ([sr-88-schedule]).
- **Training tools are not open.** "The training script is not yet open
  source" ([sr-88-script]). There is no self-service WakeNet training.
- **Published accuracy:**
  - "Hi Telly", a TTS-trained WakeNet9l: false alarm rate 1 per 24 h,
    94% correct wake rate ([sr-88-telly]).
  - WakeNet9 "Alexa" on the Korvo board: 98% wake rate in quiet and 94%
    with speech noise at 4 dB SNR, at 1 m and 3 m. False triggering is
    "once in 12 hours" ([sr-bench]).
- **Model generation.** Since 2026-09-07 requests come back as WakeNet10
  ([sr-88-wn10]). Whether WakeNet9 can still be asked for is not stated.

### Prebuilt wake words and their licence

- **Licence.** ESP-SR's licence grants free use "on all ESPRESSIF SYSTEMS
  products" ([sr-license]). The community models carry "the same license"
  and are "free for commercial use" ([sr-88]).
- **Trademarks.** The README warns that, "before any commercial use", we
  must hold rights to the wake word itself ([sr-readme]). That matters for
  "Alexa" or "Jarvis", not for "Hi ESP" *(inferred)*.
- **English options in the bundled list** ([sr-list]):
  - Alexa, Hi ESP, Hi Jason, Computer, Jarvis, Mycroft, Sophia;
  - Hey Willow, Hey Wanda, Hey Kira, Hey Ivy, Hi Joy, Hi Andy;
  - Hey Buddy, Hey Nova, Hey Hermes, Hey Pamet;
  - Hi M Five, Hi Stack Chan.

  None sounds like "Yo Dong". The closest is the Mandarin 你好东东
  ("nǐ hǎo dōng dōng", `wn9_nihaodongdong_tts2`), which is no use for
  English speakers *(inferred)*.
- **Already on our board.** The arduino-esp32 `srmodels.bin` we flash
  holds `mn7_en`, `wn9_hiesp` and `vadnet1_medium` (read from the file and
  the libs' `sdkconfig`). So "Hi ESP" works with no new download.

### Cost on the ESP32-S3

| Model | Internal RAM | PSRAM | Time per 32 ms frame | Flash (model files) |
|---|---|---|---|---|
| WakeNet9, 2 ch | 16 KB | 324 KB | 3.0 ms | 291 KB ([sr-wn9-hiesp]) |
| WakeNet9, 3 ch | 20 KB | 347 KB | 4.3 ms | – |
| WakeNet10, 3 ch | 17 KB | 523 KB | 7.1 ms (22.6% of a core) | 1.0 MB ([sr-wn10-nova]) |
| MultiNet7 | 18 KB | 2920 KB | 11 ms (we measure ~27 ms) | – |
| AFE "MR, SR, LOW_COST" (AEC, VADNet, WakeNet9) | 60 KB | 740 KB | 8.8% + 9.8% of a core | – |

All rows are from [sr-bench] except the flash sizes. Single-channel WakeNet
figures are not published.

### Measured on the board (2026-10-27)

The bundled `wn9_hiesp` running always-on in the voice task (core 0,
priority 2), mode `DET_MODE_95`, one channel, 512-sample chunks. Each
figure covers 11 s, from serial `i`, with the Dong lying on the bench.

| Core 0 | Wake word off | Wake word on |
|---|---|---|
| voice task | – | 14% |
| imu task | 32% | 33% |
| display task | 56% | 43% |
| idle | 10% | 7% |
| IMU rate | 818 Hz | 818 Hz |
| LED frames (core 1) | 240 fps | 240 fps |

- WakeNet9 takes **7.1 ms of wall time per 32 ms chunk** (worst 10.5 ms).
  The FreeRTOS run-time counters charge the voice task only 14% of the
  core (about 4.5 ms per chunk), because the IMU and mic tasks preempt it
  mid-detect.
- So the real cost is close to Espressif's 3.0 ms. That also explains most
  of our MultiNet "27 ms vs 11 ms": it is wall time under preemption, not
  slower compute *(inferred)*.
- The display task takes the hit, as intended. It has priority 1 and was
  using the slack.
- WakeNet9's `clean()` crashes (LoadProhibited in `dl_convq_queue_bzero`),
  so the firmware never calls it; the model resets itself after a
  detection.
- By voice: "Hi ESP" was heard on all 4 tries. A command after it came
  through on 2 of 3 windows, as *next palette* at p 0.15–0.17, the same
  range USR3 gets. MultiNet's p is not a confidence: with USR3 it once
  heard "next palette" as *sound off* at p 0.93.

## Option 2: MultiNet as the wake word

- **Espressif's position.** "Speech command recognition must be used with
  WakeNet. After wake-up, MultiNet detection can start" ([sr-mn]).
  - The Arduino core's own `ESP_SR` library follows this: AFE plus WakeNet,
    then MultiNet ([ard-sr]).
  - No WakeNet-free always-on mode is documented ([sr-mn]).
- **Cost.**
  - Measured: ~27 ms per 32 ms chunk, ~85% of core 0, all the time.
  - Espressif's table says 11 ms ([sr-bench]). Even that would be 34% of
    core 0, forever *(inferred)*.
- **No cheaper English MultiNet.** The S3 English models are `mn5q8_en`,
  `mn6_en` and `mn7_en` ([sr-readme]). They cost 12, 12 and 11 ms per frame
  ([sr-bench]). MultiNet5 is also the least accurate: 95.4% / 85.9% / 82.7%
  in quiet / stationary noise / speech noise, against 97.2% / 92.3% / 90.6%
  for MultiNet7 ([sr-bench]).
- **False accepts.**
  - No false-accept figure is published for MultiNet. The only knob is
    `set_det_threshold` (0.0–0.9999) ([sr-mn-iface]).
  - MultiNet is built to pick the best of up to 200 commands ([sr-mn]), not
    to reject hours of unrelated speech *(inferred)*.
- **Verdict.** Keep MultiNet for the command after the wake word, as today.

## Option 3: microWakeWord

The upstream repo is [mww] (OHF-Voice); `kahrendt/microWakeWord` is a fork
of it.

- **How it detects** ([mww]):
  - The TFLM micro_speech preprocessor makes 40 features every 10 ms, from
    30 ms windows, with noise suppression and AGC built in.
  - A streaming MixConv network runs every 30 ms.
  - A detection needs the probability to stay high over several windows.
- **Training** ([mww], [mww-nb], [mww-data]):
  1. Generate positive clips with Piper sample generator (MIT,
     [piper-sg]). The notebook default is 1,000 clips; phonetic spellings
     "may produce better samples". Piper does about 100 clips/s on a
     2080 Ti ([piper-sg]).
  2. Augment them: room impulse responses, AudioSet/FMA background noise at
     −5 to 10 dB SNR, EQ, distortion, pitch shift.
  3. Download pre-computed negative features: speech, dinner party, no
     speech. The notebook's four files total ~5.7 GB ([mww-hf]).
  4. Train. Defaults are 10,000 steps, batch 128 and negative class weight
     20, then quantize to a streaming int8 TFLite model.
  - Adversarial (sound-alike) negatives come from openWakeWord's generator
    ([mww-data], [oww-adv]).
- **Hardware and time.**
  - Python 3.10 on Linux; macOS has a fork ([mww-nb]).
  - Colab works but "is extremely slow compared to training on a local GPU"
    ([mww-nb]).
  - No training time is published. On one desktop GPU, expect hours, not
    days *(inferred)*.
- **Quality warning.** "Training a model that works well is still very
  difficult, as it typically requires experimentation with hyperparameters
  and sample generation settings" ([mww]).
  - The Home Assistant team's experimental models were "minimally trained
    and tested" with the notebook defaults ([mww-exp]).
- **Licences.**
  - Code: Apache-2.0 ([mww]).
  - Official models (Okay Nabu, Hey Jarvis, Alexa, Hey Mycroft, VAD):
    Apache-2.0, weights included ([mww-models]).
  - Training data: the notebook says models trained with its downloads
    "should be considered as appropriate for **non-commercial** personal use
    only" ([mww-nb]). The negative features are CC-BY-NC-4.0 ([mww-hf]), and
    WHAM! is CC BY-NC 4.0 ([mww-data]).
  - Fine for the Dong as a personal project *(inferred)*.
- **Size and RAM.**
  - `.tflite` files are 52–62 KB. Tensor arenas are 22.3–30 KB.
  - The VAD model is 34 KB with a 16.8 KB arena ([mww-models],
    [mww-okay-nabu]).
- **CPU.** No ESP32-S3 figure is published.
  - ESPHome measured 13.3 ms per inference with the model in PSRAM, and
    3.7 ms with it in internal RAM, on an ESP32-WROVER at ~33 inferences
    per second. That is 44% vs 12% of a core ([esphome-pr20529]).
  - A newer esp-nn made inference "7% faster" on an ESP32-S3
    ([esphome-pr15628]).
  - The S3's SIMD kernels should land at or below the ESP32 figure
    *(inferred)*. Feature extraction adds an FFT every 10 ms, also
    unmeasured.
- **Published accuracy.** Only for the 2024 v1 models, at default cutoff
  ([mww-v1]):

  | Model | False reject | False accepts/h (Picovoice set) | False accepts/h (DiPCo) |
  |---|---|---|---|
  | Okay Nabu | 2% | 0.122 | 0.187 |
  | Hey Jarvis | 0.67% | 0.081 | 0.375 |
  | Alexa | 3.49% | 0.486 | 0.187 |

  The Okay Nabu and Hey Jarvis positives were Piper-generated, not real
  voices ([mww-v1]). No numbers are published for the current v2 models.
- **Running it outside ESPHome.**
  - ESPHome's component is the reference ([esphome-src]). It uses
    `espressif/esp-tflite-micro` (pinned 1.3.3) and `esp-nn`, plus
    `esphome/esp-micro-speech-features` (Apache-2.0, a plain ESP-IDF
    component, [esp-msf]) for the frontend.
  - Our Arduino libs already link `esp-tflite-micro` 1.4.0 and `esp-nn`
    1.3.2. They contain every op ESPHome registers (VAR_HANDLE, CALL_ONCE,
    READ/ASSIGN_VARIABLE, STRIDED_SLICE, CONV_2D, DEPTHWISE_CONV_2D, MEAN,
    LOGISTIC, …) (checked with `nm` against [esphome-src]'s op list).
  - The libs lack the legacy C frontend (`FrontendProcessSamples`), so
    `esp-micro-speech-features` would be vendored as a PlatformIO lib
    *(inferred)*.
  - The frontend settings must match training exactly ([esphome-pp]):
    40 channels; 30 ms window; 10 ms step; 125–7500 Hz; noise reduction;
    PCAN on; log scale; int8 scaling `v*256/666 − 128`.
  - Arena sizes changed with esp-nn versions on the S3 ([esphome-pr15628]).
    Probe the arena size at start-up.

## Option 4: openWakeWord

- **It can't run on the ESP32-S3.** The README says it "may still take
  several seconds to process a single 80 ms frame on an ESP32-S3" and points
  to microWakeWord instead ([oww]).
- **Licence.** Code is Apache-2.0. Pre-trained models are CC BY-NC-SA 4.0
  ([oww]).
- **Reuse for microWakeWord.**
  - microWakeWord already reuses openWakeWord's sound-alike phrase
    generator, `generate_adversarial_texts` ([mww-data], [oww-adv]).
  - Its `custom_negative_phrases` idea and its advice of 20,000+ positives,
    "often 100,000+" ([oww-cfg]), carry over *(inferred)*.
  - Its pre-computed features do not carry over. They are Google
    speech-embedding outputs ([oww]), not the 40-channel micro-frontend
    features microWakeWord trains on *(inferred)*.

## Option 5: other free routes, briefly

- **Edge Impulse.**
  - The Developer plan is $0, with 3 private projects, 60 min of compute per
    job and GPU training. External distribution needs Enterprise
    ([ei-pricing]).
  - It deploys to ESP32 as an ESP-IDF C++ or Arduino library ([ei-esp32]).
    Its ESP32 keyword-spotting example takes 54 ms DSP + 4 ms inference with
    ESP-DSP ([ei-esp32]).
  - We would have to supply or synthesize the "yo dong" data. That is the
    same work as microWakeWord, without its tested wake-word training
    recipe *(inferred)*.
- **TFLM micro_speech, DIY.** A model under 20 KB for "yes"/"no", trained
  on Speech Commands v2 (CC BY) in about 2 hours ([tflm-speech],
  [tflm-train]). It has no "yo dong" data. microWakeWord is this approach
  with the data problem solved *(inferred)*.
- **Picovoice Porcupine.**
  - Its MCU support is "Arm Cortex-M (STM32, Arduino)", and the only MCU
    demo is an STM32F411 ([pv-docs], [pv-mcu]). There is no ESP32 port.
  - The FAQ says "there are no dedicated free or paid plans for personal or
    non-commercial use", only a one-time enterprise trial ([pv-faq]).
  - Out.
- **Sensory and other commercial engines:** not investigated.

## Fit with our pipeline

- **AFE: not needed.**
  - Espressif recommends calling WakeNet directly "if you are using a single
    microphone and only need to use WakeNet, and you want to reduce memory
    and CPU" ([skainet-ww]).
  - The AFE's SR pipeline is AEC → VADNet → WakeNet ([sr-bench]). AEC does
    nothing without a speaker reference *(inferred)*.
  - microWakeWord's frontend already does noise reduction and PCAN gain
    control ([mww]).
- **VAD gate: skip it at first.**
  - WakeNet9 at ~3 ms per frame is already cheap ([sr-bench]). A gate
    would need a ~1.5 s pre-roll buffer so it doesn't clip "yo"
    *(inferred; microWakeWord's clip window is 1.5 s, [mww-nb])*.
  - If WakeNet10 or microWakeWord measures heavy, gate with WebRTC VAD
    (`esp_vad.h`) or VADNet (`esp_vadn_iface.h`). Both are in our libs, and
    `vadnet1_medium` is already in `srmodels.bin`.
  - Neither has a published standalone S3 cost.
  - ESPHome uses microWakeWord's VAD model differently: not as a gate, but
    to reject non-speech false accepts ([esphome-mww]).
- **Memory: both models resident is fine.** MultiNet7 (2920 KB) plus
  WakeNet10 (523 KB) is ~3.4 MB of the 8 MB PSRAM. Internal RAM is
  18 + 17 KB ([sr-bench]). Don't run them at the same time: wake detection
  pauses while MultiNet listens *(inferred)*.
- **Flash.**
  - The `model` partition is 4 MB at 0x800000
    ([partitions/voice_16MB.csv](../../partitions/voice_16MB.csv)).
  - Today's `srmodels.bin` is 3,340,296 B, leaving 853 KB.
  - A WakeNet9 model (~291 KB) fits.
  - A WakeNet10 model (1,001,792 B) does not, unless `wn9_hiesp` is
    dropped (then ~144 KB spare) or the partition grows.
  - 0xC00000–0xFFFFFF (4 MB) is unused, so the partition can double
    *(inferred)*.
  - A custom `srmodels.bin` is built with ESP-SR's `pack_model.py`
    ([sr-pack]). `tools/pio_voice.py` would then flash that instead of the
    Arduino libs' copy.
  - microWakeWord's ~60 KB model would live in the app image instead.
- **The phrase.**
  - Picovoice: "Choosing a wake phrase with fewer than six phonemes is not
    recommended". A short word can be strengthened with a prefix such as
    "Hey" or "OK", and varied sounds cut false alarms ([pv-choose]).
  - "Yo Dong" has two back-rounded vowels (OW, AO) and one voiced stop
    *(inferred)*.
  - When requesting it, give the pronunciation in ARPAbet. Espressif asked
    another requester for exactly that ([sr-88-arpabet]).

## What it would take (recommended path)

**Goal:** an always-on "wake" stage on core 0 that hands off to the existing
MultiNet command stage. Core 1 and the LEDs are untouched.

1. **Request the model (½ hour, free).**
   - Post on [sr-88]: wake word "Yo Dong", English, ARPAbet `Y OW1 D AO1
     NG`, target ESP32-S3 with one PDM mic, and a link to the public repo
     with a two-line overview.
   - Ask for WakeNet9 if possible, to keep cost and flash down.
   - This accepts the Submission Agreement ([sr-agreement]).
2. **Build the wake stage with "Hi ESP" (1–2 days).**
   - In `src/voice.cpp`, load the WakeNet model next to MultiNet:
     `esp_srmodel_filter(models, ESP_WN_PREFIX, nullptr)`, then
     `esp_wn_handle_from_name`. Follow the direct-WakeNet example
     ([skainet-ww]).
   - The voice task (core 0, priority 2) feeds every chunk to WakeNet
     while idle. On a detection it runs MultiNet for up to N seconds, the
     same as a USR3 hold today, then goes back to WakeNet.
   - USR3 still works as a manual override *(inferred design)*.
   - Keep the mic task and stream buffer as they are. WakeNet's chunk size
     comes from `get_samp_chunksize`. Assume 512 samples, as MultiNet uses
     ([sr-wn]: 16 kHz, 16-bit mono).
   - Add a `WakeGate` state machine to `lib/core` with host tests (idle →
     listening → cooldown), alongside `PushToTalk`.
3. **Measure on the board (½ day).**
   - Per-chunk WakeNet time, mean and worst.
   - IMU rate under load (must stay at ~817 Hz).
   - Display fps drop.
   - False wakes over a few hours of room audio and podcasts.
   - Check the input level: peak −15 to −21 dBFS today. It may need a
     different gain than MultiNet.
4. **Swap in "Yo Dong" when it arrives (½ day).**
   - Pack `mn7_en` plus the new `wn*_yodong` (and `vadnet1_medium`) with
     `pack_model.py` ([sr-pack]).
   - Grow the `model` partition if it is a WakeNet10.
   - Flash it with `upload_voice_model`, then re-run step 3.
5. **CPU on core 0:**
   - Idle with WakeNet9: **14%, measured** (see
     [Measured on the board](#measured-on-the-board-2026-10-27)).
   - Idle with WakeNet10: about 2.4× that by Espressif's table, so ~33%
     *(inferred)*.
   - After a wake: ~85% for the few seconds MultiNet listens, as today.
   - The IMU task outranks all of these. The display (priority 1) absorbs
     the slack.

**Plan B: microWakeWord, if no model arrives in ~6 weeks or it performs
badly** *(effort inferred)*:

1. **Training setup (1 day).** Linux and a GPU; clone [mww]; Python 3.10.
2. **Data (1 day).**
   - Generate 10k–50k "yo dong" clips, with phonetic spellings such as
     "yo dong" and "yoh dawng". Vary speed and speakers ([piper-sg]).
   - Generate sound-alike negatives ("you don't", "yo dawg", "so long") with
     [oww-adv].
   - Download ~6 GB of negative features ([mww-hf]).
3. **Train and evaluate (several days of iteration).**
   - Tune the class weights, steps and probability cutoff ([mww-nb]).
   - Target ≤ 0.5 false accepts/h and ≤ 5% false rejects, openWakeWord's
     "annoyance threshold" ([oww]).
   - Test with real recordings of the user, not only Piper clips ([mww-nb]).
4. **Firmware (2–3 days).**
   - Vendor `esp-micro-speech-features` ([esp-msf]).
   - Port `StreamingModel` from [esphome-src] onto the Arduino libs' TFLM.
   - Put weights and arena in internal RAM, since it was 3.6× faster than
     PSRAM on the ESP32 ([esphome-pr20529]).
   - Same `WakeGate` and hand-off as above.

## Open questions and risks

- **WakeNet cost on this board.** Settled for WakeNet9: 14% of core 0.
  The 2.5× gap to Espressif's table was wall time under preemption, not
  slow compute.
- **Delivery.** Espressif may not pick up the request, or may only offer
  WakeNet10 ([sr-88-schedule], [sr-88-wn10]).
- **The phrase.** "Yo Dong" is below Picovoice's 6-phoneme guidance
  ([pv-choose]). If false accepts are high, retrain or re-request as a
  3-syllable phrase *(inferred)*.
- **microWakeWord on the S3.** There is no published S3 inference time, no
  v2 false-accept numbers, and no training-time figure. Its data licence is
  non-commercial ([mww-nb], [mww-hf]).
- **Level and frontend match.** WakeNet expects 16 kHz 16-bit mono
  ([sr-wn]). The right input level for our 4× gain path is untested.
- **Trademarks.** Espressif and ESPHome both leave wake-word trademark risk
  with the user ([sr-readme], [sr-agreement], [mww-models]). It is
  irrelevant for a personal "Yo Dong" *(inferred)*.

## Sources

Bracketed names in the text link to these.

- `sr-custom`: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html>
- `sr-wn`: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/README.html>
- `sr-bench`: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/benchmark/README.html>
- `sr-mn`: <https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_command_recognition/README.html>
- `sr-readme`: <https://github.com/espressif/esp-sr/blob/master/README.md>
- `sr-list`: <https://github.com/espressif/esp-sr/blob/master/wakeword_list.md>
- `sr-license`: <https://github.com/espressif/esp-sr/blob/master/LICENSE>
- `sr-88`: <https://github.com/espressif/esp-sr/issues/88>
- `sr-agreement`: <https://github.com/espressif/esp-sr/blob/master/docs/_static/Wake%20Word%20Submission%20Agreement.pdf>
- `sr-pack`: <https://github.com/espressif/esp-sr/blob/master/model/pack_model.py>
- `sr-mn-iface`: <https://github.com/espressif/esp-sr/blob/master/include/esp32s3/esp_mn_iface.h>
- `skainet-ww`: <https://github.com/espressif/esp-skainet/tree/master/examples/wake_word_detection>
- `ard-sr`: <https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP_SR/src/esp32-hal-sr.c>
- `mww`: <https://github.com/OHF-Voice/micro-wake-word>
- `mww-models`: <https://github.com/esphome/micro-wake-word-models>
- `esphome-mww`: <https://esphome.io/components/micro_wake_word/>
- `esphome-src`: <https://github.com/esphome/esphome/tree/dev/esphome/components/micro_wake_word>
- `esp-msf`: <https://components.espressif.com/components/esphome/esp-micro-speech-features>
- `piper-sg`: <https://github.com/rhasspy/piper-sample-generator>
- `oww`: <https://github.com/dscripka/openWakeWord>
- `pv-docs`: <https://picovoice.ai/docs/porcupine/>
- `pv-faq`: <https://picovoice.ai/docs/faq/general/>
- `pv-choose`: <https://picovoice.ai/docs/tips/choosing-a-wake-word/>
- `ei-pricing`: <https://edgeimpulse.com/pricing>
- `ei-esp32`: <https://docs.edgeimpulse.com/hardware/boards/espressif-esp32>
- `tflm-speech`: <https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/micro_speech>

**Caveats:**
- **Read locally:** the contents of `srmodels.bin`, the esp-sr (2.5.3),
  esp-tflite-micro (1.4.0) and esp-nn (1.3.2) versions, and the TFLM op
  symbols were read from the installed arduino-esp32 libs
  (`~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3`).
- **Not verified:**
  - CPU figures for microWakeWord on an ESP32-S3, and for single-channel
    WakeNet; no primary source publishes them.
  - False-accept and false-reject figures for the v2 microWakeWord models.
  - Whether Edge Impulse's "up to 1000 units" production row applies to the
    free plan; the pricing table is ambiguous.
  - The licence of Piper's LibriTTS-R generator checkpoint.
  - The phoneme counts, which come from my reading of ARPAbet, not a
    dictionary lookup.

<!-- link definitions -->
[sr-custom]: https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html
[sr-wn]: https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/README.html
[sr-bench]: https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/benchmark/README.html
[sr-mn]: https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/speech_command_recognition/README.html
[sr-readme]: https://github.com/espressif/esp-sr/blob/master/README.md
[sr-list]: https://github.com/espressif/esp-sr/blob/master/wakeword_list.md
[sr-license]: https://github.com/espressif/esp-sr/blob/master/LICENSE
[sr-88]: https://github.com/espressif/esp-sr/issues/88
[sr-88-script]: https://github.com/espressif/esp-sr/issues/88#issuecomment-2221859614
[sr-88-telly]: https://github.com/espressif/esp-sr/issues/88#issuecomment-2270658134
[sr-88-nova-req]: https://github.com/espressif/esp-sr/issues/88#issuecomment-5090680799
[sr-88-wn10]: https://github.com/espressif/esp-sr/issues/88#issuecomment-5564365968
[sr-88-budi-req]: https://github.com/espressif/esp-sr/issues/88#issuecomment-5636070317
[sr-88-buddy]: https://github.com/espressif/esp-sr/issues/88#issuecomment-5813766047
[sr-88-arpabet]: https://github.com/espressif/esp-sr/issues/88#issuecomment-5727688470
[sr-88-schedule]: https://github.com/espressif/esp-sr/issues/88#issuecomment-6360962006
[sr-agreement]: https://github.com/espressif/esp-sr/blob/master/docs/_static/Wake%20Word%20Submission%20Agreement.pdf
[sr-wn9-hiesp]: https://github.com/espressif/esp-sr/tree/master/model/wakenet_model/wn9_hiesp
[sr-wn10-nova]: https://github.com/espressif/esp-sr/tree/master/model/wakenet_model/wn10_heynova
[sr-pack]: https://github.com/espressif/esp-sr/blob/master/model/pack_model.py
[sr-mn-iface]: https://github.com/espressif/esp-sr/blob/master/include/esp32s3/esp_mn_iface.h
[skainet-ww]: https://github.com/espressif/esp-skainet/tree/master/examples/wake_word_detection
[ard-sr]: https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP_SR/src/esp32-hal-sr.c
[mww]: https://github.com/OHF-Voice/micro-wake-word
[mww-nb]: https://github.com/OHF-Voice/micro-wake-word/blob/main/notebooks/basic_training_notebook.ipynb
[mww-data]: https://github.com/OHF-Voice/micro-wake-word/blob/main/documentation/data_sources.md
[mww-v1]: https://github.com/OHF-Voice/micro-wake-word/blob/577e5d4c998aa7c1294e9dfb7bb0c05078ab22b1/README.md
[mww-hf]: https://huggingface.co/datasets/kahrendt/microwakeword
[mww-models]: https://github.com/esphome/micro-wake-word-models
[mww-okay-nabu]: https://github.com/esphome/micro-wake-word-models/blob/main/models/v2/okay_nabu.json
[mww-exp]: https://github.com/esphome/micro-wake-word-models/tree/main/models/v2/experiments
[esphome-mww]: https://esphome.io/components/micro_wake_word/
[esphome-src]: https://github.com/esphome/esphome/tree/dev/esphome/components/micro_wake_word
[esphome-pp]: https://github.com/esphome/esphome/blob/dev/esphome/components/micro_wake_word/preprocessor_settings.h
[esphome-pr20529]: https://github.com/esphome/esphome/pull/20529
[esphome-pr15628]: https://github.com/esphome/esphome/pull/15628
[esp-msf]: https://components.espressif.com/components/esphome/esp-micro-speech-features
[piper-sg]: https://github.com/rhasspy/piper-sample-generator
[oww]: https://github.com/dscripka/openWakeWord
[oww-adv]: https://github.com/dscripka/openWakeWord/blob/main/openwakeword/data.py
[oww-cfg]: https://github.com/dscripka/openWakeWord/blob/main/examples/custom_model.yml
[pv-docs]: https://picovoice.ai/docs/porcupine/
[pv-mcu]: https://github.com/Picovoice/porcupine/tree/master/demo/mcu
[pv-faq]: https://picovoice.ai/docs/faq/general/
[pv-choose]: https://picovoice.ai/docs/tips/choosing-a-wake-word/
[ei-pricing]: https://edgeimpulse.com/pricing
[ei-esp32]: https://docs.edgeimpulse.com/hardware/boards/espressif-esp32
[tflm-speech]: https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/micro_speech
[tflm-train]: https://github.com/tensorflow/tflite-micro/blob/main/tensorflow/lite/micro/examples/micro_speech/train/README.md
