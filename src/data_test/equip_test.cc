// Checks the shipped equip catalog as a whole. The model enforces whatever a
// prototype declares, which is exactly why the declarations need checking: an
// item added without one is an item the model lets the player upgrade.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "src/character/character.h"
#include "src/combat/damage.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/item.h"
#include "src/item/projectile.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

std::map<std::string, EquipPrototype> LoadEquips() {
  return LoadTestData<EquipPrototype>("equip");
}

std::map<std::string, ItemPrototype> LoadItems() {
  return LoadTestData<ItemPrototype>("items");
}

// The name column's width on a wide terminal is set for the longest name in the
// game, including a trace's " Trace" suffix. A longer name should change that
// number instead of being cut off on every screen.
TEST(EquipDataTest, EveryItemNameFitsTheWidestNameColumn) {
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    EXPECT_LE(TextColumns(entry.second.name() + " Trace"), kItemNameMax)
        << entry.first;
  }
  for (const std::pair<const std::string, ItemPrototype>& entry : LoadItems()) {
    EXPECT_LE(TextColumns(entry.second.name()), kItemNameMax) << entry.first;
  }
}

// Projectiles are ammunition, not weapons a player invests in. Checked across
// the whole catalog because each file states the refusal itself: it is
// deliberately not derived from the slot, since a later projectile might
// differ.
TEST(EquipDataTest, ProjectilesTakeNoUpgrades) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PROJECTILE) {
      continue;
    }
    ++seen;
    EXPECT_FALSE(Supports(proto, UPGRADE_SCROLL))
        << entry.first << " can be scrolled";
    EXPECT_FALSE(Supports(proto, UPGRADE_STAR_FORCE))
        << entry.first << " can be star forced";
    EXPECT_EQ(proto.upgrade_slots(), 0)
        << entry.first << " carries slots it will never spend";
  }
  EXPECT_GT(seen, 0) << "no projectiles in the catalog to check";
}

// Three ladders with the same shape: stars for claws, arrows for bows and bolts
// for crossbows. A missing level on one means a branch can't restock where the
// others can, and a projectile no weapon uses is attack the player wears but
// never fires.
TEST(EquipDataTest, EveryProjectileClimbsTheSameLadder) {
  const std::vector<int> kTiers{10, 30, 50, 70, 100};
  std::map<EquipType, std::vector<int>> ladders;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PROJECTILE) {
      continue;
    }
    EXPECT_NE(WeaponDrawing(proto.equip_type()), EQUIP_TYPE_UNSPECIFIED)
        << entry.first << " is ammunition no weapon draws";
    EXPECT_GT(proto.shop_price(), 0) << entry.first << " is not on the shelf";
    ladders[proto.equip_type()].push_back(proto.required_level());
  }
  EXPECT_EQ(ladders.size(), 3u) << "a projectile ladder is missing";
  for (std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    std::sort(ladder.second.begin(), ladder.second.end());
    EXPECT_EQ(ladder.second, kTiers)
        << FormatEquipType(ladder.first) << " has a hole in its ladder";
  }
}

// The refusal is the exception. If it spread to ordinary weapons, the catalog
// would pass every check above and the player couldn't upgrade anything.
TEST(EquipDataTest, OrdinaryWeaponsStillTakeUpgrades) {
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON) {
      continue;
    }
    EXPECT_TRUE(Supports(proto, UPGRADE_SCROLL))
        << entry.first << " cannot be scrolled";
    EXPECT_TRUE(Supports(proto, UPGRADE_STAR_FORCE))
        << entry.first << " cannot be star forced";
    EXPECT_GT(proto.upgrade_slots(), 0)
        << entry.first << " has no slots to scroll";
  }
}

// Every job the advancement picker can offer, collected the same way it does:
// stage 1 from a Beginner, then the next stage of each job found.
std::vector<Job> EveryOfferedJob() {
  std::vector<Job> jobs;
  std::vector<Job> frontier = {JOB_BEGINNER};
  for (int stage = 1; !frontier.empty(); ++stage) {
    std::vector<Job> next;
    for (Job job : frontier) {
      for (Job choice : JobChoicesForStage(job, stage)) {
        jobs.push_back(choice);
        next.push_back(choice);
      }
    }
    frontier = next;
  }
  return jobs;
}

// The job inspect screen tells the player what to buy, so no job may list
// another branch's weapon, and every row must point at something buyable. A
// type with no items yet can't prove either and is skipped. The character is
// levelled past every requirement, since the job is under test, not the tier.
TEST(EquipDataTest, EveryJobOnOfferNamesWeaponsOfItsOwnBranch) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::vector<Job> offered = EveryOfferedJob();
  ASSERT_GE(offered.size(), 20u);  // every branch at every stage
  for (Job job : offered) {
    std::vector<EquipType> weapons = ExpectedWeapons(job);
    EXPECT_FALSE(weapons.empty()) << Job_Name(job);
    std::set<EquipType> named(weapons.begin(), weapons.end());
    std::mt19937 rng(0);
    Character proto;
    proto.set_job(job);
    proto.set_level(200);
    CharacterInstance character(rng, std::move(proto));

    int holdable = 0;
    for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
      const EquipPrototype& p = entry.second;
      if (p.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON ||
          named.count(p.equip_type()) == 0) {
        continue;
      }
      EXPECT_TRUE(character.CanEquip(p))
          << Job_Name(job) << " names " << FormatEquipType(p.equip_type())
          << ", which it cannot hold: " << entry.first;
      ++holdable;
    }
    EXPECT_GT(holdable, 0) << Job_Name(job) << " names nothing that ships";
  }
}

