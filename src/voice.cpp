#include "voice.h"

#include <esp_heap_caps.h>
#include <esp_partition.h>

#include <cmath>

#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
extern "C" {
#include "flite_g2p.h"  // C, without its own extern "C"
}
#include "model_path.h"

namespace {

constexpr uint32_t kRate = 16000;
constexpr uint32_t kBufferMs = 3000;   // audio the voice task may fall behind by
constexpr uint32_t kFlushMs = 600;     // silence fed after letting go, so the last word ends
constexpr int kMultinetTimeoutMs = 6000;
constexpr int kMaxResults = 8;

const esp_mn_iface_t* multinet = nullptr;
model_iface_data_t* model = nullptr;
int chunkSamples = 0;

struct Queued {
    Voice::Result result;
    uint32_t generation;
};

}  // namespace

bool Voice::begin(UBaseType_t priority, BaseType_t core) {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model");
    if (!part) {
        error_ = "no 'model' partition (this build's partition table has none)";
        return false;
    }
    uint32_t first = 0xFFFFFFFF;
    esp_partition_read(part, 0, &first, sizeof(first));
    if (first == 0xFFFFFFFF || first == 0 || first > 64) {
        error_ = "model partition is empty: pio run -e xiao_screen -t upload_voice_model";
        return false;
    }
    srmodel_list_t* models = esp_srmodel_init("model");
    char* name = models ? esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH) : nullptr;
    if (!name) {
        error_ = "no English MultiNet model in the model partition";
        return false;
    }
    modelName_ = name;
    multinet = esp_mn_handle_from_name(name);
    model = multinet->create(name, kMultinetTimeoutMs);
    if (!model) {
        error_ = "MultiNet create failed (out of PSRAM?)";
        return false;
    }
    chunkSamples = multinet->get_samp_chunksize(model);

    // Each phrase's id is its index, so a result names the exact phrase.
    esp_mn_commands_alloc(multinet, model);
    size_t n = 0;
    const kd::VoicePhrase* phrases = kd::voicePhrases(n);
    for (size_t i = 0; i < n; ++i) {
        char* phonemes = flite_g2p(phrases[i].words, 1);
        if (!phonemes) {
            Serial.printf("# voice: no phonemes for '%s'\n", phrases[i].words);
            continue;
        }
        esp_mn_commands_phoneme_add(static_cast<int>(i), phrases[i].words, phonemes);
        free(phonemes);
    }
    if (esp_mn_error_t* err = esp_mn_commands_update()) {
        for (int i = 0; i < err->num; ++i)
            Serial.printf("# voice: rejected '%s'\n", err->phrases[i]->string);
    }

    const size_t bufferBytes = kRate * kBufferMs / 1000 * sizeof(int16_t);
    auto* storage = static_cast<uint8_t*>(heap_caps_malloc(bufferBytes + 1, MALLOC_CAP_SPIRAM));
    auto* control = static_cast<StaticStreamBuffer_t*>(heap_caps_malloc(sizeof(StaticStreamBuffer_t), MALLOC_CAP_INTERNAL));
    if (!storage || !control) {
        error_ = "no memory for the audio buffer";
        return false;
    }
    stream_ = xStreamBufferCreateStatic(bufferBytes, sizeof(int16_t) * chunkSamples, storage, control);
    results_ = xQueueCreate(kMaxResults, sizeof(Queued));
    stats_.chunkSamples = chunkSamples;
    xTaskCreatePinnedToCore(taskEntry, "voice", 8192, this, priority, &task_, core);
    error_ = nullptr;
    return true;
}

void Voice::feed(const int16_t* samples, size_t count) {
    if (!accepting_.load(std::memory_order_relaxed)) return;
    int16_t out[256];
    int32_t peak = 0;
    while (count > 0) {
        const size_t n = count < 256 ? count : 256;
        for (size_t i = 0; i < n; ++i) {
            // DC blocker (~10 Hz), then gain, saturating.
            const float x = samples[i];
            dcOut_ = x - dcIn_ + 0.996f * dcOut_;
            dcIn_ = x;
            float y = dcOut_ * gain_;
            y = y > 32767.f ? 32767.f : (y < -32768.f ? -32768.f : y);
            out[i] = static_cast<int16_t>(y);
            const int32_t a = std::abs(static_cast<int32_t>(out[i]));
            if (a > peak) peak = a;
        }
        if (xStreamBufferSend(stream_, out, n * sizeof(int16_t), 0) != n * sizeof(int16_t)) ++stats_.dropped;
        samples += n;
        count -= n;
    }
    if (peak > peak_.load(std::memory_order_relaxed)) peak_.store(peak, std::memory_order_relaxed);
}

