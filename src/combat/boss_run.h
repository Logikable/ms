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
#include <string_view>
#include <utility>
#include <vector>

#include "src/combat/boss_timing.h"
#include "src/combat/damage_breakdown.h"
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
// How long one strike of a stack shows before the next replaces it. One frame
// of a boss fight, so a twelve-slash swing flashes through all twelve.
inline constexpr double kDamageStrikeSeconds = 0.03;
// The most stacks held at once, so the list cannot grow without bound if one
// is drawn slower than the swings arrive. The oldest go first.
inline constexpr int kMaxDamageStacks = 64;
// The same for this player's own writes, filed a strike apiece: one swing
// across ten bars files a write per slash per bar.
inline constexpr int kMaxDamageWrites = 512;

// One number a landing left behind.
struct DamageNumber {
  int64_t damage = 0;
  bool crit = false;
};

// What one attack of one party member landed on one monster, in order. Read
// upwards, so a cramped corner costs the tail rather than the stack. The
// player at this screen has none: their numbers go row by row into the column
// above the monster -- see DamageWrite.
struct DamageStack {
  // The slot that took it, by the id a slot keeps for its whole life.
  int mob_id = 0;
  // Who landed it, as an index into the run's members. Never 0, which is the
  // player at this screen; everybody else's numbers are drawn dim.
  int owner = 0;
  // What did it. One monster holds at most one stack per player per source, a
  // landing replacing whatever that source last left there.
  DamageSource source;
  std::vector<DamageNumber> lines;
  // Where each strike begins in `lines`: one entry for a swing that landed
  // once, twelve for one that slashed twelve times. Never empty while `lines`
  // is not -- see StrikeAt, which is what a drawer asks.
  std::vector<int> strike_starts;
  // Seconds it has been on screen. Real ones: it is an animation, and the
  // game's pacing band has no business stretching it.
  double age = 0.0;
  // The half-open range of `lines` showing at `age`, one strike replacing the
  // last every kDamageStrikeSeconds and the last held to the end.
  std::pair<int, int> StrikeAt(double age) const;
  // The most lines any one strike landed. What the drawer reserves, so a
  // stack that flashes does not change shape under the reader.
  int TallestStrike() const;

  // Which side of the bar to try first, drawn once when the stack was made:
  // per frame, an unchanged stack would move every redraw.
  int preference = 0;
};

// One strike of the player's own damage, written into the rows above the
// monster: line i goes to row i, counting up from the bar. A write owns ROWS
// rather than a block, so a one-line attack after a fifteen-line one takes the
// bottom row and leaves the other fourteen standing.
struct DamageWrite {
  // The slot that took it, by the id a slot keeps for its whole life.
  int mob_id = 0;
  std::vector<DamageNumber> lines;
  // How long after the attack landed this strike shows: twelve slashes file
  // twelve writes at once, each kDamageStrikeSeconds behind the last.
  double delay = 0.0;
  // Seconds since the attack landed. Real ones: it is an animation, and the
  // game's pacing band has no business stretching it.
  double age = 0.0;

  // Seconds this write has been showing, negative until its strike is due.
  double showing() const {
    return age - delay;
  }
  // Whether it is on screen at all: due, and not yet faded.
  bool live() const {
    return showing() >= 0.0 && showing() < kDamageStackSeconds;
  }
};

// One row of the column above a monster. An unfilled row is one nobody has
// written to, or one whose number has faded, and holds nothing to draw.
struct DamageRow {
  bool filled = false;
  DamageNumber number;
};

// The numbers standing above `mob_id`, row 0 against the bar. Each row takes
// the newest live write that reached it, so a tall attack keeps its upper rows
// while shorter ones come and go beneath. As tall as the tallest live write; a
// caller with less room draws what fits.
std::vector<DamageRow> DamageColumn(const std::vector<DamageWrite>& writes,
                                    int mob_id);