// Each weapon type has one attack speed, and every weapon of that type uses it.
// GMS's own low-level items disagree (polearms span three stages), but by the
// level 150 tier, the one that matters, Nexon had settled each type on a single
// value. The catalog uses that value at every level.
TEST(EquipDataTest, AWeaponTypeHasOneAttackSpeed) {
  std::map<EquipType, std::pair<AttackSpeed, std::string>> speed_of_type;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_type() == EQUIP_TYPE_UNSPECIFIED ||
        proto.attack_speed() == ATTACK_SPEED_UNSPECIFIED) {
      continue;  // not a weapon, or ammunition that is never swung
    }
    std::map<EquipType, std::pair<AttackSpeed, std::string>>::iterator it =
        speed_of_type.find(proto.equip_type());
    if (it == speed_of_type.end()) {
      speed_of_type[proto.equip_type()] = {proto.attack_speed(), entry.first};
      continue;
    }
    EXPECT_EQ(proto.attack_speed(), it->second.first)
        << entry.first << " and " << it->second.second << " are both "
        << FormatEquipType(proto.equip_type())
        << " but swing at different speeds";
  }
}

// GMS gives these no upgrade slots or stars, and none of ours is near the level
// 200 tier where enhancement starts. Checked across the catalog for the same
// reason as throwing stars: each file has to state it.
TEST(EquipDataTest, SecondariesTakeNoUpgrades) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_SECONDARY) {
      continue;
    }
    ++seen;
    EXPECT_FALSE(Supports(proto, UPGRADE_SCROLL))
        << entry.first << " can be scrolled";
    EXPECT_FALSE(Supports(proto, UPGRADE_STAR_FORCE))
        << entry.first << " can be star forced";
    EXPECT_EQ(proto.upgrade_slots(), 0)
        << entry.first << " carries slots it will never spend";
  }
  EXPECT_GT(seen, 0) << "no secondaries in the catalog to check";
}

// One per branch at every tier, with no branch missing. A missing one is a
// level where a 2nd job can't replace their off-hand. The two shelves are
// counted separately: meso items climb in tiers, and token items are the Frozen
// piece and Princess No's above it.
TEST(EquipDataTest, EverySecondJobHasEveryTier) {
  const std::vector<int> kMesoTiers{30, 60, 100};
  const std::vector<int> kTokenTiers{120, 140};
  std::map<JobAdvancement, std::vector<int>> meso;
  std::map<JobAdvancement, std::vector<int>> token;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_SECONDARY) {
      continue;
    }
    JobAdvancement owner = AdvancementForSecondary(proto.equip_type());
    ASSERT_NE(owner, JOB_ADVANCEMENT_UNSPECIFIED)
        << entry.first << " is an off-hand nobody can hold";
    ASSERT_TRUE(proto.has_shop_price() || proto.token_price() > 0)
        << entry.first << " is an off-hand nothing buys";
    if (proto.has_shop_price()) {
      meso[owner].push_back(proto.required_level());
    } else {
      token[owner].push_back(proto.required_level());
    }
  }
  for (int i = JOB_ADVANCEMENT_FIGHTER; i <= JOB_ADVANCEMENT_BANDIT; ++i) {
    JobAdvancement advancement = static_cast<JobAdvancement>(i);
    std::vector<int>& own = meso[advancement];
    std::sort(own.begin(), own.end());
    EXPECT_EQ(own, kMesoTiers)
        << JobAdvancement_Name(advancement) << " has the wrong secondaries";
    std::vector<int>& bought = token[advancement];
    std::sort(bought.begin(), bought.end());
    EXPECT_EQ(bought, kTokenTiers) << JobAdvancement_Name(advancement)
                                   << " has the wrong token secondaries";
  }
}

// The priced levels of one weapon type, lowest first.
std::map<EquipType, std::vector<int>> WeaponLadders() {
  std::map<EquipType, std::vector<int>> ladders;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON ||
        !proto.has_shop_price()) {
      continue;
    }
    ladders[proto.equip_type()].push_back(proto.required_level());
  }
  for (std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    std::sort(ladder.second.begin(), ladder.second.end());
  }
  return ladders;
}

// Every weapon type climbs in steps of ten with no gaps and no tier with two of
// the same type. A branch that skips a tier is one whose weapon the player
// outgrows with nothing to replace it.
TEST(EquipDataTest, EveryWeaponTypeClimbsInTens) {
  std::map<EquipType, std::vector<int>> ladders = WeaponLadders();
  ASSERT_FALSE(ladders.empty());
  for (const std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    std::vector<int> expected;
    for (int level = ladder.second.front(); level <= ladder.second.back();
         level += 10) {
      expected.push_back(level);
    }
    EXPECT_EQ(ladder.second, expected)
        << FormatEquipType(ladder.first) << " has a hole in its ladder";
  }
}

// Every ladder reaches the top meso tier, so no branch is a tier behind what
// the others can buy. Checked against the highest tier: meso ladders stopping
// below the Frozen tier is a content gap, not a bug. The one-handed sword stops
// where the two-handed tiers start, by design.
TEST(EquipDataTest, EveryWeaponTypeReachesTheTopMesoTier) {
  std::map<EquipType, std::vector<int>> ladders = WeaponLadders();
  ASSERT_FALSE(ladders.empty());
  int top = 0;
  for (const std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    top = std::max(top, ladder.second.back());
  }
  for (const std::pair<const EquipType, std::vector<int>>& ladder : ladders) {
    int expected = ladder.first == EQUIP_TYPE_ONE_HANDED_SWORD ? 30 : top;
    EXPECT_EQ(ladder.second.back(), expected)
        << FormatEquipType(ladder.first) << " stops at the wrong tier";
  }
}

