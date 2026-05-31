#include "src/frontend.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

TEST(FrontendTest, ProcessDispatchesToRegisteredCommand) {
  Frontend frontend("> ");
  frontend.Register({"hello", "", [](std::vector<std::string>) -> std::string {
    return "Hello!";
  }});
  EXPECT_EQ(frontend.Process("hello"), "Hello!");
}

TEST(FrontendTest, ProcessStripsLeadingSlash) {
  Frontend frontend("> ");
  frontend.Register({"foo", "", [](std::vector<std::string>) -> std::string {
    return "bar";
  }});
  EXPECT_EQ(frontend.Process("/foo"), "bar");
}

TEST(FrontendTest, ProcessPassesArguments) {
  Frontend frontend("> ");
  frontend.Register({"echo", "", [](std::vector<std::string> args) -> std::string {
    return args.size() > 1 ? args[1] : "";
  }});
  EXPECT_EQ(frontend.Process("echo world"), "world");
}

TEST(FrontendTest, ProcessReturnsErrorForUnknownCommand) {
  Frontend frontend("> ");
  EXPECT_NE(frontend.Process("unknown").find("Unknown command"), std::string::npos);
}

TEST(FrontendTest, ProcessReturnsEmptyForEmptyInput) {
  Frontend frontend("> ");
  EXPECT_EQ(frontend.Process(""), "");
}

TEST(FrontendTest, HelpTextListsAllCommands) {
  Frontend frontend("> ");
  frontend.Register({"foo", "Does foo.", [](std::vector<std::string>) -> std::string {
    return "";
  }});
  frontend.Register({"bar", "Does bar.", [](std::vector<std::string>) -> std::string {
    return "";
  }});
  std::string out = frontend.HelpText();
  EXPECT_NE(out.find("/foo"), std::string::npos);
  EXPECT_NE(out.find("/bar"), std::string::npos);
}

}  // namespace
}  // namespace ms
