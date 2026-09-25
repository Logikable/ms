// Draws every screen from the shipped catalogs at the smallest terminal the
// game supports. Screens are centred (see placement.h), so one taller than the
// terminal loses rows at both ends without any error. The screens that grow are
// the data-driven ones, and they grow when a textproto is added, not when
// someone changes the layout.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/placement.h"
#include "src/frontend/screens/all_stats_panel.h"
#include "src/frontend/screens/bank_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buff_info_panel.h"
#include "src/frontend/screens/character_select_panel.h"
#include "src/frontend/screens/hyper_stat_inspect_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/job_inspect_panel.h"
#include "src/frontend/screens/jukebox_panel.h"
#include "src/frontend/screens/keybinds_panel.h"
#include "src/frontend/screens/link_skill_panel.h"
#include "src/frontend/screens/map_select_panel.h"
#include "src/frontend/screens/mob_inspect_panel.h"
#include "src/frontend/screens/multi_sell_panel.h"
#include "src/frontend/screens/options_panel.h"
#include "src/frontend/screens/scroll_panel.h"
#include "src/frontend/screens/shop_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/screens/trade_panel.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/keybinds.pb.h"
#include "src/protos/save.pb.h"
#include "src/protos/skill.pb.h"
#include "src/roster.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

// The screen's size, borders included.
struct Size {
  int rows = 0;
  int columns = 0;
};

Size Measure(ftxui::Element element) {
  element->ComputeRequirement();
  return {element->requirement().min_y, element->requirement().min_x};
}

// The worst case, built once: an endgame character who spent well, with both
// purses full and a long roster. Every test reads it and none modifies it. A
// kMax state is a whole endgame account of ten characters, and building one per
// test made this the suite's slowest target.
class ScreenFitTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    shared_ = new GameState(
        LoadTestData<EquipPrototype>("equip"), LoadTestData<Scroll>("scrolls"),
        LoadTestData<ItemPrototype>("items"), LoadTestData<Mob>("mobs"),
        LoadTestData<MapData>("maps"), LoadTestData<Skill>("skills"),
        GameMode::kMax, TestOptions{},
        /*seed=*/1, LoadTestData<EquipSet>("sets"),
        LoadTestData<Boss>("bosses"));
    // The most either purse can hold, which widens the bank's and the shop's
    // bars.
    shared_->character.AddMeso(kFullPurse);
    shared_->account.mutable_bank().AddMeso(kFullPurse);
    // More characters than the character select list has room for, so the test
    // measures the window's fixed height and not the list's. These are copies
    // of the endgame sheets, not new characters: creating one puts a level 1
    // Beginner into play, and every other screen here is drawn from the endgame
    // state.
    std::vector<CharacterSave> ceilings = shared_->inactive_characters;
    while (shared_->inactive_characters.size() < kCrowdedRoster) {
      for (const CharacterSave& save : ceilings) {
        shared_->inactive_characters.push_back(save);
      }
    }
  }

  static void TearDownTestSuite() {
    delete shared_;
    shared_ = nullptr;
  }

  void ExpectFits(ftxui::Element element, const std::string& what) {
    Size size = Measure(std::move(element));
    EXPECT_LE(size.rows, kMinTerminalRows)
        << what << " is " << size.rows << " rows tall";
    EXPECT_LE(size.columns, kMinTerminalColumns)
        << what << " is " << size.columns << " columns wide";
  }

  GameState& state_ = *shared_;

 private:
  static constexpr int64_t kFullPurse = 100000000000;
  static constexpr size_t kCrowdedRoster = 24;

  static GameState* shared_;
};

GameState* ScreenFitTest::shared_ = nullptr;

TEST_F(ScreenFitTest, MapSelect) {
  MapSelectPanel panel(state_);
  panel.Reset();
  // Every band. The list pads them all to the tallest, so the screen is one
  // size, but visiting each one proves the tallest was measured.
  for (int i = 0; i < 12; ++i) {
    ExpectFits(panel.Render(), "the map list");
    panel.ChangePage(1);
  }
}

TEST_F(ScreenFitTest, MobInspect) {
  MobInspectPanel panel(state_);
  for (const std::pair<const std::string, MapData>& entry : state_.maps) {
    panel.SetMap(entry.first);
    ExpectFits(panel.Render(), "the mobs of " + entry.first);
  }
}

