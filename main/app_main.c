/* Based on the ESP-IDF I2S Example */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "app_main.h"
#include "controls.h"

#include <stdio.h>
#include <math.h>

#include <libcsid.h>

//#include "Hexadecimal.h"

// from ESP32 Audio shield demo
#define TAG "app_main"

static QueueHandle_t i2s_event_queue;

#define I2C_EXAMPLE_MASTER_SCL_IO    14    /*!< gpio number for I2C master clock */////////////
#define I2C_EXAMPLE_MASTER_SDA_IO    13    /*!< gpio number for I2C master data  *//////////////
#define I2C_EXAMPLE_MASTER_NUM I2C_NUM_1   /*!< I2C port number for master dev */
#define I2C_EXAMPLE_MASTER_TX_BUF_DISABLE   0   /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_RX_BUF_DISABLE   0   /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_FREQ_HZ    100000     /*!< I2C master clock frequency */


#define SAMPLE_RATE CONFIG_SAMPLE_RATE
#define I2S_CHANNEL        I2S_NUM_0
// #define WAVE_FREQ_HZ    (200)
#define PI 3.14159265

// #define SAMPLE_PER_CYCLE (SAMPLE_RATE / WAVE_FREQ_HZ)
#define HALF_SAMPLERATE (SAMPLE_RATE / 2)

#define STOPED 0
#define PLAYING 1
#define STOPING 2

uint8_t playerAudio = STOPED;
uint8_t playerCPU = STOPED;

unsigned long phase = 0;
unsigned short waveform_buffer[128] = { 0, };

static void init_sdCard()
{
    ESP_LOGI(TAG, "Initializing SD card");

    sdmmc_host_t sdHost = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.

    // To use 1-line SD mode, uncomment the following line:
    // host.flags = SDMMC_HOST_FLAG_1BIT;

    // GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
    // Internal pull-ups are not sufficient. However, enabling internal pull-ups
    // does make a difference some boards, so we do that here.
    gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);   // CMD, needed in 4- and 1- line modes
    gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);    // D0, needed in 4- and 1-line modes
    gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);    // D1, needed in 4-line mode only
    gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);   // D2, needed in 4-line mode only
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);   // D3, needed in 4- and 1-line modes
    gpio_pullup_en(GPIO_NUM_12);
	gpio_pullup_en(GPIO_NUM_2);
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &sdHost, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
}

static void setup_triangle_sine_waves()
{
    int samples = 250;
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
    int c=0;
    while(playerAudio == 1) {
        setup_triangle_sine_waves();
        if(c++>20){c=0; vTaskDelay(1);}
    }
    playerAudio = STOPED;
    vTaskDelete(NULL);
}

void cpurenderer_loop(void *pvParameter) {
    int c=0;
    while(playerCPU == 1) {
        runCPU(1);
        if(c++>500){c=0; vTaskDelay(1);}
    }
    playerCPU = STOPED;
    vTaskDelete(NULL);
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

void  playerStart() {
    playerAudio = PLAYING;
    playerCPU = PLAYING;
    xTaskCreatePinnedToCore(&audiorenderer_loop, "audio", 16384, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(&cpurenderer_loop, "cpu", 16384, NULL, 5, NULL, 0);
}

void sd_player_gpio_handler_task(void *pvParams)
{
    gpio_handler_param_t *params = pvParams;
    xQueueHandle gpio_evt_queue = params->gpio_evt_queue;

    playerAudio = STOPED;
    playerCPU = STOPED;

    static FILE* f;
    char filename[128] = "/sdcard/";
    char *filenameOffset = filename+8;

    f = fopen(CONFIG_PLAYLIST_FILE, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        controls_destroy();
        return;
    }
    int32_t mt = xTaskGetTickCount();

    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {

            ESP_LOGI(TAG, "GPIO[%d] intr, val: %d", io_num, gpio_get_level(io_num));
            if(mt < xTaskGetTickCount()){
                ESP_LOGI(TAG, "mt: %u   t:%u", mt, xTaskGetTickCount());
                mt = xTaskGetTickCount()+100;

                if (playerAudio == PLAYING) playerAudio = STOPING;
                if (playerCPU == PLAYING) playerCPU = STOPING;

                while(playerAudio != STOPED && playerCPU != STOPED) {
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                }

                fgets(filenameOffset, 120, f);
                if(feof(f)) rewind(f);

                ESP_LOGI(TAG, "Filename: %s", filename);

                if(libcsid_loadFile(filename, 0)){
                    ESP_LOGI(TAG, "SID Title: %s\n", libcsid_gettitle());
                    ESP_LOGI(TAG, "SID Author: %s\n", libcsid_getauthor());
                    ESP_LOGI(TAG, "SID Info: %s\n", libcsid_getinfo());

                    playerStart();
                }
            }
        }
    }
}

void app_main() {
    printf("-----------------------------------\n");
    printf("HELLO WORLD\n");
    printf("-----------------------------------\n");

    init_sdCard();

    audioplayer_init();
    audioplayer_start();
    libcsid_init(SAMPLE_RATE, SIDMODEL_6581);

    controls_init(sd_player_gpio_handler_task, 2048, NULL);
}