void Voice::listen() {
    if (!task_) return;
    listenMs_ = millis();
    finishing_ = false;
    generation_.fetch_add(1);
    peak_ = 0;
    accepting_ = true;
    xTaskNotifyGive(task_);
}

void Voice::finish() {
    if (!task_) return;
    accepting_ = false;  // only what was said while the button was down
    finishMs_ = millis();
    finishing_ = true;
    xTaskNotifyGive(task_);
}

void Voice::post(const Result& r) {
    Queued q{r, taskGeneration_};
    xQueueSend(results_, &q, 0);
}

bool Voice::poll(Result& out) {
    if (!results_) return false;
    Queued q;
    while (xQueueReceive(results_, &q, 0) == pdTRUE) {
        if (q.generation != generation_.load()) continue;  // from an earlier press
        out = q.result;
        return true;
    }
    return false;
}

Voice::Stats Voice::stats() const {
    Stats s = stats_;
    s.gain = gain_;
    return s;
}

void Voice::detect(int16_t* chunk) {
    const int64_t t0 = esp_timer_get_time();
    const esp_mn_state_t state = multinet->detect(model, chunk);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    stats_.detectUs += (us - stats_.detectUs) * 0.05f;
    if (us > stats_.worstDetectUs) stats_.worstDetectUs = us;
    if (state == ESP_MN_STATE_DETECTED) {
        const esp_mn_results_t* r = multinet->get_results(model);
        if (r->num > 0) {
            size_t n = 0;
            const kd::VoicePhrase* phrases = kd::voicePhrases(n);
            const int id = r->command_id[0];
            if (id >= 0 && static_cast<size_t>(id) < n) {
                Result out;
                out.kind = Result::Kind::Heard;
                out.command = phrases[id].command;
                out.words = phrases[id].words;
                out.probability = r->prob[0];
                out.afterMs = millis() - listenMs_;
                post(out);
            }
        }
        multinet->clean(model);  // listen for another
    } else if (state == ESP_MN_STATE_TIMEOUT) {
        multinet->clean(model);
    }
}

void Voice::run() {
    auto* chunk = static_cast<int16_t*>(heap_caps_malloc(chunkSamples * sizeof(int16_t), MALLOC_CAP_INTERNAL));
    const size_t chunkBytes = chunkSamples * sizeof(int16_t);
    size_t filled = 0;
    bool active = false;
    for (;;) {
        const uint32_t gen = generation_.load();
        if (gen != taskGeneration_) {  // a new press: start clean
            taskGeneration_ = gen;
            multinet->clean(model);
            filled = 0;
            active = true;
        }
        if (!active) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        const size_t waiting = xStreamBufferBytesAvailable(stream_);
        const uint32_t backlogMs = waiting / sizeof(int16_t) * 1000 / kRate;
        if (backlogMs > stats_.worstBacklogMs) stats_.worstBacklogMs = backlogMs;
        filled += xStreamBufferReceive(stream_, reinterpret_cast<uint8_t*>(chunk) + filled, chunkBytes - filled,
                                       pdMS_TO_TICKS(20));
        if (filled == chunkBytes) {
            detect(chunk);
            filled = 0;
            continue;
        }
        if (!finishing_ || accepting_ || xStreamBufferBytesAvailable(stream_) > 0) continue;
        // Let go and everything said has been heard: a little silence lets
        // the recognizer close the last word.
        memset(reinterpret_cast<uint8_t*>(chunk) + filled, 0, chunkBytes - filled);
        for (uint32_t flushed = 0; flushed < kFlushMs * kRate / 1000; flushed += chunkSamples) {
            if (generation_.load() != taskGeneration_) break;
            detect(chunk);
            memset(chunk, 0, chunkBytes);
        }
        filled = 0;
        active = false;
        finishing_ = false;
        Result done;
        done.kind = Result::Kind::Finished;
        done.afterMs = millis() - finishMs_;
        const int32_t peak = peak_.load();
        done.peakDb = peak > 0 ? 20.f * std::log10(peak / 32768.f) : -90.f;
        post(done);
    }
}