TEST_F(ScreenFitTest, BossSelect) {
  BossSelectPanel panel(state_);
  panel.Reset();
  for (int i = 0; i < 4; ++i) {
    ExpectFits(panel.Render(), "the boss list");
    panel.SwitchPanel(1);
  }
}

TEST_F(ScreenFitTest, CharacterSelect) {
  CharacterSelectPanel panel(state_);
  ExpectFits(panel.Render(), "the character select");
  panel.OpenMenu();
  ExpectFits(panel.Render(), "the character menu");
}

TEST_F(ScreenFitTest, Shop) {
  ShopPanel panel(state_.character, state_.equips, state_.items);
  // Every shelf. The token shelves show a balance panel next to the window, and
  // the catalog decides how many currencies it holds.
  for (int tab = 0; tab < kNumShopTabs; ++tab) {
    for (int pay = 0; pay < kNumShopPayTabs; ++pay) {
      panel.Reset();
      panel.OnEvent(ftxui::Event::ArrowUp);  // the list -> the pay bar
      panel.OnEvent(ftxui::Event::ArrowUp);  // -> the tab bar
      for (int step = 0; step < tab; ++step) {
        panel.OnEvent(ftxui::Event::ArrowRight);
      }
      panel.OnEvent(ftxui::Event::ArrowDown);
      for (int step = 0; step < pay; ++step) {
        panel.OnEvent(ftxui::Event::ArrowRight);
      }
      ExpectFits(panel.Render(), "the shop");
    }
  }
}

TEST_F(ScreenFitTest, MultiSell) {
  MultiSellPanel panel(state_.character, state_.account);
  panel.Reset(/*tab=*/0, /*item=*/0);
  ExpectFits(panel.Render(), "the multi-sell list");
}

TEST_F(ScreenFitTest, Trade) {
  TradePanel panel(state_.character, state_.account);
  TradeState trade;
  trade.set_id("t1");
  trade.set_partner_name("Adventurer");
  trade.set_partner_joined(true);
  // The largest offer either side can make, which is what widens the row.
  trade.mutable_mine()->set_meso(100000000000);
  trade.mutable_mine()->set_spell_traces(1000000);
  *trade.mutable_theirs() = trade.mine();
  panel.SetTrade(trade);
  ExpectFits(panel.Render(), "the trade screen");
}

// Two windows stacked, both at a fixed height: the tightest screen in the game,
// where one more row in either half would push it off.
TEST_F(ScreenFitTest, Bank) {
  BankPanel panel(state_.character, state_.account, state_.items);
  panel.Reset();
  ExpectFits(panel.Render(), "the bank screen, both purses full");
}

// The three windows each have a fixed height (twelve slots, whatever the preset
// holds), so this guards the constants more than the catalog: the top two leave
// the bottom one exactly eight rows.
TEST_F(ScreenFitTest, LinkSkills) {
  LinkSkillPanel panel(state_.character, state_.skills);
  panel.Reset();
  for (int i = 0; i < 3; ++i) {
    ExpectFits(panel.Render(), "the link skills screen");
    panel.NextZone(1);
  }
}

TEST_F(ScreenFitTest, AllStats) {
  AllStatsPanel panel(state_.character, &state_.account, state_.skills);
  ExpectFits(panel.Render(), "the all-stats sheet");
}

TEST_F(ScreenFitTest, Keybinds) {
  Keybinds binds;
  KeyMap keys(&binds);
  KeybindsPanel panel(keys);
  ExpectFits(panel.Render(), "the keybinds table");
}

TEST_F(ScreenFitTest, Options) {
  OptionsPanel panel(state_.account);
  panel.Reset();
  ExpectFits(panel.Render(), "the options list");
}

// The song list is as long as the build's music, and the mode box hangs off the
// top window, so both the closed screen and the open box are measured.
TEST_F(ScreenFitTest, Jukebox) {
  MusicPlayer player(MusicPlayer::Backend::kNull);
  std::mt19937 rng(1);
  MusicDirector director(player, rng);
  JukeboxPanel panel(state_, director, state_.account);
  panel.Reset();
  ExpectFits(panel.Render(), "the jukebox");
  panel.SwitchHalf();
  panel.MoveColumn(-1);
  ASSERT_EQ(panel.selected_button(), JukeboxButton::kMode);
  panel.Activate();
  ExpectFits(panel.Render(), "the jukebox with its mode box open");
}

