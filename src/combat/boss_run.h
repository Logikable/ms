/* One run at one boss: the phases in order, the clock they are fought
 * against, and the beats between them.
 *
 * The fight itself is the same CombatSim the farming loop steps, so a boss is
 * hit with the same damage model, the same skill chooser and the same buffs --
 * what differs is the encounter it is handed. Nothing respawns, nothing hits
 * back, and it all runs in real time; see ComputeBossParams.
 *
 * A run holds no game state of its own. It reads the character out of the
 * GameState it is stepped with and pays them through the same reward path a
 * kill on a map goes through, so what a boss is worth cannot drift from what a
 * monster is worth.
 */
#ifndef MS_SRC_COMBAT_BOSS_RUN_H_
#define MS_SRC_COMBAT_BOSS_RUN_H_

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/combat/boss_timing.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/combat/fight_authority.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"

namespace ms {

// How long a dead monster's bar stays on screen. Its slot is not filled again
// -- the player watched it die there.
inline constexpr double kBossDeathHoldSeconds = 1.0;

// How long a stack of damage numbers stays on screen.
inline constexpr double kDamageStackSeconds = 0.9;
// The most stacks held at once. A generous ceiling on a phase of ten bars,
// there so a fight cannot grow the list without bound if one is ever drawn
// slower than the swings arrive. The oldest go first.
inline constexpr int kMaxDamageStacks = 64;

// One number a landing left behind.
struct DamageNumber {
  int64_t damage = 0;
  bool crit = false;
};

// One stack of numbers: what one attack landed on one monster, in the order
// the lines landed. A stack is drawn whole or not at all -- what a cramped
// corner costs is the rows furthest from the monster, not the stack.
struct DamageStack {
  // The slot that took it, by the id a slot keeps for its whole life.
  int mob_id = 0;
  // Who landed it, as an index into the run's members. 0 is the player at
  // this screen, who is always the first of them: everybody else's numbers
  // are drawn dim.
  int owner = 0;
  // What did it. One monster holds at most one stack per player per source: a
  // landing takes the place of whatever that source last left there, however
  // much life it had. So the character's swing is one stack that keeps being
  // rewritten, and a skill they switch to rewrites it too.
  DamageSource source;
  std::vector<DamageNumber> lines;
  // Seconds it has been on screen. Real ones: it is an animation, and the
  // game's pacing band has no business stretching it.
  double age = 0.0;
  // Which side of the bar the arena should try first, drawn when the stack was
  // made. Drawn once rather than per frame, or a stack that has not changed
  // would move every time it was redrawn. Unread for the swing, which always
  // stands over its monster.
  int preference = 0;
};

// One line of what a clear paid: an item's display name and how many of it
// reached the bag.
struct BossRewardItem {
  std::string name;
  int64_t count = 0;
  // DropIsPrize: the gear, or the token that buys a piece of it. The card
  // reads it, and lists what the player came for apart from what every clear
  // pays.
  bool prize = false;
  // The rate the table dropped it at, before any drop rate. The card sorts on
  // it, so the rarest thing a clear paid is the top line of its group.
  double chance = 0.0;
};

// What a cleared fight paid. What actually landed, not what the table offers:
// a drop can miss its roll, and a full bag loses one that hit, and the card
// the player reads should not claim either of them.
struct BossReward {
  int64_t meso = 0;
  int64_t exp = 0;
  // Paid only by a fight the calendar holds back. One a player can walk into
  // as often as they like is not a daily prize.
  int64_t honor = 0;
  std::vector<BossRewardItem> items;
};

// One monster's bar. A slot is made when the phase starts and never reused:
// once its monster is dead it fades and then leaves the space empty, so the
// bars beside it never move.
struct BossSlot {
  int id = 0;
  std::string name;
  // Where this one stands, from the phase that spawned it.
  int x = 0;
  int y = 0;
  double hp_fraction = 0.0;
  bool alive = true;
  // False once the dead bar's hold has run out. The slot stays in the list --
  // what it holds is a gap.
  bool visible = true;
  // Seconds since this one died, for the beat its empty bar is held for.
  double dead_for = 0.0;
};

// One player of a fight, as the arena draws them: where they stand and what
// they are winding up. A fight taken alone has one of these. Somebody whose
// client has gone is not one: the fight goes on without them, and so does the
// arena.
struct FightMember {
  // Empty for the player at this screen, who is always the first of them.
  std::string name;
  // Which of the phase's player spots they stand on, or -1 in a phase that
  // named none.
  int spot = -1;
  std::string attack_name;
  double attack_fraction = 0.0;
};

// Which of `phase`'s player spots a press moves to, as an index into
// `player_spots`. The nearest spot strictly that way wins, measured along the
// direction pressed; two the same distance along it are settled by whichever
// is nearer across it. A spot further across the arrow than along it is not
// that way at all and is passed over. `from` is returned when nothing lies
// that way, or when two spots are as good as each other -- a press with no one
// answer moves nobody.
//
// Spots in `taken` are passed over as though they were not there at all: a
// party member standing on one is not somewhere to walk to, and the walk goes
// on to whatever is behind them.
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy,
                   const std::vector<int>& taken);
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy);

