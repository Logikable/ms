#include "src/frontend/confirm_prompt.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"

namespace ms {
namespace {

std::string Render(const ConfirmPrompt& prompt) {
  ftxui::Element element = prompt.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen, element);
  return screen.ToString();
}

TEST(ConfirmPromptTest, StartsClosed) {
  ConfirmPrompt prompt;
  EXPECT_FALSE(prompt.open());
}

TEST(ConfirmPromptTest, OpensOnConfirmByDefault) {
  ConfirmPrompt prompt;
  prompt.Open();
  EXPECT_TRUE(prompt.open());
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
  EXPECT_FALSE(prompt.open());
}

// An irreversible action opens on Cancel, so Enter alone cannot fire it.
TEST(ConfirmPromptTest, OpensOnCancelWhenAsked) {
  ConfirmPrompt prompt;
  prompt.Open(/*cancel_selected=*/true);
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
}

TEST(ConfirmPromptTest, ArrowsMoveBetweenTheButtons) {
  ConfirmPrompt prompt;
  prompt.Open();
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::ArrowRight), ConfirmChoice::kPending);
  EXPECT_TRUE(prompt.open());  // moving the cursor does not answer
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);

  prompt.Open(/*cancel_selected=*/true);
  prompt.OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
}

TEST(ConfirmPromptTest, EscapeCancels) {
  ConfirmPrompt prompt;
  prompt.Open();
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
  EXPECT_FALSE(prompt.open());
}

// Everything else is swallowed: a keystroke aimed at the prompt must never
// reach the panel behind it.
TEST(ConfirmPromptTest, SwallowsUnrelatedEventsWhileOpen) {
  ConfirmPrompt prompt;
  prompt.Open();
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::ArrowUp), ConfirmChoice::kPending);
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::Character('x')),
            ConfirmChoice::kPending);
  EXPECT_TRUE(prompt.open());
}

TEST(ConfirmPromptTest, IgnoresEventsWhileClosed) {
  ConfirmPrompt prompt;
  EXPECT_EQ(prompt.OnEvent(ftxui::Event::Return), ConfirmChoice::kPending);
  EXPECT_FALSE(prompt.open());
}

// A dialog whose cursor is elsewhere -- the amount selector's textbox -- draws
// the same row with neither button lit.
TEST(ConfirmPromptTest, ButtonRowCanHighlightNeither) {
  ftxui::Element row = ConfirmButtons(ConfirmFocus::kNone);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(row),
                                               ftxui::Dimension::Fixed(1));
  ftxui::Render(screen, row);
  std::string rendered = screen.ToString();
  EXPECT_NE(rendered.find("[Confirm]"), std::string::npos);
  EXPECT_NE(rendered.find("[Cancel]"), std::string::npos);
  EXPECT_EQ(rendered.find("\033[7m"), std::string::npos);
}

TEST(ConfirmPromptTest, HighlightsTheSelectedButton) {
  ConfirmPrompt prompt;
  prompt.Open();
  std::string rendered = Render(prompt);
  EXPECT_NE(rendered.find("\033[7m[Confirm]"), std::string::npos);

  prompt.Open(/*cancel_selected=*/true);
  rendered = Render(prompt);
  EXPECT_NE(rendered.find("\033[7m[Cancel]"), std::string::npos);
}

}  // namespace
}  // namespace ms
