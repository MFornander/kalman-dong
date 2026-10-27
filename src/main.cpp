// Kalman Dong firmware for the Seeed XIAO ESP32-S3 Plus + 1.14" display.
//
// Core 0: IMU task (833 Hz, highest priority), microphone task and the status
//         display task.
// Core 1: Arduino loop -- buttons and knobs, rendering the Cloud Field, pushing
//         Pixels.
//
// All of the interesting logic lives in lib/core and is tested on the host;
// this file only wires it to the hardware.

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <soc/gpio_reg.h>
#include <soc/io_mux_reg.h>

#include <vector>

#include "kd/button_log.h"
#include "kd/controls.h"
#include "encoder8.h"
#include "kd/filter_bank.h"
#include "imu_lsm6.h"
#include "kd/loudness.h"
#include "kd/knob_panel.h"
#include "kd/motion_tracker.h"
#include "kd/mounting.h"
#include "kd/noise.h"
#include "kd/orientation_filter.h"
#include "kd/pose_readout.h"
#include "kd/power_governor.h"
#include "profiles.h"
#include "sk9822_spi.h"
#include "kd/renderer.h"
#include "status_display.h"
#include "store.h"
#include "kd/strand_map.h"
#ifdef KD_VOICE
#include "kd/push_to_talk.h"
#include "kd/voice_commands.h"
#include "voice.h"
#endif

// Set from VERSION and git by tools/pio_version.py.
#ifndef KD_VERSION
#define KD_VERSION "dev"
#endif
#ifndef KD_COMMIT
#define KD_COMMIT "unknown"
#endif

namespace {

constexpr const char* kVersion = KD_VERSION;
constexpr const char* kCommit = KD_COMMIT;

// Carrier board pins (Seeed schematic / arduino-esp32 XIAO_ESP32S3_Plus variant).
constexpr int kImuSda = 5;
constexpr int kImuScl = 6;
constexpr int kImuInt1 = 41;
constexpr int kButtonA = BUTTON_A_PIN;  // see profiles.h
constexpr int kButtonB = BUTTON_B_PIN;
constexpr int kButtonC = BUTTON_C_PIN;
constexpr int kMicClock = 1;  // PDM microphone
constexpr int kMicData = 2;

constexpr uint32_t kMinFrameUs = 1000000 / 240;  // cap at 240 fps
constexpr uint32_t kBiasSaveIntervalMs = 60000;   // spare the flash
constexpr uint32_t kBatteryPeriodMs = 500;


const kd::DongProfile& profile = dongProfile();

// Motion: written by the IMU task, read by the render loop, under trackerLock.
// Orientation filters (kd/filter_bank.h). Mahony unless the build names one,
// e.g. build_flags = -DKD_FILTER=Kalman; serial 'f' cycles them live.
#define KD_STRINGIFY2(x) #x
#define KD_STRINGIFY(x) KD_STRINGIFY2(x)
#ifdef KD_FILTER
constexpr const char* kBuildFilter = KD_STRINGIFY(KD_FILTER);
#else
constexpr const char* kBuildFilter = kd::kDefaultFilter;
#endif
kd::FilterBank filters;
kd::MotionTracker tracker(filters.byName(kBuildFilter));
SemaphoreHandle_t trackerLock;
SemaphoreHandle_t imuSignal;
ImuLsm6 imu;
bool imuOk = false;
volatile float imuHz = 0.f;
volatile float imuReadUs = 0.f;   // smoothed I2C read time per sample
volatile float imuTrackUs = 0.f;  // smoothed tracker + filter time per sample

// USB recording of raw IMU samples (tools/capture_imu.py).
struct RecordedSample {
    uint32_t tUs;
    ImuLsm6::Reading reading;
};
QueueHandle_t recordQueue;
volatile bool recording = false;

// Microphone: the mic task measures Loudness; the render loop reads it.
constexpr uint32_t kMicRate = 16000;
constexpr size_t kMicBlock = 160;  // 10 ms
I2SClass mic;
bool micOk = false;
kd::LoudnessMeter loudnessMeter(kMicRate);
volatile float loudness = 0.f;
volatile float micDb = -90.f, micQuietDb = -90.f, micLoudDb = -70.f;
volatile float micUs = 0.f;  // smoothed time to measure one block
bool soundOn = true;         // Loudness drives brightness; serial 'm' or the 8-Encoder unit's switch
bool printSound = false;     // serial 'l'

#ifdef KD_VOICE
// Voice: holding USR3 listens for a spoken command; a command heard replaces
// the button's own press (kd/push_to_talk.h, src/voice.h).
constexpr int kTalkPin = 11;  // USR3
static_assert(BUTTON_A_PIN == kTalkPin || BUTTON_B_PIN == kTalkPin || BUTTON_C_PIN == kTalkPin,
              "the talk button is one of A, B, C");
constexpr kd::Button kTalkButton = BUTTON_A_PIN == kTalkPin   ? kd::Button::A
                                   : BUTTON_B_PIN == kTalkPin ? kd::Button::B
                                                              : kd::Button::C;
// Below the IMU and mic, above the display: a busy recognizer slows the
// Status display's frames, never the IMU's samples or the LED core.
constexpr UBaseType_t kVoicePriority = 2;
Voice voice;
bool voiceOk = false;
kd::PushToTalk ptt(kTalkButton);
const float kVoiceGains[] = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f};
uint32_t serialTalkUntilMs = 0;  // serial 't': listening as if USR3 were held
#endif

// Rendering.
kd::StrandMap strandMap(profile);
kd::Renderer renderer(profile);
kd::PowerGovernor governor(profile.power, profile.battery, strandMap.physicalPixelCount());
std::vector<kd::Rgb8> lines;
std::vector<kd::Rgb8> strandPixels[kd::kMaxStrands];
kd::Rgb8* strandPtrs[kd::kMaxStrands];
Sk9822Spi leds;

kd::Settings settings;
kd::Controls controls(settings);

// The buttons are sampled on a timer, not in loop(): a press is timed from its
// real edges even while the loop is stuck in a slow draw or write.
constexpr uint32_t kButtonSampleUs = 5000;
kd::ButtonLog buttonLog;

uint8_t readButtons() {
    return static_cast<uint8_t>((gpio_get_level(static_cast<gpio_num_t>(kButtonA)) == 0) |
                                (gpio_get_level(static_cast<gpio_num_t>(kButtonB)) == 0) << 1 |
                                (gpio_get_level(static_cast<gpio_num_t>(kButtonC)) == 0) << 2);
}

