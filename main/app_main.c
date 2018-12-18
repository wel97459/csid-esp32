/* Based on the ESP-IDF I2S Example */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"
#include "esp_system.h"

#include <stdio.h>
#include <math.h>

#include <libcsid.h>

#include "xi2c.h"
#include "fonts.h"
#include "ssd1306.h"

#include "Hexadecimal.h"

// from ESP32 Audio shield demo

static QueueHandle_t i2s_event_queue;

#define I2C_EXAMPLE_MASTER_SCL_IO    14    /*!< gpio number for I2C master clock */////////////
#define I2C_EXAMPLE_MASTER_SDA_IO    13    /*!< gpio number for I2C master data  *//////////////
#define I2C_EXAMPLE_MASTER_NUM I2C_NUM_1   /*!< I2C port number for master dev */
#define I2C_EXAMPLE_MASTER_TX_BUF_DISABLE   0   /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_RX_BUF_DISABLE   0   /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_FREQ_HZ    100000     /*!< I2C master clock frequency */


#define SAMPLE_RATE     (18000)
#define I2S_CHANNEL        I2S_NUM_0
// #define WAVE_FREQ_HZ    (200)
#define PI 3.14159265

// #define SAMPLE_PER_CYCLE (SAMPLE_RATE / WAVE_FREQ_HZ)
#define HALF_SAMPLERATE (SAMPLE_RATE / 2)

unsigned long phase = 0;
unsigned short waveform_buffer[128] = { 0, };

static void setup_triangle_sine_waves()
{
    int samples = 300;
    unsigned short *mono_samples_data = (unsigned short *)malloc(2 * samples);
    unsigned short *samples_data = (unsigned short *)malloc(2 * 2 * samples);

    unsigned int i, sample_val;
    double sin_float;

    libcsid_render(mono_samples_data, samples);

    for(i = 0; i < samples; i ++) {
        sample_val = mono_samples_data[i];
        samples_data[i * 2 + 0] = sample_val;

        if (i < 128) {
            waveform_buffer[i] = sample_val;
        }

        samples_data[i * 2 + 1] = sample_val;
    }

    int pos = 0;
    int left = 2 * 2 * samples;
    unsigned char *ptr = (unsigned char *)samples_data;

    while (left > 0) {
        int written = i2s_write_bytes(I2S_CHANNEL, (const char *)ptr, left, 1);
        pos += written;
        ptr += written;
        left -= written;
    }

    free(samples_data);
    free(mono_samples_data);
}


void audiorenderer_loop(void *pvParameter) {
    while(1) {
        setup_triangle_sine_waves();
    }
}

void cpurenderer_loop(void *pvParameter) {
    while(1) {
        runCPU(1);
    }
}

void renderer_zero_dma_buffer() {
    i2s_zero_dma_buffer(I2S_CHANNEL);
}

void audioplayer_init() {
    i2s_mode_t mode = I2S_MODE_MASTER | I2S_MODE_TX;

    i2s_config_t i2s_config = {
        .mode = mode,          // Only TX
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = 16,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,   // 2-channels
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
        .dma_buf_count = 32,                            // number of buffers, 128 max.
        .dma_buf_len = 32 * 2,                          // size of each buffer
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1        // Interrupt level 1
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = 26,
        .ws_io_num = 25,
        .data_out_num = 22,
        .data_in_num = I2S_PIN_NO_CHANGE    // Not used
    };

    i2s_driver_install(I2S_CHANNEL, &i2s_config, 1, &i2s_event_queue);
    i2s_set_pin(I2S_CHANNEL, &pin_config);
    i2s_set_sample_rates(I2S_CHANNEL, SAMPLE_RATE);
    i2s_stop(I2S_CHANNEL);
}

void audioplayer_start() {
    i2s_start(I2S_CHANNEL);
    i2s_zero_dma_buffer(I2S_CHANNEL);
}

void app_main() {
    printf("-----------------------------------\n");
    printf("HELLO WORLD\n");
    printf("-----------------------------------\n");

    audioplayer_init();
    audioplayer_start();

    libcsid_init(SAMPLE_RATE, SIDMODEL_6581);
    libcsid_load((unsigned char *)&rawData, sizeof(rawData), 0);

    printf("SID Title: %s\n", libcsid_gettitle());
    printf("SID Author: %s\n", libcsid_getauthor());
    printf("SID Info: %s\n", libcsid_getinfo());


    xTaskCreatePinnedToCore(&audiorenderer_loop, "audio", 16384, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(&cpurenderer_loop, "cpu", 16384, NULL, 5, NULL, 0);
}
