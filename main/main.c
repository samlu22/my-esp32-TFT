/*
 * TFT pureflow image display
 * Direct port of sketch_apr18a.ino to ESP-IDF
 *
 * Files in main/:
 *   main.c       (this file)
 *   pureflow.c   (from LVGL converter)
 *   lvgl.h       (your existing header)
 *
 * Pins match User_Setup.h exactly:
 *   MOSI=17 SCLK=18 CS=6 DC=7 RST=5 BL=15
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

// declared in pureflow.c
extern const lv_image_dsc_t pureflow;

static const char *TAG = "tft";

#define TFT_MOSI  17
#define TFT_SCLK  18
#define TFT_CS     6
#define TFT_DC     7
#define TFT_RST    5
#define TFT_BL    15   // matches sketch_apr18a.ino line 16

static spi_device_handle_t s_spi;

// ─── SPI helpers ──────────────────────────────────────────────────────────────

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

// ─── Init — mirrors TFT_eSPI ST7789 for T-Display-S3 ─────────────────────────

static void tft_init(void) {
    // BL pin — same as sketch_apr18a.ino:
    //   pinMode(15, OUTPUT); digitalWrite(15, HIGH);
    gpio_reset_pin(TFT_BL);
    gpio_set_direction(TFT_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_BL, 1);
    ESP_LOGI(TAG, "BL on");

    gpio_reset_pin(TFT_RST);
    gpio_set_direction(TFT_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    gpio_reset_pin(TFT_DC);
    gpio_set_direction(TFT_DC, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_DC, 1);

    spi_bus_config_t bc = {
        .mosi_io_num   = TFT_MOSI, .miso_io_num = -1,
        .sclk_io_num   = TFT_SCLK, .quadwp_io_num = -1,
        .quadhd_io_num = -1, .max_transfer_sz = 320 * 170 * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bc, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dc = {
        .clock_speed_hz = 40000000, .mode = 0,
        .spics_io_num   = TFT_CS,   .queue_size = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dc, &s_spi));

    uint8_t v;
    tft_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150)); // SW reset
    tft_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120)); // sleep out
    tft_cmd(0x3A); v = 0x55; tft_data(&v, 1);      // RGB565
    tft_cmd(0x36); v = 0x60; tft_data(&v, 1);      // MADCTL landscape
    tft_cmd(0x21);                                  // invert ON  (ST7789 T-Display-S3)
    tft_cmd(0x13);                                  // normal display on
    tft_cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));  // display on

    ESP_LOGI(TAG, "TFT init done");
}

// ─── Fill screen ──────────────────────────────────────────────────────────────

static void tft_fill(uint16_t color) {
    uint8_t ca[] = {0x00,0x00,0x01,0x3F}; // x 0..319
    uint8_t ra[] = {0x00,0x00,0x00,0xA9}; // y 0..169
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

// ─── Push image — mirrors tft.setSwapBytes(true) + tft.pushImage() ───────────
// pureflow_map is little-endian RGB565 (LVGL format)
// setSwapBytes(true) means TFT_eSPI swaps byte pairs before sending
// → we must also swap: send [hi, lo] instead of [lo, hi]

static void tft_push_image(const lv_image_dsc_t *img) {
    uint16_t w = (uint16_t)img->header.w;
    uint16_t h = (uint16_t)img->header.h;
    const uint8_t *src = img->data;

    uint8_t ca[4] = {0x00,0x00,(uint8_t)((w-1)>>8),(uint8_t)(w-1)};
    uint8_t ra[4] = {0x00,0x00,(uint8_t)((h-1)>>8),(uint8_t)(h-1)};
    tft_cmd(0x2A); tft_data(ca, 4);
    tft_cmd(0x2B); tft_data(ra, 4);
    tft_cmd(0x2C);
    gpio_set_level(TFT_DC, 1);

    // Swap bytes per pixel to match setSwapBytes(true)
    // Allocate one row buffer on heap — 320*2 = 640 bytes
    static uint8_t row[320 * 2];
    for (int y = 0; y < h; y++) {
        const uint8_t *p = src + y * w * 2;
        for (int x = 0; x < w; x++) {
            row[x*2]   = p[x*2 + 1]; // hi byte
            row[x*2+1] = p[x*2];     // lo byte
        }
        spi_transaction_t t = { .length = (size_t)w*2*8, .tx_buffer = row };
        spi_device_polling_transmit(s_spi, &t);
    }
    ESP_LOGI(TAG, "image pushed %dx%d", w, h);
}

// ─── Main ─────────────────────────────────────────────────────────────────────

void app_main(void) {
    ESP_LOGI(TAG, "=== pureflow image test ===");

    tft_init();

    // white flash — proves backlight + SPI
    ESP_LOGI(TAG, "white");
    tft_fill(0xFFFF);
    vTaskDelay(pdMS_TO_TICKS(400));

    // black
    tft_fill(0x0000);
    vTaskDelay(pdMS_TO_TICKS(100));

    // push pureflow image
    ESP_LOGI(TAG, "pushing pureflow %dx%d", pureflow.header.w, pureflow.header.h);
    tft_push_image(&pureflow);

    ESP_LOGI(TAG, "done — holding");
    while (1) vTaskDelay(pdMS_TO_TICKS(5000));
}
