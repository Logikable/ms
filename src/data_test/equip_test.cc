// Checks the shipped equip catalog rather than any one function. The model
// enforces whatever a prototype declares, which is exactly why the declaration
// itself needs pinning: an item added without it is an item the model happily
// lets the player upgrade.
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
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/item.h"
#include "src/item/projectile.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

std::map<std::string, EquipPrototype> LoadEquips() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<EquipPrototype>(runfiles->Rlocation("ms/data/equip"));
}

std::map<std::string, ItemPrototype> LoadItems() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<ItemPrototype>(runfiles->Rlocation("ms/data/items"));
}

// A projectile is ammunition, not a weapon a player invests in. Asserted over
// the whole catalog because the refusal has to be written on each one: nothing
// derives it from the slot, deliberately, since a later one may well differ.
// The name column an item list grows to on a wide terminal is chosen for the
// longest name the game ships -- a trace's " Trace" included, since a trace
// is drawn in the same lists. A longer one arriving has to move that number
// rather than sit cut on every screen.
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

// Three ladders, one shape: stars for the claw, arrows for the bow and for the
// crossbow. A rung missing from one is a branch that cannot re-arm where the
// others can, and a projectile whose type no weapon draws is attack a player
// wears and never fires.
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

// The refusal is the exception. A catalog where it spread to ordinary weapons
// would pass every check above and leave the player unable to upgrade anything.
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

// Every job the advancement picker can offer, gathered the way it gathers
// them: stage 1 from a Beginner, then the next stage of each job that gave.
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

// The job inspect screen tells a player what to go and buy, so no job may name
// a weapon of somebody else's branch, and every job's row must point at
// something buyable. A named type nothing ships yet -- the one-handed axe and
// blunt, which both warrior books name and no item is -- proves neither, and
// is skipped rather than failed. Levelled past every requirement, since what is
// under test is the job and not the tier.
TEST(EquipDataTest, EveryJobOnOfferNamesWeaponsOfItsOwnBranch) {
  std::map<std::string, EquipPrototype> equips = LoadEquips();
  std::vector<Job> offered = EveryOfferedJob();
  ASSERT_GE(offered.size(), 20u);  // 4 + 9 + 9 as the game stands
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

// A weapon type has one attack speed, and every weapon of it swings at that
// speed. GMS's own low-level items disagree among themselves -- the polearms
// range over three stages -- but by the level 150 tier, the one that matters,
// Nexon had settled each type on a single value. That value is what the
// catalog uses, all the way down.
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

// GMS gives none of these a slot or a star, and none of ours is near the level
// 200 tier where enhancement begins. Asserted over the catalog for the same
// reason the throwing stars are: each file has to say so itself.
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

// One per branch at every tier, and no branch left out. A missing one is a 2nd
// job with a level it cannot re-arm its off hand at. The two shelves are
// counted apart: what meso buys climbs in tiers, and what a token buys is the
// one Frozen piece.
TEST(EquipDataTest, EverySecondJobHasEveryTier) {
  const std::vector<int> kMesoTiers{30, 60, 100};
  const std::vector<int> kTokenTiers{120};
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
    EXPECT_EQ(token[advancement], kTokenTiers)
        << JobAdvancement_Name(advancement)
        << " has the wrong Frozen secondary";
  }
}

// The priced levels of one weapon type, low to high.
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

// Every weapon type climbs in steps of ten with no gap and no tier holding two
// of a kind. A branch that skips a tier is one the player outgrows their weapon
// on and cannot re-arm.
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

// Every ladder reaches the top of the shelf, so no branch is left a tier short
// of what the others can buy. Asked against the highest tier there is rather
// than against a level written here: the Frozen tier a token buys sits above
// all of these, and the meso ladders stopping short of it is the content gap,
// not a fault in one of them. The one-handed sword is the exception by design:
// the warrior takes two hands at their 2nd job, so it stops where the
// two-handed tiers start.
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

