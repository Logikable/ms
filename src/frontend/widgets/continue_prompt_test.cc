#include "src/frontend/widgets/continue_prompt.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

std::string Render(const ContinuePrompt& prompt) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(20),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen, prompt.Render());
  return screen.ToString();
}

TEST(ContinuePromptTest, EitherKeyDismissesIt) {
  ContinuePrompt prompt;
  EXPECT_FALSE(prompt.open());
  EXPECT_FALSE(prompt.OnEvent(ftxui::Event::Return)) << "closed, so no answer";
  prompt.Open();
  EXPECT_TRUE(prompt.open());
  EXPECT_FALSE(prompt.OnEvent(ftxui::Event::ArrowLeft));
  EXPECT_TRUE(prompt.open()) << "and everything else is swallowed";
  EXPECT_TRUE(prompt.OnEvent(ftxui::Event::Return));
  EXPECT_FALSE(prompt.open());

  prompt.Open();
  EXPECT_TRUE(prompt.OnEvent(ftxui::Event::Escape));
  EXPECT_FALSE(prompt.open());
}

TEST(ContinuePromptTest, TheButtonIsTheOnlyThingToPress) {
  ContinuePrompt prompt;
  EXPECT_NE(Render(prompt).find("Continue"), std::string::npos);
}

}  // namespace
}  // namespace ms
