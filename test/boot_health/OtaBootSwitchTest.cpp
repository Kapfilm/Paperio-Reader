#include <gtest/gtest.h>
#include <array>
#include "sdk.h"
#include "src/network/OtaBootSwitch.cpp"
namespace {
std::array<uint8_t,8192> flash;
esp_partition_t data{0xe000,"ota",1,0,8192,false};
esp_partition_t app{0x10000,"app0",0,16,0x640000,false};
int writes=0,erases=0;bool failWrite=false;
const esp_partition_t* running = &app;
}
uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t* buf,uint32_t len){
  for(uint32_t i=0;i<len;++i){crc^=buf[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320:0);}
  return crc;
}
const esp_partition_t* esp_partition_find_first(uint32_t,uint32_t,const char*){return &data;}
int esp_partition_read(const esp_partition_t*,size_t off,void* out,size_t n){if(off+n>flash.size())return ESP_FAIL;memcpy(out,flash.data()+off,n);return ESP_OK;}
int esp_partition_write(const esp_partition_t*,size_t off,const void* in,size_t n){
  ++writes;if(failWrite||off+n>flash.size())return ESP_FAIL;
  auto* b=static_cast<const uint8_t*>(in);
  for(size_t i=0;i<n;++i)if((flash[off+i]&b[i])!=b[i])return ESP_FAIL;
  for(size_t i=0;i<n;++i)flash[off+i]&=b[i];return ESP_OK;
}
int esp_partition_erase_range(const esp_partition_t*,size_t off,size_t n){++erases;if(off+n>flash.size())return ESP_FAIL;memset(flash.data()+off,255,n);return ESP_OK;}
int esp_ota_get_app_partition_count(){return 2;}
const esp_partition_t* esp_ota_get_running_partition(){return running;}
class OtaMetadata:public ::testing::Test {
 protected:
  void SetUp() override{flash.fill(255);writes=erases=0;failWrite=false;data.encrypted=false;running=&app;}
  void entry(int slot,uint32_t seq,uint32_t state){ota_boot::SelectEntry e{};e.ota_seq=seq;e.ota_state=state;e.crc=ota_boot::computeSeqCrc(seq);memcpy(flash.data()+slot*4096,&e,sizeof(e));}
};
TEST_F(OtaMetadata, PlannedResetOnlyClearsPendingBitWithoutErasingEitherSlot){
  entry(0,3,ESP_OTA_IMG_PENDING_VERIFY);entry(1,2,ESP_OTA_IMG_VALID);
  auto expected=flash;uint32_t state=ESP_OTA_IMG_NEW;memcpy(expected.data()+offsetof(ota_boot::SelectEntry,ota_state),&state,4);
  EXPECT_TRUE(ota_boot::rearmPending(&app));EXPECT_EQ(flash,expected);EXPECT_EQ(erases,0);EXPECT_EQ(writes,1);
}
TEST_F(OtaMetadata, ConfirmedOrInvalidImageCannotBeRearmed){
  for(uint32_t state:{ESP_OTA_IMG_VALID,ESP_OTA_IMG_INVALID,ESP_OTA_IMG_ABORTED}){
    entry(0,3,state);EXPECT_FALSE(ota_boot::rearmPending(&app));
  }
  EXPECT_EQ(writes,0);EXPECT_EQ(erases,0);
}
TEST_F(OtaMetadata, BadCrcAndEncryptedMetadataFailClosed){
  entry(0,3,ESP_OTA_IMG_PENDING_VERIFY);flash[0]^=8;EXPECT_FALSE(ota_boot::rearmPending(&app));
  entry(0,3,ESP_OTA_IMG_PENDING_VERIFY);data.encrypted=true;EXPECT_FALSE(ota_boot::rearmPending(&app));EXPECT_EQ(writes,0);
}
TEST_F(OtaMetadata, FailedWritePreservesPreviousAndPendingImages){
  entry(0,3,ESP_OTA_IMG_PENDING_VERIFY);entry(1,2,ESP_OTA_IMG_VALID);const auto before=flash;
  failWrite=true;EXPECT_FALSE(ota_boot::rearmPending(&app));EXPECT_EQ(flash,before);EXPECT_EQ(erases,0);
}
TEST_F(OtaMetadata, SdUpdateSelectsNewImageWithoutDestroyingCurrentEntry){
  entry(0,3,ESP_OTA_IMG_VALID);auto other=app;other.subtype=17;other.address=0x650000;
  std::array<uint8_t,4096> before;memcpy(before.data(),flash.data(),4096);
  EXPECT_TRUE(ota_boot::switchTo(&other));EXPECT_EQ(memcmp(before.data(),flash.data(),4096),0);
  ota_boot::SelectEntry e;memcpy(&e,flash.data()+4096,sizeof(e));EXPECT_EQ(e.ota_state,ESP_OTA_IMG_NEW);EXPECT_EQ(e.ota_seq,4u);
}