// Every weapon type whose meso ladder reaches the top has all three token tiers
// above it, so no branch has to farm tokens for a weapon it can't use. The
// one-handed sword is excluded for the same reason its ladder stops: nobody
// uses one past their 2nd job.
TEST(EquipDataTest, EveryWeaponTypeHasEveryTokenTier) {
  // Level -> type -> the one weapon of that type a token buys at that level.
  std::map<int, std::map<EquipType, std::string>> token_tiers;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON ||
        proto.token_price() <= 0) {
      continue;
    }
    std::map<EquipType, std::string>& tier =
        token_tiers[proto.required_level()];
    EXPECT_EQ(tier.count(proto.equip_type()), 0u)
        << entry.first << " is a second " << FormatEquipType(proto.equip_type())
        << " at level " << proto.required_level();
    tier[proto.equip_type()] = entry.first;
  }
  // Frozen, then Root Abyss, then AbsoLab.
  const int kTokenTiers[] = {120, 150, 160};
  for (int level : kTokenTiers) {
    ASSERT_GT(token_tiers.count(level), 0u)
        << "no token weapons at level " << level;
  }
  EXPECT_EQ(token_tiers.size(), std::size(kTokenTiers))
      << "a token weapon sits outside the three tiers";
  for (const std::pair<const EquipType, std::vector<int>>& ladder :
       WeaponLadders()) {
    if (ladder.first == EQUIP_TYPE_ONE_HANDED_SWORD) {
      continue;
    }
    for (int level : kTokenTiers) {
      EXPECT_EQ(token_tiers[level].count(ladder.first), 1u)
          << FormatEquipType(ladder.first) << " has no level " << level
          << " token tier";
    }
  }
}

// The trophy slots. GMS allows no scrolls or stars on any of them, so every
// item in those three slots must refuse upgrades. A badge added later that
// quietly took stars wouldn't look wrong anywhere else.
TEST(EquipDataTest, NoTrophyTakesAnUpgrade) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_BADGE &&
        proto.equip_slot() != EQUIP_SLOT_EMBLEM &&
        proto.equip_slot() != EQUIP_SLOT_MEDAL) {
      continue;
    }
    ++seen;
    EXPECT_FALSE(Supports(proto, UPGRADE_SCROLL)) << entry.first;
    EXPECT_FALSE(Supports(proto, UPGRADE_STAR_FORCE)) << entry.first;
    EXPECT_EQ(proto.upgrade_slots(), 0) << entry.first;
  }
  EXPECT_GT(seen, 0) << "no trophies in the catalog to check";
}

// A price only works if the currency exists. An item priced in a token no data
// file defines would sit on the shelf at a price nobody can pay, and the loader
// wouldn't report it.
TEST(EquipDataTest, EveryTokenPriceNamesATokenThatExists) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.token_price() <= 0 && proto.token_item().empty()) {
      continue;
    }
    ++seen;
    EXPECT_GT(proto.token_price(), 0) << entry.first << " names a token for 0";
    std::map<std::string, ItemPrototype>::const_iterator it =
        items.find(proto.token_item());
    ASSERT_NE(it, items.end())
        << entry.first << " is bought with " << proto.token_item()
        << ", which is not an item";
    EXPECT_FALSE(it->second.currency_mark().empty())
        << proto.token_item() << " pays for " << entry.first
        << " without a mark to draw in the cost column";
    EXPECT_EQ(it->second.kind(), ITEM_KIND_TOKEN)
        << proto.token_item() << " pays for " << entry.first
        << " without saying it is a token, so the bag files it as a drop";
    EXPECT_FALSE(proto.has_shop_price())
        << entry.first << " is on both shelves at once";
  }
  EXPECT_GT(seen, 0) << "nothing in the catalog is bought with a token";
}

// Every token must buy something, or FillTokenShelves gives it no shelf and the
// bag sorts it last.
TEST(EquipDataTest, EveryTokenBuysSomething) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  FillTokenShelves(LoadEquips(), items);
  int tokens = 0;
  for (const std::pair<const std::string, ItemPrototype>& entry : items) {
    if (entry.second.kind() == ITEM_KIND_TOKEN) {
      ++tokens;
      EXPECT_GT(entry.second.currency_level(), 0)
          << entry.first << " is a token nothing in the catalog is sold for";
    }
  }
  EXPECT_GT(tokens, 0);
}

// The token shelf's per-branch shoulders. Cygnus drops one token that four
// shoulders are priced in, one per branch, and AbsoLab's coin buys the tier
// above it the same way. A missing branch would get nothing from the clear, and
// a second shoulder for a branch would be a choice between two identical items.
TEST(EquipDataTest, EveryBranchHasAShoulderAtEachTokenTier) {
  // The level each token's shoulder is worn at. This also confirms which tiers
  // exist, so a third one can't appear unnoticed.
  const std::map<std::string, int> kTiers = {{"cygnus_shoulder_token", 140},
                                             {"absolab_coin", 160}};
  // Token -> branch -> the one shoulder for that branch it buys.
  std::map<std::string, std::map<EquipJobCategory, std::string>> shoulders;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_SHOULDER || proto.token_price() <= 0) {
      continue;
    }
    ASSERT_EQ(proto.equip_job_categories_size(), 1)
        << entry.first << " is a shoulder for more than one branch";
    std::map<std::string, int>::const_iterator tier =
        kTiers.find(proto.token_item());
    ASSERT_NE(tier, kTiers.end())
        << entry.first << " is bought with " << proto.token_item()
        << ", which is a shoulder tier nothing here knows about";
    EXPECT_EQ(proto.required_level(), tier->second) << entry.first;
    EquipJobCategory branch = proto.equip_job_categories(0);
    std::map<EquipJobCategory, std::string>& worn = shoulders[tier->first];
    EXPECT_TRUE(worn.emplace(branch, entry.first).second)
        << entry.first << " and " << worn[branch] << " are both the "
        << tier->first << " shoulder for " << EquipJobCategory_Name(branch);
  }
  const std::vector<EquipJobCategory> kBranches = {
      EQUIP_JOB_CATEGORY_WARRIOR, EQUIP_JOB_CATEGORY_MAGICIAN,
      EQUIP_JOB_CATEGORY_BOWMAN, EQUIP_JOB_CATEGORY_THIEF};
  EXPECT_EQ(shoulders.size(), kTiers.size());
  for (const std::pair<const std::string,
                       std::map<EquipJobCategory, std::string>>& tier :
       shoulders) {
    for (EquipJobCategory branch : kBranches) {
      EXPECT_EQ(tier.second.count(branch), 1u)
          << EquipJobCategory_Name(branch) << " has no " << tier.first
          << " shoulder";
    }
    EXPECT_EQ(tier.second.size(), kBranches.size()) << tier.first;
  }
}

