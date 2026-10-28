#include "src/save_lock.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"

namespace ms {
namespace {

std::string SavePath() {
  return std::string(testing::TempDir()) + "/ms.save";
}

TEST(SaveLockTest, ASecondOpenFindsTheSaveTaken) {
  SaveLock first(SavePath());
  EXPECT_FALSE(first.taken_elsewhere());

  SaveLock second(SavePath());
  EXPECT_TRUE(second.taken_elsewhere());
}

TEST(SaveLockTest, ClosingTheGameFreesTheSave) {
  std::unique_ptr<SaveLock> first = std::make_unique<SaveLock>(SavePath());
  first.reset();

  SaveLock second(SavePath());
  EXPECT_FALSE(second.taken_elsewhere());
}

TEST(SaveLockTest, AnotherSaveIsNotTaken) {
  SaveLock first(SavePath());

  SaveLock other(std::string(testing::TempDir()) + "/other.save");
  EXPECT_FALSE(other.taken_elsewhere());
}

}  // namespace
}  // namespace ms