// Every weapon type the shop's ladder reaches the cap with has a Frozen one
// above it, so no branch is asked to farm tokens for a weapon it cannot hold.
// The one-handed sword is out for the reason its ladder stops: nobody swings
// one past their 2nd job.
TEST(EquipDataTest, EveryWeaponTypeHasAFrozenTier) {
  std::map<EquipType, int> frozen;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (proto.equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON &&
        proto.token_price() > 0) {
      EXPECT_EQ(frozen.count(proto.equip_type()), 0u)
          << entry.first << " is a second Frozen "
          << FormatEquipType(proto.equip_type());
      frozen[proto.equip_type()] = proto.required_level();
    }
  }
  for (const std::pair<const EquipType, std::vector<int>>& ladder :
       WeaponLadders()) {
    if (ladder.first == EQUIP_TYPE_ONE_HANDED_SWORD) {
      continue;
    }
    ASSERT_EQ(frozen.count(ladder.first), 1u)
        << FormatEquipType(ladder.first) << " has no Frozen tier";
    EXPECT_EQ(frozen[ladder.first], 120)
        << "the Frozen " << FormatEquipType(ladder.first)
        << " is worn at the wrong level";
  }
}

// The trophies. GMS lets no scroll and no star near any of them, so the refusal
// belongs to every item in those three slots rather than to the two written so
// far -- a badge added later that quietly took stars would read as a mistake
// nowhere.
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

// A price is only a price if something answers it. An item naming a token that
// no data file defines would sit on the shelf at a cost nobody can pay, and the
// loader would say nothing.
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
    EXPECT_FALSE(proto.has_shop_price())
        << entry.first << " is on both shelves at once";
  }
  EXPECT_GT(seen, 0) << "nothing in the catalog is bought with a token";
}

// One tier, one price. Every weapon a level opens costs the same, so the choice
// between branches is never a choice of what the player can afford -- and a
// mistyped price cannot hide among items nobody compares it with.
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

// GMS buys equipment back at a few percent of what it charges, rising with the
// tier; a flat tenth sits inside that band the whole way. Pinned over the
// catalog because the failure it guards against is an item that pays more than
// it costs, which is not a mispriced item but a meso printer.
TEST(EquipDataTest, StockedEquipsSellForATenthOfTheirPrice) {
  int seen = 0;
  for (const std::pair<const std::string, EquipPrototype>& entry :
       LoadEquips()) {
    const EquipPrototype& proto = entry.second;
    if (!proto.has_shop_price()) {
      // Not stocked, so there is no price to take a share of and the item
      // names its own. Most of them name nothing and sell for nothing -- that
      // is how a starter sword leaves the bag -- but a dropped piece is worth
      // what it is worth whether or not a shop ever sold one.
      continue;
    }
    ++seen;
    EXPECT_EQ(proto.sell_price(), proto.shop_price() / 10)
        << entry.first << " does not sell for a tenth of its price";
  }
  EXPECT_GT(seen, 0) << "no stocked equips in the catalog to check";
}

std::map<std::string, EquipSet> LoadSets() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<EquipSet>(runfiles->Rlocation("ms/data/sets"));
}

// A set names its pieces by display name, and a name that matches nothing is a
// piece that can never be worn toward the bonus -- silently, because counting
// what is worn cannot tell a misspelling from an item nobody has found yet.
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
      // Items or a family of them, never both and never neither: the inspect
      // screen prints one column from whichever it is, and a member that is
      // both would count a family toward the tiers.
      EXPECT_NE(member.items().name().empty(), member.family().empty())
          << entry.first << " has a member that is not items or one family";
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

// Tiers are read as "at least this many pieces", so one asking for more than
// the finished set will ever hold is a bonus nobody can reach, and one asking
// for none pays everybody. Both are data mistakes rather than states the model
// handles. Asked against the finished set rather than the members written so
// far: a set can pay at nine pieces while two of them exist.
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

// The wiki states a set twice: what each tier adds, which is what the data
// holds, and what the whole is worth once it is on. Pinned against the second
// column, because adding the first one up is exactly where a typo hides.
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
    // All four stats climb together, and magic attack shadows attack.
    EXPECT_EQ(effect.dex(), effect.str());
    EXPECT_EQ(effect.int_(), effect.str());
    EXPECT_EQ(effect.luk(), effect.str());
    EXPECT_EQ(effect.magic_attack(), effect.attack());
  }
  // The pools stop climbing at five pieces; the two damage levers arrive once
  // each, at seven and at nine.
  EXPECT_DOUBLE_EQ(set->tiers(1).effect().max_hp_pct(), 0.05);
  EXPECT_DOUBLE_EQ(set->tiers(2).effect().max_hp_pct(), 0.0);
  EXPECT_DOUBLE_EQ(set->tiers(2).effect().ied_pct(), 0.10);
  EXPECT_DOUBLE_EQ(set->tiers(3).effect().boss_pct(), 0.10);
}