// One tier, one price. Every weapon at a level costs the same, so choosing a
// branch is never a question of what the player can afford, and a mistyped
// price can't hide among items nobody compares.
TEST(EquipDataTest, ATierHasOnePrice) {
  std::map<std::pair<EquipSlot, int>, std::pair<int, std::string>> price_of;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (!proto.has_shop_price()) {
      continue;
    }
    std::pair<EquipSlot, int> tier{proto.equip_slot(), proto.required_level()};
    if (price_of.find(tier) == price_of.end()) {
      price_of[tier] = {proto.shop_price(), entry.first};
      continue;
    }
    EXPECT_EQ(proto.shop_price(), price_of[tier].first)
        << entry.first << " and " << price_of[tier].second
        << " share a tier but not a price";
  }
}

// GMS buys equipment back at a few percent of its price, rising with the tier;
// a flat tenth stays within that range at every tier. The danger is an item
// that sells for more than it costs, which would be a meso printer, so a
// stocked item can't set its own sell price at all and SellPrice computes the
// tenth.
TEST(EquipDataTest, StockedEquipsSellForATenthOfTheirPrice) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (!proto.has_shop_price()) {
      // Not stocked, so there is no price to take a share of and the item sets
      // its own. Most set nothing and sell for nothing (that is how a starter
      // sword leaves the bag), but a dropped item is worth what it's worth
      // whether or not a shop ever sold one.
      continue;
    }
    ++seen;
    EXPECT_FALSE(proto.has_sell_price())
        << entry.first << " pins a sell price the shelf already decides";
    EXPECT_EQ(SellPrice(proto), proto.shop_price() / 10)
        << entry.first << " does not sell for a tenth of its price";
  }
  EXPECT_GT(seen, 0) << "no stocked equips in the catalog to check";
}

// A token is earned, not bought, and the item it trades for is the whole point
// of earning it. Giving either a sell price would let players turn the token
// into meso, which is exactly what the shelf must not allow.
TEST(EquipDataTest, TokenGearAndItsTokensSellForNothing) {
  std::map<std::string, ItemPrototype> items = LoadItems();
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.token_item().empty()) {
      continue;
    }
    ++seen;
    EXPECT_EQ(SellPrice(proto), 0) << entry.first << " sells for meso";
    ASSERT_GT(items.count(proto.token_item()), 0u) << proto.token_item();
    EXPECT_EQ(items.at(proto.token_item()).sell_price(), 0)
        << proto.token_item() << " sells for meso";
  }
  EXPECT_GT(seen, 0) << "no token-traded gear in the catalog to check";
}

std::map<std::string, EquipSet> LoadSets() {
  return LoadTestData<EquipSet>("sets");
}

// A set names its pieces by display name, and a name that matches nothing is a
// piece that never counts toward the bonus. It fails silently, since counting
// worn pieces can't tell a misspelling from an item nobody has found yet.
TEST(EquipDataTest, EverySetMemberIsAnItemThatExists) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  int checked = 0;
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    EXPECT_FALSE(FormatEquipSet(entry.second.name()).empty())
        << entry.first << " is an unnamed set";
    std::set<EquipSlot> slots;
    for (const EquipSetMember& member : entry.second.members()) {
      EXPECT_FALSE(FormatSlot(member.slot()).empty())
          << entry.first << " has a member in an unnamed slot";
      EXPECT_TRUE(slots.insert(member.slot()).second)
          << entry.first << " fills " << FormatSlot(member.slot()) << " twice";
      // Named items, a family, or both, but never neither, which would be a
      // slot nothing can fill.
      EXPECT_FALSE(member.items().name().empty() && member.family().empty())
          << entry.first << " has a member naming no piece at all";
      if (member.has_family()) {
        ++checked;
        bool found = false;
        for (const std::pair<const std::string, EquipPrototype>& equip :
             equips) {
          if (equip.second.set_family() == member.family()) {
            found = true;
            break;
          }
        }
        EXPECT_TRUE(found) << entry.first << " asks for \"" << member.family()
                           << "\", which no equip file belongs to";
      }
      for (const std::string& fills : member.items().name()) {
        ++checked;
        bool found = false;
        for (const std::pair<const std::string, EquipPrototype>& equip :
             equips) {
          if (equip.second.name() == fills) {
            found = true;
            break;
          }
        }
        EXPECT_TRUE(found) << entry.first << " counts \"" << fills
                           << "\", which no equip file defines";
      }
    }
  }
  EXPECT_GT(checked, 0) << "no sets in the catalog to check";
}

