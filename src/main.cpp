// Kalman Dong firmware for the Seeed XIAO ESP32-S3 Plus + 1.14" display.
//
// Core 0: IMU task (833 Hz, highest priority) and the status display task.
// Core 1: Arduino loop -- buttons, rendering the Cloud Field, pushing Pixels.
//
// All of the interesting logic lives in lib/core and is tested on the host;
// this file only wires it to the hardware.

#include <Arduino.h>
#include <FastLED.h>
#include <Wire.h>

#include <vector>

#include "kd/controls.h"
#include "imu_lsm6.h"
#include "kd/motion_tracker.h"
#include "kd/orientation_filter.h"
#include "kd/power_governor.h"
#include "profiles.h"
#include "kd/renderer.h"
#include "status_display.h"
#include "store.h"
#include "kd/strand_map.h"

namespace {

// Carrier board pins (Seeed schematic / arduino-esp32 XIAO_ESP32S3_Plus variant).
constexpr int kImuSda = 5;
constexpr int kImuScl = 6;
constexpr int kImuInt1 = 41;
constexpr int kButtonA = 43;
constexpr int kButtonB = 44;
constexpr int kButtonC = 11;

constexpr uint32_t kMinFrameUs = 1000000 / 240;  // cap at 240 fps
constexpr uint32_t kBiasSaveIntervalMs = 60000;   // spare the flash
constexpr uint32_t kBatteryPeriodMs = 500;

static_assert(sizeof(CRGB) == sizeof(kd::Rgb8), "Pixel buffers are shared between core and FastLED");

const kd::DongProfile& profile = dongProfile();

// Motion: written by the IMU task, read by the render loop, under trackerLock.
kd::MahonyFilter filter;
kd::MotionTracker tracker(filter);
SemaphoreHandle_t trackerLock;
SemaphoreHandle_t imuSignal;
ImuLsm6 imu;
bool imuOk = false;
volatile float imuHz = 0.f;

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

kd::Settings settings;
kd::Controls controls(settings);

// Status and the latest Lines, shared with the display task.
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
Status status;
std::vector<kd::Rgb8> shownLines;
volatile bool wakeDisplay = true;

// Latest raw IMU reading, for the 'a' axis-check printout.
portMUX_TYPE readingMux = portMUX_INITIALIZER_UNLOCKED;
ImuLsm6::Reading lastReading;
bool printAxes = false;

kd::Vec3 toBody(const kd::Vec3& sensor) {
    const float v[3] = {sensor.x, sensor.y, sensor.z};
    const kd::BodyAxisMap& m = profile.imuAxes;
    return {m.sign[0] * v[m.source[0]], m.sign[1] * v[m.source[1]], m.sign[2] * v[m.source[2]]};
}

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
        // missed edge.
        xSemaphoreTake(imuSignal, pdMS_TO_TICKS(3));
        if (!imu.dataReady()) continue;
        ImuLsm6::Reading r;
        if (!imu.read(r)) continue;
        int64_t now = esp_timer_get_time();

        kd::ImuSample s;
        s.dt = (now - last) * 1e-6f;
        s.gyro = toBody(r.gyro);
        s.accel = toBody(r.accel);
        last = now;

        xSemaphoreTake(trackerLock, portMAX_DELAY);
        tracker.update(s);
        xSemaphoreGive(trackerLock);

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
        if (wakeDisplay) {
            wakeDisplay = false;
            status_display::wake(snapshot, now);
        } else {
            status_display::update(snapshot, linesCopy.data(), profile.lineCount, profile.pixelsPerLine, now);
        }
        vTaskDelay(pdMS_TO_TICKS(status_display::bladeView() ? 15 : 200));
    }
}

