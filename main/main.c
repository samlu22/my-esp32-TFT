/*
 * TFT Hello World — T-Display-S3
 * Pins from sketch_apr18a.ino / User_Setup.h:
 *   MOSI=17  SCLK=18  CS=6  DC=7  RST=5  BL=15
 * Upload via COM7 (CH340 UART port)
 * Debug via TeraTerm at 115200 baud
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "tft";

#define TFT_MOSI  17
#define TFT_SCLK  18
#define TFT_CS     6
#define TFT_DC     7
#define TFT_RST    5
#define TFT_BL    15

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

static void tft_init(void) {
    ESP_LOGI(TAG, "BL pin %d HIGH", TFT_BL);
    gpio_set_direction(TFT_BL,  GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_BL,  1);

    gpio_set_direction(TFT_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    gpio_set_direction(TFT_DC, GPIO_MODE_OUTPUT);
    gpio_set_level(TFT_DC, 1);

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

    uint8_t v;
    tft_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));
    tft_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));
    tft_cmd(0x3A); v = 0x55; tft_data(&v, 1);
    tft_cmd(0x36); v = 0x60; tft_data(&v, 1);
    tft_cmd(0x21);
    tft_cmd(0x13);
    tft_cmd(0x29); vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "TFT init done");
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

// 5x7 font — A-Z 0-9 space colon
static const uint8_t FONT[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // space
    {0x3E,0x11,0x11,0x11,0x3E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x01,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x3A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x04,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
};

static int ch_idx(char c) {
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 1 + (c - 'a');
    if (c >= '0' && c <= '9') return 27 + (c - '0');
    if (c == ':') return 37;
    return 0;
}

static void tft_pixel(int x, int y, uint16_t color) {
    uint8_t ca[4] = {(uint8_t)(x>>8),(uint8_t)x,(uint8_t)(x>>8),(uint8_t)x};
    uint8_t ra[4] = {(uint8_t)(y>>8),(uint8_t)y,(uint8_t)(y>>8),(uint8_t)y};
    uint8_t c[2]  = {(uint8_t)(color>>8),(uint8_t)color};
    tft_cmd(0x2A); tft_data(ca,4);
    tft_cmd(0x2B); tft_data(ra,4);
    tft_cmd(0x2C); gpio_set_level(TFT_DC,1);
    spi_transaction_t t = {.length=16,.tx_buffer=c};
    spi_device_polling_transmit(s_spi,&t);
}

static void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int sc) {
    int idx = ch_idx(c);
    for (int col = 0; col < 5; col++) {
        uint8_t line = FONT[idx][col];
        for (int row = 0; row < 7; row++) {
            uint16_t color = (line & (1 << row)) ? fg : bg;
            for (int sy = 0; sy < sc; sy++)
                for (int sx = 0; sx < sc; sx++)
                    tft_pixel(x + col*sc + sx, y + row*sc + sy, color);
        }
    }
}

static void tft_print(int x, int y, const char *s, uint16_t fg, uint16_t bg, int sc) {
    for (int i = 0; s[i] && x < 312; i++, x += (5+1)*sc)
        draw_char(x, y, s[i], fg, bg, sc);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== TFT Hello World starting ===");

    tft_init();

    ESP_LOGI(TAG, "fill white");
    tft_fill(0xFFFF);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "fill black");
    tft_fill(0x0000);

    ESP_LOGI(TAG, "drawing text");
    tft_print(40, 60,  "HELLO WORLD",  0x07FF, 0x0000, 3);
    tft_print(60, 110, "T-DISPLAY-S3", 0xFFFF, 0x0000, 2);
    tft_print(80, 145, "ESP32-S3",     0x03DF, 0x0000, 2);

    ESP_LOGI(TAG, "display done — looping");

    int cnt = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "alive %d", cnt++);
    }
}