// Tiers mean "at least this many pieces", so a tier needing more than the
// finished set will hold can never be reached, and one needing none pays
// everyone. Both are data mistakes, not states the model handles. Checked
// against the finished set size, not the members listed, since a set can ship
// before all its pieces exist.
TEST(EquipDataTest, EverySetTierIsReachable) {
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    int complete = entry.second.complete_pieces();
    EXPECT_GE(complete, entry.second.members_size())
        << entry.first << " spans fewer slots than it already names";
    for (const EquipSetTier& tier : entry.second.tiers()) {
      EXPECT_GT(tier.pieces(), 1)
          << entry.first << " pays a tier for wearing one piece";
      EXPECT_LE(tier.pieces(), complete)
          << entry.first << " has a tier past the pieces the set will hold";
    }
  }
}

// The wiki lists a set twice: what each tier adds, which is what the data
// holds, and the total once all of it is worn. Checked against the totals,
// because adding up the per-tier column is where typos hide.
TEST(EquipDataTest, TheBossAccessorySetAddsUpToItsWikiTotals) {
  const EquipSet* set = nullptr;
  std::map<std::string, EquipSet> sets = LoadSets();
  for (const std::pair<const std::string, EquipSet>& entry : sets) {
    if (entry.second.name() == EQUIP_SET_NAME_BOSS_ACCESSORY) {
      set = &entry.second;
    }
  }
  ASSERT_NE(set, nullptr);
  const int kStat[] = {10, 20, 30, 45};
  const int kAttack[] = {5, 10, 20, 30};
  const int kDef[] = {60, 120, 200, 300};
  ASSERT_EQ(set->tiers_size(), 4);
  int stat = 0;
  int attack = 0;
  int def = 0;
  for (int i = 0; i < set->tiers_size(); ++i) {
    const SkillEffect& effect = set->tiers(i).effect();
    stat += effect.str();
    attack += effect.attack();
    def += effect.def();
    EXPECT_EQ(stat, kStat[i]) << "at " << set->tiers(i).pieces() << " pieces";
    EXPECT_EQ(attack, kAttack[i]) << "at " << set->tiers(i).pieces();
    EXPECT_EQ(def, kDef[i]) << "at " << set->tiers(i).pieces();
    // All four stats rise together, and magic attack matches attack.
    EXPECT_EQ(effect.dex(), effect.str());
    EXPECT_EQ(effect.int_(), effect.str());
    EXPECT_EQ(effect.luk(), effect.str());
    EXPECT_EQ(effect.magic_attack(), effect.attack());
  }
  // HP and MP stop rising at five pieces; the two damage bonuses each come
  // once, at seven and at nine.
  EXPECT_DOUBLE_EQ(set->tiers(1).effect().max_hp_pct(), 0.05);
  EXPECT_DOUBLE_EQ(set->tiers(2).effect().max_hp_pct(), 0.0);
  EXPECT_DOUBLE_EQ(set->tiers(2).effect().ied_pct(), 0.10);
  EXPECT_DOUBLE_EQ(set->tiers(3).effect().boss_pct(), 0.10);
}

// The Sengoku Treasure Set's totals, checked the same way for the same reason.
// It is a set a player never assembles piece by piece: Princess No drops all
// three pieces in one clear, so the 3-piece tier is what it is worth in
// practice.
TEST(EquipDataTest, TheSengokuTreasureSetAddsUpToItsWikiTotals) {
  const EquipSet* set = nullptr;
  std::map<std::string, EquipSet> sets = LoadSets();
  for (const std::pair<const std::string, EquipSet>& entry : sets) {
    if (entry.second.name() == EQUIP_SET_NAME_SENGOKU_TREASURE) {
      set = &entry.second;
    }
  }
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->complete_pieces(), 3);
  ASSERT_EQ(set->members_size(), 3);
  ASSERT_EQ(set->tiers_size(), 2);
  const int kStat[] = {2, 10};
  const int kAttack[] = {3, 15};
  const int kDef[] = {20, 100};
  const double kDamage[] = {0.03, 0.09};
  int stat = 0;
  int attack = 0;
  int def = 0;
  double damage = 0.0;
  for (int i = 0; i < set->tiers_size(); ++i) {
    const SkillEffect& effect = set->tiers(i).effect();
    EXPECT_EQ(set->tiers(i).pieces(), i + 2);
    stat += effect.str();
    attack += effect.attack();
    def += effect.def();
    damage += effect.damage_pct();
    EXPECT_EQ(stat, kStat[i]) << "at " << set->tiers(i).pieces() << " pieces";
    EXPECT_EQ(attack, kAttack[i]) << "at " << set->tiers(i).pieces();
    EXPECT_EQ(def, kDef[i]) << "at " << set->tiers(i).pieces();
    EXPECT_DOUBLE_EQ(damage, kDamage[i]) << "at " << set->tiers(i).pieces();
    // All four stats rise together, and magic attack matches attack.
    EXPECT_EQ(effect.dex(), effect.str());
    EXPECT_EQ(effect.int_(), effect.str());
    EXPECT_EQ(effect.luk(), effect.str());
    EXPECT_EQ(effect.magic_attack(), effect.attack());
  }
}

