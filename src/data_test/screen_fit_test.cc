// Every screen, drawn from the shipped catalogs, against the smallest terminal
// the game is laid out for. A screen stands CENTRED (see placement.h), so one
// taller than the terminal loses rows off both ends and nothing says so -- and
// the screens that grow are the data-driven ones, which grow when a textproto
// lands rather than when anybody touches the layout.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/frontend/keybinds.h"
#include "src/frontend/placement.h"
#include "src/frontend/screens/all_stats_panel.h"
#include "src/frontend/screens/boss_select_panel.h"
#include "src/frontend/screens/buff_info_panel.h"
#include "src/frontend/screens/character_select_panel.h"
#include "src/frontend/screens/hyper_stat_inspect_panel.h"
#include "src/frontend/screens/inspect_panel.h"
#include "src/frontend/screens/job_inspect_panel.h"
#include "src/frontend/screens/keybinds_panel.h"
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
#include "src/protos/skill.pb.h"
#include "src/roster.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

// What the screen comes to, borders included.
struct Size {
  int rows = 0;
  int columns = 0;
};

Size Measure(ftxui::Element element) {
  element->ComputeRequirement();
  return {element->requirement().min_y, element->requirement().min_x};
}

class ScreenFitTest : public testing::Test {
 protected:
  // The ceiling a player who spent well stands in: every mechanic open, the
  // bag full, every buff bought. The worst case for a screen's height, and the
  // one a fit test wants.
  ScreenFitTest()
      : state_(LoadTestData<EquipPrototype>("equip"),
               LoadTestData<Scroll>("scrolls"),
               LoadTestData<ItemPrototype>("items"), LoadTestData<Mob>("mobs"),
               LoadTestData<MapData>("maps"), LoadTestData<Skill>("skills"),
               GameMode::kMax, TestOptions{},
               /*seed=*/1, LoadTestData<EquipSet>("sets"),
               LoadTestData<Boss>("bosses")) {
  }

  void ExpectFits(ftxui::Element element, const std::string& what) {
    Size size = Measure(std::move(element));
    EXPECT_LE(size.rows, kMinTerminalRows)
        << what << " is " << size.rows << " rows tall";
    EXPECT_LE(size.columns, kMinTerminalColumns)
        << what << " is " << size.columns << " columns wide";
  }

  GameState state_;
};

TEST_F(ScreenFitTest, MapSelect) {
  MapSelectPanel panel(state_);
  panel.Reset();
  // Every band: the list pads them all to the tallest, so the screen is one
  // size, but walking them is what proves the tallest was measured.
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
  // A full account: the list scrolls past this, so the tallest the screen can
  // be is the height the two windows are fixed at.
  for (int i = 0; i < 12; ++i) {
    CreateCharacter(state_);
  }
  CharacterSelectPanel panel(state_);
  ExpectFits(panel.Render(), "the character select");
  panel.OpenMenu();
  ExpectFits(panel.Render(), "the character menu");
}

TEST_F(ScreenFitTest, Shop) {
  ShopPanel panel(state_.character, state_.equips, state_.items);
  // Every shelf: the token ones stand a balance panel beside the window, and
  // how many currencies that holds is the catalog's to say.
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
  // The biggest either side can put up, which is what widens the row.
  trade.mutable_mine()->set_meso(100000000000);
  trade.mutable_mine()->set_spell_traces(1000000);
  *trade.mutable_theirs() = trade.mine();
  panel.SetTrade(trade);
  ExpectFits(panel.Render(), "the trade screen");
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

// Every skill the game ships, at the two ends of its levels: the card is the
// tallest thing the Skills tab and the job book both put on screen.
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

// Every job's book beside the tallest card in it, which is the size the
// screen is held to however short the card under the cursor happens to be.
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
  // A guard on the loop itself: SetJob taking a stage nothing answers to would
  // leave every book empty and the test asserting nothing.
  EXPECT_GT(books, 0);
}

// Every equip, weighed against itself so the screen carries the compared card
// and the set card as well as the item's own.
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

// Every equip's shelf of scrolls, which is as long as the catalog makes it.
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