TEST_F(ScreenFitTest, BuffInfo) {
  BuffInfoPanel panel;
  for (int i = 1; i <= ConsumableType_MAX; ++i) {
    ConsumableType type = static_cast<ConsumableType>(i);
    if (!ConsumableType_IsValid(i)) {
      continue;
    }
    panel.SetBuff(type, /*owned=*/false);
    ExpectFits(panel.Render(), ConsumableType_Name(type) + "'s card");
  }
}

TEST_F(ScreenFitTest, HyperStatInspect) {
  HyperStatInspectPanel panel;
  for (int i = 1; i <= HyperStatField_MAX; ++i) {
    if (!HyperStatField_IsValid(i)) {
      continue;
    }
    HyperStatField field = static_cast<HyperStatField>(i);
    panel.SetStat(field, /*level=*/5, /*max_level=*/15);
    ExpectFits(panel.Render(), HyperStatField_Name(field) + "'s card");
  }
}

// Every skill in the game, at its lowest and highest level. The skill card is
// the tallest thing the Skills tab and the job book show.
TEST_F(ScreenFitTest, SkillInspect) {
  SkillInspectPanel panel;
  panel.SetMaxRows(kMinTerminalRows);
  panel.SetWidthBounds(0, kMinTerminalColumns);
  for (const std::pair<const std::string, Skill>& entry : state_.skills) {
    const Skill& skill = entry.second;
    panel.SetSkill(&skill, /*level=*/1, /*bonus=*/0);
    ExpectFits(panel.Render(), entry.first + "'s card at level 1");
    panel.SetSkill(&skill, skill.max_level(), /*bonus=*/0);
    ExpectFits(panel.Render(), entry.first + "'s card at master level");
  }
}

// Every job's book beside its tallest card. The screen is sized for that,
// however short the card under the cursor is.
TEST_F(ScreenFitTest, JobInspect) {
  JobInspectPanel book(state_.skills);
  SkillInspectPanel card;
  card.SetMaxRows(kMinTerminalRows);
  int books = 0;
  for (int i = 1; i <= Job_MAX; ++i) {
    if (!Job_IsValid(i)) {
      continue;
    }
    Job job = static_cast<Job>(i);
    for (int stage = 1; stage <= 5; ++stage) {
      book.SetJob(job, stage);
      std::vector<const Skill*> skills = book.Skills();
      if (skills.empty()) {
        continue;
      }
      ++books;
      PreviewCardSize size = LargestPreviewCard(
          skills, kMinTerminalColumns - kJobInspectBookWidth);
      card.SetSkill(skills.front(), 0, 0, SkillInspectPanel::kPreview);
      card.SetWidthBounds(size.columns, size.columns);
      ExpectFits(JobInspectScreen(book.Render(), card.Render(),
                                  std::min(size.rows, kMinTerminalRows)),
                 Job_Name(job) + " stage " + std::to_string(stage));
    }
  }
  // Guards the loop itself: if SetJob got a stage nothing matches, every book
  // would be empty and the test would check nothing.
  EXPECT_GT(books, 0);
}

// Every equip compared with itself, so the screen shows the comparison card and
// the set card as well as the item's own.
TEST_F(ScreenFitTest, Inspect) {
  InspectPanel panel;
  panel.UseCharacter(state_.character);
  panel.SetMaxRows(kMinTerminalRows);
  panel.SetMaxColumns(kMinTerminalColumns);
  for (const std::pair<const std::string, EquipPrototype>& entry :
       state_.equips) {
    EquipInstance item(entry.second);
    panel.SetItem(&item);
    panel.SetComparison(&item);
    ExpectFits(panel.Render(), entry.first + "'s card");
  }
}

// Every equip's scroll list, which is as long as the catalog makes it.
TEST_F(ScreenFitTest, Scroll) {
  ScrollPanel panel(state_.character, state_.scrolls);
  for (const std::pair<const std::string, EquipPrototype>& entry :
       state_.equips) {
    if (!panel.SetFilterForPrototype(entry.second)) {
      continue;
    }
    ExpectFits(panel.Render(/*focused=*/true),
               "the scrolls for " + entry.first);
  }
}

}  // namespace
}  // namespace ms
