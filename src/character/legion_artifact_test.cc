#include "src/character/legion_artifact.h"

#include <gtest/gtest.h>

#include "src/protos/character.pb.h"
#include "src/protos/legion.pb.h"

namespace ms {
namespace {

ArtifactCrystal Crystal(int level, ArtifactStat a, ArtifactStat b,
                        ArtifactStat c) {
  ArtifactCrystal crystal;
  crystal.set_level(level);
  crystal.add_stats(a);
  crystal.add_stats(b);
  crystal.add_stats(c);
  return crystal;
}

// Two level-5 crystals max a stat; a third adds nothing.
TEST(LegionArtifactTest, AStatSumsItsCrystalsUpToTen) {
  LegionArtifact artifact;
  for (int i = 0; i < 3; ++i) {
    *artifact.add_crystals() =
        Crystal(5, ARTIFACT_STAT_BOSS_DAMAGE, ARTIFACT_STAT_ATTACK,
                i == 0 ? ARTIFACT_STAT_EXP : ARTIFACT_STAT_CRIT_DAMAGE);
  }
  EXPECT_TRUE(ArtifactAllowed(artifact));
  EXPECT_EQ(ArtifactStatLevel(artifact, ARTIFACT_STAT_BOSS_DAMAGE), 10);
  EXPECT_EQ(ArtifactStatLevel(artifact, ARTIFACT_STAT_EXP), 5);
  const LegionExtraTotals totals = ArtifactTotalsFor(artifact);
  EXPECT_DOUBLE_EQ(totals.boss_pct, 0.15);
  EXPECT_EQ(totals.attack, 30);
  EXPECT_DOUBLE_EQ(totals.crit_dmg, 0.04);
  EXPECT_DOUBLE_EQ(totals.exp_pct, 0.06);
}

// The wiki's maxima: 150 all stats, 7,500 HP, 20% ignored defense, 12% drop.
TEST(LegionArtifactTest, MaxedStatsMatchTheWiki) {
  LegionArtifact artifact;
  *artifact.add_crystals() =
      Crystal(5, ARTIFACT_STAT_ALL_STATS, ARTIFACT_STAT_MAX_HP_MP,
              ARTIFACT_STAT_IGNORE_DEFENSE);
  *artifact.add_crystals() =
      Crystal(5, ARTIFACT_STAT_ALL_STATS, ARTIFACT_STAT_MAX_HP_MP,
              ARTIFACT_STAT_ITEM_DROP);
  *artifact.add_crystals() =
      Crystal(5, ARTIFACT_STAT_IGNORE_DEFENSE, ARTIFACT_STAT_ITEM_DROP,
              ARTIFACT_STAT_CRIT_RATE);
  const LegionExtraTotals totals = ArtifactTotalsFor(artifact);
  EXPECT_EQ(totals.all_stats, 150);
  EXPECT_EQ(totals.max_hp, 7500);
  EXPECT_DOUBLE_EQ(totals.ied, 0.20);
  EXPECT_DOUBLE_EQ(totals.item_drop_pct, 0.12);
  EXPECT_DOUBLE_EQ(totals.crit_rate, 0.10);
}

TEST(LegionArtifactTest, ACrystalHoldsThreeDifferentStats) {
  LegionArtifact artifact;
  *artifact.add_crystals() =
      Crystal(5, ARTIFACT_STAT_DAMAGE, ARTIFACT_STAT_DAMAGE, ARTIFACT_STAT_EXP);
  EXPECT_FALSE(ArtifactAllowed(artifact));
  artifact.mutable_crystals(0)->set_stats(1, ARTIFACT_STAT_MESO);
  EXPECT_TRUE(ArtifactAllowed(artifact));
  artifact.mutable_crystals(0)->set_level(6);
  EXPECT_FALSE(ArtifactAllowed(artifact));
}

LegionChampion Champion(Job job, ChampionRank rank) {
  LegionChampion champion;
  champion.set_job(job);
  champion.set_rank(rank);
  return champion;
}

// Four SSS Champions pay each of them 80 all stats, 40 ATT, 20% boss, 12%
// crit damage and 20% ignored defense; anyone else gets nothing.
TEST(LegionChampionTest, EveryRankPaysEveryChampion) {
  Legion legion;
  for (Job job : {JOB_HERO, JOB_BOW_MASTER, JOB_BISHOP, JOB_NIGHT_LORD}) {
    *legion.add_champions() = Champion(job, CHAMPION_RANK_SSS);
  }
  const LegionExtraTotals hero =
      ChampionTotalsFor(legion.champions(), JOB_HERO);
  EXPECT_EQ(hero.all_stats, 80);
  EXPECT_EQ(hero.max_hp, 4000);
  EXPECT_EQ(hero.attack, 40);
  EXPECT_DOUBLE_EQ(hero.boss_pct, 0.20);
  EXPECT_DOUBLE_EQ(hero.crit_dmg, 0.12);
  EXPECT_DOUBLE_EQ(hero.ied, 0.20);
  EXPECT_EQ(ChampionTotalsFor(legion.champions(), JOB_PALADIN).all_stats, 0);
  legion.mutable_champions(1)->set_rank(CHAMPION_RANK_A);
  const LegionExtraTotals lower =
      ChampionTotalsFor(legion.champions(), JOB_HERO);
  EXPECT_EQ(lower.attack, 40);
  EXPECT_DOUBLE_EQ(lower.boss_pct, 0.15);
}

}  // namespace
}  // namespace ms
