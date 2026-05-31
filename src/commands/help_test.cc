#include "src/commands/help.h"

#include <gtest/gtest.h>
#include "src/frontend.h"

namespace ms {
namespace {

TEST(HelpCommandTest, ListsRegisteredCommands) {
  Frontend frontend("> ");
  frontend.Register({"foo", "Does foo.", [](std::vector<std::string>) -> std::string {
    return "";
  }});
  frontend.Register({"bar", "Does bar.", [](std::vector<std::string>) -> std::string {
    return "";
  }});
  std::string out = frontend.HelpText();
  EXPECT_NE(out.find("/foo"), std::string::npos);
  EXPECT_NE(out.find("Does foo."), std::string::npos);
  EXPECT_NE(out.find("/bar"), std::string::npos);
}

TEST(HelpCommandTest, DescriptionsAlignedAtSameColumn) {
  Frontend frontend("> ");
  frontend.Register({"a", "Short.", [](std::vector<std::string>) -> std::string {
    return "";
  }});
  frontend.Register({"longer_name", "Also short.", [](std::vector<std::string>) -> std::string {
    return "";
  }});
  std::string out = frontend.HelpText();
  size_t line1_desc_col = out.find("Short.");
  size_t line2_start = out.find('\n') + 1;
  size_t line2_desc_col = out.find("Also short.", line2_start) - line2_start;
  EXPECT_EQ(line1_desc_col, line2_desc_col);
}

TEST(HelpCommandTest, HelpListsItself) {
  Frontend frontend("> ");
  RegisterHelpCommand(frontend);
  EXPECT_NE(frontend.HelpText().find("/help"), std::string::npos);
}

}  // namespace
}  // namespace ms