TEST_F(OtaMetadata, RollbackRejectsDuplicateCurrentEntriesAndInvalidFallback){
  auto other=app;other.subtype=17;other.address=0x650000;
  entry(0,3,ESP_OTA_IMG_VALID);entry(1,2,ESP_OTA_IMG_VALID);
  EXPECT_TRUE(ota_boot::rollbackCandidateMatches(&app,&other));
  entry(1,1,ESP_OTA_IMG_VALID);EXPECT_FALSE(ota_boot::rollbackCandidateMatches(&app,&other));
  entry(1,2,ESP_OTA_IMG_INVALID);EXPECT_FALSE(ota_boot::rollbackCandidateMatches(&app,&other));
  entry(1,4,ESP_OTA_IMG_VALID);EXPECT_FALSE(ota_boot::rollbackCandidateMatches(&app,&other));
  EXPECT_EQ(writes,0);EXPECT_EQ(erases,0);
}

TEST_F(OtaMetadata, ManualSwitchUsesValidStateAndPreservesCurrentSlot) {
  entry(0,3,ESP_OTA_IMG_VALID); entry(1,2,ESP_OTA_IMG_VALID);
  auto other=app; other.subtype=17; other.address=0x650000;
  auto before=flash;
  ASSERT_TRUE(ota_boot::switchToInstalled(&other));
  EXPECT_EQ(memcmp(before.data(),flash.data(),4096),0);
  ota_boot::SelectEntry e;memcpy(&e,flash.data()+4096,sizeof(e));
  EXPECT_EQ(e.ota_state,ESP_OTA_IMG_VALID); EXPECT_EQ(e.ota_seq,4u);
  EXPECT_EQ(e.crc,ota_boot::computeSeqCrc(e.ota_seq));
}
TEST_F(OtaMetadata, ManualSwitchRejectsCurrentMissingOrNonOtaImage) {
  entry(0,3,ESP_OTA_IMG_VALID);
  EXPECT_FALSE(ota_boot::switchToInstalled(nullptr));
  EXPECT_FALSE(ota_boot::switchToInstalled(&app));
  auto other=app;other.address=0x650000;other.subtype=18;
  EXPECT_FALSE(ota_boot::switchToInstalled(&other));
  EXPECT_EQ(writes,0);EXPECT_EQ(erases,0);
}
TEST_F(OtaMetadata, SequenceExhaustionCannotEraseCurrentOrWrap) {
  entry(0,UINT32_MAX-2,ESP_OTA_IMG_VALID);
  auto other=app;other.address=0x650000;other.subtype=17;
  EXPECT_FALSE(ota_boot::switchToInstalled(&other));
  EXPECT_EQ(writes,0);EXPECT_EQ(erases,0);
}

TEST_F(OtaMetadata, ManualSwitchWorksFromSlotOneBackToSlotZero) {
  auto other=app; other.subtype=17; other.address=0x650000;
  running=&other;
  entry(0,3,ESP_OTA_IMG_VALID); entry(1,4,ESP_OTA_IMG_VALID);
  const auto before=flash;
  ASSERT_TRUE(ota_boot::switchToInstalled(&app));
  EXPECT_EQ(memcmp(before.data()+4096,flash.data()+4096,4096),0);
  ota_boot::SelectEntry e;memcpy(&e,flash.data(),sizeof(e));
  EXPECT_EQ(e.ota_seq,5u); EXPECT_EQ(e.ota_state,ESP_OTA_IMG_VALID);
  EXPECT_EQ(e.crc,ota_boot::computeSeqCrc(e.ota_seq));
}
TEST_F(OtaMetadata, ManualSwitchWriteFailurePreservesCurrentBootRecord) {
  entry(0,3,ESP_OTA_IMG_VALID); entry(1,2,ESP_OTA_IMG_VALID);
  auto other=app; other.subtype=17; other.address=0x650000;
  const auto before=flash; failWrite=true;
  EXPECT_FALSE(ota_boot::switchToInstalled(&other));
  EXPECT_EQ(memcmp(before.data(),flash.data(),4096),0);
}