// The Frozen set's totals, checked the same way for the same reason: the data
// states what each tier adds, and the player sees the running total. Read from
// the wiki's totals column, since a typo in the middle of the per-tier column
// is invisible.
TEST(EquipDataTest, TheFrozenSetAddsUpToItsWikiTotals) {
  const EquipSet* set = nullptr;
  std::map<std::string, EquipSet> sets = LoadSets();
  for (const std::pair<const std::string, EquipSet>& entry : sets) {
    if (entry.second.name() == EQUIP_SET_NAME_FROZEN) {
      set = &entry.second;
    }
  }
  ASSERT_NE(set, nullptr);
  // Eight slots here against GMS's five, so the whole bonus arrives at five
  // pieces and the three tiers above that add nothing. See the textproto.
  ASSERT_EQ(set->complete_pieces(), 8);
  ASSERT_EQ(set->tiers_size(), 3);
  const int kStat[] = {7, 7, 15};
  const int kAttack[] = {6, 20, 40};
  const double kPool[] = {0.0, 0.20, 0.20};
  const double kDamage[] = {0.0, 0.09, 0.09};
  int stat = 0;
  int attack = 0;
  double pool = 0.0;
  double damage = 0.0;
  for (int i = 0; i < set->tiers_size(); ++i) {
    const SkillEffect& effect = set->tiers(i).effect();
    EXPECT_EQ(set->tiers(i).pieces(), i + 3);
    stat += effect.str();
    attack += effect.attack();
    pool += effect.max_hp_pct();
    damage += effect.damage_pct();
    EXPECT_EQ(stat, kStat[i]) << "at " << set->tiers(i).pieces() << " pieces";
    EXPECT_EQ(attack, kAttack[i]) << "at " << set->tiers(i).pieces();
    EXPECT_DOUBLE_EQ(pool, kPool[i]) << "at " << set->tiers(i).pieces();
    EXPECT_DOUBLE_EQ(damage, kDamage[i]) << "at " << set->tiers(i).pieces();
    // All four stats rise together, magic attack matches attack, and MP matches
    // HP.
    EXPECT_EQ(effect.dex(), effect.str());
    EXPECT_EQ(effect.int_(), effect.str());
    EXPECT_EQ(effect.luk(), effect.str());
    EXPECT_EQ(effect.magic_attack(), effect.attack());
    EXPECT_DOUBLE_EQ(effect.max_mp_pct(), effect.max_hp_pct());
  }
  // The one bonus that comes once, with the last tier. GMS gives the Frozen set
  // no boss damage at all; that starts with Root Abyss below.
  EXPECT_DOUBLE_EQ(set->tiers(2).effect().ied_pct(), 0.30);
  for (const EquipSetTier& tier : set->tiers()) {
    EXPECT_DOUBLE_EQ(tier.effect().boss_pct(), 0.0);
  }
}

// The Dawn Boss Set's totals, checked the same way for the same reason. Only
// one of its four slots has an item today, so none of its tiers can be reached
// yet, which is exactly why the numbers need a test instead of a playtest.
TEST(EquipDataTest, TheDawnBossSetAddsUpToItsWikiTotals) {
  const EquipSet* set = nullptr;
  std::map<std::string, EquipSet> sets = LoadSets();
  for (const std::pair<const std::string, EquipSet>& entry : sets) {
    if (entry.second.name() == EQUIP_SET_NAME_DAWN_BOSS) {
      set = &entry.second;
    }
  }
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->complete_pieces(), 4);
  ASSERT_EQ(set->members_size(), 1);
  ASSERT_EQ(set->tiers_size(), 3);
  const int kStat[] = {10, 20, 30};
  const int kAttack[] = {10, 20, 30};
  const int kPool[] = {250, 500, 750};
  int stat = 0;
  int attack = 0;
  int pool = 0;
  for (int i = 0; i < set->tiers_size(); ++i) {
    const SkillEffect& effect = set->tiers(i).effect();
    EXPECT_EQ(set->tiers(i).pieces(), i + 2);
    stat += effect.str();
    attack += effect.attack();
    pool += effect.max_hp();
    EXPECT_EQ(stat, kStat[i]) << "at " << set->tiers(i).pieces() << " pieces";
    EXPECT_EQ(attack, kAttack[i]) << "at " << set->tiers(i).pieces();
    EXPECT_EQ(pool, kPool[i]) << "at " << set->tiers(i).pieces();
    // All four stats rise together, and magic attack matches attack.
    EXPECT_EQ(effect.dex(), effect.str());
    EXPECT_EQ(effect.int_(), effect.str());
    EXPECT_EQ(effect.luk(), effect.str());
    EXPECT_EQ(effect.magic_attack(), effect.attack());
    // MP is the one thing it doesn't give, unlike the Boss Accessory Set.
    EXPECT_EQ(effect.max_mp(), 0) << "at " << set->tiers(i).pieces();
  }
  // Boss damage comes at two pieces and defence at four, once each.
  EXPECT_DOUBLE_EQ(set->tiers(0).effect().boss_pct(), 0.10);
  EXPECT_DOUBLE_EQ(set->tiers(1).effect().boss_pct(), 0.0);
  EXPECT_EQ(set->tiers(2).effect().def(), 100);
  EXPECT_DOUBLE_EQ(set->tiers(2).effect().ied_pct(), 0.10);
}

// The Guardian Angel Ring is in two sets at once, which no other item is. GMS
// sells a scroll that converts it from one set to the other and this game has
// no such mechanism, so it counts for both. Checked because a set counting a
// piece twice is the kind of thing a later edit does by accident.
TEST(EquipDataTest, TheGuardianAngelRingFillsASlotOfTwoSets) {
  std::set<EquipSetName> holding;
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    for (const EquipSetMember& member : entry.second.members()) {
      for (const std::string& fills : member.items().name()) {
        if (fills == "Guardian Angel Ring") {
          EXPECT_EQ(member.slot(), EQUIP_SLOT_RING) << entry.first;
          holding.insert(entry.second.name());
        }
      }
    }
  }
  EXPECT_EQ(holding, std::set<EquipSetName>({EQUIP_SET_NAME_BOSS_ACCESSORY,
                                             EQUIP_SET_NAME_DAWN_BOSS}));
}

