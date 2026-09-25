#include "src/multiplayer/session.h"

#include <chrono>
#include <map>
#include <string>
#include <utility>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/util/message_differencer.h"
#include "src/character/character.h"
#include "src/character/link.h"
#include "src/game_state.h"
#include "src/multiplayer/client.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

Character PublicSheet(const CharacterInstance& character) {
  // ToProto instead of proto(): worn equipment lives in a C++ container and is
  // only written into the message when the whole thing is requested.
  Character sheet = character.ToProto();
  // A party member sees what they could tell by watching: stats, equipment, and
  // the passives and points behind both. Not the bag or purse, and not Honor,
  // which rises with every kill and would send a sheet per kill. EXP rises as
  // fast, but the EXP bar needs it.
  sheet.clear_inventory();
  sheet.clear_stacks();
  sheet.clear_buy_backs();
  sheet.clear_pinned_scrolls();
  sheet.clear_boss_clears();
  sheet.clear_meso();
  sheet.clear_honor();
  return sheet;
}

PlayerInfo PlayerFor(const GameState& state) {
  PlayerInfo player;
  player.set_account_id(state.account.multiplayer_account_id());
  player.set_name(state.character.username());
  player.set_level(state.character.proto().level());
  player.set_job(AdvancementForJobStage(state.character.proto().job(),
                                        state.character.proto().job_stage()));
  *player.mutable_sheet() = PublicSheet(state.character);
  // Kept next to the sheet, not in it: the server checks these before letting a
  // party fight a boss with a reset timer, and PublicSheet strips them.
  *player.mutable_boss_clears() = state.character.proto().boss_clears();
  // The owner's setting, not the reader's: the owner has already decided which
  // allocation their sheet shows.
  player.set_autoswap_presets(state.account.autoswap_presets());
  // Their account's progress, for the same reason: the link skills on the sheet
  // are levelled by characters this message doesn't include.
  for (const std::pair<const Job, int>& line :
       state.character.link_tally().best_by_line()) {
    (*player.mutable_link_lines())[line.first] = line.second;
  }
  // Their boss screen settings. The server requires the whole party to share
  // one set before it starts a fight.
  *player.mutable_boss_options() = state.boss_options;
  return player;
}

MultiplayerSession::MultiplayerSession(std::string host, int port)
    : client_(std::move(host), port) {
}

void MultiplayerSession::Start(GameState& state) {
  if (started_) {
    return;
  }
  started_ = true;
  told_ = PlayerFor(state);
  client_.Start(told_, state.account.multiplayer_token());
}

void MultiplayerSession::Stop() {
  client_.Stop();
  started_ = false;
}

void MultiplayerSession::Advance(GameState& state) {
  if (!started_) {
    return;
  }
  MultiplayerSnapshot snapshot = client_.Snapshot();
  if (!snapshot.account_id.empty() &&
      (snapshot.account_id != state.account.multiplayer_account_id() ||
       snapshot.token != state.account.multiplayer_token())) {
    state.account.SetMultiplayerAccount(snapshot.account_id, snapshot.token);
  }

  // Compared as a whole, not field by field: a re-scrolled weapon changes what
  // the Inspect screen draws, and a check on only name, level and job would
  // never send it. Uses MessageDifferencer instead of comparing bytes, since
  // the sheet has maps whose entries may encode in any order.
  PlayerInfo player = PlayerFor(state);
  // The account is identity, not something the lobby draws, and the server
  // takes it from the session instead of from the message. Learning ours on the
  // first tick isn't worth an update.
  told_.set_account_id(player.account_id());
  google::protobuf::util::MessageDifferencer differencer;
  differencer.IgnoreField(Character::descriptor()->FindFieldByName("exp"));
  bool only_exp = differencer.Compare(player, told_);
  if (only_exp && player.sheet().exp() == told_.sheet().exp()) {
    return;
  }
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  // EXP alone changes with every kill (312 times in one second on the live
  // server), and each change would send a whole sheet. A bar in someone else's
  // lobby is worth at most one sheet per second. Any other change to the sheet
  // is sent immediately.
  if (only_exp && now - sent_ < kExpUpdatePeriod) {
    return;
  }
  sent_ = now;
  told_ = player;
  client_.SetPlayer(player);
}

}  // namespace ms
