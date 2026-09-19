/* BankInstance is the account's shared storage: two tabs of kTabCapacity
 * slots and a purse, reachable by every character on the save.
 *
 * It is built out of the same pieces a character's bag is -- an
 * InventoryInstance for the equip tab, a StackTab for Etc, a CurrencyPurse
 * and a meso balance -- so what the bank holds is drawn, sorted and counted by
 * the same code that draws the bag. Nothing here knows about a character: the
 * Bank screen is what moves an item across, and it checks both ends for room.
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

  // Takes `item` onto the equip tab, appended at the end. False and nothing
  // taken when the tab is full.
  bool AddEquip(std::unique_ptr<EquipTabItem> item);
  // Hands the `index`-th equip back, or nullptr for an index out of range.
  std::unique_ptr<EquipTabItem> TakeEquip(int index);

  // Adds `count` of `proto` and returns how many went in: a currency always
  // takes all of them, and an Etc drop takes what fits. Routed on the
  // prototype's kind, as the character routes a drop.
  int AddItem(const ItemPrototype& proto, int count);
  // How many more copies of `proto` the bank could take.
  int RoomFor(const ItemPrototype& proto) const;
  // Takes `count` off the `index`-th stack, clamped to what is in it, and
  // returns how many came out.
  int TakeStack(int index, int count);

  // Meso in and out. Spend is all or nothing.
  void AddMeso(int64_t amount);
  bool SpendMeso(int64_t amount);
  // The balance in a currency, and spending one. Both name it as the player
  // sees it, which is how everything crossing a save names an item.
  int64_t CountCurrency(const std::string& name) const;
  bool SpendCurrency(const std::string& name, int64_t count);

  // Files both tabs, which is what Sort does on either half of the screen.
  void SortEquips(const std::function<bool(const EquipPrototype&)>& equippable);
  void SortStacks();

  // Reads what the save holds, resolving names against the catalogs. A name
  // no longer in data/ is dropped.
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
