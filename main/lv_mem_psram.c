/*
 * LVGL memory in PSRAM.
 *
 * With the C library's malloc, every LVGL object (a few hundred bytes each) went into the
 * ESP32's internal RAM, because ESP-IDF serves small malloc() calls from internal RAM first.
 * The UI has thousands of them, leaving too little for Wi-Fi: it crashed when connecting
 * (abort in lock_init_generic, from wpa_sta_connect). Here LVGL allocates from PSRAM instead,
 * falling back to internal RAM only if PSRAM is full.
 *
 * Selected by CONFIG_LV_USE_CUSTOM_MALLOC=y (sdkconfig.defaults).
 */
#include "lvgl.h"
#include "esp_heap_caps.h"

#define LV_CAPS_FIRST   (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_CAPS_BACKUP  (MALLOC_CAP_DEFAULT)

void lv_mem_init(void)   { }
void lv_mem_deinit(void) { }

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void          lv_mem_remove_pool(lv_mem_pool_t pool)   { (void)pool; }

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc_prefer(size, 2, LV_CAPS_FIRST, LV_CAPS_BACKUP);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc_prefer(p, new_size, 2, LV_CAPS_FIRST, LV_CAPS_BACKUP);
}

void lv_free_core(void *p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    lv_memzero(mon, sizeof(*mon));
    mon->total_size  = info.total_free_bytes + info.total_allocated_bytes;
    mon->free_size   = info.total_free_bytes;
    mon->free_biggest_size = info.largest_free_block;
    mon->used_cnt    = info.allocated_blocks;
    mon->free_cnt    = info.free_blocks;
    mon->max_used    = mon->total_size - info.minimum_free_bytes;
    mon->used_pct    = mon->total_size ? (uint8_t)(100 - (100ULL * info.total_free_bytes) / mon->total_size) : 0;
    mon->frag_pct    = info.total_free_bytes
                     ? (uint8_t)(100 - (100ULL * info.largest_free_block) / info.total_free_bytes) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(MALLOC_CAP_SPIRAM, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}
