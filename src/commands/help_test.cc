#include "src/commands/help.h"

#include <gtest/gtest.h>

#include "src/frontend.h"

namespace ms {
namespace {

class HelpCommandTest : public testing::Test {
 protected:
  Frontend frontend_{">"};
};

TEST_F(HelpCommandTest, ListsRegisteredCommands) {
  frontend_.Register(
      {"foo", "Does foo.",
       [](std::vector<std::string>) -> std::string { return ""; }});
  frontend_.Register(
      {"bar", "Does bar.",
       [](std::vector<std::string>) -> std::string { return ""; }});
  std::string out = frontend_.HelpText();
  EXPECT_NE(out.find("/foo"), std::string::npos);
  EXPECT_NE(out.find("Does foo."), std::string::npos);
  EXPECT_NE(out.find("/bar"), std::string::npos);
}

TEST_F(HelpCommandTest, DescriptionsAlignedAtSameColumn) {
  frontend_.Register(
      {"a", "Short.",
       [](std::vector<std::string>) -> std::string { return ""; }});
  frontend_.Register(
      {"longer_name", "Also short.",
       [](std::vector<std::string>) -> std::string { return ""; }});
  std::string out = frontend_.HelpText();
  size_t line1_desc_col = out.find("Short.");
  size_t line2_start = out.find('\n') + 1;
  size_t line2_desc_col = out.find("Also short.", line2_start) - line2_start;
  EXPECT_EQ(line1_desc_col, line2_desc_col);
}

TEST_F(HelpCommandTest, HelpListsItself) {
  RegisterHelpCommand(frontend_);
  EXPECT_NE(frontend_.HelpText().find("/help"), std::string::npos);
}

}  // namespace
}  // namespace ms