// The four Root Abyss sets are one set written once per branch, so their
// bonuses must match piece for piece; one class getting a weaker bonus would be
// a typo nothing else catches. Totals, not per-tier additions, for the same
// reason as the sets above.
TEST(EquipDataTest, EveryRootAbyssSetAddsUpToTheSameTotals) {
  const std::set<EquipSetName> kBranches = {
      EQUIP_SET_NAME_ROOT_ABYSS_WARRIOR, EQUIP_SET_NAME_ROOT_ABYSS_BOWMAN,
      EQUIP_SET_NAME_ROOT_ABYSS_MAGICIAN, EQUIP_SET_NAME_ROOT_ABYSS_THIEF};
  std::set<EquipSetName> seen;
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    const EquipSet& set = entry.second;
    if (kBranches.count(set.name()) == 0) {
      continue;
    }
    seen.insert(set.name());
    ASSERT_EQ(set.complete_pieces(), 4) << entry.first;
    ASSERT_EQ(set.tiers_size(), 3) << entry.first;
    // Two of the four stats, and which two depends on the branch, so they are
    // summed instead of named here.
    int stat = 0;
    int attack = 0;
    for (const EquipSetTier& tier : set.tiers()) {
      const SkillEffect& effect = tier.effect();
      stat += effect.str() + effect.dex() + effect.int_() + effect.luk();
      attack += effect.attack() + effect.magic_attack();
    }
    EXPECT_EQ(stat, 40) << entry.first;
    EXPECT_EQ(attack, 50) << entry.first;
    EXPECT_EQ(set.tiers(0).pieces(), 2) << entry.first;
    EXPECT_EQ(set.tiers(0).effect().max_hp(), 1000) << entry.first;
    EXPECT_EQ(set.tiers(0).effect().max_mp(), 1000) << entry.first;
    EXPECT_EQ(set.tiers(1).pieces(), 3) << entry.first;
    EXPECT_DOUBLE_EQ(set.tiers(1).effect().max_hp_pct(), 0.10) << entry.first;
    EXPECT_DOUBLE_EQ(set.tiers(1).effect().max_mp_pct(), 0.10) << entry.first;
    EXPECT_EQ(set.tiers(2).pieces(), 4) << entry.first;
    EXPECT_DOUBLE_EQ(set.tiers(2).effect().boss_pct(), 0.30) << entry.first;
  }
  EXPECT_EQ(seen, kBranches) << "a branch has no Root Abyss set";
}

// The four AbsoLab sets, checked the same way for the same reason. Totals, not
// per-tier additions, and all are GMS's own: GMS's set covers seven slots where
// this one covers eight, because the Armor and the Pants are one overall there,
// so the tiers are spread differently but end at the same total.
TEST(EquipDataTest, EveryAbsoLabSetAddsUpToTheSameTotals) {
  const std::set<EquipSetName> kBranches = {
      EQUIP_SET_NAME_ABSOLAB_WARRIOR, EQUIP_SET_NAME_ABSOLAB_BOWMAN,
      EQUIP_SET_NAME_ABSOLAB_MAGICIAN, EQUIP_SET_NAME_ABSOLAB_THIEF};
  std::set<EquipSetName> seen;
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    const EquipSet& set = entry.second;
    if (kBranches.count(set.name()) == 0) {
      continue;
    }
    seen.insert(set.name());
    ASSERT_EQ(set.complete_pieces(), 8) << entry.first;
    ASSERT_EQ(set.members_size(), 8) << entry.first;
    ASSERT_EQ(set.tiers_size(), 7) << entry.first;
    int stat = 0;
    int attack = 0;
    int def = 0;
    int pool = 0;
    double pool_pct = 0.0;
    double boss = 0.0;
    // Ignored defence is the one bonus a set gives twice, and two shares of it
    // multiply instead of adding, as the character's own does.
    double ied = 0.0;
    for (int i = 0; i < set.tiers_size(); ++i) {
      const SkillEffect& effect = set.tiers(i).effect();
      EXPECT_EQ(set.tiers(i).pieces(), i + 2) << entry.first;
      stat += effect.str();
      attack += effect.attack();
      def += effect.def();
      pool += effect.max_hp();
      pool_pct += effect.max_hp_pct();
      boss += effect.boss_pct();
      ied = CombineIgnoredDefense(ied, effect.ied_pct());
      // All four stats rise together, magic attack matches attack, and MP
      // matches HP.
      EXPECT_EQ(effect.dex(), effect.str()) << entry.first;
      EXPECT_EQ(effect.int_(), effect.str()) << entry.first;
      EXPECT_EQ(effect.luk(), effect.str()) << entry.first;
      EXPECT_EQ(effect.magic_attack(), effect.attack()) << entry.first;
      EXPECT_EQ(effect.max_mp(), effect.max_hp()) << entry.first;
      EXPECT_DOUBLE_EQ(effect.max_mp_pct(), effect.max_hp_pct()) << entry.first;
    }
    EXPECT_EQ(stat, 30) << entry.first;
    EXPECT_EQ(attack, 135) << entry.first;
    EXPECT_EQ(def, 200) << entry.first;
    EXPECT_EQ(pool, 1500) << entry.first;
    EXPECT_DOUBLE_EQ(pool_pct, 0.20) << entry.first;
    EXPECT_DOUBLE_EQ(boss, 0.30) << entry.first;
    EXPECT_DOUBLE_EQ(ied, 0.19) << entry.first;
  }
  EXPECT_EQ(seen, kBranches) << "a branch has no AbsoLab set";
}