// One line of what a clear paid: an item's display name and how many of it
// reached the bag.
struct BossRewardItem {
  std::string name;
  int64_t count = 0;
  // DropIsPrize: the gear, or the token that buys a piece of it. The card
  // lists what the player came for apart from what every clear pays.
  bool prize = false;
  // The rate the table dropped it at, before any drop rate. The card sorts on
  // it, so the rarest thing a clear paid is the top line of its group.
  double chance = 0.0;
};

// What a cleared fight paid: what LANDED, not what the table offers. A drop
// can miss its roll and a full bag can lose one that hit.
struct BossReward {
  int64_t meso = 0;
  int64_t exp = 0;
  // Paid only by a fight the calendar holds back. One a player can walk into
  // as often as they like is not a daily prize.
  int64_t honor = 0;
  std::vector<BossRewardItem> items;
};

// One monster's bar. Made when the phase starts and never reused: a dead
// monster's slot fades and then empties, so the bars beside it never move.
struct BossSlot {
  int id = 0;
  std::string name;
  // Where this one stands. It begins on the cell the phase spawned it on and
  // stays there, unless it is one of the few that walk: see walk.
  int x = 0;
  int y = 0;
  // How it wanders. An unset walk stands still, which is all of them but
  // Vellum, Papulatus, Damien and the Guardian Angel Slime.
  ArenaWalk walk;
  // Moves of that walk already behind it, which each new one is drawn off.
  // Kept so a move costs one move's work rather than a replay of thousands.
  int steps_taken = 0;
  // When the next move and dash fall due, in the run's seconds. Set from the
  // walk when the slot is made, so a monster arriving in a later phase walks
  // the clock the fight has already spent.
  double next_move_at = 0.0;
  double next_dash_at = 0.0;
  // Cells of a dash still to run, and which way it is going.
  int dash_left = 0;
  int dash_dx = 0;
  // A jump in the air: the row it left, and when it comes back down. The walk
  // is suspended until it does, so it always lands on the cell it left.
  bool airborne = false;
  int ground_y = 0;
  double next_jump_at = 0.0;
  double land_at = 0.0;
  double hp_fraction = 0.0;
  bool alive = true;
  // False once the dead bar's hold has run out. The slot stays in the list --
  // what it holds is a gap.
  bool visible = true;
  // Seconds since this one died, for the beat its empty bar is held for.
  double dead_for = 0.0;
};

// One player of a fight as the arena draws them: where they stand and what
// they are winding up. Somebody whose client has gone is not one.
struct FightMember {
  // Empty for the player at this screen, who is always the first of them.
  std::string name;
  // Which of the phase's player spots they stand on, or -1 in a phase that
  // named none.
  int spot = -1;
  std::string attack_name;
  double attack_fraction = 0.0;
  // How many of their buffs stand.
  int buff_count = 0;
};

// Which of `phase`'s player spots a press moves to. The nearest spot strictly
// that way wins, measured along the direction pressed and settled across it;
// one further across the arrow than along it is not that way at all. `from` is
// returned when nothing lies that way or two spots tie -- a press with no one
// answer moves nobody. Spots in `taken` are passed over entirely, the walk
// going on to whatever stands behind them.
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy,
                   const std::vector<int>& taken);
int NextPlayerSpot(const BossPhase& phase, int from, int dx, int dy);

class BossRun {
 public:
  // `boss` and `authority` are owned elsewhere and must outlive the run. An
  // invalid `difficulty_index` makes a run that is over before it starts, and
  // a null `authority` fights the boss alone -- deciding its own phases, its
  // own clock and what its monsters have left. `practice` is the fight taken
  // for nothing: it pays nothing, and the caller records no clear for it.
  BossRun(std::string boss_key, const Boss& boss, int difficulty_index,
          FightAuthority* authority = nullptr, bool practice = false);

