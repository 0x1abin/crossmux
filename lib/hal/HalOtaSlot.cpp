#include "HalOtaSlot.h"

#include <Logging.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace {
const esp_partition_t* asPartition(const void* partition) { return static_cast<const esp_partition_t*>(partition); }

constexpr HalOtaSlot::RunningImageState classifyImageState(esp_ota_img_states_t state) {
  switch (state) {
    case ESP_OTA_IMG_PENDING_VERIFY:
      return HalOtaSlot::RunningImageState::PendingVerify;
    case ESP_OTA_IMG_VALID:
      return HalOtaSlot::RunningImageState::Confirmed;
    case ESP_OTA_IMG_UNDEFINED:
      return HalOtaSlot::RunningImageState::Untracked;
    case ESP_OTA_IMG_NEW:
    case ESP_OTA_IMG_INVALID:
    case ESP_OTA_IMG_ABORTED:
      return HalOtaSlot::RunningImageState::Unsafe;
  }
  return HalOtaSlot::RunningImageState::Unsafe;
}

static_assert(classifyImageState(ESP_OTA_IMG_PENDING_VERIFY) == HalOtaSlot::RunningImageState::PendingVerify);
static_assert(classifyImageState(ESP_OTA_IMG_VALID) == HalOtaSlot::RunningImageState::Confirmed);
static_assert(classifyImageState(ESP_OTA_IMG_UNDEFINED) == HalOtaSlot::RunningImageState::Untracked);
static_assert(classifyImageState(ESP_OTA_IMG_NEW) == HalOtaSlot::RunningImageState::Unsafe);
static_assert(classifyImageState(ESP_OTA_IMG_INVALID) == HalOtaSlot::RunningImageState::Unsafe);
static_assert(classifyImageState(ESP_OTA_IMG_ABORTED) == HalOtaSlot::RunningImageState::Unsafe);
}  // namespace

#ifdef CONFIG_APP_ROLLBACK_ENABLE
extern "C" bool verifyRollbackLater() { return true; }
#endif

HalOtaSlot HalOtaSlot::inactive() {
  const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!partition || partition == running || partition->type != ESP_PARTITION_TYPE_APP ||
      partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN ||
      partition->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
    return {};
  }
  return HalOtaSlot(partition, partition->size);
}

HalOtaSlot::RunningImageState HalOtaSlot::runningImageState() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running || esp_ota_get_boot_partition() != running) return RunningImageState::Unsafe;

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t result = esp_ota_get_state_partition(running, &state);
  return result == ESP_OK ? classifyImageState(state) : RunningImageState::Unsafe;
}

bool HalOtaSlot::confirmRunningImage() {
  switch (runningImageState()) {
    case RunningImageState::Confirmed:
    case RunningImageState::Untracked:
      return true;
    case RunningImageState::PendingVerify:
      if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
        LOG_ERR("OTA", "Failed to confirm running OTA image");
        return false;
      }
      return runningImageState() == RunningImageState::Confirmed;
    case RunningImageState::Unsafe:
      return false;
  }
  return false;
}

bool HalOtaSlot::safeForScratchWrite() const {
  if (!valid()) return false;

  switch (runningImageState()) {
    case RunningImageState::Confirmed:
    case RunningImageState::Untracked:
      return true;
    case RunningImageState::PendingVerify:
    case RunningImageState::Unsafe:
      return false;
  }
  return false;
}

bool HalOtaSlot::read(size_t offset, void* data, size_t length) const {
  return contains(offset, length) && esp_partition_read(asPartition(partition_), offset, data, length) == ESP_OK;
}

bool HalOtaSlot::erase(size_t offset, size_t length) const {
  return contains(offset, length) && offset % ERASE_SIZE == 0 && length % ERASE_SIZE == 0 &&
         esp_partition_erase_range(asPartition(partition_), offset, length) == ESP_OK;
}

bool HalOtaSlot::write(size_t offset, const void* data, size_t length) const {
  return contains(offset, length) && esp_partition_write(asPartition(partition_), offset, data, length) == ESP_OK;
}

bool HalOtaSlot::contains(size_t offset, size_t length) const {
  return valid() && offset <= size_ && length <= size_ - offset;
}
