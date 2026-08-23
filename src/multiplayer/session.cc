#include "src/multiplayer/session.h"

#include <string>
#include <utility>

#include "src/character/character.h"
#include "src/game_state.h"
#include "src/multiplayer/client.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

PlayerInfo PlayerFor(const GameState& state) {
  PlayerInfo player;
  player.set_account_id(state.account.multiplayer_account_id());
  player.set_name(state.character.username());
  player.set_level(state.character.proto().level());
  player.set_job(AdvancementForJobStage(state.character.proto().job(),
                                        state.character.proto().job_stage()));
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

  PlayerInfo player = PlayerFor(state);
  if (player.name() == told_.name() && player.level() == told_.level() &&
      player.job() == told_.job()) {
    return;
  }
  told_ = player;
  client_.SetPlayer(player);
}

}  // namespace ms