  // Steps the run by elapsed_seconds of real time, paying the character for
  // whatever died. Does nothing once the run is finished.
  void Advance(GameState& state, double elapsed_seconds);
  // Gives up the run. The screen goes straight back rather than holding a
  // beat: the player asked to leave.
  void Abort();
  // Walks the player one spot in the direction pressed. Nothing once the
  // fight is over, or in a phase naming nowhere else to stand.
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
  // Whether this run is a practice run, which pays nothing and spends no
  // clear. The caller asks before writing one down.
  bool practice() const {
    return practice_;
  }
  // The boss without its difficulty: what the player is asked about on the
  // way out.
  const std::string& boss_name() const {
    return boss_name_;
  }
  // Which phase is being fought, counting from 1.
  int phase() const {
    return phase_ + 1;
  }
  // The track this phase names, or the last one a phase named: saying nothing
  // keeps playing what the one before started.
  std::string_view bgm() const;
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
  // This player's damage by skill, and the seconds it was dealt over: the
  // fighting ones, which leave out the count-in and the gaps between phases.
  const DamageBreakdown& breakdown() const {
    return breakdown_;
  }
  // Everyone's rows, this player's first and unnamed. The rest arrive with
  // the end of a party's fight, as each of them last reported.
  std::vector<PlayerBreakdown> breakdowns() const;
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
  // How long the run has been on screen, count-in included. For anything the
  // arena animates on its own rather than off the fight.
  double elapsed_seconds() const {
    return elapsed_seconds_;
  }
  // Every bar of the current phase, in the order they were spawned.
  const std::vector<BossSlot>& slots() const {
    return slots_;
  }
  // The party's damage numbers still on screen, oldest first. Never this
  // player's, whose numbers are written rows rather than stacks.
  const std::vector<DamageStack>& damage_stacks() const {
    return damage_stacks_;
  }
  // This player's own numbers still on screen, oldest first. Ask
  // DamageColumn what a monster's rows hold.
  const std::vector<DamageWrite>& damage_writes() const {
    return damage_writes_;
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
  // Where the player has WALKED to in this phase, and how many cells the
  // arena holds. The size is measured off the spots when the phase names
  // neither, which leaves no margin.
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
  // Ages the numbers on screen by dt, this player's and the party's both, and
  // drops the ones whose time is up.
  void AgeDamageNumbers(double dt);
  // Turns what this player's fight just landed into writes, one per strike
  // per monster.
  void CollectDamageWrites();
  // Puts `stack` on screen in place of whatever that member's source last left
  // on the same monster.
  void Replace(DamageStack stack);
  // Draws a bar per monster of a phase just started, each on the spot its
  // spawn named for it.
  void FillSlots(const CombatParams& params);
  // Rebuilds the bars from the fight's roster: what is still standing keeps
  // its bar, and what has gone starts fading in the slot it held.
  void SyncSlots(double dt);
  // Walks whatever walks to where the run's clock says it stands. Called after
  // the slots are in step with the roster, alone and in a shared fight both.
  void DriftSlots();
  // Walks, dashes and jumps `slot` up to `elapsed` seconds into the run, one
  // move at a time.
  void DriftSlot(const BossPhase& phase, BossSlot& slot, double elapsed);
  // When `slot`'s next move falls due: the next cell of a dash it is running,
  // or the sooner of its next step and its next dash.
  static double NextMoveAt(const BossSlot& slot);
  // Makes `slot`'s next move, whichever of the two it is due, and schedules
  // the one after it.
  void MoveSlot(const BossPhase& phase, BossSlot& slot);
  // Takes `slot` one step of its walk, to wherever the run's clock sends it.
  // Stands it still when its walk has nowhere to go.
  void StepSlot(const BossPhase& phase, BossSlot& slot);
  // Takes `slot` one cell along the dash it is running. Returns false when
  // the cell is not one it may enter, which ends the dash where it stands.
  bool DashSlot(const BossPhase& phase, BossSlot& slot);
  // When `slot`'s next jump event falls due -- its landing while it is up,
  // its next leap while it is down. kNeverMoves for a slot that never jumps.
  static double NextJumpAt(const BossSlot& slot);
  // Takes `slot` off its row or puts it back, and schedules the other half.
  static void JumpSlot(BossSlot& slot);
  // What is left of the phase, over what it holds when full.
  void ComputePhaseHp(const CombatParams& params);
  // Steps one phase of the fight forward, moving on when it empties.
  void RunPhase(GameState& state, double dt);
  // Pays the difficulty's table once for a cleared fight and records what
  // landed. The meso divides by share_count_ and the EXP is whole for
  // everyone; `awards` is what the drops came to.
  void PayReward(GameState& state, const std::vector<SharedAward>& awards);
  // The drops a fight taken alone pays: one roll each against this
  // character's own Item Drop Rate. A party's are dealt by the authority, so a
  // certain drop is certain and a one-off falls to exactly one person.
  std::vector<SharedAward> RollAwards(GameState& state,
                                      double item_drop_pct) const;
  // One step of a fight the party shares: take what the server has, swing
  // locally, report what that landed, and draw everyone else's.
  void AdvanceShared(GameState& state, double dt);
  // Takes the phase, the clock and where everyone is standing.
  void TakeShared(const SharedFight& shared);
  // Steps the local fight against the shared roster: this player's swings
  // land, the roster is clamped to the server's, and the rest is reported.
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
  bool practice_ = false;
  std::string boss_name_;
  int phases_ = 0;

  BossRunState state_ = BossRunState::kCountdown;
  int phase_ = 0;
  double countdown_left_ = kBossCountdownSeconds;
  double seconds_left_ = 0.0;
  double clear_seconds_ = 0.0;
  DamageBreakdown breakdown_;
  // The party's, from the authority, and which account is this player's.
  std::vector<PlayerBreakdown> shared_breakdowns_;
  std::string self_account_;
  double elapsed_seconds_ = 0.0;
  // Seconds left of whatever beat is being held: the gap between phases, or
  // the pause at the end before the screen goes back.
  double hold_left_ = 0.0;
  double phase_hp_fraction_ = 0.0;
  // The phase's damage table, and what it was built for. Nothing going into
  // it moves inside a phase, and rebuilding it every frame was 94% of what a
  // sim playing the dailies spends its time on. The LEVEL is watched as well
  // as the phase: a clear pays EXP while the last phase is still stepping.
  CombatParams params_;
  int params_phase_ = -1;
  int params_level_ = 0;
  // Which of the phase's player spots they stand on. -1 for a phase that named
  // none, whose player stands at the origin and never moves.
  int player_at_ = -1;
  std::vector<BossSlot> slots_;
  std::vector<DamageStack> damage_stacks_;
  std::vector<DamageWrite> damage_writes_;
  // The party's shared fight, or null for a boss taken alone.
  FightAuthority* authority_ = nullptr;
  std::vector<FightMember> members_;
  int share_count_ = 1;
  // Which slot each monster stands in, and the monster in each slot. An id is
  // this client's own; a SLOT is the same number on every client.
  std::map<int, int> slot_of_mob_;
  std::vector<int> mob_of_slot_;
  // Which member each of the shared fight's players is, since this player is
  // held first and the server holds them in party order.
  std::vector<int> member_of_player_;
  // What this run has landed since its last report. Rounded as the numbers on
  // screen are, so the roster loses what its players watched.
  std::vector<SharedLine> landed_;
  // Seconds until the next report goes out. 0 sends on the coming step, which
  // is what a fight and a new phase both open on.
  double report_due_ = 0.0;
  // The Drop preset's rate from the last step, which is what the party's
  // best-rate rule is told. Held for a clear declared on a step that computed
  // nothing.
  double item_drop_pct_ = 0.0;
  // Picks which side of a bar each stack asks for. Default-seeded, so a run
  // plays out the same way twice and a test can say where a stack went.
  std::mt19937 rng_;
  BossReward reward_;
  CombatSim sim_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_BOSS_RUN_H_
