#include "src/achievement.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/character/familiar.h"
#include "src/game_state.h"
#include "src/protos/achievement.pb.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/save.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

constexpr int64_t kDay = 24 * 60 * 60;
constexpr int64_t kCreated = 1'700'000'000;

Potential Lines(
    PotentialRank rank,
    std::vector<std::pair<PotentialLineType, PotentialRank>> lines) {
  Potential potential;
  potential.set_rank(rank);
  for (const auto& [type, line_rank] : lines) {
    PotentialLine* line = potential.add_lines();
    line->set_type(type);
    line->set_rank(line_rank);
  }
  return potential;
}

Equip Flamed(std::vector<std::pair<FlameStat, int>> lines) {
  Equip equip;
  for (const auto& [stat, tier] : lines) {
    FlameLine* line = equip.add_flame();
    line->set_stat(stat);
    line->set_tier(tier);
  }
  return equip;
}

constexpr PotentialRank kL = POTENTIAL_RANK_LEGENDARY;
constexpr PotentialRank kU = POTENTIAL_RANK_UNIQUE;

TEST(TriplePrimeTest, ThreeLegendaryLinesOfOneStat) {
  EXPECT_TRUE(IsTriplePrime(Lines(kL, {{POTENTIAL_LINE_TYPE_STR_PCT, kL},
                                       {POTENTIAL_LINE_TYPE_STR_PCT, kL},
                                       {POTENTIAL_LINE_TYPE_STR_PCT, kL}})));
  EXPECT_TRUE(
      IsTriplePrime(Lines(kL, {{POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT, kL},
                               {POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT, kL},
                               {POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT, kL}})))
      << "Max HP% on a bonus potential";
  EXPECT_TRUE(
      IsTriplePrime(Lines(kL, {{POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40, kL},
                               {POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35, kL},
                               {POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30, kL}})))
      << "the sizes of one effect are one stat";
  EXPECT_TRUE(
      IsTriplePrime(Lines(kL, {{POTENTIAL_LINE_TYPE_ALL_STATS_PCT, kL},
                               {POTENTIAL_LINE_TYPE_ALL_STATS_PCT, kL},
                               {POTENTIAL_LINE_TYPE_ALL_STATS_PCT, kL}})));
}

TEST(TriplePrimeTest, AllStatIsNotPartOfAnotherStat) {
  EXPECT_FALSE(
      IsTriplePrime(Lines(kL, {{POTENTIAL_LINE_TYPE_STR_PCT, kL},
                               {POTENTIAL_LINE_TYPE_STR_PCT, kL},
                               {POTENTIAL_LINE_TYPE_ALL_STATS_PCT, kL}})));
}

TEST(TriplePrimeTest, EveryLineMustBePrime) {
  EXPECT_FALSE(IsTriplePrime(Lines(kL, {{POTENTIAL_LINE_TYPE_STR_PCT, kL},
                                        {POTENTIAL_LINE_TYPE_STR_PCT, kL},
                                        {POTENTIAL_LINE_TYPE_STR_PCT, kU}})));
  EXPECT_FALSE(IsTriplePrime(Lines(kU, {{POTENTIAL_LINE_TYPE_STR_PCT, kU},
                                        {POTENTIAL_LINE_TYPE_STR_PCT, kU},
                                        {POTENTIAL_LINE_TYPE_STR_PCT, kU}})))
      << "a Unique potential's prime lines aren't Legendary";
  EXPECT_FALSE(IsTriplePrime(Potential())) << "no potential";
}

TEST(PerfectFlameTest, FourTopTierLinesUsefulToOneJob) {
  EXPECT_TRUE(IsPerfectFlame(Flamed({{FLAME_STAT_STR, 7},
                                     {FLAME_STAT_STR_DEX, 7},
                                     {FLAME_STAT_BOSS_DAMAGE, 7},
                                     {FLAME_STAT_ATTACK, 7}})));
  EXPECT_TRUE(IsPerfectFlame(Flamed({{FLAME_STAT_INT, 7},
                                     {FLAME_STAT_INT_LUK, 7},
                                     {FLAME_STAT_ALL_STAT, 7},
                                     {FLAME_STAT_MAGIC_ATTACK, 7}})));
}

TEST(PerfectFlameTest, NotWhenALineIsUselessOrBelowTheTopTier) {
  EXPECT_FALSE(IsPerfectFlame(Flamed({{FLAME_STAT_STR, 7},
                                      {FLAME_STAT_STR_DEX, 7},
                                      {FLAME_STAT_DAMAGE, 7},
                                      {FLAME_STAT_ATTACK, 6}})));
  EXPECT_FALSE(IsPerfectFlame(Flamed({{FLAME_STAT_STR, 7},
                                      {FLAME_STAT_DEX_LUK, 7},
                                      {FLAME_STAT_DAMAGE, 7},
                                      {FLAME_STAT_ATTACK, 7}})))
      << "a pair without STR";
  EXPECT_FALSE(IsPerfectFlame(Flamed({{FLAME_STAT_INT, 7},
                                      {FLAME_STAT_INT_LUK, 7},
                                      {FLAME_STAT_DAMAGE, 7},
                                      {FLAME_STAT_ATTACK, 7}})))
      << "ATT is no use to a mage";
  EXPECT_FALSE(IsPerfectFlame(Flamed({{FLAME_STAT_STR, 7},
                                      {FLAME_STAT_MAX_HP, 7},
                                      {FLAME_STAT_DAMAGE, 7},
                                      {FLAME_STAT_ATTACK, 7}})));
}

