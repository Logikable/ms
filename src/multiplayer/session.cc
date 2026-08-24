#include "src/multiplayer/session.h"

#include <string>
#include <utility>

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
  // What a party member is shown is what they could work out by watching:
  // the stats, what is worn, and the passives behind both. The bag, the
  // purse and the shelf of what was sold are nobody else's business, and
  // sending them would put a save's worth of message on every update.
  sheet.clear_inventory();
  sheet.clear_stacks();
  sheet.clear_buy_backs();
  sheet.clear_pinned_scrolls();
  sheet.clear_boss_clears();
  sheet.clear_meso();
  sheet.clear_exp();
  sheet.clear_ap();
  sheet.clear_sp_by_stage();
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

  // Compared whole rather than field by field: a re-scrolled weapon or a
  // spent skill point changes what the Inspect screen draws, and a check that
  // knew only about the name, the level and the job would never send it.
  //
  // Through MessageDifferencer rather than the serialized bytes, because the
  // sheet holds maps -- what is worn, and what is learned -- and two encodings
  // of one map need not put its entries in the same order.
  PlayerInfo player = PlayerFor(state);
  // The account is identity rather than anything the lobby draws, and the
  // server takes it from the session instead of from what arrives. Learning
  // ours on the first tick is not news worth an update.
  told_.set_account_id(player.account_id());
  if (google::protobuf::util::MessageDifferencer::Equals(player, told_)) {
    return;
  }
  told_ = player;
  client_.SetPlayer(player);
}

}  // namespace ms