// The Frozen set's own totals, pinned the same way and for the same reason:
// the data states what each tier ADDS, and the number a player sees is the
// running sum. Six tiers over eight pieces, so a typo in the middle of it is
// invisible in the file and loud here.
TEST(EquipDataTest, TheFrozenSetAddsUpToItsAgreedTotals) {
  const EquipSet* set = nullptr;
  std::map<std::string, EquipSet> sets = LoadSets();
  for (const std::pair<const std::string, EquipSet>& entry : sets) {
    if (entry.second.name() == EQUIP_SET_NAME_FROZEN) {
      set = &entry.second;
    }
  }
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->complete_pieces(), 8);
  ASSERT_EQ(set->tiers_size(), 6);
  const int kStat[] = {5, 5, 12, 12, 21, 21};
  const int kAttack[] = {3, 8, 15, 24, 35, 48};
  const double kPool[] = {0.0, 0.05, 0.05, 0.15, 0.15, 0.30};
  const double kDamage[] = {0.0, 0.0, 0.03, 0.03, 0.09, 0.09};
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
    // All four stats climb together, magic attack shadows attack, and MP
    // shadows HP.
    EXPECT_EQ(effect.dex(), effect.str());
    EXPECT_EQ(effect.int_(), effect.str());
    EXPECT_EQ(effect.luk(), effect.str());
    EXPECT_EQ(effect.magic_attack(), effect.attack());
    EXPECT_DOUBLE_EQ(effect.max_mp_pct(), effect.max_hp_pct());
  }
  // The two levers that arrive once each, at the end of the two halves.
  EXPECT_DOUBLE_EQ(set->tiers(3).effect().ied_pct(), 0.30);
  EXPECT_DOUBLE_EQ(set->tiers(5).effect().boss_pct(), 0.20);
}

// The levers the inspect screen's set card writes a row for. A tier that pulls
// one outside this list pays the player a bonus nothing tells them about, so
// the card and this list move together -- see InspectPanel::EffectLines.
const char* const kShownLevers[] = {
    "str",        "dex",          "int",           "luk",        "def",
    "attack",     "magic_attack", "attack_pct",    "max_hp_pct", "max_mp_pct",
    "damage_pct", "boss_pct",     "ied_pct",       "crit_rate",  "crit_dmg",
    "meso_pct",   "exp_pct",      "item_drop_pct",
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

// The accessories are boss rewards, and a boss is fought by everybody. One
// written for a branch would be a piece of the set that a whole class can
// never wear, and the set has no second route to that slot. The shoulderpad
// counts here too: GMS scrolls it with the armour, but it drops from a boss
// and belongs to the same set.
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
    EXPECT_EQ(proto.equip_job_categories(0), EQUIP_JOB_CATEGORY_UNIVERSAL)
        << entry.first << " is not worn by every job";
    EXPECT_GT(proto.upgrade_slots(), 0) << entry.first << " has no slots";
    EXPECT_TRUE(Supports(proto, UPGRADE_SCROLL)) << entry.first;
    EXPECT_TRUE(Supports(proto, UPGRADE_STAR_FORCE)) << entry.first;
  }
  EXPECT_GT(seen, 0) << "no accessories in the catalog to check";
}

// A slot or a type added without a display name shows up as a blank column in
// the bag, which reads as a broken item rather than a missing label.
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

// The six Arcane Symbols, one per Arcane River area. Each wears in a slot of
// its own -- which is what lets a character carry all six at once and no more
// than one of each -- and none of them is an item the upgrade paths touch.
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
    EXPECT_EQ(proto.sell_price(), 0) << entry.first << " sells for meso";
    EXPECT_EQ(proto.upgrade_slots(), 0) << entry.first;
    EXPECT_FALSE(Supports(proto, UPGRADE_SCROLL)) << entry.first;
    EXPECT_FALSE(Supports(proto, UPGRADE_STAR_FORCE)) << entry.first;
    EXPECT_TRUE(proto.base_stats().SerializeAsString().empty())
        << entry.first << " carries flat stats; a symbol's come from its level";
  }
  EXPECT_EQ(slots.size(), 6u) << "the six Arcane River areas are not all here";
  // The ladder GMS charges by area, 8 through 18: six areas, six prices, and a
  // repeat would mean two files were copied from one.
  EXPECT_EQ(costs.size(), 6u) << "two symbols level up at the same price";
}

// A stack has no stats, so its description is the whole of what inspecting one
// tells the player. Without it the card is a name over an empty box.
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