class BossRun {
 public:
  // `boss` is owned by the GameState and must outlive the run. `difficulty` is
  // an index into its difficulties; an invalid one makes a run that is over
  // before it starts.
  //
  // `authority` is the party's shared fight, and must outlive the run too.
  // Null fights the boss alone, which is every run that decides its own
  // phases, its own clock and what its monsters have left.
  BossRun(std::string boss_key, const Boss& boss, int difficulty_index,
          FightAuthority* authority = nullptr);

  // Steps the run by elapsed_seconds of real time, paying the character for
  // whatever died. Does nothing once the run is finished.
  void Advance(GameState& state, double elapsed_seconds);
  // Gives up the run. The screen goes straight back rather than holding a
  // beat: the player asked to leave.
  void Abort();
  // Walks the player one spot in the direction pressed, which is one of the
  // four unit vectors. Does nothing once the fight is over, or in a phase that
  // named nowhere else to stand.
  void MovePlayer(int dx, int dy);

  BossRunState state() const {
    return state_;
  }
  // True once the run has finished AND its closing beat has been held: the
  // moment the screen should go back.
  bool done() const;
  // True for a run that cleared every phase, whether or not the beat is up.
  bool won() const {
    return state_ == BossRunState::kWon;
  }
  // What the fight is called: "Normal Zakum".
  const std::string& title() const {
    return title_;
  }
  // The boss alone, without its difficulty. What the player is asked about on
  // the way out: they are leaving Zakum, and which Zakum it was is not the
  // question.
  const std::string& boss_name() const {
    return boss_name_;
  }
  // Which phase is being fought, counting from 1.
  int phase() const {
    return phase_ + 1;
  }
  int phase_count() const {
    return phases_;
  }
  // What the fight paid. Empty until it is won.
  const BossReward& reward() const {
    return reward_;
  }
  // How long the clear took: the fight's clock from the end of the count-in
  // to the last monster falling, phase gaps included. 0 until it is won.
  double clear_seconds() const {
    return clear_seconds_;
  }
  // What is left of the current phase, over what it started with. Every
  // monster in the phase counts toward it, so eight arms at half HP reads 50%.
  double phase_hp_fraction() const {
    return phase_hp_fraction_;
  }
  // Seconds left on the fight's clock. Runs from the moment the countdown
  // ends, and stops when the fight does.
  double seconds_left() const {
    return seconds_left_;
  }
  // Seconds left of the pause before the fight starts. 0 once it has.
  double countdown_left() const {
    return countdown_left_;
  }
  // How long the run has been on screen, count-in included. A clock for
  // anything the arena animates on its own rather than off the fight -- a
  // name too long for its nameplate, sliding under it.
  double elapsed_seconds() const {
    return elapsed_seconds_;
  }
  // Every bar of the current phase, in the order they were spawned.
  const std::vector<BossSlot>& slots() const {
    return slots_;
  }
  // The damage numbers still on screen, oldest first.
  const std::vector<DamageStack>& damage_stacks() const {
    return damage_stacks_;
  }
  // Everyone fighting it, the player at this screen first. One member for a
  // fight taken alone.
  const std::vector<FightMember>& members() const {
    return members_;
  }
  // How many the reward is split between: everyone who was in the fight when
  // it began, and 1 for one taken alone.
  int share_count() const {
    return share_count_;
  }
  // Where the player stands in the current phase -- where they have walked
  // to, not where the phase started them -- and how many cells the arena
  // holds around everyone. The size is measured off the spots when the phase
  // names neither, which leaves the arena no margin.
  ArenaSpot player_spot() const;
  // Everywhere the player may stand this phase, the spot they are on
  // included. Empty for a phase that named none.
  std::vector<ArenaSpot> player_spots() const;
  int arena_width() const;
  int arena_height() const;
  // The swing being charged and how far along it is, for the player's bar.
  const std::string& attack_name() const {
    return sim_.view().attack_name;
  }
  double attack_fraction() const {
    return sim_.view().attack_fraction;
  }

