#include "src/frontend.h"

#include <gtest/gtest.h>

namespace ms {
namespace {

class FrontendTest : public testing::Test {
 protected:
  Frontend frontend_{">"};
};

TEST_F(FrontendTest, ProcessDispatchesToRegisteredCommand) {
  frontend_.Register({"hello", "", [](std::vector<std::string>) -> std::string {
                        return "Hello!";
                      }});
  EXPECT_EQ(frontend_.Process("hello"), "Hello!");
}

TEST_F(FrontendTest, ProcessStripsLeadingSlash) {
  frontend_.Register({"foo", "", [](std::vector<std::string>) -> std::string {
                        return "bar";
                      }});
  EXPECT_EQ(frontend_.Process("/foo"), "bar");
}

TEST_F(FrontendTest, ProcessPassesArguments) {
  frontend_.Register(
      {"echo", "", [](std::vector<std::string> args) -> std::string {
         return args.size() > 1 ? args[1] : "";
       }});
  EXPECT_EQ(frontend_.Process("echo world"), "world");
}

TEST_F(FrontendTest, ProcessReturnsErrorForUnknownCommand) {
  EXPECT_NE(frontend_.Process("unknown").find("Unknown command"),
            std::string::npos);
}

TEST_F(FrontendTest, ProcessReturnsEmptyForEmptyInput) {
  EXPECT_EQ(frontend_.Process(""), "");
}

TEST_F(FrontendTest, HelpTextListsAllCommands) {
  frontend_.Register(
      {"foo", "Does foo.",
       [](std::vector<std::string>) -> std::string { return ""; }});
  frontend_.Register(
      {"bar", "Does bar.",
       [](std::vector<std::string>) -> std::string { return ""; }});
  std::string out = frontend_.HelpText();
  EXPECT_NE(out.find("/foo"), std::string::npos);
  EXPECT_NE(out.find("/bar"), std::string::npos);
}

}  // namespace
}  // namespace ms
