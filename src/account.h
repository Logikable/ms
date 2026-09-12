/* AccountInstance wraps the Account proto: what the player owns rather than
 * what one character does.
 *
 * Everything here is shared by every character on the save -- key bindings,
 * the record of what the player has already been shown, and how far the
 * account has climbed. Progress a character earns stays on the character. See
 * account.proto for the rule to apply when a new field needs a home.
 */
#ifndef MS_SRC_ACCOUNT_H_
#define MS_SRC_ACCOUNT_H_

#include <algorithm>
#include <string>

#include "src/protos/account.pb.h"
#include "src/protos/keybinds.pb.h"

namespace ms {

// The volume the music ships at, and what an account that has never touched
// the sliders plays at. Quiet on purpose: the game is read, not watched.
inline constexpr int kDefaultBgmVolume = 10;
// The loudest either slider goes. The scale is a percentage.
inline constexpr int kMaxBgmVolume = 100;

class AccountInstance {
 public:
  AccountInstance() = default;
  explicit AccountInstance(Account account);

  const Account& proto() const {
    return account_;
  }

  // What the player has bound their keys to. Handed out mutable because the
  // frontend's KeyMap edits the bindings in place.
  Keybinds* mutable_keybinds() {
    return account_.mutable_keybinds();
  }
  const Keybinds& keybinds() const {
    return account_.keybinds();
  }

  // Whether the player has already been shown whatever is recorded under
  // `key`: a tab they have opened, a gold trail they have walked. The keys
  // belong to the frontend; the account only keeps the record.
  bool Seen(const std::string& key) const;
  // Records that they have. Marking a key already marked does nothing.
  void MarkSeen(const std::string& key);

  // The highest level and job stage any character on the account has reached,
  // which is what a feature's unlock is measured against.
  int max_level() const {
    return account_.max_level();
  }
  int max_job_stage() const {
    return account_.max_job_stage();
  }

  // What the multiplayer server calls this player, and the token that proves
  // it. Empty until they have connected once.
  const std::string& multiplayer_account_id() const {
    return account_.multiplayer_account_id();
  }
  const std::string& multiplayer_token() const {
    return account_.multiplayer_token();
  }
  // Keeps what the server issued, so the next connection comes back as the
  // same player.
  void SetMultiplayerAccount(const std::string& account_id,
                             const std::string& token);

  // Whether the focused panel's title blinks, and the switch that sets it.
  // Off by default: the blink is the option, not the chip that marks focus.
  bool panel_title_blink() const {
    return account_.options().panel_title_blink();
  }
  void SetPanelTitleBlink(bool on) {
    account_.mutable_options()->set_panel_title_blink(on);
  }

  // How loud a map's music and a boss fight's play, 0 to 100. An account that
  // has never moved either slider -- including a save older than they are --
  // gets kDefaultBgmVolume. Both setters clamp.
  int map_bgm_volume() const {
    return account_.options().has_map_bgm_volume()
               ? account_.options().map_bgm_volume()
               : kDefaultBgmVolume;
  }
  void SetMapBgmVolume(int volume) {
    account_.mutable_options()->set_map_bgm_volume(
        std::clamp(volume, 0, kMaxBgmVolume));
  }
  int boss_bgm_volume() const {
    return account_.options().has_boss_bgm_volume()
               ? account_.options().boss_bgm_volume()
               : kDefaultBgmVolume;
  }
  void SetBossBgmVolume(int volume) {
    account_.mutable_options()->set_boss_bgm_volume(
        std::clamp(volume, 0, kMaxBgmVolume));
  }

  // Raises the two watermarks to take in a character who has reached `level`
  // and `job_stage`. They only ever climb: an account does not forget a
  // feature it has opened.
  void RecordProgress(int level, int job_stage);

 private:
  Account account_;
};

}  // namespace ms

#endif  // MS_SRC_ACCOUNT_H_