void addStrands() {
    for (size_t s = 0; s < profile.strandCount; ++s) {
        strandPixels[s].assign(strandMap.strandLength(s), kd::Rgb8{});
        strandPtrs[s] = strandPixels[s].data();
    }
    CRGB* s0 = reinterpret_cast<CRGB*>(strandPtrs[0]);
    FastLED.addLeds<LED_CHIP, LED_STRAND0_DATA, LED_STRAND0_CLOCK, LED_COLOR_ORDER, DATA_RATE_MHZ(LED_DATA_MHZ)>(
        s0, strandPixels[0].size());
#ifdef LED_STRAND1_DATA
    CRGB* s1 = reinterpret_cast<CRGB*>(strandPtrs[1]);
    FastLED.addLeds<LED_CHIP, LED_STRAND1_DATA, LED_STRAND1_CLOCK, LED_COLOR_ORDER, DATA_RATE_MHZ(LED_DATA_MHZ)>(
        s1, strandPixels[1].size());
#endif
    FastLED.setBrightness(255);  // PowerGovernor does the limiting
    FastLED.clear(true);
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
            case 'i':
                Serial.printf("# profile=%s chip=%s imu=%.0fHz fps=%.0f bias=%.4f,%.4f,%.4f\n", profile.name,
                              imu.chipName(), imuHz, status.fps, tracker.gyroBias().x, tracker.gyroBias().y,
                              tracker.gyroBias().z);
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
    pinMode(kButtonA, INPUT_PULLUP);
    pinMode(kButtonB, INPUT_PULLUP);
    pinMode(kButtonC, INPUT_PULLUP);

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
    addStrands();

    trackerLock = xSemaphoreCreateMutex();
    imuSignal = xSemaphoreCreateBinary();
    recordQueue = xQueueCreate(512, sizeof(RecordedSample));

    Wire.begin(kImuSda, kImuScl, 400000);
    imuOk = imu.begin(Wire);
    tracker.begin(store::loadGyroBias());
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

    const int64_t frameStartUs = esp_timer_get_time();
    const uint32_t nowMs = millis();

    handleSerial();
    drainRecording();
    if (printAxes && !recording && nowMs - lastAxisPrintMs >= 200) {
        printAxisCheck();
        lastAxisPrintMs = nowMs;
    }

    kd::ControlEvent e = controls.update(!digitalRead(kButtonA), !digitalRead(kButtonB), !digitalRead(kButtonC), nowMs);
    if (e.kind != kd::ControlEvent::Kind::None) {
        lastChanged = e.changed;
        if (e.kind == kd::ControlEvent::Kind::Relevel) {
            xSemaphoreTake(trackerLock, portMAX_DELAY);
            tracker.relevel();
            xSemaphoreGive(trackerLock);
        }
        wakeDisplay = true;
    }
    if (controls.shouldSave(nowMs)) {
        store::saveSettings(settings);
        controls.markSaved();
    }

    if (nowMs - lastBatteryMs >= kBatteryPeriodMs) {
        battery = readBattery(battery);
        lastBatteryMs = nowMs;
    }

    kd::FrameInput in;
    in.showTime = frameStartUs * 1e-6;  // Show Clock: local until Dongs sync
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
        xSemaphoreGive(trackerLock);

        if (ignited && ignitedAtUs < 0) ignitedAtUs = frameStartUs;
        if (biasGeneration != savedBiasGeneration &&
            (savedBiasGeneration == 0 || nowMs - lastBiasSaveMs >= kBiasSaveIntervalMs)) {
            store::saveGyroBias(bias);
            savedBiasGeneration = biasGeneration;
            lastBiasSaveMs = nowMs;
        }
    } else if (ignitedAtUs < 0) {
        ignitedAtUs = frameStartUs;
    }
    in.sinceIgnition = ignitedAtUs < 0 ? 0.f : (frameStartUs - ignitedAtUs) * 1e-6f;

    renderer.render(in, settings, lines.data());
    governor.limit(lines.data(), lines.size(), battery);
    strandMap.scatter(lines.data(), strandPtrs);
    FastLED.show();

    // Pixels latch at the end of show(): predict that far ahead next frame.
    const int64_t frameUs = esp_timer_get_time() - frameStartUs;
    lookahead += (frameUs * 1e-6f - lookahead) * 0.05f;
    fps += (1e6f / float(frameUs < kMinFrameUs ? kMinFrameUs : frameUs) - fps) * 0.05f;

    portENTER_CRITICAL(&statusMux);
    memcpy(shownLines.data(), lines.data(), lines.size() * sizeof(kd::Rgb8));
    status.settings = settings;
    status.changed = lastChanged;
    status.batteryFraction = battery;
    status.fps = fps;
    status.imuHz = imuHz;
    status.atRest = atRest;
    status.recording = recording;
    portEXIT_CRITICAL(&statusMux);

    if (frameUs < kMinFrameUs) delayMicroseconds(kMinFrameUs - frameUs);
}
