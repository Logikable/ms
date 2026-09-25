/* BankInstance is the account's shared storage: two tabs of kTabCapacity slots
 * and a purse, available to every character in the save.
 *
 * It's built from the same pieces as a character's bag (an InventoryInstance
 * for the equip tab, a StackTab for Etc, a CurrencyPurse and a meso balance),
 * so the bank's contents are drawn, sorted and counted by the same code as the
 * bag. Nothing here knows about characters: the Bank screen moves items across
 * and checks both sides for room.
 */
#ifndef MS_SRC_ITEM_BANK_H_
#define MS_SRC_ITEM_BANK_H_

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

class BankInstance {
 public:
  const InventoryInstance& equips() const {
    return equips_;
  }
  const StackTab& stacks() const {
    return stacks_;
  }
  const CurrencyPurse& currencies() const {
    return currencies_;
  }
  int64_t meso() const {
    return meso_;
  }

  // Adds `item` to the end of the equip tab. Returns false and takes nothing if
  // the tab is full.
  bool AddEquip(std::unique_ptr<EquipTabItem> item);
  // Removes and returns the `index`-th equip, or nullptr if out of range.
  std::unique_ptr<EquipTabItem> TakeEquip(int index);

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
  // The balance of a currency, and spending it. Both use the display name, as
  // everything in a save does.
  int64_t CountCurrency(const std::string& name) const;
  bool SpendCurrency(const std::string& name, int64_t count);

  // Sorts both tabs, which is what Sort does on either half of the screen.
  void SortEquips(const std::function<bool(const EquipPrototype&)>& equippable);
  void SortStacks();

  // Loads what the save holds, resolving names against the catalogs. A name no
  // longer in data/ is dropped.
  void RestoreFrom(const Bank& saved,
                   const std::map<std::string, const EquipPrototype*>& equips,
                   const std::map<std::string, const ItemPrototype*>& items);
  Bank ToProto() const;

 private:
  InventoryInstance equips_;
  StackTab stacks_;
  CurrencyPurse currencies_;
  int64_t meso_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_ITEM_BANK_H_
