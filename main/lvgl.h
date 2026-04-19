#ifndef LVGL_H
#define LVGL_H

#include <stdint.h>

// 1. 定義 LVGL v9 必備的常量
#define LV_IMAGE_HEADER_MAGIC    0x19
#define LV_COLOR_FORMAT_RGB565   0x12  // v9 的 RGB565 代碼

// 2. 定義 .c 檔裡那些長長的屬性宏 (讓它們變空白即可)
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_IMAGE_PUREFLOW

// 3. 定義 LVGL v9 的 64-bit 檔頭結構
typedef struct {
    uint32_t magic : 8;
    uint32_t cf : 8;      // Color Format
    uint32_t flags : 5;
    uint32_t w : 11;
    uint32_t h : 11;
    uint32_t res : 21;
} lv_image_header_t;

// 4. 定義圖片描述結構體
typedef struct {
    lv_image_header_t header;
    uint32_t data_size;
    const uint8_t * data;
} lv_image_dsc_t;

#endif