void sampleButtons(void*) { buttonLog.sample(readButtons(), millis()); }

// An 8-Encoder unit on the Grove port, if one was plugged in at boot (plugging
// into a powered bus can hang it). It shares the IMU's bus, so the IMU task
// reads it between samples and queues what it saw.
constexpr int64_t kKnobPeriodUs = 20000;
constexpr int64_t kKnobRetryUs = 2000000;  // after the unit stops answering, look again this often
Encoder8 knobUnit;
bool knobsFound = false;  // answered at boot
volatile bool knobsOk = false;
volatile float knobReadUs = 0.f;  // smoothed time per read of the unit
QueueHandle_t knobQueue;
kd::KnobPanel knobPanel;
bool printKnobs = false;  // serial 'k'

// IMU task only.
void pollKnobs() {
    static int failures = 0;
    if (!knobsOk) {
        knobsOk = knobUnit.begin(Wire);
        failures = 0;
        return;
    }
    const int64_t start = esp_timer_get_time();
    kd::KnobReading r;
    if (knobUnit.read(r)) {
        failures = 0;
        xQueueSend(knobQueue, &r, 0);
    } else if (++failures >= 3) {
        knobsOk = false;
    }
    knobReadUs = knobReadUs + (float(esp_timer_get_time() - start) - knobReadUs) * 0.05f;
}

// Status and the latest Lines, shared with the display task.
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
Status status;
std::vector<kd::Rgb8> shownLines;
volatile bool wakeDisplay = true;
volatile bool toggleStatusPage = false;  // C quick: open the Status page, or close it
// Counted on the board, so 'i' can show whether presses landed even when
// nothing was reading the serial port at the time.
uint32_t quickCCount = 0;                // Quick presses of C decoded
volatile uint32_t statusToggleCount = 0;  // ... and Status page toggles the display task applied

// Per-stage frame timing (us, smoothed), printed by 'i'.
struct FrameTiming {
    float render = 0, limit = 0, show = 0, total = 0;
    uint32_t worstUs = 0;  // slowest whole loop since the last 'i'
} timing;
uint32_t logTestUntilMs = 0;  // serial 'z': log a line every frame for a while

// Latest raw IMU reading, for the 'a' axis-check printout.
portMUX_TYPE readingMux = portMUX_INITIALIZER_UNLOCKED;
ImuLsm6::Reading lastReading;
bool printAxes = false;

// Mounting (sensor -> body) and its calibration; both used by the IMU task
// and changed only under trackerLock.
kd::Quat mounting;
kd::MountingCalibration calibration;

kd::Vec3 toBody(const kd::Vec3& sensor) { return mounting.rotate(sensor); }

void IRAM_ATTR onImuInterrupt() {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(imuSignal, &woken);
    if (woken) portYIELD_FROM_ISR();
}

void imuTask(void*) {
    int64_t last = esp_timer_get_time();
    int64_t lastKnobUs = last;
    int64_t windowStart = last;
    uint32_t windowCount = 0;
    for (;;) {
        // The data-ready interrupt normally wakes us; the timeout covers a
        // missed edge, and only then is it worth asking the IMU (an extra
        // I2C transaction) whether data is ready.
        const bool signalled = xSemaphoreTake(imuSignal, pdMS_TO_TICKS(3)) == pdTRUE;
        const int64_t readStart = esp_timer_get_time();
        if (!signalled && !imu.dataReady()) continue;
        ImuLsm6::Reading r;
        if (!imu.read(r)) continue;
        int64_t now = esp_timer_get_time();
        imuReadUs = imuReadUs + (float(now - readStart) - imuReadUs) * 0.02f;

        kd::ImuSample s;
        s.dt = (now - last) * 1e-6f;
        s.gyro = toBody(r.gyro);
        s.accel = toBody(r.accel);
        last = now;

        xSemaphoreTake(trackerLock, portMAX_DELAY);
        tracker.update(s);
        if (calibration.active()) calibration.update(r.gyro, r.accel, s.dt);
        xSemaphoreGive(trackerLock);
        imuTrackUs = imuTrackUs + (float(esp_timer_get_time() - now) - imuTrackUs) * 0.02f;

        portENTER_CRITICAL(&readingMux);
        lastReading = r;
        portEXIT_CRITICAL(&readingMux);

        if (recording) {
            RecordedSample rec{static_cast<uint32_t>(now), r};
            xQueueSend(recordQueue, &rec, 0);  // drop rather than stall
        }

        // Right after a sample the bus is free until the next one.
        if (knobsFound && now - lastKnobUs >= (knobsOk ? kKnobPeriodUs : kKnobRetryUs)) {
            lastKnobUs = now;
            pollKnobs();
        }

        ++windowCount;
        if (now - windowStart >= 1000000) {
            imuHz = windowCount * 1e6f / float(now - windowStart);
            windowStart = now;
            windowCount = 0;
        }
    }
}