 private:
  const BossDifficulty* difficulty() const;
  // The damage table for the phase being fought, rebuilt only when something
  // that feeds it has moved. See params_.
  const CombatParams& PhaseParams(const GameState& state);
  // The phase being fought, or null once the run is over.
  const BossPhase* current_phase() const;
  // Ages the stacks of numbers by dt and drops the ones whose time is up.
  void AgeDamageStacks(double dt);
  // Turns what the fight just landed into stacks, one per attack per monster.
  void CollectDamageStacks();
  // Puts `stack` on screen in place of whatever its source last left on the
  // same monster.
  void Replace(DamageStack stack);
  // Draws a bar per monster of a phase just started, each on the spot its
  // spawn named for it.
  void FillSlots(const CombatParams& params);
  // Rebuilds the bars from the fight's roster: what is still standing keeps
  // its bar, and what has gone starts fading in the slot it held.
  void SyncSlots(double dt);
  // What is left of the phase, over what it holds when full.
  void ComputePhaseHp(const CombatParams& params);
  // Steps one phase of the fight forward, moving on when it empties.
  void RunPhase(GameState& state, double dt);
  // Pays the difficulty's reward table, once, for a fight that was cleared,
  // and records what landed. The meso is divided by share_count_ and the EXP
  // is flat and whole for everyone; `awards` is what the drops came to.
  void PayReward(GameState& state, const std::vector<SharedAward>& awards);
  // The drops a fight taken alone pays: one roll each, against this
  // character's own Item Drop Rate the way a monster's are. A party's are
  // dealt by the authority instead, so that a certain drop is certain and a
  // one-off falls to exactly one person.
  std::vector<SharedAward> RollAwards(GameState& state,
                                      double item_drop_pct) const;
  // One step of a fight the party shares: take what the server has, swing
  // locally, report what that landed, and draw everyone else's.
  void AdvanceShared(GameState& state, double dt);
  // Takes the phase, the clock and where everyone is standing.
  void TakeShared(const SharedFight& shared);
  // Steps the local fight against the shared roster: this player's own swings
  // land, the roster is brought down to what the server says is left, and
  // what was landed is reported back.
  void RunSharedPhase(GameState& state, double dt, const SharedFight& shared);
  // Tells the party what this run has landed, on the wire's own beat rather
  // than the screen's, which is the faster of the two.
  void ReportToParty(double dt);
  // Draws what everybody else landed. A line naming a monster this client has
  // already buried is dropped: there is nowhere left to put it.
  void AddSharedStacks(const std::vector<SharedLine>& lines);
  // Steps a fight taken alone: its own count-in, its own phases, its own
  // clock.
  void RunAlone(GameState& state, double dt);
  // Stands this player at the front of the members, where a stack they landed
  // has owner 0.
  void StandSelf();
  // The spots somebody else is standing on, which a walk passes over.
  std::vector<int> TakenSpots() const;
  // Ends the run in `outcome`, holding the screen for the closing beat -- or
  // for nothing at all, if the run was given up.
  void Finish(BossRunState outcome);
  // Stands the player on the first of the phase's spots. Every phase is its
  // own arena, so where they walked to in the last one means nothing here.
  void StandPlayerAtStart();

  std::string boss_key_;
  const Boss* boss_ = nullptr;
  int difficulty_index_ = 0;
  std::string title_;
  std::string boss_name_;
  int phases_ = 0;

  BossRunState state_ = BossRunState::kCountdown;
  int phase_ = 0;
  double countdown_left_ = kBossCountdownSeconds;
  double seconds_left_ = 0.0;
  double clear_seconds_ = 0.0;
  double elapsed_seconds_ = 0.0;
  // Seconds left of whatever beat is being held: the gap between phases, or
  // the pause at the end before the screen goes back.
  double hold_left_ = 0.0;
  double phase_hp_fraction_ = 0.0;
  // The phase's damage table, and what it was built for. Nothing that goes
  // into it -- the character, their book, the monsters of the phase -- moves
  // inside a phase of a fight taken alone, and building it again every frame
  // was almost the whole cost of a fight: it is 94% of what a sim playing the
  // dailies out spends its time on, and sixty of them a second on the boss
  // screen. The level is watched as well as the phase, since a clear pays EXP
  // and the last phase is still being stepped when it lands.
  CombatParams params_;
  int params_phase_ = -1;
  int params_level_ = 0;
  // Which of the phase's player spots they stand on. -1 for a phase that named
  // none, whose player stands at the origin and never moves.
  int player_at_ = -1;
  std::vector<BossSlot> slots_;
  std::vector<DamageStack> damage_stacks_;
  // The party's shared fight, or null for a boss taken alone.
  FightAuthority* authority_ = nullptr;
  std::vector<FightMember> members_;
  int share_count_ = 1;
  // Which slot of the phase each monster stands in, and the monster in each
  // slot. A monster's id is this client's own; a slot is the same number on
  // every client, which is what damage is reported and read against.
  std::map<int, int> slot_of_mob_;
  std::vector<int> mob_of_slot_;
  // Which member each of the shared fight's players is, since this player is
  // held first and the server holds them in party order.
  std::vector<int> member_of_player_;
  // What this run has landed since its last report, in the shape the report
  // takes. Rounded as the numbers on screen are, so what the party's roster
  // loses is what its players watched.
  std::vector<SharedLine> landed_;
  // Seconds until the next report goes out. 0 sends on the coming step, which
  // is what a fight and a new phase both open on.
  double report_due_ = 0.0;
  // What the last step's params said drop rate was, for a clear that is
  // declared on a step this run computed nothing.
  double item_drop_pct_ = 0.0;
  // Picks which side of a bar each stack asks for. Default-seeded, so a run
  // plays out the same way twice and a test can say where a stack went.
  std::mt19937 rng_;
  BossReward reward_;
  CombatSim sim_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_RUN_H_
