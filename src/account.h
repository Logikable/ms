/* AccountInstance wraps the Account proto: what the player owns, as opposed to
 * what one character has.
 *
 * Everything here is shared by every character in the save: key bindings, the
 * record of what the player has already been shown, and the account's progress.
 * Progress a character earns stays on the character. See account.proto for how
 * to decide where a new field belongs.
 */
#ifndef MS_SRC_ACCOUNT_H_
#define MS_SRC_ACCOUNT_H_

#include <algorithm>
#include <map>
#include <string>

#include "src/item/bank.h"
#include "src/protos/account.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/keybinds.pb.h"

namespace ms {

// The default music volume, used by an account that has never touched the
// sliders. Quiet on purpose, since the game is mostly read rather than watched.
inline constexpr int kDefaultBgmVolume = 10;
// The maximum for either slider. The scale is a percentage.
inline constexpr int kMaxBgmVolume = 100;

// Raises `account`'s two high-water marks to include a character who has
// reached `level` and `job_stage`. They only ever go up: an account doesn't
// lose a feature it has unlocked.
void RecordProgress(Account& account, int level, int job_stage);

class AccountInstance {
 public:
  AccountInstance() = default;
  explicit AccountInstance(Account account);

  // The session's copy. The bank is a live container, not a proto field, so
  // this has whatever `bank` held on load; ToProto writes the current bank.
  const Account& proto() const {
    return account_;
  }
  // The account as the save stores it, with the bank written back in.
  Account ToProto() const;

  // The shared storage, and the catalogs it's rebuilt against on load.
  const BankInstance& bank() const {
    return bank_;
  }
  BankInstance& mutable_bank() {
    return bank_;
  }
  void RestoreBank(const Bank& saved,
                   const std::map<std::string, const EquipPrototype*>& equips,
                   const std::map<std::string, const ItemPrototype*>& items);

  // The player's key bindings. Mutable because the frontend's KeyMap edits them
  // in place.
  Keybinds* mutable_keybinds() {
    return account_.mutable_keybinds();
  }
  const Keybinds& keybinds() const {
    return account_.keybinds();
  }

  // Whether the player has already been shown whatever is recorded under `key`,
  // such as a tab they've opened or a gold trail they've followed. The frontend
  // owns the keys; the account only stores the record.
  bool Seen(const std::string& key) const;
  // Records that they have. Marking a key twice does nothing.
  void MarkSeen(const std::string& key);

  // The highest level and job stage any character on the account has reached,
  // which feature unlocks are checked against.
  int max_level() const {
    return account_.max_level();
  }
  int max_job_stage() const {
    return account_.max_job_stage();
  }

  // The multiplayer server's id for this player, and the token that proves it.
  // Empty until they've connected once.
  const std::string& multiplayer_account_id() const {
    return account_.multiplayer_account_id();
  }
  const std::string& multiplayer_token() const {
    return account_.multiplayer_token();
  }
  // Saves what the server issued, so the next connection is recognised as the
  // same player.
  void SetMultiplayerAccount(const std::string& account_id,
                             const std::string& token);

  // Whether the game picks a preset based on what the character is doing. Off
  // by default, so the player chooses one themselves. Copied onto the character
  // being played; see GameState::MirrorAccount.
  bool autoswap_presets() const {
    return account_.options().autoswap_presets();
  }
  void SetAutoswapPresets(bool on) {
    account_.mutable_options()->set_autoswap_presets(on);
  }

  // Where the music comes from: the map's own track, the song list in order, or
  // shuffle. Set on the Jukebox screen.
  JukeboxMode jukebox_mode() const {
    return account_.options().jukebox_mode();
  }
  void SetJukeboxMode(JukeboxMode mode) {
    account_.mutable_options()->set_jukebox_mode(mode);
  }

  bool buff_indicators() const {
    return account_.options().buff_indicators();
  }
  void SetBuffIndicators(bool on) {
    account_.mutable_options()->set_buff_indicators(on);
  }

  // Whether the focused panel's title blinks. Off by default: the blink is an
  // option, separate from the chip that marks focus.
  bool panel_title_blink() const {
    return account_.options().panel_title_blink();
  }
  void SetPanelTitleBlink(bool on) {
    account_.mutable_options()->set_panel_title_blink(on);
  }

  // The volume of map music and boss fight music, 0 to 100. An account that has
  // never moved a slider, including a save older than the sliders, gets
  // kDefaultBgmVolume. Both setters clamp.
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

  // Raises this account's high-water marks. See RecordProgress above.
  void RecordProgress(int level, int job_stage);

 private:
  Account account_;
  BankInstance bank_;
};

}  // namespace ms

#endif  // MS_SRC_ACCOUNT_H_
