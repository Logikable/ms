#include "src/frontend/screens/achievements_panel.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/achievement.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/widgets/colors.h"
#include "src/protos/achievement.pb.h"

namespace ms {
namespace {

google::protobuf::RepeatedPtrField<EarnedAchievement> Earned(
    std::vector<std::pair<Achievement, int64_t>> list) {
  google::protobuf::RepeatedPtrField<EarnedAchievement> earned;
  for (const auto& [achievement, when] : list) {
    EarnedAchievement* one = earned.Add();
    one->set_achievement(achievement);
    one->set_earned_unix_seconds(when);
  }
  return earned;
}

const std::map<Achievement, int64_t> kTargets = {
    {ACHIEVEMENT_STARS_17, 17},
    {ACHIEVEMENT_STARS_22, 22},
    {ACHIEVEMENT_STARS_25, 25},
    {ACHIEVEMENT_MESO_10B, 10'000'000'000},
    {ACHIEVEMENT_MESO_1T, 1'000'000'000'000},
};

TEST(AchievementEntriesTest, NewestFirstThenTheRestInTheCatalogsOrder) {
  std::vector<AchievementEntry> entries =
      AchievementEntries(Earned({{ACHIEVEMENT_LEVEL_300, 100},
                                 {ACHIEVEMENT_STARS_22, 300},
                                 {ACHIEVEMENT_STARS_17, 100}}),
                         {}, kTargets);
  ASSERT_EQ(entries.size(), AllAchievements().size());
  EXPECT_EQ(entries[0].achievement, ACHIEVEMENT_STARS_22);
  EXPECT_EQ(entries[1].achievement, ACHIEVEMENT_STARS_17)
      << "a tie keeps the catalog's order";
  EXPECT_EQ(entries[2].achievement, ACHIEVEMENT_LEVEL_300);
  EXPECT_EQ(entries[3].achievement, ACHIEVEMENT_STARS_25);
  EXPECT_EQ(entries[3].earned_unix_seconds, 0);
  EXPECT_EQ(entries[4].achievement, ACHIEVEMENT_TRIPLE_PRIME);
}

TEST(AchievementEntriesTest, OnlyACounterCarriesAValue) {
  std::vector<AchievementEntry> entries =
      AchievementEntries(Earned({}), {{ACHIEVEMENT_STARS_25, 19}}, kTargets);
  for (const AchievementEntry& entry : entries) {
    if (entry.achievement == ACHIEVEMENT_STARS_25) {
      EXPECT_EQ(entry.current, 19);
      EXPECT_EQ(entry.target, 25);
    }
    if (entry.achievement == ACHIEVEMENT_MESO_10B) {
      EXPECT_FALSE(entry.current.has_value()) << "not sent";
      EXPECT_EQ(entry.target, 10'000'000'000);
    }
    if (!HasCounter(entry.achievement)) {
      EXPECT_FALSE(entry.current.has_value());
      EXPECT_EQ(entry.target, 0);
    }
  }
}

class AchievementsPanelTest : public PanelTest {
 protected:
  void SetUp() override {
    panel_.Reset(" Achievements ");
  }

  void Show(google::protobuf::RepeatedPtrField<EarnedAchievement> earned,
            std::map<Achievement, int64_t> progress) {
    panel_.SetEntries(AchievementEntries(earned, progress, kTargets));
  }

  // Moves the cursor onto `achievement`.
  void Select(Achievement achievement) {
    for (size_t i = 0; i < AllAchievements().size(); ++i) {
      if (panel_.selected()->achievement == achievement) {
        return;
      }
      panel_.OnEvent(ftxui::Event::ArrowDown);
    }
    FAIL() << "not listed";
  }

  AchievementsPanel panel_;
};

TEST_F(AchievementsPanelTest, TheListShowsEveryNameAndTheEarnedDate) {
  const int64_t when = 1'800'000'000;
  Show(Earned({{ACHIEVEMENT_STARS_17, when}}), {});
  const std::string list = RenderElement(panel_.RenderList());
  EXPECT_NE(list.find("Starstruck"), std::string::npos) << list;
  EXPECT_NE(list.find(LocalDate(when)), std::string::npos) << list;
  EXPECT_NE(list.find("Supernova"), std::string::npos) << list;
}

TEST_F(AchievementsPanelTest, TheCardOfAnUnearnedCounter) {
  Show(Earned({}), {{ACHIEVEMENT_MESO_1T, 135'645'123'135}});
  Select(ACHIEVEMENT_MESO_1T);
  const std::string card = RenderElement(panel_.RenderCard());
  EXPECT_NE(card.find("Trillionaire"), std::string::npos) << card;
  EXPECT_NE(card.find("135,645,123,135 / 1,000,000,000,000 meso"),
            std::string::npos)
      << card;
}

TEST_F(AchievementsPanelTest, AnEarnedCounterShowsItsTargetReached) {
  const int64_t when = 1'800'000'000;
  Show(Earned({{ACHIEVEMENT_STARS_22, when}}), {{ACHIEVEMENT_STARS_22, 15}});
  Select(ACHIEVEMENT_STARS_22);
  const std::string card = RenderElement(panel_.RenderCard());
  EXPECT_NE(card.find(LocalDateTime(when)), std::string::npos) << card;
  EXPECT_NE(card.find("22 / 22 stars"), std::string::npos)
      << "the stars may be lost since, not the achievement: " << card;
}

// Whether any cell of `element` is drawn in `color`, filled or written.
bool Paints(ftxui::Element element, ftxui::Color color) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(20));
  ftxui::Render(screen, element);
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      const ftxui::Pixel& pixel = screen.PixelAt(x, y);
      if (pixel.background_color == color || pixel.foreground_color == color) {
        return true;
      }
    }
  }
  return false;
}

TEST_F(AchievementsPanelTest, OnlyAnEarnedBarIsGold) {
  Show(Earned({{ACHIEVEMENT_STARS_17, 5}}),
       {{ACHIEVEMENT_STARS_17, 17}, {ACHIEVEMENT_STARS_22, 17}});
  Select(ACHIEVEMENT_STARS_17);
  EXPECT_TRUE(Paints(panel_.RenderCard(), kGold));
  Select(ACHIEVEMENT_STARS_22);
  EXPECT_FALSE(Paints(panel_.RenderCard(), kGold));
}

// A description longer than the card flows onto the next line, filling each.
TEST_F(AchievementsPanelTest, ALongDescriptionWrapsInsideTheCard) {
  Show(Earned({}), {});
  Select(ACHIEVEMENT_SACRED_SYMBOLS_MAXED);
  const std::string card = RenderElement(panel_.RenderCard());
  EXPECT_NE(card.find("Max every Sacred Symbol"), std::string::npos) << card;
  EXPECT_NE(card.find("count."), std::string::npos) << card;
}

TEST_F(AchievementsPanelTest, SomeoneElsesPurseIsHidden) {
  Show(Earned({}), {});
  Select(ACHIEVEMENT_MESO_10B);
  EXPECT_NE(RenderElement(panel_.RenderCard()).find("Hidden"),
            std::string::npos);
}

TEST_F(AchievementsPanelTest, AnAchievementWithoutACounterHasNoBar) {
  Show(Earned({}), {});
  Select(ACHIEVEMENT_TRIPLE_PRIME);
  const std::string card = RenderElement(panel_.RenderCard());
  EXPECT_NE(card.find("Triple Prime"), std::string::npos) << card;
  EXPECT_EQ(card.find(" / "), std::string::npos) << card;
}

TEST_F(AchievementsPanelTest, TheCursorFollowsItsAchievementWhenTheListMoves) {
  Show(Earned({}), {});
  Select(ACHIEVEMENT_LEVEL_300);
  Show(Earned({{ACHIEVEMENT_LEVEL_300, 5}}), {});
  EXPECT_EQ(panel_.selected()->achievement, ACHIEVEMENT_LEVEL_300);
}

TEST_F(AchievementsPanelTest, EscapeCloses) {
  Show(Earned({}), {});
  EXPECT_EQ(panel_.OnEvent(ftxui::Event::ArrowDown),
            AchievementsPanel::Action::kNone);
  EXPECT_EQ(panel_.OnEvent(ftxui::Event::Escape),
            AchievementsPanel::Action::kClose);
}

}  // namespace
}  // namespace ms