void micTask(void*) {
    int16_t block[kMicBlock];
    for (;;) {
        const size_t bytes = mic.readBytes(reinterpret_cast<char*>(block), sizeof(block));
        if (bytes == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const int64_t start = esp_timer_get_time();
        loudnessMeter.update(block, bytes / sizeof(int16_t));
        loudness = loudnessMeter.level();
        micDb = loudnessMeter.decibels();
        micQuietDb = loudnessMeter.quietDecibels();
        micLoudDb = loudnessMeter.loudDecibels();
        micUs = micUs + (float(esp_timer_get_time() - start) - micUs) * 0.05f;
#ifdef KD_VOICE
        if (voiceOk) voice.feed(block, bytes / sizeof(int16_t));
#endif
    }
}

void displayTask(void*) {
    std::vector<kd::Rgb8> linesCopy(shownLines.size());
    for (;;) {
        Status snapshot;
        portENTER_CRITICAL(&statusMux);
        snapshot = status;
        memcpy(linesCopy.data(), shownLines.data(), linesCopy.size() * sizeof(kd::Rgb8));
        portEXIT_CRITICAL(&statusMux);
        uint32_t now = millis();
        if (toggleStatusPage) {
            toggleStatusPage = false;
            statusToggleCount = statusToggleCount + 1;
            status_display::toggle(snapshot, now);
        } else if (wakeDisplay) {
            wakeDisplay = false;
            status_display::wake(snapshot, now);
        } else {
            status_display::update(snapshot, linesCopy.data(), profile.lineCount, profile.pixelsPerLine, now);
        }
        vTaskDelay(pdMS_TO_TICKS(status_display::bladeView() ? 4 : 200));
    }
}

bool addStrands() {
    const Sk9822Spi::Strand pins[] = {
        {LED_STRAND0_DATA, LED_STRAND0_CLOCK, strandMap.strandLength(0)},
#ifdef LED_STRAND1_DATA
        {LED_STRAND1_DATA, LED_STRAND1_CLOCK, strandMap.strandLength(1)},
#endif
    };
    for (size_t s = 0; s < profile.strandCount; ++s) {
        strandPixels[s].assign(strandMap.strandLength(s), kd::Rgb8{});
        strandPtrs[s] = strandPixels[s].data();
    }
    if (!leds.begin(pins, profile.strandCount, LED_DATA_MHZ * 1000000u, LED_WIRE_ORDER)) return false;
    leds.show(strandPtrs);  // start dark
    return true;
}

void startCalibration() {
    xSemaphoreTake(trackerLock, portMAX_DELAY);
    calibration.begin();
    xSemaphoreGive(trackerLock);
    Serial.println("# Mounting Calibration: hold the tip straight up, then Line 0 to the sky");
}

// Call with trackerLock held. The gyro bias is kept across the change by
// carrying it through the sensor frame; Ignition then runs again.
void applyMounting(const kd::Quat& next) {
    kd::Vec3 sensorBias = mounting.conj().rotate(tracker.gyroBias());
    mounting = next;
    tracker.begin(mounting.rotate(sensorBias));
}

// The carrier's name for a button's pin, as printed by the button.
const char* padName(int pin) {
    switch (pin) {
        case 43: return "USR1";
        case 44: return "USR2";
        case 11: return "USR3";
        default: return "?";
    }
}

// Serial '?': the buttons, then every command grouped by what it's for.
void printHelp() {
    Serial.printf(
        "# kalman-dong %s (%s), profile %s\n"
        "#\n"
        "# Buttons on this build: A = %s, B = %s, C = %s (each Dong Profile picks them)\n"
        "#   Quick: let go before 2 s; Long: let go at 2-5 s; Held: reaches 5 s\n"
        "#        Quick          Long         Held\n"
        "#   A    next Palette   cloud size   step Clear Sky\n"
        "#   B    cloud motion   brightness   step Girth\n"
        "#   C    Status page    Re-level     Mounting Calibration\n"
        "#   An 8-Encoder unit's knob 8 pushes like C, and its switch turns sound on and off (README, Knobs).\n"
        "#\n",
        kVersion, kCommit, profile.name, padName(kButtonA), padName(kButtonB), padName(kButtonC));
    Serial.print(
        "# Serial commands (one key each, no Enter needed)\n"
        "#\n"
        "# Status\n"
        "#   ?  this help (also h)\n"
        "#   i  status: profile, IMU, filter, rates, frame and display timings, mic, knobs\n"
        "#   p  raw button levels (0 = pressed) and how GPIO43 is wired\n"
        "#\n"
        "# Controls\n"
        "#   w  as a quick press of C: open or close the Status page\n"
        "#   v  Blade view on or off\n"
        "#   c  start Mounting Calibration (as holding C for 5 s)\n"
        "#   k  log the 8-Encoder unit's counts, pushes and switch as they change (again: stop)\n"
        "#\n"
        "# Sound\n"
        "#   m  sound drives brightness, or not (as the 8-Encoder unit's switch)\n"
        "#   l  log the mic's level and Loudness 10 times a second (again: stop)\n"
        "#\n"
#ifdef KD_VOICE
        "# Voice (hold USR3 and speak two words: next palette, lights up, go faster, bigger clouds, ...)\n"
        "#   t  listen for 3 s, as if USR3 were held\n"
        "#   g  next voice gain (1x to 32x; 'i' shows it)\n"
        "#   V  list every spoken command\n"
        "#\n"
#endif
        "# Motion\n"
        "#   f  next orientation filter (Mahony, Kalman)\n"
        "#   a  axis check, 5 times a second: which sensor axis sees gravity, where the Blade points (again: stop)\n"
        "#   R  start streaming raw IMU samples as CSV (tools/capture_imu.py)\n"
        "#   S  stop streaming\n"
        "#\n"
        "# Performance\n"
        "#   b  Cloud Field micro-benchmark\n"
        "#   z  log a line every frame for 3 s, then 'i' shows the worst loop\n");
}

void handleSerial() {
    while (Serial.available()) {
        const int key = Serial.read();
        switch (key) {
            case '?':
            case 'h':
                printHelp();
                break;
            case 'R':
                xQueueReset(recordQueue);
                Serial.printf("# kalman-dong imu v1 fw=%s profile=%s chip=%s rate=%.0f frame=sensor\n", kVersion,
                              profile.name, imu.chipName(), ImuLsm6::kRateHz);
                Serial.println("t_us,gx,gy,gz,ax,ay,az");
                recording = true;
                break;
            case 'S':
                recording = false;
                Serial.println("# end");
                break;
            case 'v':
                status_display::setBladeView(!status_display::bladeView());
                Serial.printf("# blade view %s\n", status_display::bladeView() ? "on" : "off");
                break;
            case 'a':
                printAxes = !printAxes;
                break;
            case 'f': {  // next orientation filter
                xSemaphoreTake(trackerLock, portMAX_DELAY);
                tracker.setFilter(filters.next(tracker.filter()));
                xSemaphoreGive(trackerLock);
                Serial.printf("# filter %s\n", tracker.filter().name());
                break;
            }
            case 'b': {  // micro-benchmark of the Cloud Field's building blocks
                volatile float sink = 0;
                int64_t t0 = esp_timer_get_time();
                for (int k = 0; k < 2000; ++k) sink = sink + kd::noise3(k * 0.013f, 1.7f, k * 0.007f);
                int64_t t1 = esp_timer_get_time();
                kd::CloudField::Instant inst = renderer.cloudField().at(12.0);
                for (int k = 0; k < 2000; ++k) sink = sink + inst.colorAt({k * 0.001f, 0.2f, 0.3f}, kd::palette(0)).r;
                int64_t t2 = esp_timer_get_time();
                for (int k = 0; k < 2000; ++k) sink = sink + inst.hue({k * 0.001f, 0.2f, 0.3f});
                int64_t t3 = esp_timer_get_time();
                for (int k = 0; k < 2000; ++k) sink = sink + kd::fbm3(k * 0.013f, 1.7f, k * 0.007f, 2);
                int64_t t4 = esp_timer_get_time();
                for (int k = 0; k < 2000; ++k) sink = sink + kd::palette(0).at(k * 0.0005f).g;
                int64_t t5 = esp_timer_get_time();
                Serial.printf("# noise3 %.2f us, colorAt %.2f us, hue %.2f us, fbm3x2 %.2f us, palette %.2f us\n",
                              (t1 - t0) / 2000.f, (t2 - t1) / 2000.f, (t3 - t2) / 2000.f, (t4 - t3) / 2000.f,
                              (t5 - t4) / 2000.f);
                break;
            }
            case 'c':
                startCalibration();
                break;
            case 'z':  // log a line every frame for 3 s, to test what logging costs
                logTestUntilMs = millis() + 3000;
                break;
            case 'm':  // sound drives brightness, or not
                soundOn = !soundOn;
                Serial.printf("# sound %s\n", soundOn ? "drives brightness" : "off");
                break;
            case 'l':  // log the sound level 10 times a second
                printSound = !printSound;
                break;
            case 'k':  // log the 8-Encoder unit's readings as they change
                printKnobs = !printKnobs;
                break;
            case 'w':  // as if C were pressed: open or close the Status page
                toggleStatusPage = true;
                break;
            case 'p':  // raw button levels (0 = pressed), and how GPIO43 is wired inside the chip
                Serial.printf("# buttons A(gpio%d)=%d B(gpio%d)=%d C(gpio%d)=%d\n", kButtonA,
                              gpio_get_level(static_cast<gpio_num_t>(kButtonA)), kButtonB,
                              gpio_get_level(static_cast<gpio_num_t>(kButtonB)), kButtonC,
                              gpio_get_level(static_cast<gpio_num_t>(kButtonC)));
                Serial.printf("# gpio43 iomux=0x%08lx outsel=0x%08lx enable1=0x%08lx\n",
                              (unsigned long)REG_READ(IO_MUX_GPIO43_REG),
                              (unsigned long)REG_READ(GPIO_FUNC43_OUT_SEL_CFG_REG),
                              (unsigned long)REG_READ(GPIO_ENABLE1_REG));
                break;
#ifdef KD_VOICE
            case 'g': {
                const size_t n = sizeof(kVoiceGains) / sizeof(kVoiceGains[0]);
                size_t i = 0;
                while (i < n && kVoiceGains[i] != voice.gain()) ++i;
                voice.setGain(kVoiceGains[(i + 1) % n]);
                Serial.printf("# voice gain %.0fx\n", voice.gain());
                break;
            }
            case 't':
                if (voiceOk && !ptt.talking()) {
                    voice.listen();
                    serialTalkUntilMs = millis() + 3000;
                    Serial.println("# voice: listening for 3 s");
                }
                break;
            case 'V': {
                size_t n = 0;
                const kd::VoicePhrase* phrases = kd::voicePhrases(n);
                for (size_t i = 0; i < n; ++i)
                    Serial.printf("# say '%s': %s\n", phrases[i].words, kd::voiceCommandName(phrases[i].command));
                break;
            }
#endif
            case 'i':
                Serial.printf("# kalman-dong %s (%s)\n", kVersion, kCommit);
                Serial.printf("# profile=%s chip=%s filter=%s imu=%.0fHz fps=%.0f bias=%.4f,%.4f,%.4f\n",
                              profile.name, imu.chipName(), tracker.filter().name(), imuHz, status.fps,
                              tracker.gyroBias().x, tracker.gyroBias().y, tracker.gyroBias().z);
                Serial.printf(
                    "# frame us: render %.0f limit+scatter %.0f show %.0f (waited %lu, sends %lu) total %.0f | "
                    "ignited %d rest %.1fs | worst loop %lu us\n",
                    timing.render, timing.limit, timing.show, (unsigned long)leds.lastWaitUs(),
                    (unsigned long)leds.sendUs(), timing.total, tracker.ignited(), tracker.restSeconds(),
                    (unsigned long)timing.worstUs);
                timing.worstUs = 0;
                Serial.printf("# buttons: %lu quick C presses, %lu Status page toggles (including 'w')\n",
                              (unsigned long)quickCCount, (unsigned long)statusToggleCount);
                Serial.printf("# imu us: read %.0f track %.0f (filter %s)\n", imuReadUs, imuTrackUs,
                              tracker.filter().name());
                Serial.printf("# mic %s: %.1f dB (quiet %.1f, loud %.1f) loudness %.2f, %.0f us per %u samples, %s\n",
                              micOk ? "ok" : "failed", micDb, micQuietDb, micLoudDb, loudness, micUs,
                              unsigned(kMicBlock), soundOn ? "drives brightness" : "off");
#ifdef KD_VOICE
                if (voiceOk) {
                    const Voice::Stats v = voice.stats();
                    Serial.printf("# voice: %s on %s, gain %.0fx, detect %.0f us (worst %lu) per %lu samples, "
                                  "worst backlog %lu ms, %lu blocks dropped\n",
                                  voice.modelName(), padName(kTalkPin), v.gain, v.detectUs,
                                  (unsigned long)v.worstDetectUs, (unsigned long)v.chunkSamples,
                                  (unsigned long)v.worstBacklogMs, (unsigned long)v.dropped);
                } else {
                    Serial.printf("# voice off: %s\n", voice.error());
                }
#endif
                if (knobsOk)
                    Serial.printf("# knobs: 8-Encoder unit firmware %u, read %.0f us, switch %s\n",
                                  unsigned(knobUnit.firmware()), knobReadUs, knobPanel.switchOn() ? "on" : "off");
                else
                    Serial.println(knobsFound ? "# knobs: 8-Encoder unit stopped answering"
                                              : "# knobs: no 8-Encoder unit at boot");
                {
                    uint32_t textUs, barsUs;
                    status_display::drawTimes(textUs, barsUs);
                    Serial.printf("# display us: text %lu bars %lu status %lu, page %s\n", (unsigned long)textUs,
                                  (unsigned long)barsUs, (unsigned long)status_display::statusDrawUs(),
                                  status_display::pageName());
                }
                break;
            default:
                if (key > ' ' && key < 127) Serial.printf("# unknown command '%c'; ? for help\n", key);
                break;
        }
    }
}

void drainRecording() {
    RecordedSample rec;
    while (Serial.availableForWrite() > 96 && xQueueReceive(recordQueue, &rec, 0) == pdTRUE) {
        const ImuLsm6::Reading& r = rec.reading;
        Serial.printf("%lu,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f\n", static_cast<unsigned long>(rec.tUs), r.gyro.x,
                      r.gyro.y, r.gyro.z, r.accel.x, r.accel.y, r.accel.z);
    }
}

// Axis check: which sensor axis sees gravity, and where the tracker thinks
// the Blade points. Used to fill in imuAxes in a Dong Profile.
void printAxisCheck() {
    portENTER_CRITICAL(&readingMux);
    ImuLsm6::Reading r = lastReading;
    portEXIT_CRITICAL(&readingMux);
    const float a[3] = {r.accel.x, r.accel.y, r.accel.z};
    int up = 0;
    for (int i = 1; i < 3; ++i)
        if (fabsf(a[i]) > fabsf(a[up])) up = i;
    xSemaphoreTake(trackerLock, portMAX_DELAY);
    kd::Vec3 blade = tracker.pose().orientation.rotate({1, 0, 0});
    xSemaphoreGive(trackerLock);
    Serial.printf("sensor accel %6.2f %6.2f %6.2f  up=%c%c | Blade pitch %5.1f heading %6.1f deg\n", a[0], a[1],
                  a[2], a[up] >= 0 ? '+' : '-', "XYZ"[up], kd::radToDeg(asinf(kd::clampf(blade.z, -1.f, 1.f))),
                  kd::radToDeg(atan2f(blade.y, blade.x)));
}

// A reset in the middle of an IMU read can leave the IMU holding SDA low.
// Clock it out of the half-finished byte and send a STOP before Wire starts.
void releaseI2cBus() {
    pinMode(kImuSda, INPUT_PULLUP);
    pinMode(kImuScl, OUTPUT_OPEN_DRAIN);
    digitalWrite(kImuScl, HIGH);
    for (int i = 0; i < 9 && digitalRead(kImuSda) == LOW; ++i) {
        digitalWrite(kImuScl, LOW);
        delayMicroseconds(5);
        digitalWrite(kImuScl, HIGH);
        delayMicroseconds(5);
    }
    pinMode(kImuSda, OUTPUT_OPEN_DRAIN);
    digitalWrite(kImuSda, LOW);
    delayMicroseconds(5);
    digitalWrite(kImuSda, HIGH);  // STOP: SDA rises while SCL is high
    delayMicroseconds(5);
    pinMode(kImuSda, INPUT);
    pinMode(kImuScl, INPUT);
}

float readBattery(float previous) {
#if BATTERY_PIN >= 0
    float volts = analogReadMilliVolts(BATTERY_PIN) * 0.001f * profile.battery.dividerRatio;
    float f = governor.batteryFraction(volts);
    return previous < 0.f ? f : previous + (f - previous) * 0.2f;
#else
    (void)previous;
    return -1.f;
#endif
}

}  // namespace

