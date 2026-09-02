#include "src/frontend/screens/inspect_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

// --- the set card ---

// The Frozen Set as its data file writes it: four pieces written, a weapon and
// a secondary still waiting on items, and the two tiers those four can reach.
std::map<std::string, EquipSet> FrozenSet() {
  const EquipSlot kSlots[] = {EQUIP_SLOT_HAT, EQUIP_SLOT_TOP, EQUIP_SLOT_BOTTOM,
                              EQUIP_SLOT_CAPE};
  const char* kNames[] = {"Frozen Hat", "Frozen Top", "Frozen Bottom",
                          "Frozen Cape"};
  EquipSet set;
  set.set_name(EQUIP_SET_NAME_FROZEN);
  for (int i = 0; i < 4; ++i) {
    EquipSetMember* member = set.add_members();
    member->set_slot(kSlots[i]);
    member->mutable_items()->add_name(kNames[i]);
  }
  EquipSetMember* weapon = set.add_members();
  weapon->set_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon->set_family("Frozen Weapon");
  EquipSetMember* secondary = set.add_members();
  secondary->set_slot(EQUIP_SLOT_SECONDARY);
  secondary->set_family("Frozen Secondary");

  EquipSetTier* three = set.add_tiers();
  three->set_pieces(3);
  SkillEffect* effect = three->mutable_effect();
  effect->set_str(7);
  effect->set_dex(7);
  effect->set_int_(7);
  effect->set_luk(7);
  effect->set_attack(5);
  effect->set_magic_attack(5);
  EquipSetTier* four = set.add_tiers();
  four->set_pieces(4);
  four->mutable_effect()->set_max_hp_pct(0.20);
  four->mutable_effect()->set_max_mp_pct(0.20);
  four->mutable_effect()->set_damage_pct(0.09);
  return {{"frozen", set}};
}

EquipPrototype FrozenPiece(const std::string& name, EquipSlot slot) {
  EquipPrototype proto;
  proto.set_name(name);
  proto.set_equip_slot(slot);
  return proto;
}

// A piece the set names by family rather than by name, as every Frozen weapon
// and secondary is.
EquipPrototype FrozenFamilyPiece(const std::string& name, EquipSlot slot,
                                 const std::string& family) {
  EquipPrototype proto = FrozenPiece(name, slot);
  proto.set_set_family(family);
  return proto;
}

// Wears one piece of the set, so the tiers it reaches light up.
void Wear(CharacterInstance& character, const std::string& name,
          EquipSlot slot) {
  character.PickUp(std::make_unique<EquipInstance>(FrozenPiece(name, slot)));
  character.Equip(static_cast<int>(character.inventory().size()) - 1);
}

void WearFamilyPiece(CharacterInstance& character, const std::string& name,
                     EquipSlot slot, const std::string& family) {
  character.PickUp(
      std::make_unique<EquipInstance>(FrozenFamilyPiece(name, slot, family)));
  character.Equip(static_cast<int>(character.inventory().size()) - 1);
}

