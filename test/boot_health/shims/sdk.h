#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NOT_FOUND=-2, NVS_READWRITE=1, NVS_READONLY=0;
struct esp_partition_t {
  uint32_t address; const char* label;
  uint32_t type=0,subtype=0,size=0;
  bool encrypted=false;
};
constexpr uint32_t ESP_PARTITION_TYPE_APP=0,ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_OTA=0;
constexpr uint32_t ESP_PARTITION_SUBTYPE_APP_OTA_0=16,ESP_PARTITION_SUBTYPE_APP_OTA_1=17;
const esp_partition_t* esp_partition_find_first(uint32_t,uint32_t,const char*);
int esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
int esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);
int esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
struct esp_app_desc_t { uint8_t app_elf_sha256[32]; };
enum esp_ota_img_states_t { ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED= -1 };
enum esp_reset_reason_t { ESP_RST_POWERON, ESP_RST_SW, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_CPU_LOCKUP };
using nvs_handle_t=int;
unsigned long millis();
const char* esp_err_to_name(int);
esp_reset_reason_t esp_reset_reason();
const esp_partition_t* esp_ota_get_running_partition();
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*);
const esp_partition_t* esp_ota_get_boot_partition();
int esp_ota_get_app_partition_count();
int esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t*);
int esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t*);
bool esp_ota_check_rollback_is_possible();
int esp_ota_mark_app_invalid_rollback();
int esp_ota_mark_app_valid_cancel_rollback();
void esp_restart();
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_get_blob(int,const char*,void*,size_t*);
int nvs_set_blob(int,const char*,const void*,size_t);
int nvs_commit(int);
void nvs_close(int);