void setup() {
    Serial.setTxBufferSize(4096);  // room for '?' and 'i' in one go: writes never wait (below)
    Serial.begin(921600);
    // Never wait on USB: with the port closed, a full buffer made every log
    // line block for up to 2 s and stalled the whole loop.
    Serial.setTxTimeoutMs(0);
    // Buttons go straight through ESP-IDF. GPIO43 (USR1) boots as UART0's TX
    // and the Arduino core keeps it registered to the UART, so it must be
    // reset to plain GPIO (detaching the UART) before it reads a press.
    for (int pin : {kButtonA, kButtonB, kButtonC}) {
        const gpio_num_t g = static_cast<gpio_num_t>(pin);
        gpio_reset_pin(g);
        gpio_set_direction(g, GPIO_MODE_INPUT);
        gpio_set_pull_mode(g, GPIO_PULLUP_ONLY);
    }
    esp_timer_create_args_t buttonTimer = {};
    buttonTimer.callback = sampleButtons;
    buttonTimer.name = "buttons";
    esp_timer_handle_t buttonTimerHandle;
    esp_timer_create(&buttonTimer, &buttonTimerHandle);
    esp_timer_start_periodic(buttonTimerHandle, kButtonSampleUs);

    store::begin();
    settings = store::loadSettings();

    if (strandMap.error()) {
        for (;;) {
            Serial.printf("Dong Profile '%s' is broken: %s\n", profile.name, strandMap.error());
            delay(1000);
        }
    }
    lines.assign(profile.lineCount * profile.pixelsPerLine, kd::Rgb8{});
    shownLines.assign(lines.size(), kd::Rgb8{});
    if (!addStrands()) Serial.println("LED output failed to start (SPI2)");

    trackerLock = xSemaphoreCreateMutex();
    imuSignal = xSemaphoreCreateBinary();
    recordQueue = xQueueCreate(512, sizeof(RecordedSample));
    knobQueue = xQueueCreate(8, sizeof(kd::KnobReading));

    releaseI2cBus();
    Wire.begin(kImuSda, kImuScl, 400000);
    for (int attempt = 0; attempt < 5 && !imuOk; ++attempt) {
        if (attempt) delay(20);
        imuOk = imu.begin(Wire);
    }
    if (!store::loadMounting(mounting) && !kd::mountingFromAxisMap(profile.imuAxes, mounting))
        Serial.println("Dong Profile imuAxes is not a rotation; using sensor axes as-is");
    tracker.begin(mounting.rotate(store::loadGyroBias()));
    knobsOk = knobsFound = knobUnit.begin(Wire);
    if (knobsOk) Serial.printf("8-Encoder unit found, firmware %u\n", unsigned(knobUnit.firmware()));
    if (imuOk) {
        pinMode(kImuInt1, INPUT);
        attachInterrupt(kImuInt1, onImuInterrupt, RISING);
        xTaskCreatePinnedToCore(imuTask, "imu", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 0);
    } else {
        Serial.println("No LSM6 IMU found; showing un-anchored clouds.");
    }

    mic.setPinsPdmRx(kMicClock, kMicData);
    micOk = mic.begin(I2S_MODE_PDM_RX, kMicRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    if (micOk)
        xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, configMAX_PRIORITIES - 3, nullptr, 0);
    else
        Serial.println("Microphone failed to start; sound won't change brightness.");
#ifdef KD_VOICE
    if (micOk) {
        const uint32_t t0 = millis();
        voiceOk = voice.begin(kVoicePriority, 0);
        if (voiceOk)
            Serial.printf("# voice: %s loaded in %lu ms; hold %s and speak\n", voice.modelName(),
                          (unsigned long)(millis() - t0), padName(kTalkPin));
        else
            Serial.printf("# voice off: %s\n", voice.error());
    }
#endif

    status.profile = profile.name;
    status.imuChip = imu.chipName();
    status.micOk = micOk;
    status.settings = settings;
    status_display::begin();
#ifdef KD_SCREEN_BLADE
    status_display::setBladeView(true);
    wakeDisplay = false;  // go straight to the Blade view so Ignition is visible
#endif
    xTaskCreatePinnedToCore(displayTask, "display", 6144, nullptr, 1, nullptr, 0);
    Serial.printf("# kalman-dong %s (%s) ready; ? for help\n", kVersion, kCommit);
}

