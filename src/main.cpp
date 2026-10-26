// Kalman Dong firmware for the Seeed XIAO ESP32-S3 Plus + 1.14" display.
//
// Core 0: IMU task (833 Hz, highest priority) and the status display task.
// Core 1: Arduino loop -- buttons, rendering the Cloud Field, pushing Pixels.
//
// All of the interesting logic lives in lib/core and is tested on the host;
// this file only wires it to the hardware.

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <soc/gpio_reg.h>
#include <soc/io_mux_reg.h>

#include <vector>

#include "kd/button_log.h"
#include "kd/controls.h"
#include "kd/filter_bank.h"
#include "imu_lsm6.h"
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

namespace {

// Carrier board pins (Seeed schematic / arduino-esp32 XIAO_ESP32S3_Plus variant).
constexpr int kImuSda = 5;
constexpr int kImuScl = 6;
constexpr int kImuInt1 = 41;
constexpr int kButtonA = BUTTON_A_PIN;  // see profiles.h
constexpr int kButtonB = BUTTON_B_PIN;
constexpr int kButtonC = BUTTON_C_PIN;

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

// Status and the latest Lines, shared with the display task.
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
Status status;
std::vector<kd::Rgb8> shownLines;
volatile bool wakeDisplay = true;
volatile bool toggleStatusPage = false;  // C quick: open the Status page, or close it

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