class AchievementTest : public testing::Test {
 protected:
  void SetUp() override {
    EquipPrototype arcane;
    arcane.set_name("Arcane Symbol: Arcana");
    arcane.mutable_arcane_symbol();
    EquipPrototype morass;
    morass.set_name("Arcane Symbol: Morass");
    morass.mutable_arcane_symbol();
    EquipPrototype sacred;
    sacred.set_name("Sacred Symbol: Cernium");
    sacred.mutable_sacred_symbol();
    EquipPrototype grand;
    grand.set_name("Grand Sacred Symbol: Tallahart");
    grand.mutable_sacred_symbol()->set_grand(true);
    Skill noblesse;
    noblesse.set_guild(GUILD_SKILL_NOBLESSE);
    noblesse.set_max_level(2);
    Boss boss;
    boss.add_difficulties()->set_name("Normal");
    boss.add_difficulties()->set_name("Hard");
    state_ = std::make_unique<GameState>(
        std::map<std::string, EquipPrototype>{{"arcana", arcane},
                                              {"morass", morass},
                                              {"cernium", sacred},
                                              {"tallahart", grand}},
        std::map<std::string, Scroll>{}, std::map<std::string, ItemPrototype>{},
        std::map<std::string, Mob>{}, std::map<std::string, MapData>{},
        std::map<std::string, Skill>{{"boss_slayers", noblesse}},
        GameMode::kPlay, TestOptions{}, std::nullopt,
        std::map<std::string, EquipSet>{},
        std::map<std::string, Boss>{{"lotus", boss}});
    state_->account.SetCreatedUnixSeconds(kCreated);
  }

  // A character beside the played one, which the account counts as much.
  Character& Alt() {
    return *state_->inactive_characters.emplace_back().mutable_character();
  }

  Equip& AltItem() {
    return *Alt().mutable_inventory()->add_equip_tab();
  }

  bool Met(Achievement achievement, int64_t now = kCreated) {
    std::vector<Achievement> met = MetAchievements(*state_, now);
    return std::find(met.begin(), met.end(), achievement) != met.end();
  }

  std::unique_ptr<GameState> state_;
};

TEST_F(AchievementTest, ANewAccountHasNone) {
  EXPECT_TRUE(MetAchievements(*state_, kCreated).empty());
}

TEST_F(AchievementTest, StarsOnAnyCharactersItem) {
  AltItem().set_stars(22);
  EXPECT_TRUE(Met(ACHIEVEMENT_STARS_17));
  EXPECT_TRUE(Met(ACHIEVEMENT_STARS_22));
  EXPECT_FALSE(Met(ACHIEVEMENT_STARS_25));
}

TEST_F(AchievementTest, PotentialAndFlameOnAnItem) {
  Equip& item = AltItem();
  *item.mutable_bonus_potential() =
      Lines(kL, {{POTENTIAL_LINE_TYPE_BONUS_LUK_PCT, kL},
                 {POTENTIAL_LINE_TYPE_BONUS_LUK_PCT, kL},
                 {POTENTIAL_LINE_TYPE_BONUS_LUK_PCT, kL}});
  EXPECT_TRUE(Met(ACHIEVEMENT_TRIPLE_PRIME)) << "bonus potential counts";
  EXPECT_FALSE(Met(ACHIEVEMENT_PERFECT_FLAME));
  for (FlameStat stat : {FLAME_STAT_LUK, FLAME_STAT_DEX_LUK, FLAME_STAT_DAMAGE,
                         FLAME_STAT_ATTACK}) {
    FlameLine* line = item.add_flame();
    line->set_stat(stat);
    line->set_tier(7);
  }
  EXPECT_TRUE(Met(ACHIEVEMENT_PERFECT_FLAME));
}

TEST_F(AchievementTest, SymbolsMaxedOnOneCharacterLeavingOutTheGrandOnes) {
  Character& alt = Alt();
  for (const char* name : {"Arcane Symbol: Arcana", "Arcane Symbol: Morass"}) {
    Equip* symbol = alt.mutable_inventory()->add_equip_tab();
    symbol->set_equip_name(name);
    symbol->set_symbol_level(20);
  }
  EXPECT_TRUE(Met(ACHIEVEMENT_ARCANE_SYMBOLS_MAXED));
  alt.mutable_inventory()->mutable_equip_tab(1)->set_symbol_level(19);
  EXPECT_FALSE(Met(ACHIEVEMENT_ARCANE_SYMBOLS_MAXED));

  Equip& sacred = AltItem();
  sacred.set_equip_name("Sacred Symbol: Cernium");
  sacred.set_symbol_level(11);
  EXPECT_TRUE(Met(ACHIEVEMENT_SACRED_SYMBOLS_MAXED))
      << "without the Grand Sacred Symbol";
}

