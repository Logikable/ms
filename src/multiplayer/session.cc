#include "src/multiplayer/session.h"

#include <chrono>
#include <string>
#include <utility>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/util/message_differencer.h"
#include "src/character/character.h"
#include "src/game_state.h"
#include "src/multiplayer/client.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

Character PublicSheet(const CharacterInstance& character) {
  // ToProto rather than proto(): what is worn lives in a C++ container and is
  // only folded into the message when someone asks for the lot.
  Character sheet = character.ToProto();
  // What a party member is shown is what they could work out by WATCHING: the
  // stats, what is worn, the passives behind both, and the points still
  // waiting to be spent on them. The bag and the purse are nobody else's
  // business and would put a save's worth of message on every update.
  //
  // Honor stays behind with them. It climbs with every kill, and an update
  // goes out whenever the sheet moves -- so leaving it in sent a whole sheet
  // per kill, for a number the Ability tab reads without. EXP moves as fast
  // and the exp bar needs it; see MultiplayerSession::Advance for what pays
  // for it.
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
  // Beside the sheet rather than in it: the server checks these before it
  // lets a party at a boss on a reset clock, and PublicSheet strips them.
  *player.mutable_boss_clears() = state.character.proto().boss_clears();
  // Their switch, not the reader's: whose allocation a sheet shows is the
  // question its owner has already answered.
  player.set_autoswap_presets(state.account.autoswap_presets());
  // What they have set on the boss screen. The server holds a whole party to
  // one set of these before it opens a fight.
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

  // Compared WHOLE rather than field by field: a re-scrolled weapon changes
  // what the Inspect screen draws, and a check knowing only the name, level
  // and job would never send it. Through MessageDifferencer rather than the
  // bytes, the sheet holding maps, whose entries need not encode in order.
  PlayerInfo player = PlayerFor(state);
  // The account is identity rather than anything the lobby draws, and the
  // server takes it from the session instead of from what arrives. Learning
  // ours on the first tick is not news worth an update.
  told_.set_account_id(player.account_id());
  google::protobuf::util::MessageDifferencer differencer;
  differencer.IgnoreField(Character::descriptor()->FindFieldByName("exp"));
  bool only_exp = differencer.Compare(player, told_);
  if (only_exp && player.sheet().exp() == told_.sheet().exp()) {
    return;
  }
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  // EXP alone moves with every kill -- 312 in one second against the live
  // server -- and every one of those would be a whole sheet. A bar in
  // somebody else's lobby is worth a sheet a second and no more. Anything
  // else on the sheet goes out the moment it moves.
  if (only_exp && now - sent_ < kExpUpdatePeriod) {
    return;
  }
  sent_ = now;
  told_ = player;
  client_.SetPlayer(player);
}

}  // namespace ms