        ++windowCount;
        if (now - windowStart >= 1000000) {
            imuHz = windowCount * 1e6f / float(now - windowStart);
            windowStart = now;
            windowCount = 0;
        }
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

void handleSerial() {
    while (Serial.available()) {
        switch (Serial.read()) {
            case 'R':
                xQueueReset(recordQueue);
                Serial.printf("# kalman-dong imu v1 profile=%s chip=%s rate=%.0f frame=sensor\n", profile.name,
                              imu.chipName(), ImuLsm6::kRateHz);
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
            case 'i':
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
                Serial.printf("# imu us: read %.0f track %.0f (filter %s)\n", imuReadUs, imuTrackUs,
                              tracker.filter().name());
                {
                    uint32_t textUs, barsUs;
                    status_display::drawTimes(textUs, barsUs);
                    Serial.printf("# display us: text %lu bars %lu status %lu, page %s\n", (unsigned long)textUs,
                                  (unsigned long)barsUs, (unsigned long)status_display::statusDrawUs(),
                                  status_display::pageName());
                }
                break;
            default:
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

    releaseI2cBus();
    Wire.begin(kImuSda, kImuScl, 400000);
    for (int attempt = 0; attempt < 5 && !imuOk; ++attempt) {
        if (attempt) delay(20);
        imuOk = imu.begin(Wire);
    }
    if (!store::loadMounting(mounting) && !kd::mountingFromAxisMap(profile.imuAxes, mounting))
        Serial.println("Dong Profile imuAxes is not a rotation; using sensor axes as-is");
    tracker.begin(mounting.rotate(store::loadGyroBias()));
    if (imuOk) {
        pinMode(kImuInt1, INPUT);
        attachInterrupt(kImuInt1, onImuInterrupt, RISING);
        xTaskCreatePinnedToCore(imuTask, "imu", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 0);
    } else {
        Serial.println("No LSM6 IMU found; showing un-anchored clouds.");
    }

    status.profile = profile.name;
    status.imuChip = imu.chipName();
    status.settings = settings;
    status_display::begin();
#ifdef KD_SCREEN_BLADE
    status_display::setBladeView(true);
    wakeDisplay = false;  // go straight to the Blade view so Ignition is visible
#endif
    xTaskCreatePinnedToCore(displayTask, "display", 6144, nullptr, 1, nullptr, 0);
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
    static bool calibrating = false;
    static char banner[sizeof(Status::banner)] = "";
    static uint32_t bannerUntilMs = 0;

    const int64_t frameStartUs = esp_timer_get_time();
    const uint32_t nowMs = millis();

    handleSerial();
    drainRecording();
    if (static_cast<int32_t>(logTestUntilMs - nowMs) > 0) Serial.printf("# log test %lu\n", (unsigned long)nowMs);
    if (printAxes && !recording && nowMs - lastAxisPrintMs >= 200) {
        printAxisCheck();
        lastAxisPrintMs = nowMs;
    }

    auto onControl = [&](const kd::ControlEvent& e) {
        static const char* const kKinds[] = {"none", "changed", "wake display", "re-level", "calibrate mounting"};
        static const char* const kSettings[] = {"",      "Palette",      "Clear Sky",  "brightness",
                                                "Girth", "cloud motion", "cloud size"};
        Serial.printf("# control: %s %s -> palette %s, brightness %.0f%%, girth %.2f m, clear sky %.0f%%\n",
                      kKinds[static_cast<int>(e.kind)], kSettings[static_cast<int>(e.changed)],
                      kd::palette(settings.palette).name, settings.brightnessValue() * 100, settings.girthValue(),
                      settings.clearSkyValue() * 100);
        lastChanged = e.changed;
        if (e.kind == kd::ControlEvent::Kind::Relevel) {
            xSemaphoreTake(trackerLock, portMAX_DELAY);
            tracker.relevel();
            xSemaphoreGive(trackerLock);
        }
        if (e.kind == kd::ControlEvent::Kind::CalibrateMounting) startCalibration();
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
                case kd::Setting::None:
                    snprintf(banner, sizeof(banner), "%s", e.kind == kd::ControlEvent::Kind::Relevel ? "Re-leveled" : "");
                    break;
            }
            bannerUntilMs = nowMs + 2000;
        } else if (e.kind == kd::ControlEvent::Kind::WakeDisplay) {
            toggleStatusPage = true;
        } else {
            wakeDisplay = true;
        }
    };
    // Feed every queued button change at the time it was sampled, then the
    // current level at a time read afterwards, so time only moves forward.
    static uint8_t buttonsDown = 0;
    kd::ButtonLog::Sample sample;
    while (buttonLog.pop(sample)) {
        for (int i = 0; i < 3; ++i)
            if (((sample.down ^ buttonsDown) >> i) & 1)
                Serial.printf("# button %c %s\n", "ABC"[i], (sample.down >> i) & 1 ? "down" : "up");
        buttonsDown = sample.down;
        for (const kd::ControlEvent& e : controls.update(sample.down & 1, sample.down & 2, sample.down & 4, sample.ms))
            onControl(e);
    }
    const uint32_t buttonsMs = millis();
    for (const kd::ControlEvent& e : controls.update(buttonsDown & 1, buttonsDown & 2, buttonsDown & 4, buttonsMs))
        onControl(e);
    // While a button is held, say what letting go will do (kd/controls.h).
    const kd::Controls::PressInProgress pressNow = controls.pressInProgress(buttonsMs);
    const int holdSegments = pressNow.button >= 0 ? pressNow.segments : -1;
    if (pressNow.button >= 0) {
        static const char* const kOnRelease[3][3] = {
            {"Release:\npalette", "Release:\ncloud size", ""},
            {"Release:\nclouds", "Release:\nbrightness", ""},
            {"Release:\nstatus", "Release:\nre-level", ""},
        };
        const int col = pressNow.onRelease == kd::ButtonTracker::Press::Quick  ? 0
                        : pressNow.onRelease == kd::ButtonTracker::Press::Long ? 1
                                                                               : 2;
        snprintf(banner, sizeof(banner), "%s", kOnRelease[pressNow.button][col]);
        bannerUntilMs = nowMs + 300;
        if (!status_display::bladeView()) wakeDisplay = true;  // the Status page shows the bar
    }
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
    bool atRest = false;
    if (imuOk) {
        xSemaphoreTake(trackerLock, portMAX_DELAY);
        in.pose = tracker.pose(lookahead);
        in.ignitionProgress = tracker.ignitionProgress();
        in.restSeconds = tracker.restSeconds();
        atRest = tracker.atRest();
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
    {
        const kd::PoseReadout r = kd::readout(in.pose);
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
