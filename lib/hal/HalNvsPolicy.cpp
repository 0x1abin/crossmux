#if defined(FREEINK_DEVICE_METALIO_EINK4_PLUS) && FREEINK_DEVICE_METALIO_EINK4_PLUS

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

extern "C" esp_err_t __real_nvs_flash_init();

extern "C" esp_err_t __wrap_nvs_flash_init() {
  const esp_err_t result = __real_nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // Arduino init formats NVS on these two errors. Preserve the existing
    // ThingsOS store and require an explicit migration/recovery instead.
    ESP_LOGE("NVS", "Automatic NVS erase blocked on Metalio Plus (init=0x%x)", static_cast<unsigned>(result));
    return ESP_ERR_INVALID_STATE;
  }
  return result;
}

#endif