TEST_F(AchievementTest, SymbolsSplitAcrossCharactersDontCount) {
  for (const char* name : {"Arcane Symbol: Arcana", "Arcane Symbol: Morass"}) {
    Equip& symbol = AltItem();
    symbol.set_equip_name(name);
    symbol.set_symbol_level(20);
  }
  EXPECT_FALSE(Met(ACHIEVEMENT_ARCANE_SYMBOLS_MAXED));
}

TEST_F(AchievementTest, EveryFamiliarAtLevelFourOnOneCharacter) {
  Character& alt = Alt();
  for (const FamiliarSpecies& species : FamiliarRoster()) {
    Familiar* familiar = alt.mutable_familiars()->add_familiars();
    familiar->set_name(species.name);
    familiar->set_level(kFamiliarMaxLevel);
  }
  EXPECT_TRUE(Met(ACHIEVEMENT_FAMILIARS_MAXED));
  alt.mutable_familiars()->mutable_familiars(0)->set_level(3);
  EXPECT_FALSE(Met(ACHIEVEMENT_FAMILIARS_MAXED));
}

TEST_F(AchievementTest, MesoHeldAcrossTheAccount) {
  Alt().set_meso(6'000'000'000);
  Alt().set_meso(4'000'000'000);
  EXPECT_TRUE(Met(ACHIEVEMENT_MESO_10B));
  EXPECT_FALSE(Met(ACHIEVEMENT_MESO_100B));
}

TEST_F(AchievementTest, LevelAndLegion) {
  for (int i = 0; i < 42; ++i) {
    Character& alt = Alt();
    alt.set_job(JOB_HERO);
    alt.set_level(i == 0 ? 300 : 190);
  }
  EXPECT_TRUE(Met(ACHIEVEMENT_LEVEL_300));
  // 300 + 41 x 190 = 8,090, and 7,999 with one at 99.
  EXPECT_TRUE(Met(ACHIEVEMENT_LEGION_8000));
  state_->inactive_characters.back().mutable_character()->set_level(99);
  EXPECT_FALSE(Met(ACHIEVEMENT_LEGION_8000));
}

TEST_F(AchievementTest, GuildSkillsNeedEveryNoblesseLevel) {
  state_->account.RecordProgress(110, 4);
  state_->account.RecordSoloClear("lotus", "Normal");
  EXPECT_FALSE(Met(ACHIEVEMENT_GUILD_SKILLS_MAXED));
  state_->account.RecordSoloClear("lotus", "Hard");
  EXPECT_TRUE(Met(ACHIEVEMENT_GUILD_SKILLS_MAXED));
}

TEST_F(AchievementTest, AccountAgeFromItsCreation) {
  EXPECT_FALSE(Met(ACHIEVEMENT_ACCOUNT_90_DAYS, kCreated + 90 * kDay - 1));
  EXPECT_TRUE(Met(ACHIEVEMENT_ACCOUNT_90_DAYS, kCreated + 90 * kDay));
  EXPECT_FALSE(Met(ACHIEVEMENT_ACCOUNT_365_DAYS, kCreated + 364 * kDay));
  EXPECT_FALSE(Met(ACHIEVEMENT_ACCOUNT_3_YEARS, kCreated + 1095 * kDay));
  EXPECT_TRUE(Met(ACHIEVEMENT_ACCOUNT_3_YEARS, kCreated + 1096 * kDay));
}

TEST_F(AchievementTest, CheckRecordsOnceAndKeepsWhatIsLost) {
  Equip& item = AltItem();
  item.set_stars(17);
  EXPECT_EQ(CheckAchievements(*state_, kCreated + 5),
            std::vector<Achievement>{ACHIEVEMENT_STARS_17});
  EXPECT_TRUE(CheckAchievements(*state_, kCreated + 9).empty());
  state_->inactive_characters.clear();
  EXPECT_TRUE(CheckAchievements(*state_, kCreated + 9).empty());
  ASSERT_EQ(state_->account.achievements().size(), 1);
  EXPECT_EQ(state_->account.achievements()[0].earned_unix_seconds(),
            kCreated + 5);
}

TEST(AchievementCatalogTest, EveryAchievementIsListedOnce) {
  std::vector<Achievement> listed;
  for (const AchievementInfo& info : AllAchievements()) {
    listed.push_back(info.achievement);
  }
  for (int number = Achievement_MIN + 1; number <= Achievement_MAX; ++number) {
    ASSERT_TRUE(Achievement_IsValid(number));
    EXPECT_EQ(std::count(listed.begin(), listed.end(),
                         static_cast<Achievement>(number)),
              1)
        << Achievement_Name(static_cast<Achievement>(number));
  }
  EXPECT_EQ(listed.size(), static_cast<size_t>(Achievement_MAX));
}

}  // namespace
}  // namespace ms
