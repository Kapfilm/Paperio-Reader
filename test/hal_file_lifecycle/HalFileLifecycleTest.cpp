#include <HalStorage.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

class HalFileLifecycleTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(Storage.begin());
    storage_test::unlockedIo = 0;
    storage_test::closes = 0;
    storage_test::closeFails = false;
    storage_test::failNextSpiTake = false;
    storage_test::failedTakes = 0;
    storage_test::spiTakeCalls = 0;
    std::lock_guard guard(storage_test::gateMutex);
    storage_test::blockRead = storage_test::readEntered = storage_test::releaseRead = false;
  }
  void TearDown() override { EXPECT_EQ(storage_test::unlockedIo.load(), 0u); }
};
TEST_F(HalFileLifecycleTest, DestructorClosesOpenFileUnderBothLocks) {
  {
    auto file = Storage.open("book");
    ASSERT_TRUE(file);
  }
  EXPECT_EQ(storage_test::closes.load(), 1u);
}
TEST_F(HalFileLifecycleTest, MoveAssignmentClosesPreviousFileUnderBothLocks) {
  auto first = Storage.open("first");
  auto second = Storage.open("second");
  first = std::move(second);
  EXPECT_EQ(storage_test::closes.load(), 1u);
  EXPECT_FALSE(second);
  EXPECT_TRUE(first);
  first = std::move(first);
  EXPECT_EQ(storage_test::closes.load(), 1u);
}
TEST_F(HalFileLifecycleTest, ReopenAndDestructorCloseFailureRemainSerializedWithoutDeadlock) {
  auto file = Storage.open("original");
  EXPECT_NO_THROW({ ASSERT_TRUE(Storage.openFileForRead("TEST", "new", file)); });
  EXPECT_NO_THROW({ ASSERT_TRUE(Storage.openFileForWrite("TEST", "output", file)); });
  EXPECT_EQ(storage_test::closes.load(), 2u);
  storage_test::closeFails = true;
  file = HalFile();
  EXPECT_EQ(storage_test::closes.load(), 3u);
}
TEST_F(HalFileLifecycleTest, StorageStreamMayWriteToAnotherFileFromItsPrintCallback) {
  struct Writer : Print {
    HalFile file = Storage.open("cache");
    size_t write(uint8_t byte) override { return file.write(byte); }
  } writer;
  EXPECT_NO_THROW({ EXPECT_TRUE(Storage.readFileToStream("chapter", writer)); });
}
TEST_F(HalFileLifecycleTest, DestructorOnAnotherTaskWaitsUntilActiveSdReadCompletes) {
  auto reader = Storage.open("reader");
  auto closing = Storage.open("closing");
  storage_test::blockRead = true;
  std::thread readThread([&] {
    char data;
    reader.read(&data, 1);
  });
  {
    std::unique_lock gate(storage_test::gateMutex);
    ASSERT_TRUE(storage_test::gateCv.wait_for(gate, std::chrono::seconds(2), [] { return storage_test::readEntered; }));
  }
  std::promise<void> attempted, done;
  auto attempt = attempted.get_future();
  auto completion = done.get_future();
  std::thread closeThread([&, file = std::move(closing)]() mutable {
    attempted.set_value();
    file = HalFile();
    done.set_value();
  });
  attempt.wait();
  EXPECT_EQ(completion.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  {
    std::lock_guard gate(storage_test::gateMutex);
    storage_test::releaseRead = true;
    storage_test::gateCv.notify_all();
  }
  readThread.join();
  closeThread.join();
  EXPECT_EQ(completion.wait_for(std::chrono::seconds(1)), std::future_status::ready);
}
TEST_F(HalFileLifecycleTest, SpiTimeoutCannotLetStorageProceedWithoutTheBusLock) {
  storage_test::failNextSpiTake = true;
  {
    auto file = Storage.open("retry");
    ASSERT_TRUE(file);
  }
  EXPECT_EQ(storage_test::failedTakes.load(), 1u);
  EXPECT_GE(storage_test::spiTakeCalls.load(), 2u);
}