// The bonuses the inspect screen's set card shows a row for. A tier granting
// one outside this list gives the player a bonus nothing tells them about, so
// the card and this list must change together; see InspectPanel::EffectLines.
const char* const kShownLevers[] = {
    "str",        "dex",          "int",        "luk",      "def",
    "attack",     "magic_attack", "attack_pct", "max_hp",   "max_mp",
    "max_hp_pct", "max_mp_pct",   "damage_pct", "boss_pct", "ied_pct",
    "crit_rate",  "crit_dmg",     "meso_pct",   "exp_pct",  "item_drop_pct",
};

TEST(EquipDataTest, EverySetTierLeverHasARowOnTheInspectScreen) {
  std::set<std::string> shown(std::begin(kShownLevers), std::end(kShownLevers));
  int checked = 0;
  for (const std::pair<const std::string, EquipSet>& entry : LoadSets()) {
    for (const EquipSetTier& tier : entry.second.tiers()) {
      std::vector<const google::protobuf::FieldDescriptor*> fields;
      tier.effect().GetReflection()->ListFields(tier.effect(), &fields);
      EXPECT_FALSE(fields.empty())
          << entry.first << " has a tier that pays nothing";
      for (const google::protobuf::FieldDescriptor* field : fields) {
        ++checked;
        EXPECT_TRUE(shown.count(std::string(field->name())) > 0)
            << entry.first << " pays " << field->name()
            << ", which the inspect screen has no row for";
      }
    }
  }
  EXPECT_GT(checked, 0) << "no set tiers in the catalog to check";
}

// Accessories are boss rewards and every class fights bosses, so one made for a
// single branch would be a set piece a whole class can never wear. The
// shoulderpad counts: it drops from a boss and belongs to the same set. The
// Cygnus shoulders are one per branch by design, not a gap.
TEST(EquipDataTest, AccessoriesAreUniversalAndUpgradeable) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_FACE_ACCESSORY &&
        proto.equip_slot() != EQUIP_SLOT_EYE_ACCESSORY &&
        proto.equip_slot() != EQUIP_SLOT_SHOULDER) {
      continue;
    }
    ++seen;
    ASSERT_EQ(proto.equip_job_categories_size(), 1) << entry.first;
    if (proto.token_price() <= 0) {
      EXPECT_EQ(proto.equip_job_categories(0), EQUIP_JOB_CATEGORY_UNIVERSAL)
          << entry.first << " is not worn by every job";
    }
    EXPECT_GT(proto.upgrade_slots(), 0) << entry.first << " has no slots";
    EXPECT_TRUE(Supports(proto, UPGRADE_SCROLL)) << entry.first;
    EXPECT_TRUE(Supports(proto, UPGRADE_STAR_FORCE)) << entry.first;
  }
  EXPECT_GT(seen, 0) << "no accessories in the catalog to check";
}

// A slot or type added without a display name shows as a blank column in the
// bag, which looks like a broken item, not a missing label.
TEST(EquipDataTest, EveryItemsSlotAndTypeHaveNames) {
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    EXPECT_FALSE(FormatSlot(proto.equip_slot()).empty())
        << entry.first << " wears in an unnamed slot";
    if (proto.equip_type() != EQUIP_TYPE_UNSPECIFIED) {
      EXPECT_FALSE(FormatEquipType(proto.equip_type()).empty())
          << entry.first << " is an unnamed kind of item";
    }
  }
}

// The six Arcane Symbols, one per Arcane River area. Each goes in its own slot,
// which lets a character wear all six but no more than one of each, and no
// upgrade path applies to any of them.
TEST(EquipDataTest, EverySymbolIsUniversalAndWearsItsOwnSlot) {
  std::set<EquipSlot> slots;
  std::set<int> costs;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (!proto.has_arcane_symbol()) {
      continue;
    }
    EXPECT_TRUE(slots.insert(proto.equip_slot()).second)
        << entry.first << " shares a slot with another symbol";
    costs.insert(proto.arcane_symbol().meso_cost_base());
    EXPECT_EQ(proto.required_level(), 200) << entry.first;
    EXPECT_EQ(proto.equip_job_categories_size(), 1) << entry.first;
    EXPECT_EQ(proto.equip_job_categories(0), EQUIP_JOB_CATEGORY_UNIVERSAL)
        << entry.first << " is not open to every job";
    EXPECT_EQ(SellPrice(proto), 0) << entry.first << " sells for meso";
    EXPECT_EQ(proto.upgrade_slots(), 0) << entry.first;
    EXPECT_FALSE(Supports(proto, UPGRADE_SCROLL)) << entry.first;
    EXPECT_FALSE(Supports(proto, UPGRADE_STAR_FORCE)) << entry.first;
    EXPECT_TRUE(proto.base_stats().SerializeAsString().empty())
        << entry.first << " carries flat stats; a symbol's come from its level";
  }
  EXPECT_EQ(slots.size(), 6u) << "the six Arcane River areas are not all here";
  // GMS's per-area cost ladder, 8 through 18: six areas, six prices. A repeat
  // would mean two files were copied from one.
  EXPECT_EQ(costs.size(), 6u) << "two symbols level up at the same price";
}

// A stack has no stats, so its description is all that inspecting it shows the
// player. Without one, the card is a name over an empty box.
TEST(EquipDataTest, EveryStackableDescribesItself) {
  int checked = 0;
  for (const std::pair<const std::string, ItemPrototype>& entry : LoadItems()) {
    ++checked;
    EXPECT_FALSE(entry.second.description().empty())
        << entry.first << " has nothing to say about itself";
  }
  EXPECT_GT(checked, 0) << "no stackables in the catalog to check";
}

}  // namespace
}  // namespace ms
