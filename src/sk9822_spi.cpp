#include "sk9822_spi.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <soc/spi_periph.h>

#include <cstring>

namespace {

constexpr spi_host_device_t kHost = SPI2_HOST;
constexpr size_t kMaxTransferBytes = 32 * 1024;  // one S3 SPI DMA transaction
constexpr size_t kStartBytes = 4;
constexpr uint8_t kFullGlobalBrightness = 0xE0 | 31;

spi_device_handle_t device;
spi_transaction_t transactions[kd::kMaxStrands];

// Copied to RAM for the transfer callback, which runs in an interrupt.
DRAM_ATTR int routeCount = 0;
DRAM_ATTR int routeData[kd::kMaxStrands];
DRAM_ATTR int routeClock[kd::kMaxStrands];
DRAM_ATTR uint32_t dataSignal = 0;
DRAM_ATTR uint32_t clockSignal = 0;

// Points SPI2 at Strand `t->user`, parking the other Strands' pins low.
void IRAM_ATTR routeToStrand(spi_transaction_t* t) {
    if (routeCount < 2) return;
    const int strand = static_cast<int>(reinterpret_cast<intptr_t>(t->user));
    for (int s = 0; s < routeCount; ++s) {
        const bool active = s == strand;
        esp_rom_gpio_connect_out_signal(routeData[s], active ? dataSignal : SIG_GPIO_OUT_IDX, false, false);
        esp_rom_gpio_connect_out_signal(routeClock[s], active ? clockSignal : SIG_GPIO_OUT_IDX, false, false);
    }
}

size_t frameBytes(size_t pixels) {
    // Start frame, 4 bytes per Pixel, then enough zero clocks to push the
    // data through every Pixel (n/2 bits) plus SK9822's 32-bit reset frame.
    return kStartBytes + 4 * pixels + (pixels + 15) / 16 + 4;
}

}  // namespace

bool Sk9822Spi::begin(const Strand* strands, size_t count, uint32_t clockHz, const char* wireOrder) {
    if (count < 1 || count > kd::kMaxStrands) return false;
    count_ = count;
    size_t largest = 0;
    for (size_t s = 0; s < count; ++s) {
        strands_[s] = strands[s];
        size_t bytes = frameBytes(strands[s].length);
        if (bytes > kMaxTransferBytes) return false;
        largest = bytes > largest ? bytes : largest;
        for (Buffer* set : {buffers_[0], buffers_[1]}) {
            set[s].size = bytes;
            set[s].bytes = static_cast<uint8_t*>(heap_caps_calloc(1, bytes, MALLOC_CAP_DMA));
            if (!set[s].bytes) return false;
        }
    }
    size_t totalBytes = 0;
    for (size_t s = 0; s < count; ++s) totalBytes += frameBytes(strands[s].length);
    sendUs_ = static_cast<uint32_t>(totalBytes * 8ull * 1000000ull / clockHz);
    for (int i = 0; i < 3; ++i) {
        const char c = wireOrder[i];
        order_[i] = c == 'R' ? 0 : (c == 'G' ? 1 : 2);
    }

    // Park every pin low first; the bus then claims Strand 0's pins.
    for (size_t s = 0; s < count; ++s) {
        for (int pin : {strands[s].dataPin, strands[s].clockPin}) {
            gpio_reset_pin(static_cast<gpio_num_t>(pin));
            gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
            gpio_set_level(static_cast<gpio_num_t>(pin), 0);
        }
        routeData[s] = strands[s].dataPin;
        routeClock[s] = strands[s].clockPin;
    }
    routeCount = static_cast<int>(count);
    dataSignal = spi_periph_signal[kHost].spid_out;
    clockSignal = spi_periph_signal[kHost].spiclk_out;

    spi_bus_config_t bus = {};
    bus.mosi_io_num = strands[0].dataPin;
    bus.miso_io_num = -1;
    bus.sclk_io_num = strands[0].clockPin;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = static_cast<int>(largest);
    if (spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = static_cast<int>(clockHz);
    dev.spics_io_num = -1;
    dev.queue_size = static_cast<int>(kd::kMaxStrands);
    dev.pre_cb = routeToStrand;
    return spi_bus_add_device(kHost, &dev, &device) == ESP_OK;
}

void Sk9822Spi::encode(size_t strand, const kd::Rgb8* px, Buffer& out) const {
    uint8_t* p = out.bytes + kStartBytes;  // start frame and end frame stay zero
    for (size_t i = 0; i < strands_[strand].length; ++i) {
        const uint8_t rgb[3] = {px[i].r, px[i].g, px[i].b};
        *p++ = kFullGlobalBrightness;
        *p++ = rgb[order_[0]];
        *p++ = rgb[order_[1]];
        *p++ = rgb[order_[2]];
    }
}

void Sk9822Spi::show(const kd::Rgb8* const* pixels) {
    Buffer* set = buffers_[back_];
    for (size_t s = 0; s < count_; ++s) encode(s, pixels[s], set[s]);

    const uint32_t waitStart = micros();
    for (; inFlight_ > 0; --inFlight_) {
        spi_transaction_t* done;
        spi_device_get_trans_result(device, &done, portMAX_DELAY);
    }
    lastWaitUs_ = micros() - waitStart;

    for (size_t s = 0; s < count_; ++s) {
        spi_transaction_t& t = transactions[s];
        memset(&t, 0, sizeof(t));
        t.length = set[s].size * 8;
        t.tx_buffer = set[s].bytes;
        t.user = reinterpret_cast<void*>(static_cast<intptr_t>(s));
        if (spi_device_queue_trans(device, &t, portMAX_DELAY) == ESP_OK) ++inFlight_;
    }
    back_ ^= 1;
}
