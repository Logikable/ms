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
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/projectile.h"
#include "src/item/ring_box.h"
#include "src/item/tradeable.h"
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

// Projectiles are ammunition, not weapons a player invests in. Each file states
// the refusal itself rather than deriving it from the slot.
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

// Stars, arrows and bolts climb the same ladder. A missing level means a branch
// can't restock where the others can.
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
// another branch's weapon, and every row must point at something buyable.
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

// Each weapon type has one attack speed. GMS's low-level items disagree, but by
// the level 150 tier each type had settled on one value, which the catalog uses
// at every level.
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

// One off-hand per branch at every tier, meso and token shelves counted
// separately. A missing one is a level where a 2nd job can't replace theirs.
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
    ASSERT_TRUE(proto.has_shop_price() || !proto.token_prices().empty())
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

// Every ladder reaches the top meso tier. The one-handed sword stops where the
// two-handed tiers start, by design.
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

// Every weapon type whose meso ladder reaches the top has all four token tiers
// above it. The one-handed sword is excluded: nobody uses one past 2nd job.
TEST(EquipDataTest, EveryWeaponTypeHasEveryTokenTier) {
  // Level -> type -> the one weapon of that type a token buys at that level.
  std::map<int, std::map<EquipType, std::string>> token_tiers;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON ||
        proto.token_prices().empty()) {
      continue;
    }
    std::map<EquipType, std::string>& tier =
        token_tiers[proto.required_level()];
    EXPECT_EQ(tier.count(proto.equip_type()), 0u)
        << entry.first << " is a second " << FormatEquipType(proto.equip_type())
        << " at level " << proto.required_level();
    tier[proto.equip_type()] = entry.first;
  }
  // Frozen, then Root Abyss, then AbsoLab, then Arcane Umbra.
  const int kTokenTiers[] = {120, 150, 160, 200};
  for (int level : kTokenTiers) {
    ASSERT_GT(token_tiers.count(level), 0u)
        << "no token weapons at level " << level;
  }
  EXPECT_EQ(token_tiers.size(), std::size(kTokenTiers))
      << "a token weapon sits outside the four tiers";
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
    if (proto.token_prices().empty()) {
      continue;
    }
    ++seen;
    EXPECT_FALSE(proto.has_shop_price())
        << entry.first << " is on both shelves at once";
    std::set<std::string> named;
    for (const TokenPrice& price : proto.token_prices()) {
      EXPECT_GT(price.count(), 0) << entry.first << " names a token for 0";
      EXPECT_TRUE(named.insert(price.token_item()).second)
          << entry.first << " is priced twice in " << price.token_item();
      std::map<std::string, ItemPrototype>::const_iterator it =
          items.find(price.token_item());
      ASSERT_NE(it, items.end())
          << entry.first << " is bought with " << price.token_item()
          << ", which is not an item";
      EXPECT_FALSE(it->second.currency_mark().empty())
          << price.token_item() << " pays for " << entry.first
          << " without a mark to draw in the cost column";
      EXPECT_EQ(it->second.kind(), ITEM_KIND_TOKEN)
          << price.token_item() << " pays for " << entry.first
          << " without saying it is a token, so the bag files it as a drop";
    }
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

// GMS's Combat Power prices a weapon as the bow of its level, so every weapon
// needs one. The starting Sword is level 1, below the first bow, and keeps its
// own attack.
TEST(EquipDataTest, EveryWeaponHasABowOfItsLevel) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  FillBowAttack(equips);
  int weapons = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    const EquipPrototype& weapon = entry.second;
    if (weapon.equip_slot() != EQUIP_SLOT_PRIMARY_WEAPON ||
        weapon.name() == "Sword") {
      continue;
    }
    ++weapons;
    EXPECT_GT(weapon.bow_attack(), 0)
        << weapon.name() << " has no bow at level " << weapon.required_level();
  }
  EXPECT_GT(weapons, 100);
}

// Cygnus's token buys four shoulders, one per branch, and AbsoLab's and Will's
// coins buy the tiers above the same way. A missing branch gets nothing from
// the clear.
TEST(EquipDataTest, EveryBranchHasAShoulderAtEachTokenTier) {
  // The level each token's shoulder is worn at. This also confirms which tiers
  // exist, so a new one can't appear unnoticed.
  const std::map<std::string, int> kTiers = {{"cygnus_shoulder_token", 140},
                                             {"absolab_coin", 160},
                                             {"arachno_coin", 200}};
  // Token -> branch -> the one shoulder for that branch it buys.
  std::map<std::string, std::map<EquipJobCategory, std::string>> shoulders;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() != EQUIP_SLOT_SHOULDER ||
        proto.token_prices_size() != 1) {
      continue;
    }
    ASSERT_EQ(proto.equip_job_categories_size(), 1)
        << entry.first << " is a shoulder for more than one branch";
    const std::string& token = proto.token_prices(0).token_item();
    std::map<std::string, int>::const_iterator tier = kTiers.find(token);
    ASSERT_NE(tier, kTiers.end())
        << entry.first << " is bought with " << token
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

// A tier needing more pieces than the finished set holds is never reached, and
// one needing none pays everyone. Checked against the finished size, since a
// set can ship before all its pieces exist.
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

// GMS's level odds for each Boss Ring Box, and every ring and item a box
// offers exists at every level it can roll.
TEST(EquipDataTest, TheRingBoxesRollGmsLevelsOfRingsThatExist) {
  const std::map<std::string, std::map<int, double>> kOdds = {
      {"black_jade_boss_ring_box", {{1, 0.25}, {2, 0.25}, {3, 0.3}, {4, 0.2}}},
      {"white_jade_boss_ring_box", {{3, 0.65}, {4, 0.35}}},
      {"life_boss_ring_box", {{3, 0.3}, {4, 0.7}}},
  };
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::map<std::string, ItemPrototype> items = LoadItems();
  int boxes = 0;
  for (const std::pair<const std::string, ItemPrototype>& entry : items) {
    if (!entry.second.has_ring_box()) {
      continue;
    }
    ++boxes;
    const RingBox& box = entry.second.ring_box();
    ASSERT_TRUE(kOdds.count(entry.first)) << entry.first;
    std::map<int, double> odds;
    for (const RingBox::LevelChance& chance : box.levels()) {
      odds[chance.level()] = chance.chance();
    }
    EXPECT_EQ(odds, kOdds.at(entry.first)) << entry.first;
    EXPECT_EQ(box.skills_size(), 2) << entry.first;
    for (const std::string& skill : box.skills()) {
      for (const std::pair<const int, double>& level : odds) {
        EXPECT_NE(RingAt(skill, level.first, equips), nullptr)
            << entry.first << ": " << skill << " Lv. " << level.first;
      }
    }
    for (const std::string& item : box.items()) {
      EXPECT_TRUE(items.count(item)) << entry.first << ": " << item;
    }
  }
  EXPECT_EQ(boxes, 3);
  EXPECT_EQ(items.at("life_boss_ring_box").ring_box().items_size(), 1);
}

// A dropped_as can't spend more scrolls than the item has slots.
TEST(EquipDataTest, EveryDroppedStateFitsItsSlots) {
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    EXPECT_LE(proto.dropped_as().scroll_successes(), proto.upgrade_slots())
        << entry.first;
    EXPECT_TRUE(proto.dropped_as().equip_name().empty()) << entry.first;
  }
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

// Every class fights bosses, so an accessory made for one branch is a set piece
// a whole class can never wear. The Cygnus shoulders are one per branch by
// design.
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
    if (proto.token_prices().empty()) {
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