class InspectPanelTest : public PanelTest {
 protected:
  static std::string Render(InspectPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, panel.Render());
    return StripAnsi(screen.ToString());
  }

  // A screen with room for an item and the set card beside it, which the 80x20
  // one clips on both axes.
  static ftxui::Screen Draw(InspectPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(34));
    ftxui::Render(screen, panel.Render());
    return screen;
  }

  static std::string RenderWide(InspectPanel& panel) {
    return StripAnsi(Draw(panel).ToString());
  }

  // The columns the panel needs when nothing squeezes or stretches it. Not the
  // columns it draws in: a window fills whatever box it is handed, so a bare
  // render is always as wide as the screen.
  static int NaturalWidth(InspectPanel& panel) {
    ftxui::Element body = panel.Render();
    body->ComputeRequirement();
    return body->requirement().min_x;
  }

  // How many rendered rows carry `text`.
  static int RowsWith(InspectPanel& panel, const std::string& text) {
    std::string rendered = RenderWide(panel);
    int rows = 0;
    size_t start = 0;
    while (start < rendered.size()) {
      size_t end = rendered.find('\n', start);
      if (end == std::string::npos) {
        end = rendered.size();
      }
      if (rendered.substr(start, end - start).find(text) != std::string::npos) {
        ++rows;
      }
      start = end + 1;
    }
    return rows;
  }

  // Whether the first cell of `label` came out dimmed. False when the label is
  // not on screen at all, so a test asserting dimness has to find it first.
  //
  // A row is searched as bytes and read as columns, which are not the same
  // thing: a border or a star is one column and three bytes, so the byte a
  // match starts at is nowhere near the column it is drawn in.
  static bool DimAt(InspectPanel& panel, const std::string& label) {
    ftxui::Screen screen = Draw(panel);
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      std::vector<int> column_of_byte;
      for (int x = 0; x < screen.dimx(); ++x) {
        std::string cell = screen.PixelAt(x, y).character;
        if (cell.empty()) {
          cell = " ";
        }
        row += cell;
        column_of_byte.insert(column_of_byte.end(), cell.size(), x);
      }
      size_t at = row.find(label);
      if (at != std::string::npos) {
        return screen.PixelAt(column_of_byte[at], y).dim;
      }
    }
    return false;
  }

  // Strips ANSI escape sequences so substring searches work regardless of
  // color.
  static std::string StripAnsi(const std::string& s) {
    std::string out;
    bool in_esc = false;
    for (char c : s) {
      if (c == '\x1b') {
        in_esc = true;
        continue;
      }
      if (in_esc) {
        if (c == 'm') {
          in_esc = false;
        }
        continue;
      }
      out += c;
    }
    return out;
  }

  // The set card as most of these tests open it: a Frozen Hat, on a character
  // who knows the Frozen Set. The fixture owns the item so the panel has
  // something to point at for the whole test.
  InspectPanel& Card() {
    c_.UseEquipSets(FrozenSet());
    card_.UseCharacter(c_);
    card_.SetItem(&hat_);
    return card_;
  }

  EquipInstance hat_{FrozenPiece("Frozen Hat", EQUIP_SLOT_HAT)};
  InspectPanel card_;
};

