/*
 * T-Display-S3 TFT diagnostic
 * Cycles through all MADCTL + invert combos automatically
 * Also verifies pureflow data is read correctly
 *
 * Watch TeraTerm at 115200 to see which combo number works
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

extern const lv_image_dsc_t pureflow;

static const char *TAG = "tft";

#define TFT_MOSI  17
#define TFT_SCLK  18
#define TFT_CS     6
#define TFT_DC     7
#define TFT_RST    5
#define TFT_BL    38

static spi_device_handle_t s_spi;

static void tft_cmd(uint8_t cmd) {
    gpio_set_level(TFT_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_polling_transmit(s_spi, &t);
}

static void tft_data(const uint8_t *d, int len) {
    if (!len) return;
    gpio_set_level(TFT_DC, 1);
    spi_transaction_t t = { .length = (size_t)len * 8, .tx_buffer = d };
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_init(void) {
    gpio_reset_pin(TFT_BL);
    gpio_set_direction(TFT_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_BL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(TFT_BL, 1);
    ESP_LOGI(TAG, "BL=38 HIGH");

    gpio_reset_pin(TFT_RST);
    gpio_set_direction(TFT_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    gpio_reset_pin(TFT_DC);
    gpio_set_direction(TFT_DC, GPIO_MODE_OUTPUT);

    spi_bus_config_t bc = {
        .mosi_io_num = TFT_MOSI, .miso_io_num = -1,
        .sclk_io_num = TFT_SCLK, .quadwp_io_num = -1,
        .quadhd_io_num = -1, .max_transfer_sz = 320 * 170 * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bc, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dc = {
        .clock_speed_hz = 40000000, .mode = 0,
        .spics_io_num = TFT_CS, .queue_size = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dc, &s_spi));
}

// Apply ST7789 init with given MADCTL and invert setting
static void st7789_init(uint8_t madctl, bool invert) {
    uint8_t v;
    tft_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));   // SW reset
    tft_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));   // sleep out
    tft_cmd(0x3A); v = 0x55; tft_data(&v, 1);        // RGB565
    tft_cmd(0x36); tft_data(&madctl, 1);              // MADCTL
    tft_cmd(invert ? 0x21 : 0x20);                   // invert on/off
    tft_cmd(0x13);                                    // normal mode
    tft_cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));    // display on
}

static void tft_fill(uint16_t color) {
    uint8_t ca[] = {0x00,0x00,0x01,0x3F};
    uint8_t ra[] = {0x00,0x00,0x00,0xA9};
    tft_cmd(0x2A); tft_data(ca, 4);
    tft_cmd(0x2B); tft_data(ra, 4);
    tft_cmd(0x2C);
    gpio_set_level(TFT_DC, 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    uint8_t buf[320 * 2];
    for (int i = 0; i < 320; i++) { buf[i*2] = hi; buf[i*2+1] = lo; }
    spi_transaction_t t = { .length = 320*2*8, .tx_buffer = buf };
    for (int y = 0; y < 170; y++) spi_device_polling_transmit(s_spi, &t);
}

static void tft_push_image(void) {
    uint16_t w = (uint16_t)pureflow.header.w;
    uint16_t h = (uint16_t)pureflow.header.h;
    const uint8_t *src = pureflow.data;

    uint8_t ca[4] = {0x00,0x00,(uint8_t)((w-1)>>8),(uint8_t)(w-1)};
    uint8_t ra[4] = {0x00,0x00,(uint8_t)((h-1)>>8),(uint8_t)(h-1)};
    tft_cmd(0x2A); tft_data(ca, 4);
    tft_cmd(0x2B); tft_data(ra, 4);
    tft_cmd(0x2C);
    gpio_set_level(TFT_DC, 1);

    static uint8_t row[320 * 2];
    for (int y = 0; y < h; y++) {
        const uint8_t *p = src + y * w * 2;
        for (int x = 0; x < w; x++) {
            row[x*2]   = p[x*2+1];  // swap bytes
            row[x*2+1] = p[x*2];
        }
        spi_transaction_t t = { .length = (size_t)w*2*8, .tx_buffer = row };
        spi_device_polling_transmit(s_spi, &t);
    }
}

// Verify pureflow data is actually readable and non-zero
static void verify_pureflow(void) {
    uint16_t w = (uint16_t)pureflow.header.w;
    uint16_t h = (uint16_t)pureflow.header.h;
    uint32_t sz = pureflow.data_size;
    const uint8_t *d = pureflow.data;

    ESP_LOGI(TAG, "--- pureflow verify ---");
    ESP_LOGI(TAG, "  w=%d h=%d data_size=%lu", w, h, (unsigned long)sz);
    ESP_LOGI(TAG, "  data ptr = %p", d);

    // Print first 8 bytes as hex
    if (d) {
        ESP_LOGI(TAG, "  first bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
            d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7]);
    } else {
        ESP_LOGE(TAG, "  data pointer is NULL!");
    }

    // Count non-zero bytes in first 1000
    int nonzero = 0;
    for (int i = 0; i < 1000 && i < (int)sz; i++) if (d[i]) nonzero++;
    ESP_LOGI(TAG, "  non-zero in first 1000 bytes: %d", nonzero);
    ESP_LOGI(TAG, "-----------------------");
}

void app_main(void) {
    ESP_LOGI(TAG, "=== TFT diagnostic — cycling MADCTL + invert ===");

    // Verify pureflow data before touching display
    verify_pureflow();

    spi_init();

    // All MADCTL values to try (covers all rotations and mirrors)
    static const uint8_t madctl_vals[] = {
        0x00, 0x60, 0xA0, 0xC0,   // no swap
        0x20, 0x40, 0x80, 0xE0,   // other common values
    };
    static const bool invert_vals[] = { false, true };

    int combo = 0;
    while (1) {
        for (int iv = 0; iv < 2; iv++) {
            for (int mv = 0; mv < 8; mv++) {
                uint8_t madctl = madctl_vals[mv];
                bool invert = invert_vals[iv];

                ESP_LOGI(TAG, "=== Combo %d: MADCTL=0x%02X invert=%s ===",
                    combo, madctl, invert ? "ON" : "OFF");

                st7789_init(madctl, invert);

                // White flash first — if display works you'll see white
                tft_fill(0xFFFF);
                vTaskDelay(pdMS_TO_TICKS(300));

                // Then push image
                tft_push_image();
                vTaskDelay(pdMS_TO_TICKS(2000));

                combo++;
            }
        }
        // After full cycle, just hold last working one if you saw it
        ESP_LOGI(TAG, "=== Full cycle done, repeating ===");
    }
}
