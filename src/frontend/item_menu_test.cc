#include "src/frontend/item_menu.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

class ItemMenuTest : public testing::Test {
 protected:
  ItemMenu menu_{{"One", "Two", "Three"}};
};

TEST_F(ItemMenuTest, DefaultSelectionIsZero) {
  EXPECT_EQ(menu_.selected(), 0);
}

TEST_F(ItemMenuTest, DownMovesSelection) {
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 1);
}

TEST_F(ItemMenuTest, UpClampsAtFirst) {
  menu_.Up();
  EXPECT_EQ(menu_.selected(), 0);
}

TEST_F(ItemMenuTest, DownClampsAtLast) {
  menu_.Down();
  menu_.Down();
  menu_.Down();
  EXPECT_EQ(menu_.selected(), 2);
}

TEST_F(ItemMenuTest, ResetReturnsToZero) {
  menu_.Down();
  menu_.Down();
  menu_.Reset();
  EXPECT_EQ(menu_.selected(), 0);
}

}  // namespace
}  // namespace ms
