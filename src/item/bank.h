/* BankInstance is the account's shared storage: an Equip tab of kBankPages
 * pages, an Etc tab and a purse, available to every character in the save.
 *
 * It's built from the same pieces as a character's bag (an InventoryInstance
 * per equip page, a StackTab for Etc, a CurrencyPurse and a meso balance),
 * so the bank's contents are drawn, sorted and counted by the same code as the
 * bag. Nothing here knows about characters: the Bank screen moves items across
 * and checks both sides for room.
 */
#ifndef MS_SRC_ITEM_BANK_H_
#define MS_SRC_ITEM_BANK_H_

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "src/item/currency.h"
#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/item/stack_tab.h"
#include "src/protos/account.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// The Equip tab's pages, which together hold what the old single tab did.
inline constexpr int kBankPages = 8;
inline constexpr int kBankPageCapacity = kTabCapacity / kBankPages;

class BankInstance {
 public:
  // The `page`-th page of the Equip tab. `page` must be in range.
  const InventoryInstance& page(int page) const {
    return pages_[page];
  }
  bool PageFull(int page) const;
  const StackTab& stacks() const {
    return stacks_;
  }
  const CurrencyPurse& currencies() const {
    return currencies_;
  }
  int64_t meso() const {
    return meso_;
  }
  int64_t v_points() const {
    return v_points_;
  }

  // Adds `item` to the end of `page`. Returns false and takes nothing if the
  // page is full.
  bool AddEquip(int page, std::unique_ptr<EquipTabItem> item);
  // Removes and returns the `index`-th equip on `page`, or nullptr if out of
  // range.
  std::unique_ptr<EquipTabItem> TakeEquip(int page, int index);

  // Adds `count` of `proto` and returns how many were added: a currency always
  // takes all of them, and an Etc drop takes what fits. Routed by the
  // prototype's kind, the same way a character handles a drop.
  int AddItem(const ItemPrototype& proto, int count);
  // How many more copies of `proto` the bank could take.
  int RoomFor(const ItemPrototype& proto) const;
  // Takes `count` from the `index`-th stack, clamped to what it holds, and
  // returns how many were taken.
  int TakeStack(int index, int count);

  // Meso in and out. Spending is all or nothing.
  void AddMeso(int64_t amount);
  bool SpendMeso(int64_t amount);
  // V Points in and out, the same way.
  void AddVPoints(int64_t amount);
  bool SpendVPoints(int64_t amount);
  // The balance of a currency, and spending it. Both use the display name, as
  // everything in a save does.
  int64_t CountCurrency(const std::string& name) const;
  bool SpendCurrency(const std::string& name, int64_t count);

  // What Sort does on the bank's half of the screen: one equip page, or the
  // Etc tab.
  void SortEquips(int page,
                  const std::function<bool(const EquipPrototype&)>& equippable);
  void SortStacks();

  // Loads what the save holds, resolving names against the catalogs. A name no
  // longer in data/ is dropped.
  void RestoreFrom(const Bank& saved,
                   const std::map<std::string, const EquipPrototype*>& equips,
                   const std::map<std::string, const ItemPrototype*>& items);
  Bank ToProto() const;

 private:
  std::array<InventoryInstance, kBankPages> pages_;
  StackTab stacks_;
  CurrencyPurse currencies_;
  int64_t meso_ = 0;
  int64_t v_points_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_BANK_H_