void loop() {
    static int64_t ignitedAtUs = -1;
    static float lookahead = 0.004f;
    static float fps = 0.f;
    static float battery = -1.f;
    static uint32_t lastBatteryMs = 0;
    static unsigned savedBiasGeneration = 0;
    static uint32_t lastBiasSaveMs = 0;
    static kd::Setting lastChanged = kd::Setting::None;
    static uint32_t lastAxisPrintMs = 0;
    static uint32_t lastSoundPrintMs = 0;
    static bool calibrating = false;
    static char banner[sizeof(Status::banner)] = "";
    static uint32_t bannerUntilMs = 0;
    static uint32_t lastActiveMs = 0;  // a button pressed or the Dong picked up; see kSleepAfterMs

    const int64_t frameStartUs = esp_timer_get_time();
    const uint32_t nowMs = millis();

    handleSerial();
    drainRecording();
    if (static_cast<int32_t>(logTestUntilMs - nowMs) > 0) Serial.printf("# log test %lu\n", (unsigned long)nowMs);
    if (printAxes && !recording && nowMs - lastAxisPrintMs >= 200) {
        printAxisCheck();
        lastAxisPrintMs = nowMs;
    }
    if (printSound && !recording && nowMs - lastSoundPrintMs >= 100) {
        Serial.printf("sound %6.1f dB  quiet %6.1f  loud %6.1f  loudness %.2f\n", micDb, micQuietDb, micLoudDb,
                      loudness);
        lastSoundPrintMs = nowMs;
    }

    auto onControl = [&](const kd::ControlEvent& e) {
        static const char* const kKinds[] = {"none",     "changed",            "wake display", "re-level",
                                             "calibrate mounting", "sound on", "sound off"};
        static const char* const kSettings[] = {"",      "Palette",      "Clear Sky",  "brightness",
                                                "Girth", "cloud motion", "cloud size", "quiet glow"};
        lastActiveMs = nowMs;
        Serial.printf("# control at %lu ms: %s %s -> palette %s, brightness %.0f%%, girth %.2f m, clear sky %.0f%%\n",
                      (unsigned long)nowMs, kKinds[static_cast<int>(e.kind)], kSettings[static_cast<int>(e.changed)],
                      kd::palette(settings.palette).name, settings.brightnessValue() * 100, settings.girthValue(),
                      settings.clearSkyValue() * 100);
        lastChanged = e.changed;
        if (e.kind == kd::ControlEvent::Kind::Relevel) {
            xSemaphoreTake(trackerLock, portMAX_DELAY);
            tracker.relevel();
            xSemaphoreGive(trackerLock);
        }
        if (e.kind == kd::ControlEvent::Kind::CalibrateMounting) startCalibration();
        if (e.kind == kd::ControlEvent::Kind::SoundOn || e.kind == kd::ControlEvent::Kind::SoundOff)
            soundOn = e.kind == kd::ControlEvent::Kind::SoundOn;  // the 8-Encoder unit's switch
        // In the Blade view a change shows in the header and the bars stay in
        // sight; only C's short press (its job) opens the Status page.
        if (status_display::bladeView() && e.kind != kd::ControlEvent::Kind::WakeDisplay) {
            switch (e.changed) {
                case kd::Setting::Palette:
                    snprintf(banner, sizeof(banner), "Palette\n%s", kd::palette(settings.palette).name);
                    break;
                case kd::Setting::Brightness:
                    snprintf(banner, sizeof(banner), "Brightness\n%.0f%%", settings.brightnessValue() * 100);
                    break;
                case kd::Setting::Girth: snprintf(banner, sizeof(banner), "Girth\n%.2f m", settings.girthValue()); break;
                case kd::Setting::ClearSky:
                    snprintf(banner, sizeof(banner), "Clear sky\n%.0f%%", settings.clearSkyValue() * 100);
                    break;
                case kd::Setting::Motion:
                    if (settings.motionValue() == 0.f)
                        snprintf(banner, sizeof(banner), "Clouds\nfrozen");
                    else
                        snprintf(banner, sizeof(banner), "Clouds\n%.0fx speed", settings.motionValue());
                    break;
                case kd::Setting::CloudSize:
                    snprintf(banner, sizeof(banner), "Cloud size\n%.1f m", settings.cloudSizeValue());
                    break;
                case kd::Setting::QuietGlow:
                    snprintf(banner, sizeof(banner), "Quiet glow\n%.0f%%", settings.quietGlowValue() * 100);
                    break;
                case kd::Setting::None:
                    snprintf(banner, sizeof(banner), "%s",
                             e.kind == kd::ControlEvent::Kind::Relevel    ? "Re-leveled"
                             : e.kind == kd::ControlEvent::Kind::SoundOn  ? "Sound\non"
                             : e.kind == kd::ControlEvent::Kind::SoundOff ? "Sound\noff"
                                                                          : "");
                    break;
            }
            bannerUntilMs = nowMs + 2000;
        } else if (e.kind == kd::ControlEvent::Kind::WakeDisplay) {
            ++quickCCount;
            toggleStatusPage = true;
        } else {
            wakeDisplay = true;
        }
    };
    // Feed every queued button change at the time it was sampled, then the
    // current level at a time read afterwards, so time only moves forward.
    static uint8_t buttonsDown = 0;
    static kd::ButtonLog::Sample sample;
    static bool heldBack = false;  // `sample` waits for the recognizer (kd/push_to_talk.h)
    bool waitForVoice = false;
#ifdef KD_VOICE
    // Spoken commands first, so one heard before the talk button's release
    // cancels its press before the release reaches Controls.
    static bool heardThisPress = false;
    Voice::Result heard;
    while (voiceOk && voice.poll(heard)) {
        if (heard.kind == Voice::Result::Kind::Heard) {
            Serial.printf("# voice: heard '%s' (%s, p %.2f) %lu ms after pressing\n", heard.words,
                          kd::voiceCommandName(heard.command), heard.probability, (unsigned long)heard.afterMs);
            if (ptt.talking()) controls.cancel(ptt.talk());
            heardThisPress = true;
            onControl(kd::applyVoice(heard.command, controls, nowMs));
        } else {
            ptt.finished();
            Serial.printf("# voice: done %lu ms after letting go, peak %.0f dBFS%s\n", (unsigned long)heard.afterMs,
                          heard.peakDb, heardThisPress ? "" : ", nothing heard");
        }
    }
    if (serialTalkUntilMs && static_cast<int32_t>(nowMs - serialTalkUntilMs) >= 0) {
        serialTalkUntilMs = 0;
        voice.finish();
    }
#endif
    while (heldBack || buttonLog.pop(sample)) {
        heldBack = true;
#ifdef KD_VOICE
        if (voiceOk) {
            const kd::PushToTalk::Admit a = ptt.admit(sample.down, nowMs);
            if (a.ask == kd::PushToTalk::Ask::Listen) {
                heardThisPress = false;
                voice.listen();
            } else if (a.ask == kd::PushToTalk::Ask::Finish) {
                voice.finish();
            }
            if (!a.now) {
                waitForVoice = true;
                break;
            }
        }
#endif
        heldBack = false;
        for (int i = 0; i < 3; ++i)
            if (((sample.down ^ buttonsDown) >> i) & 1)
                Serial.printf("# button %c %s at %lu ms\n", "ABC"[i], (sample.down >> i) & 1 ? "down" : "up",
                              (unsigned long)sample.ms);
        buttonsDown = sample.down;
        for (const kd::ControlEvent& e : controls.update(sample.down & 1, sample.down & 2, sample.down & 4, sample.ms))
            onControl(e);
    }
    if (buttonsDown) lastActiveMs = nowMs;
    const uint32_t buttonsMs = millis();
    if (!waitForVoice)  // time stands still for Controls while a release is held back
        for (const kd::ControlEvent& e : controls.update(buttonsDown & 1, buttonsDown & 2, buttonsDown & 4, buttonsMs))
            onControl(e);
    kd::KnobReading knobs;
    while (xQueueReceive(knobQueue, &knobs, 0) == pdTRUE) {
        static kd::KnobReading shown;
        if (printKnobs && (memcmp(knobs.counts, shown.counts, sizeof(knobs.counts)) != 0 ||
                           knobs.pushed != shown.pushed || knobs.switchOn != shown.switchOn)) {
            Serial.printf("# knobs at %lu ms: %ld %ld %ld %ld %ld %ld %ld %ld pushed 0x%02x switch %d\n",
                          (unsigned long)knobs.ms, (long)knobs.counts[0], (long)knobs.counts[1],
                          (long)knobs.counts[2], (long)knobs.counts[3], (long)knobs.counts[4],
                          (long)knobs.counts[5], (long)knobs.counts[6], (long)knobs.counts[7], knobs.pushed,
                          knobs.switchOn);
            shown = knobs;
        }
        if (knobs.pushed) lastActiveMs = nowMs;
        for (const kd::ControlEvent& e : knobPanel.update(knobs, controls)) onControl(e);
    }
    // While a button or knob is held, say what letting go will do
    // (kd/controls.h, kd/knob_panel.h). Knob 8 is button C.
    const kd::Controls::PressInProgress pressNow = controls.pressInProgress(buttonsMs);
    const kd::KnobPanel::PressInProgress knobNow = knobPanel.pressInProgress(buttonsMs);
    int pressRow = pressNow.button;
    int holdSegments = pressNow.segments;
    kd::ButtonTracker::Press onRelease = pressNow.onRelease;
    if (pressRow < 0 && knobNow.knob >= 0) {
        pressRow = knobNow.knob == 7 ? 2 : 3;
        holdSegments = knobNow.segments;
        onRelease = knobNow.onRelease;
    }
    if (pressRow < 0) holdSegments = -1;
    if (pressRow >= 0) {
        static const char* const kOnRelease[4][3] = {
            {"Release:\npalette", "Release:\ncloud size", ""},
            {"Release:\nclouds", "Release:\nbrightness", ""},
            {"Release:\nstatus", "Release:\nre-level", ""},
            {"Release:\nstatus", "Release:\ndefault", ""},  // knobs 1-6
        };
        const int col = onRelease == kd::ButtonTracker::Press::Quick  ? 0
                        : onRelease == kd::ButtonTracker::Press::Long ? 1
                                                                      : 2;
        snprintf(banner, sizeof(banner), "%s", kOnRelease[pressRow][col]);
#ifdef KD_VOICE
        if (voiceOk && pressRow == static_cast<int>(kTalkButton) && pressNow.button >= 0) {
            const char* action = strchr(kOnRelease[pressRow][col], '\n');  // "palette" in "Release:\npalette"
            snprintf(banner, sizeof(banner), "Listening\nor %s", action ? action + 1 : "nothing");
        }
#endif
        bannerUntilMs = nowMs + 300;
        if (!status_display::bladeView()) wakeDisplay = true;  // the Status page shows the bar
    }
#ifdef KD_VOICE
    else if (voiceOk && ptt.talking() && !heardThisPress) {
        snprintf(banner, sizeof(banner), "Listening");
        bannerUntilMs = nowMs + 300;
    }
#endif
    if (controls.shouldSave(buttonsMs)) {
        store::saveSettings(settings);
        controls.markSaved();
    }

    if (nowMs - lastBatteryMs >= kBatteryPeriodMs) {
        battery = readBattery(battery);
        lastBatteryMs = nowMs;
    }

    kd::FrameInput in;
    // Show Clock (local until Dongs sync), scaled by the wielder's cloud
    // motion: frozen, 1x or 2x. A change of speed jumps the clouds; every
    // frame stays a function of the clock and settings (ADR 0003).
    in.showTime = frameStartUs * 1e-6 * settings.motionValue();
    in.localTime = in.showTime;
    in.batteryFraction = battery;
    const float loudnessNow = loudness;
    if (micOk && soundOn) in.loudness = loudnessNow;
    bool atRest = false;
    bool lying = false;
    kd::Quat rotZero;
    if (imuOk) {
        xSemaphoreTake(trackerLock, portMAX_DELAY);
        in.pose = tracker.pose(lookahead);
        rotZero = tracker.zeroOrientation();
        in.ignitionProgress = tracker.ignitionProgress();
        in.restSeconds = tracker.restSeconds();
        atRest = tracker.atRest();
        lying = tracker.lying();
        bool ignited = tracker.ignited();
        unsigned biasGeneration = tracker.biasGeneration();
        kd::Vec3 bias = tracker.gyroBias();

        // Mounting Calibration: guide on the Pixels, apply when finished.
        const kd::MountingCalibration::Step step = calibration.step();
        const bool calTurned = calibration.turned();
        in.calibration.active = calibration.active();
        in.calibration.step = step == kd::MountingCalibration::Step::LineZeroUp ? 1 : 0;
        in.calibration.progress = calibration.holdProgress();
        const bool finished = calibrating && !calibration.active();
        if (finished && step == kd::MountingCalibration::Step::Done) applyMounting(calibration.sensorToBody());
        const kd::Quat mountingNow = mounting;
        xSemaphoreGive(trackerLock);

        if (in.calibration.active || finished) {
            // Two short rows: the header shows about 11 large letters a row.
            using CalStep = kd::MountingCalibration::Step;
            const int pct = int(in.calibration.progress * 100.f) / 10 * 10;
            if (step == CalStep::TipUp && !calTurned)
                snprintf(banner, sizeof(banner), "Point the\nblade up");
            else if (step == CalStep::TipUp)
                snprintf(banner, sizeof(banner), "Blade up\nhold %d%%", pct);
            else if (step == CalStep::LineZeroUp && !calTurned)
                snprintf(banner, sizeof(banner), "Blade level\nline 0 up");
            else if (step == CalStep::LineZeroUp)
                snprintf(banner, sizeof(banner), "Blade level\nhold %d%%", pct);
            else if (step == CalStep::Done)
                snprintf(banner, sizeof(banner), "Mounting\nsaved");
            else
                snprintf(banner, sizeof(banner), "Mounting\nfailed");
            bannerUntilMs = nowMs + 3000;
            if (strcmp(banner, status.banner) != 0) {
                wakeDisplay = !status_display::bladeView();
                char shown[sizeof(banner)];
                snprintf(shown, sizeof(shown), "%s", banner);
                if (char* nl = strchr(shown, '\n')) *nl = ' ';
                Serial.printf("# banner: %s (%s)\n", shown, calibration.instruction());
            }
        }
        if (finished) {
            if (step == kd::MountingCalibration::Step::Done) {
                store::saveMounting(mountingNow);
                ignitedAtUs = -1;
                Serial.printf("# Mounting saved: %.4f %.4f %.4f %.4f\n", mountingNow.w, mountingNow.x,
                              mountingNow.y, mountingNow.z);
            } else {
                Serial.printf("# Mounting Calibration failed: %s\n", calibration.failure());
            }
        }
        calibrating = in.calibration.active;

        if (ignited && ignitedAtUs < 0) ignitedAtUs = frameStartUs;
        if (biasGeneration != savedBiasGeneration &&
            (savedBiasGeneration == 0 || nowMs - lastBiasSaveMs >= kBiasSaveIntervalMs)) {
            store::saveGyroBias(mounting.conj().rotate(bias));
            savedBiasGeneration = biasGeneration;
            lastBiasSaveMs = nowMs;
        }
    } else if (ignitedAtUs < 0) {
        ignitedAtUs = frameStartUs;
    }
    in.sinceIgnition = ignitedAtUs < 0 ? 0.f : (frameStartUs - ignitedAtUs) * 1e-6f;

    const int64_t t0 = esp_timer_get_time();
    renderer.render(in, settings, lines.data());
    const int64_t t1 = esp_timer_get_time();
    governor.limit(lines.data(), lines.size(), battery);
    strandMap.scatter(lines.data(), strandPtrs);
    const int64_t t2 = esp_timer_get_time();
    leds.show(strandPtrs);
    const int64_t t3 = esp_timer_get_time();
    auto smooth = [](float& v, int64_t us) { v += (float(us) - v) * 0.05f; };
    smooth(timing.render, t1 - t0);
    smooth(timing.limit, t2 - t1);
    smooth(timing.show, t3 - t2);
    smooth(timing.total, t3 - frameStartUs);

    // Pixels light once the queued frame has been clocked out: predict that
    // far ahead next frame.
    const int64_t frameUs = esp_timer_get_time() - frameStartUs;
    if (frameUs > timing.worstUs) timing.worstUs = static_cast<uint32_t>(frameUs);
    lookahead += ((frameUs + leds.sendUs()) * 1e-6f - lookahead) * 0.05f;
    fps += (1e6f / float(frameUs < kMinFrameUs ? kMinFrameUs : frameUs) - fps) * 0.05f;

    portENTER_CRITICAL(&statusMux);
    memcpy(shownLines.data(), lines.data(), lines.size() * sizeof(kd::Rgb8));
    status.settings = settings;
    status.changed = lastChanged;
    status.fps = fps;
    status.imuHz = imuHz;
    status.atRest = atRest;
    status.soundOn = soundOn;
    status.loudness = loudnessNow;
    if (!lying || in.calibration.active) lastActiveMs = nowMs;
    status.asleep = nowMs - lastActiveMs >= status_display::kSleepAfterMs;
    {
        const kd::PoseReadout r = kd::readout(in.pose, rotZero);
        memcpy(status.pos, r.pos, sizeof(status.pos));
        memcpy(status.rot, r.rot, sizeof(status.rot));
    }
    status.recording = recording;
    status.holdSegments = holdSegments;
    if (static_cast<int32_t>(nowMs - bannerUntilMs) >= 0) banner[0] = '\0';
    memcpy(status.banner, banner, sizeof(status.banner));
    portEXIT_CRITICAL(&statusMux);

    if (frameUs < kMinFrameUs) delayMicroseconds(kMinFrameUs - frameUs);
}