TEST_F(InspectPanelTest, NullItemShowsPlaceholder) {
  InspectPanel panel;
  EXPECT_NE(Render(panel).find("(no item)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsTheItemsOwnFields) {
  sword_.set_name("Iron Sword");
  sword_.set_required_level(30);
  sword_.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword_.set_attack_speed(ATTACK_SPEED_AVERAGE);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Iron Sword"), std::string::npos);
  EXPECT_NE(rendered.find("Req Lev: 30"), std::string::npos);
  EXPECT_NE(rendered.find("Warrior"), std::string::npos);
  EXPECT_NE(rendered.find("Type: One-Handed Sword"), std::string::npos);
  EXPECT_NE(rendered.find("Attack Speed: Stage 4 (Average)"),
            std::string::npos);
}

TEST_F(InspectPanelTest, IneligibleJobsStillRendered) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Bowman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician"), std::string::npos);
  EXPECT_NE(rendered.find("Pirate"), std::string::npos);
}

TEST_F(InspectPanelTest, UniversalShowsAllJobGroups) {
  sword_.clear_equip_job_categories();
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Beginner"), std::string::npos);
  EXPECT_NE(rendered.find("Warrior"), std::string::npos);
  EXPECT_NE(rendered.find("Bowman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician"), std::string::npos);
  EXPECT_NE(rendered.find("Thief"), std::string::npos);
  EXPECT_NE(rendered.find("Pirate"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsBaseStat) {
  sword_.mutable_base_stats()->set_attack(7);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("+7 "), std::string::npos);
  EXPECT_EQ(Render(panel).find("(7"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsScrollStatBreakdown) {
  sword_.mutable_base_stats()->set_attack(5);
  Equip state;
  state.set_equip_name("Sword");
  state.set_remaining_upgrade_slots(3);
  state.mutable_scroll_stats()->set_attack(3);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(Render(panel).find("+8 (5 +3)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsScrollInfo) {
  sword_.set_upgrade_slots(7);
  Equip state;
  state.set_equip_name("Sword");
  state.set_remaining_upgrade_slots(4);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  // 7 slots, 4 left, 0 successes → 3 restores.
  EXPECT_NE(Render(panel).find("0 Successful Scrolls"), std::string::npos);
  EXPECT_NE(Render(panel).find("4 Left, 3 Restores"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsPercentageStatsUnderTheFlatOnes) {
  sword_.mutable_base_stats()->set_attack(7);
  sword_.mutable_base_stats()->set_max_hp_pct(10);
  sword_.mutable_base_stats()->set_boss_damage(30);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Max HP  +10%"), std::string::npos);
  EXPECT_NE(rendered.find("Boss Damage  +30%"), std::string::npos);
  EXPECT_LT(rendered.find("+7 "), rendered.find("Max HP  +10%"));
  EXPECT_EQ(rendered.find("Max MP"), std::string::npos);
}

// A stat left at zero is not a row of its own, and an item with none of them
// says so rather than showing an empty column.
TEST_F(InspectPanelTest, AnItemWithNoStatsSaysSoRatherThanShowingZeroes) {
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("DEF"), std::string::npos);
  EXPECT_NE(rendered.find("(no stats)"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsStarForceStatBreakdown) {
  sword_.mutable_base_stats()->set_attack(7);
  sword_.set_required_level(10);
  Equip state;
  state.set_equip_name("Sword");
  state.set_stars(5);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  // 5★ on a level-10 warrior weapon: SF gives STR=10, DEX=10, ATT=5.
  // STR: base=0, sf=10 → "+10 (0 +10)".
  // ATT: base=7, sf=5 → "+12 (7 +5)".
  EXPECT_NE(Render(panel).find("+10 (0 +10)"), std::string::npos);
  EXPECT_NE(Render(panel).find("+12 (7 +5)"), std::string::npos);
}

TEST_F(InspectPanelTest, StarBarShowsFilledAndEmptyStars) {
  // Level 0 item (max 5★) at 3★: expect 3 filled and 2 empty.
  Equip state;
  state.set_stars(3);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("★★★☆☆"), std::string::npos);
}

TEST_F(InspectPanelTest, StarBarLengthReflectsItemMaxStars) {
  // Level 95 item has max 8★; bar is split into two groups of 5 and 3.
  sword_.set_required_level(95);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  // All-empty 8★ bar: "☆☆☆☆☆ ☆☆☆" (5 + space + 3).
  EXPECT_NE(Render(panel).find("☆☆☆☆☆"), std::string::npos);
}

TEST_F(InspectPanelTest, AnItemThatRefusesStarForceHasNoBar) {
  sword_.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("☆"), std::string::npos);
  EXPECT_EQ(rendered.find("★"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsTraceNameWithSuffix) {
  sword_.set_name("Iron Sword");
  Equip state;
  state.set_equip_name("Iron Sword");
  EquipTrace trace(sword_, state);
  InspectPanel panel;
  panel.SetItem(&trace);
  EXPECT_NE(Render(panel).find("Iron Sword Trace"), std::string::npos);
}

// --- stackable items ---

// The same screen, reached the same way, for an item that has a sentence
// instead of statistics.
ItemPrototype MakeStackable(const std::string& name,
                            const std::string& description) {
  ItemPrototype item;
  item.set_name(name);
  item.set_category(ITEM_CATEGORY_USE);
  item.set_description(description);
  return item;
}

TEST_F(InspectPanelTest, ShowsAStackablesNameAndDescription) {
  ItemPrototype item = MakeStackable("Red Potion", "Recovers 50 HP.");
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Red Potion"), std::string::npos);
  EXPECT_NE(rendered.find("Recovers 50 HP."), std::string::npos);
}

// A description longer than the window wraps rather than running off the edge
// or stretching the window to fit it.
TEST_F(InspectPanelTest, WrapsALongDescription) {
  ItemPrototype item = MakeStackable(
      "Elixir",
      "A thick green draught that restores every point of health and magic "
      "the drinker has spent, and tastes of pine needles besides.");
  InspectPanel panel;
  panel.SetItem(&item);
  ftxui::Element element = panel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                               ftxui::Dimension::Fit(element));
  ftxui::Render(screen, element);
  EXPECT_LE(screen.dimx(), 44);
  EXPECT_GT(screen.dimy(), 5) << "the description took more than one row";
}

// Every description reads at the same width, so the window does not resize as
// the cursor moves between one item and the next.
TEST_F(InspectPanelTest, EveryStackableIsTheSameWidth) {
  ItemPrototype terse = MakeStackable("Pill", "Small.");
  ItemPrototype wordy = MakeStackable(
      "Elixir", "A thick green draught that restores every point of health.");
  InspectPanel panel;
  panel.SetItem(&terse);
  ftxui::Element narrow = panel.Render();
  panel.SetItem(&wordy);
  ftxui::Element wide = panel.Render();
  EXPECT_EQ(ftxui::Screen::Create(ftxui::Dimension::Fit(narrow)).dimx(),
            ftxui::Screen::Create(ftxui::Dimension::Fit(wide)).dimx());
}

TEST_F(InspectPanelTest, SaysSoWhenAStackableHasNoDescription) {
  ItemPrototype item = MakeStackable("Green Snail Shell", "");
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Green Snail Shell"), std::string::npos);
  EXPECT_NE(rendered.find("(no description)"), std::string::npos);
}

// The two kinds are exclusive: the panel describes the item the cursor was
// last on, not both at once.
TEST_F(InspectPanelTest, EitherKindOfItemReplacesTheOther) {
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  EquipInstance equip(proto);
  ItemPrototype potion = MakeStackable("Red Potion", "Recovers 50 HP.");

  InspectPanel panel;
  panel.SetItem(&equip);
  panel.SetItem(&potion);
  EXPECT_EQ(Render(panel).find("Sword"), std::string::npos);

  panel.SetItem(&equip);
  std::string back = Render(panel);
  EXPECT_NE(back.find("Sword"), std::string::npos);
  EXPECT_EQ(back.find("Recovers 50 HP."), std::string::npos);
}

// --- a narrow item gets a narrow card ---

// The six job categories are the same six on every item, and the star bar is
// as long as the item's level allows. Neither should be what decides how wide
// the card is.
TEST_F(InspectPanelTest, FoldsTheJobRowWhenNothingElseIsWide) {
  sword_.clear_equip_job_categories();
  sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Beginner / Warrior / Bowman"), std::string::npos);
  EXPECT_NE(rendered.find("Magician / Thief / Pirate"), std::string::npos);
  EXPECT_EQ(rendered.find("Bowman / Magician"), std::string::npos);
  EXPECT_LT(NaturalWidth(panel), 35);
}

TEST_F(InspectPanelTest, KeepsTheJobRowWholeWhenTheCardIsWideAnyway) {
  sword_.set_name("Fafnir Mistilteinn of Preposterous Length and Renown Trace");
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_NE(RenderWide(panel).find("Bowman / Magician"), std::string::npos);
}

TEST_F(InspectPanelTest, FoldsAStarBarPastFifteen) {
  sword_.set_required_level(150);  // 30 stars
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(RowsWith(panel, "☆"), 2);
  // Fifteen to a rank, grouped in fives from the start of each rank.
  EXPECT_EQ(RowsWith(panel, "☆☆☆☆☆ ☆☆☆☆☆ ☆☆☆☆☆"), 2);
  EXPECT_LT(NaturalWidth(panel), 35);
}

// The excess is a rank of its own however short it is, and it is centred under
// the first.
TEST_F(InspectPanelTest, FoldsAStarBarIntoFifteenAndTheRest) {
  sword_.set_required_level(128);  // 20 stars
  Equip state;
  state.set_stars(17);
  EquipInstance item(sword_, state);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(RowsWith(panel, "★★★★★ ★★★★★ ★★★★★"), 1);
  EXPECT_EQ(RowsWith(panel, "★★☆☆☆"), 1);
}

TEST_F(InspectPanelTest, KeepsAStarBarOfFifteenOnOneRow) {
  sword_.set_required_level(118);  // 15 stars
  EquipInstance item(sword_);
  InspectPanel panel;
  panel.SetItem(&item);
  EXPECT_EQ(RowsWith(panel, "☆"), 1);
}

TEST_F(InspectPanelTest, NoSetCardForAnItemInNoSet) {
  EquipInstance sword(sword_);
  c_.UseEquipSets(FrozenSet());

  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&sword);
  EXPECT_EQ(RenderWide(panel).find("Set Effect"), std::string::npos);
}

// The panel is free to be used without a character behind it -- a test, or a
// screen that only ever shows one item -- and then knows of no sets at all.
TEST_F(InspectPanelTest, NoSetCardWithoutACharacter) {
  InspectPanel panel;
  panel.SetItem(&hat_);
  EXPECT_EQ(RenderWide(panel).find("Set Effect"), std::string::npos);
}

TEST_F(InspectPanelTest, ShowsTheWholeSetBesideOneOfItsPieces) {
  InspectPanel& panel = Card();
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Set Effect"), std::string::npos);
  EXPECT_NE(rendered.find("Frozen Set"), std::string::npos);
  EXPECT_NE(rendered.find("Hat        Frozen Hat"), std::string::npos);
  EXPECT_NE(rendered.find("Cape       Frozen Cape"), std::string::npos);
  // The two slots with no item written yet name what they are waiting for.
  EXPECT_NE(rendered.find("Weapon     Choose 1 Frozen Weapon"),
            std::string::npos);
  EXPECT_NE(rendered.find("Secondary  Choose 1 Frozen Secondary"),
            std::string::npos);
}

// The scroll screen puts its list where the set card would go, and three
// windows in a row leaves none of them the width they need.
TEST_F(InspectPanelTest, TheCardAloneLeavesTheSetCardOut) {
  InspectPanel& panel = Card();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(34));
  ftxui::Render(screen, panel.RenderItemOnly());
  std::string rendered = StripAnsi(screen.ToString());
  EXPECT_EQ(rendered.find("Set Effect"), std::string::npos);
  EXPECT_NE(rendered.find("Frozen Hat"), std::string::npos);
}

TEST_F(InspectPanelTest, ReadsWhatEachTierPays) {
  InspectPanel& panel = Card();
  std::string rendered = RenderWide(panel);
  // Four equal stats read as one row, and so do the pairs that travel
  // together. Only the first line of a tier carries its label.
  EXPECT_NE(rendered.find("3 Set Effect   All Stats +7"), std::string::npos);
  EXPECT_NE(rendered.find("                Attack Power & Magic ATT +5"),
            std::string::npos);
  EXPECT_NE(rendered.find("4 Set Effect   Max HP & MP +20%"),
            std::string::npos);
  EXPECT_NE(rendered.find("                Damage +9%"), std::string::npos);
}

TEST_F(InspectPanelTest, SplitsAPairWhoseHalvesDisagree) {
  std::map<std::string, EquipSet> sets = FrozenSet();
  SkillEffect* three = sets["frozen"].mutable_tiers(0)->mutable_effect();
  three->set_magic_attack(3);
  three->set_luk(4);
  c_.UseEquipSets(sets);

  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&hat_);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Attack Power +5"), std::string::npos);
  EXPECT_NE(rendered.find("Magic ATT +3"), std::string::npos);
  EXPECT_EQ(rendered.find("Attack Power & Magic ATT"), std::string::npos);
  // Stats that no longer agree are four rows of their own.
  EXPECT_EQ(rendered.find("All Stats"), std::string::npos);
  EXPECT_NE(rendered.find("STR +7"), std::string::npos);
  EXPECT_NE(rendered.find("LUK +4"), std::string::npos);
}

TEST_F(InspectPanelTest, DimsThePiecesNotBeingWorn) {
  InspectPanel& panel = Card();
  EXPECT_TRUE(DimAt(panel, "Hat        Frozen Hat"));
  EXPECT_TRUE(DimAt(panel, "Cape       Frozen Cape"));
  // Nothing of that family is on the character, so the slot is still asking.
  EXPECT_TRUE(DimAt(panel, "Weapon     Choose 1 Frozen Weapon"));

  // The item being inspected does not count -- only what is on the character.
  Wear(c_, "Frozen Hat", EQUIP_SLOT_HAT);
  EXPECT_FALSE(DimAt(panel, "Hat        Frozen Hat"));
  EXPECT_TRUE(DimAt(panel, "Cape       Frozen Cape"));
}

// A slot is filled by whichever of its alternates is on, not only by the first
// one written -- and a slot family wide enough to hold two of them lights both.
TEST_F(InspectPanelTest, EveryWornAlternateOfASlotIsLit) {
  EquipSet set;
  set.set_name(EQUIP_SET_NAME_BOSS_ACCESSORY);
  EquipSetMember* member = set.add_members();
  member->set_slot(EQUIP_SLOT_RING);
  for (const char* name : {"Ring A", "Ring B", "Ring C"}) {
    member->mutable_items()->add_name(name);
  }
  // Inspected from another slot of the same set, so the ring names appear on
  // the set card and nowhere else.
  set.add_members()->set_slot(EQUIP_SLOT_BELT);
  set.mutable_members(1)->mutable_items()->add_name("Belt");
  c_.UseEquipSets({{"rings", set}});
  EquipInstance belt(FrozenPiece("Belt", EQUIP_SLOT_BELT));
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&belt);
  EXPECT_TRUE(DimAt(panel, "Ring       Ring A"));
  EXPECT_TRUE(DimAt(panel, "Ring B"));

  // The second alternate fills the slot, so the slot reads as filled even
  // though the piece above it is not the one that filled it.
  Wear(c_, "Ring B", EQUIP_SLOT_RING);
  EXPECT_FALSE(DimAt(panel, "Ring       Ring A"));
  EXPECT_TRUE(DimAt(panel, "Ring A"));
  EXPECT_FALSE(DimAt(panel, "Ring B"));

  // A second ring goes on a ring slot of its own, and lights on its own.
  Wear(c_, "Ring C", EQUIP_SLOT_RING);
  EXPECT_FALSE(DimAt(panel, "Ring C"));
  EXPECT_TRUE(DimAt(panel, "Ring A"));
}

// A slot can name pieces outright and accept a family beside them: the Boss
// Accessory shoulder is one Magnus drop or any of the four Cygnus shoulders.
TEST_F(InspectPanelTest, ASlotNamesItsOwnPiecesAndItsFamily) {
  EquipSet set;
  set.set_name(EQUIP_SET_NAME_BOSS_ACCESSORY);
  EquipSetMember* member = set.add_members();
  member->set_slot(EQUIP_SLOT_SHOULDER);
  member->mutable_items()->add_name("Plain Shoulder");
  member->set_family("Cygnus Shoulder");
  c_.UseEquipSets({{"boss", set}});

  EquipInstance shoulder(FrozenPiece("Plain Shoulder", EQUIP_SLOT_SHOULDER));
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&shoulder);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Shoulder   Plain Shoulder"), std::string::npos);
  EXPECT_NE(rendered.find("Choose 1 Cygnus Shoulder"), std::string::npos);
  EXPECT_TRUE(DimAt(panel, "Shoulder   Plain Shoulder"));

  // The family answers, and the slot is filled -- once, however it was.
  WearFamilyPiece(c_, "Lionheart Battle Shoulder", EQUIP_SLOT_SHOULDER,
                  "Cygnus Shoulder");
  rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Lionheart Battle Shoulder"), std::string::npos);
  EXPECT_EQ(rendered.find("Choose 1"), std::string::npos);
  EXPECT_FALSE(DimAt(panel, "Shoulder   Plain Shoulder"));
  EXPECT_EQ(c_.PiecesWornOf(set), 1);
}

// A family slot asks for a piece until one is worn, and then names the one
// that answered. Which is also the moment the tiers past four become reachable.
TEST_F(InspectPanelTest, AWornFamilyPieceNamesItselfInItsSlot) {
  InspectPanel& panel = Card();
  ASSERT_NE(RenderWide(panel).find("Weapon     Choose 1 Frozen Weapon"),
            std::string::npos);

  WearFamilyPiece(c_, "Frozen Polearm", EQUIP_SLOT_PRIMARY_WEAPON,
                  "Frozen Weapon");
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Weapon     Frozen Polearm"), std::string::npos);
  EXPECT_EQ(rendered.find("Choose 1 Frozen Weapon"), std::string::npos);
  EXPECT_FALSE(DimAt(panel, "Weapon     Frozen Polearm"));
}

// The set names no weapon, so a weapon has to be matched by its family or the
// card would never open beside one.
TEST_F(InspectPanelTest, AFamilyPieceOpensTheSameCard) {
  EquipPrototype proto = FrozenFamilyPiece(
      "Frozen Polearm", EQUIP_SLOT_PRIMARY_WEAPON, "Frozen Weapon");
  EquipInstance polearm(proto);
  c_.UseEquipSets(FrozenSet());

  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&polearm);
  std::string rendered = RenderWide(panel);
  EXPECT_NE(rendered.find("Frozen Set"), std::string::npos);
  EXPECT_NE(rendered.find("Hat        Frozen Hat"), std::string::npos);
}

TEST_F(InspectPanelTest, DimsTheTiersTheCharacterHasNotEarned) {
  InspectPanel& panel = Card();
  EXPECT_TRUE(DimAt(panel, "3 Set Effect"));
  EXPECT_TRUE(DimAt(panel, "4 Set Effect"));

  Wear(c_, "Frozen Hat", EQUIP_SLOT_HAT);
  Wear(c_, "Frozen Top", EQUIP_SLOT_TOP);
  Wear(c_, "Frozen Bottom", EQUIP_SLOT_BOTTOM);
  EXPECT_FALSE(DimAt(panel, "3 Set Effect"));
  // Every line of the tier lights up, not only the labelled one.
  EXPECT_FALSE(DimAt(panel, "Attack Power & Magic ATT +5"));
  EXPECT_TRUE(DimAt(panel, "4 Set Effect"));
}

// The card sits beside the item, so a card that resized with its contents
// would walk the item panel across the screen.
TEST_F(InspectPanelTest, TheCardIsOneWidthWhateverTheSetHolds) {
  InspectPanel& panel = Card();
  int wide = NaturalWidth(panel);

  std::map<std::string, EquipSet> small = FrozenSet();
  SkillEffect* three = small["frozen"].mutable_tiers(0)->mutable_effect();
  three->Clear();
  three->set_str(1);
  c_.UseEquipSets(small);

  EXPECT_EQ(NaturalWidth(panel), wide);
}

// --- scrolling ---

// A set with enough tiers written to outgrow any terminal, so the card has
// something to scroll.
std::map<std::string, EquipSet> TallSet() {
  std::map<std::string, EquipSet> sets = FrozenSet();
  EquipSet& set = sets["frozen"];
  for (int i = 5; i < 40; ++i) {
    EquipSetTier* tier = set.add_tiers();
    tier->set_pieces(i);
    tier->mutable_effect()->set_str(i);
  }
  return sets;
}

// A panel on a Frozen Hat with a set card beside it, both cut to `rows`.
InspectPanel TallPanel(CharacterInstance& character, int rows) {
  character.UseEquipSets(TallSet());
  InspectPanel panel;
  panel.UseCharacter(character);
  panel.SetMaxRows(rows);
  return panel;
}

// Only the tiers move: the set's name and the pieces it is made of are what
// the tiers are read against, so they stay where they are.
TEST_F(InspectPanelTest, ScrollsTheSetCardWithoutMovingTheItemCard) {
  InspectPanel panel = TallPanel(c_, 18);
  panel.SetItem(&hat_);
  ASSERT_NE(RenderWide(panel).find("3 Set Effect"), std::string::npos);

  ASSERT_TRUE(panel.SwapCard());
  EXPECT_EQ(panel.focused_card(), InspectPanel::kSetCard);
  panel.ScrollBy(3);
  std::string rendered = RenderWide(panel);
  EXPECT_EQ(rendered.find("3 Set Effect"), std::string::npos)
      << "the tiers have moved";
  EXPECT_NE(rendered.find("Frozen Set"), std::string::npos)
      << "the head has not";
  EXPECT_NE(rendered.find("Req Lev"), std::string::npos)
      << "and neither has the item card";
}

// The stats are the only part of the item card that moves.
TEST_F(InspectPanelTest, ScrollsTheItemCardBetweenItsHeadAndItsFoot) {
  sword_.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword_.set_upgrade_slots(7);
  EquipStats* stats = sword_.mutable_base_stats();
  stats->set_str(5);
  stats->set_dex(5);
  stats->set_int_(5);
  stats->set_luk(5);
  stats->set_max_hp(5);
  stats->set_max_mp(5);
  stats->set_attack(5);
  Equip state;
  state.set_equip_name("Sword");
  state.set_remaining_upgrade_slots(7);
  EquipInstance sword(sword_, state);
  InspectPanel panel;
  // Room for the head, the foot and two lines of stats between them.
  panel.SetMaxRows(14);
  panel.SetItem(&sword);
  ASSERT_NE(RenderWide(panel).find("Type:"), std::string::npos);

  panel.ScrollBy(2);
  std::string rendered = RenderWide(panel);
  EXPECT_EQ(rendered.find("Type:"), std::string::npos) << "the stats moved";
  EXPECT_NE(rendered.find("Req Lev"), std::string::npos) << "the head did not";
  EXPECT_NE(rendered.find("Successful Scroll"), std::string::npos)
      << "and neither did the foot";
}

TEST_F(InspectPanelTest, ResetPutsBothCardsBackAtTheTop) {
  InspectPanel panel = TallPanel(c_, 12);
  panel.SetItem(&hat_);
  RenderWide(panel);
  ASSERT_TRUE(panel.SwapCard());
  panel.ScrollBy(5);
  RenderWide(panel);

  panel.Reset();
  EXPECT_EQ(panel.focused_card(), InspectPanel::kItemCard);
  EXPECT_NE(RenderWide(panel).find("Frozen Set"), std::string::npos);
}

// Two cards make a ring of two: the switch key comes back to the card it
// started on rather than stopping on the far one.
TEST_F(InspectPanelTest, TabCyclesBackToTheItemCard) {
  InspectPanel panel = TallPanel(c_, 18);
  panel.SetItem(&hat_);
  RenderWide(panel);

  ASSERT_TRUE(panel.SwapCard());
  ASSERT_EQ(panel.focused_card(), InspectPanel::kSetCard);
  EXPECT_TRUE(panel.SwapCard());
  EXPECT_EQ(panel.focused_card(), InspectPanel::kItemCard);
}

// A card that fits is still a stop: leaving it out would strand the arrows on
// the other one.
TEST_F(InspectPanelTest, TabReachesACardWithNothingToScroll) {
  c_.UseEquipSets(FrozenSet());
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetMaxRows(34);
  panel.SetItem(&hat_);
  RenderWide(panel);
  EXPECT_TRUE(panel.SwapCard()) << "the whole set fits, and is still a stop";
  EXPECT_EQ(panel.focused_card(), InspectPanel::kSetCard);
}

TEST_F(InspectPanelTest, NoTabWithoutASetCard) {
  EquipInstance sword(sword_);
  c_.UseEquipSets(FrozenSet());
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetMaxRows(6);
  panel.SetItem(&sword);
  RenderWide(panel);
  EXPECT_FALSE(panel.HasSetCard());
  EXPECT_FALSE(panel.SwapCard());
}

// --- Arcane Symbols ---

// How many times `glyph` appears, for counting the pips of a growth bar.
int Count(const std::string& rendered, const std::string& glyph) {
  int found = 0;
  for (size_t at = rendered.find(glyph); at != std::string::npos;
       at = rendered.find(glyph, at + glyph.size())) {
    ++found;
  }
  return found;
}

// The row drawn under the one holding `text`, for checking a rule falls where
// it should.
std::string LineAfter(const std::string& rendered, const std::string& text) {
  size_t at = rendered.find(text);
  if (at == std::string::npos) {
    return "";
  }
  size_t eol = rendered.find('\n', at);
  if (eol == std::string::npos) {
    return "";
  }
  return rendered.substr(eol + 1, rendered.find('\n', eol + 1) - eol - 1);
}

// A symbol grants nothing an equip's rows could show, so it gets a card of its
// own: where its level stands, and what that level is worth.
TEST_F(InspectPanelTest, ASymbolCardIsItsLevelExpStatAndForce) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_HERO);
  proto.set_job_stage(4);
  CharacterInstance hero(rng_, std::move(proto));

  Equip state;
  state.set_symbol_level(8);
  state.set_symbol_exp(12);
  EquipInstance symbol(VanishingJourneySymbol(), state);

  InspectPanel panel;
  panel.UseCharacter(hero);
  panel.SetItem(&symbol);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Growth Level  8"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("EXP  12 / 75"), std::string::npos) << rendered;
  // How far it has grown is not a stat it pays, so a rule splits the two.
  EXPECT_NE(LineAfter(rendered, "EXP  12 / 75").find("──"), std::string::npos)
      << rendered;
  EXPECT_NE(rendered.find("STR  +1000"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Arcane Force  +100"), std::string::npos) << rendered;
  // The head an equip carries, since a symbol has the same two facts to state.
  EXPECT_NE(rendered.find("Req Lev: 200"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Warrior"), std::string::npos) << rendered;
  // A symbol takes no scrolls and no star force, so neither row appears.
  EXPECT_EQ(rendered.find("Successful Scroll"), std::string::npos);
  EXPECT_EQ(rendered.find("★"), std::string::npos);
  EXPECT_EQ(rendered.find("☆"), std::string::npos);
  // In their place, a pip a level, filled to where the symbol stands.
  EXPECT_EQ(Count(rendered, "◆"), 8) << rendered;
  EXPECT_EQ(Count(rendered, "◇"), kMaxSymbolLevel - 8) << rendered;
}

// The stat a symbol grants is the wearer's own, so a magician reads INT off
// the same item a warrior reads STR off.
TEST_F(InspectPanelTest, TheSymbolStatFollowsTheWearer) {
  Character proto;
  proto.set_level(200);
  proto.set_job(JOB_BISHOP);
  proto.set_job_stage(4);
  CharacterInstance bishop(rng_, std::move(proto));

  EquipInstance symbol(VanishingJourneySymbol());
  InspectPanel panel;
  panel.UseCharacter(bishop);
  panel.SetItem(&symbol);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("INT  +300"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Arcane Force  +30"), std::string::npos) << rendered;
}

// A maxed symbol has no next level to be along, so the row says so rather than
// showing a bar that can never fill.
TEST_F(InspectPanelTest, AMaxedSymbolReadsMax) {
  Equip state;
  state.set_symbol_level(kMaxSymbolLevel);
  EquipInstance symbol(VanishingJourneySymbol(), state);
  InspectPanel panel;
  panel.UseCharacter(c_);
  panel.SetItem(&symbol);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("EXP  MAX"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Arcane Force  +220"), std::string::npos) << rendered;
  EXPECT_EQ(Count(rendered, "◆"), kMaxSymbolLevel) << rendered;
  EXPECT_EQ(Count(rendered, "◇"), 0) << rendered;
}

}  // namespace
}  // namespace ms
