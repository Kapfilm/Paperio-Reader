#include <gtest/gtest.h>
#include "sdk.h"
#include "src/network/BootHealth.cpp"

namespace fake {
uint32_t now=0; esp_reset_reason_t reason=ESP_RST_POWERON;
esp_partition_t parts[2]={{0x10000,"app0"},{0x650000,"app1"}};
const esp_partition_t* running=&parts[0]; const esp_partition_t* boot=running;
esp_ota_img_states_t state=ESP_OTA_IMG_PENDING_VERIFY;
boot_health::Record stored; bool have=false,writeFail=false,verify=true,description=true;
uint8_t otherHash=2;
bool emptyMetadata=false, commitFail=false;
int failOnWrite=0;
boot_health::Record pending;
int confirmations=0,rollbacks=0,rearms=0,writes=0;
struct Restart {};
void runtimeReset() {
  boot_health::record={}; boot_health::window={}; boot_health::uiRendered=false;
  boot_health::storageReady=false;boot_health::confirmed=false;boot_health::recovery=false;
  boot_health::started=false;boot_health::updateStaged=false;boot_health::running=nullptr;
  now=0;
}
void fresh() {
  runtimeReset();stored={};have=false;writeFail=false;verify=true;description=true;
  running=&parts[0];boot=running;state=ESP_OTA_IMG_PENDING_VERIFY;reason=ESP_RST_POWERON;
  confirmations=rollbacks=rearms=writes=0;otherHash=2;emptyMetadata=false;commitFail=false;failOnWrite=0;pending={};
}
void healthy() {
  boot_health::rendered();
  for(now=0;now<=60000;now+=1000) boot_health::tick(false);
}
}
unsigned long millis(){return fake::now;}
const char* esp_err_to_name(int){return "failure";}
esp_reset_reason_t esp_reset_reason(){return fake::reason;}
const esp_partition_t* esp_ota_get_running_partition(){return fake::running;}
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*){return &fake::parts[1];}
const esp_partition_t* esp_ota_get_boot_partition(){return fake::boot;}
int esp_ota_get_app_partition_count(){return 2;}
int esp_ota_get_partition_description(const esp_partition_t* p,esp_app_desc_t* d){
  if(!p||!fake::description)return ESP_FAIL;
  memset(d,0,sizeof(*d));d->app_elf_sha256[0]=p==fake::running?1:fake::otherHash;return ESP_OK;
}
int esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t* s){if(fake::emptyMetadata)return ESP_ERR_NOT_FOUND;*s=fake::state;return ESP_OK;}
bool esp_ota_check_rollback_is_possible(){return fake::verify;}
int esp_ota_mark_app_invalid_rollback(){++fake::rollbacks;fake::state=ESP_OTA_IMG_INVALID;fake::boot=&fake::parts[1];return ESP_OK;}
int esp_ota_mark_app_valid_cancel_rollback(){++fake::confirmations;fake::state=ESP_OTA_IMG_VALID;return ESP_OK;}
void esp_restart(){__wrap_esp_restart();}
int nvs_open(const char*,int mode,nvs_handle_t* h){*h=1;return mode==NVS_READONLY&&!fake::have?ESP_FAIL:ESP_OK;}
int nvs_get_blob(int,const char*,void* out,size_t* size){if(!fake::have)return ESP_FAIL;memcpy(out,&fake::stored,sizeof(fake::stored));*size=sizeof(fake::stored);return ESP_OK;}
int nvs_set_blob(int,const char*,const void* in,size_t){++fake::writes;if(fake::writeFail||fake::writes==fake::failOnWrite)return ESP_FAIL;memcpy(&fake::pending,in,sizeof(fake::pending));return ESP_OK;}
int nvs_commit(int){if(fake::commitFail)return ESP_FAIL;fake::stored=fake::pending;fake::have=true;return ESP_OK;}
void nvs_close(int){}
namespace ota_boot {bool rollbackCandidateMatches(const esp_partition_t*,const esp_partition_t*){return true;}
bool rearmPending(const esp_partition_t*){++fake::rearms;fake::state=ESP_OTA_IMG_NEW;return true;}}
extern "C" void __real_esp_restart(){throw fake::Restart{};}
extern "C" void __real_esp_deep_sleep_start(){throw fake::Restart{};}
class BootIntegration:public ::testing::Test{void SetUp() override{fake::fresh();}};
TEST_F(BootIntegration, ConfirmsOnlyAfterRealRenderAndHealthyMinute){
  boot_health::begin();fake::now=120000;boot_health::tick(false);EXPECT_EQ(fake::confirmations,0);
  fake::healthy();EXPECT_EQ(fake::confirmations,1);EXPECT_EQ(fake::stored.trusted,1u);
  EXPECT_EQ(fake::stored.failures,0u);
}
TEST_F(BootIntegration, PlannedSleepRearmsWithoutConfirmingAndDoesNotUndoUpdateSelection){
  boot_health::begin();EXPECT_THROW(__wrap_esp_deep_sleep_start(),fake::Restart);
  EXPECT_EQ(fake::rearms,1);EXPECT_EQ(fake::confirmations,0);
  fake::state=ESP_OTA_IMG_PENDING_VERIFY;fake::boot=&fake::parts[1];
  EXPECT_THROW(__wrap_esp_restart(),fake::Restart);EXPECT_EQ(fake::rearms,1);
}
TEST_F(BootIntegration, FailedNvsDoesNotArmUpdateOrConfirmImage){
  fake::writeFail=true;boot_health::begin();fake::healthy();EXPECT_EQ(fake::confirmations,0);
  EXPECT_FALSE(boot_health::stageUpdate(&fake::parts[1]));
}
TEST_F(BootIntegration, UpdateStoresExactKnownHealthyPreviousImage){
  boot_health::begin();fake::healthy();ASSERT_TRUE(boot_health::stageUpdate(&fake::parts[1]));
  EXPECT_EQ(fake::stored.current.address,0x650000u);EXPECT_EQ(fake::stored.current.sha[0],2);
  EXPECT_EQ(fake::stored.fallback.address,0x10000u);EXPECT_EQ(fake::stored.fallback.sha[0],1);
  EXPECT_EQ(fake::stored.armed,1u);const auto expected=fake::stored;
  fake::now+=90000;boot_health::tick(false);
  EXPECT_EQ(memcmp(&expected,&fake::stored,sizeof(expected)),0);
}
static void atThirdCrash(bool armed=true) {
  boot_health::begin();fake::stored.failures=2;fake::stored.trusted=1;fake::stored.armed=armed;
  fake::stored.fallback.address=0x650000;fake::stored.fallback.sha[0]=2;
  fake::runtimeReset();fake::reason=ESP_RST_PANIC;
}
TEST_F(BootIntegration, ThirdCrashRollsBackOnceAndLatchPreventsPingPong){
  atThirdCrash();EXPECT_THROW(boot_health::begin(),fake::Restart);
  EXPECT_EQ(fake::rollbacks,1);EXPECT_EQ(fake::stored.blocked,1u);EXPECT_EQ(fake::rearms,0);
  fake::runtimeReset();boot_health::begin();EXPECT_EQ(fake::rollbacks,1);EXPECT_TRUE(boot_health::recoveryRequired());
}
TEST_F(BootIntegration, UnknownPreviousImageUsesRecoveryNotUsbPartition){
  atThirdCrash(false);boot_health::begin();EXPECT_EQ(fake::rollbacks,0);EXPECT_TRUE(boot_health::recoveryRequired());
}
TEST_F(BootIntegration, ReplacedOrCorruptFallbackNeverBoots){
  atThirdCrash();fake::otherHash=9;boot_health::begin();EXPECT_EQ(fake::rollbacks,0);EXPECT_TRUE(boot_health::recoveryRequired());
  fake::runtimeReset();fake::otherHash=2;fake::verify=false;boot_health::begin();EXPECT_EQ(fake::rollbacks,0);
}
TEST_F(BootIntegration, FailingLatchWriteCannotStartRollback){
  atThirdCrash();fake::writeFail=true;boot_health::begin();EXPECT_EQ(fake::rollbacks,0);EXPECT_TRUE(boot_health::recoveryRequired());
}
TEST_F(BootIntegration, BrownoutSleepAndSoftwareResetDoNotIncrementCrashCount){
  boot_health::begin();fake::stored.failures=2;
  for(auto reason:{ESP_RST_BROWNOUT,ESP_RST_DEEPSLEEP,ESP_RST_SW,ESP_RST_POWERON}){
    fake::runtimeReset();fake::reason=reason;boot_health::begin();EXPECT_EQ(fake::stored.failures,2u);
  }
}

TEST_F(BootIntegration, DirectFlashWithEmptyOtadataCanBecomeHealthyUpdateSource){
  fake::emptyMetadata=true;boot_health::begin();fake::healthy();
  EXPECT_EQ(fake::confirmations,0);EXPECT_EQ(fake::stored.trusted,1u);
  ASSERT_TRUE(boot_health::stageUpdate(&fake::parts[1]));EXPECT_EQ(fake::stored.armed,1u);
}

TEST_F(BootIntegration, RollbackRequiresDurableLatchEvenWhenBootRecordSaved){
  atThirdCrash();fake::failOnWrite=fake::writes+2;
  boot_health::begin();EXPECT_EQ(fake::rollbacks,0);EXPECT_TRUE(boot_health::recoveryRequired());
  EXPECT_EQ(fake::stored.blocked,0u);
}
TEST_F(BootIntegration, FailedNvsCommitCannotSelectNextFirmware){
  boot_health::begin();fake::healthy();const auto before=fake::stored;
  fake::commitFail=true;EXPECT_FALSE(boot_health::stageUpdate(&fake::parts[1]));
  EXPECT_EQ(memcmp(&before,&fake::stored,sizeof(before)),0);
}
