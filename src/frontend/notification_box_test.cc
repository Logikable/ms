#include "src/frontend/notification_box.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"

namespace ms {
namespace {

std::string Drawn(const NotificationBox& notification) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(40),
                                               ftxui::Dimension::Fixed(6));
  ftxui::Element element = notification.Render();
  ftxui::Render(screen, element);
  return ScreenText(screen);
}

TEST(NotificationBoxTest, StandsUntilBothTheClockAndAKey) {
  NotificationBox notification;
  notification.Raise({"Trade request from", "Dagger"});
  EXPECT_TRUE(notification.visible());
  EXPECT_NE(Drawn(notification).find("Trade request from"), std::string::npos);
  EXPECT_NE(Drawn(notification).find("Dagger"), std::string::npos);

  // A key while the clock is still running takes nothing down.
  notification.Touch();
  notification.Advance(1.0);
  EXPECT_TRUE(notification.visible());

  notification.Advance(kNotificationSeconds);
  EXPECT_FALSE(notification.visible());
}

TEST(NotificationBoxTest, WaitsOutAPlayerWhoIsAway) {
  NotificationBox notification;
  notification.Raise({"Trade request from", "Dagger"});

  // The clock alone would take it down in front of nobody.
  notification.Advance(kNotificationSeconds * 10);
  EXPECT_TRUE(notification.visible());

  notification.Touch();
  EXPECT_FALSE(notification.visible());
}

TEST(NotificationBoxTest, ANewBoxReplacesTheOneStanding) {
  NotificationBox notification;
  notification.Raise({"Trade request from", "Dagger"});
  notification.Touch();
  notification.Advance(kNotificationSeconds);
  ASSERT_FALSE(notification.visible());

  notification.Raise({"Trade request from", "Wand"});
  EXPECT_TRUE(notification.visible());
  EXPECT_EQ(Drawn(notification).find("Dagger"), std::string::npos);
  EXPECT_NE(Drawn(notification).find("Wand"), std::string::npos);

  notification.Dismiss();
  EXPECT_FALSE(notification.visible());
}

}  // namespace
}  // namespace ms
