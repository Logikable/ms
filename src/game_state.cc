#include "src/game_state.h"

#include <random>
#include <string>
#include <utility>

#include "src/character.h"
#include "src/protos/character.pb.h"

namespace ms {

namespace {

// The level-1 Beginner every character starts from, before any leveling.
Character MakeBaseBeginnerProto() {
  Character proto;
  proto.set_level(1);
  proto.set_job(JOB_BEGINNER);
  proto.set_ap(0);
  proto.mutable_allocated_stats()->set_str(13);
  proto.mutable_allocated_stats()->set_dex(4);
  proto.mutable_allocated_stats()->set_int_(4);
  proto.mutable_allocated_stats()->set_luk(4);
  proto.mutable_allocated_stats()->set_hp(50);
  proto.mutable_allocated_stats()->set_mp(15);
  return proto;
}

// The level a fresh character starts at, for testing. Level 30 is the top of
// the 1st-job band, so the whole 61-point SP pool is on hand -- enough to max
// every 1st-job skill and see what they do.
constexpr int kStartingLevel = 30;

// The job the starting character advances into at level 10. Archer for now, to
// exercise the new job -- there is no advancement UI, so this is the only way
// to reach anything but a Beginner. Swap it to test a different job.
constexpr Job kStartingJob = JOB_ARCHER;

// A first-job character to start with, so there is 1st-job SP (and AP) on hand
// to test skills. Built by running the real leveling and advancement mechanics
// rather than hardcoding totals, so AP and SP stay consistent with the level
// and job if either is changed here.
Character MakeStartingCharacterProto() {
  // LevelUp and AdvanceJob don't consume randomness; a local rng keeps this
  // independent of GameState's member rng and its construction order.
  std::mt19937 rng(0);
  CharacterInstance character(rng, MakeBaseBeginnerProto());
  while (character.proto().level() < kStartingLevel) {
    // Advance at the first level it is allowed, the way a real character would:
    // a level-up grants HP and MP at the job held at the time, so leaving the
    // advancement to the end would bank every level at the Beginner rate.
    // PendingJobAdvancement gates the WHEN (level 10); the job itself is
    // forced, since that path only offers Warrior and no UI chooses otherwise
    // yet.
    if (character.PendingJobAdvancement() != JOB_UNSPECIFIED) {
      character.AdvanceJob(kStartingJob);
    }
    character.LevelUp();
  }
  return character.proto();
}

}  // namespace

GameState::GameState(std::map<std::string, EquipPrototype> equips_arg,
                     std::map<std::string, Scroll> scrolls_arg,
                     std::map<std::string, ItemPrototype> items_arg,
                     std::map<std::string, Mob> mobs_arg,
                     std::map<std::string, MapData> maps_arg,
                     std::map<std::string, Skill> skills_arg)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      items(std::move(items_arg)),
      mobs(std::move(mobs_arg)),
      maps(std::move(maps_arg)),
      skills(std::move(skills_arg)),
      rng(std::random_device{}()),
      character(rng, MakeStartingCharacterProto()) {
}

}  // namespace ms
