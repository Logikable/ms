/* Completing a trade on this client: checking whether the bag can take what's
 * coming, and doing the exchange.
 *
 * The server holds no items. It relays what each side says it's offering and
 * never checks that they have it, so both halves of a trade happen here,
 * against this client's own character and catalogs. An item arrives as its
 * saved state plus a name; what it is comes from the reader's catalog, which
 * the protocol version keeps consistent.
 */
#ifndef MS_SRC_MULTIPLAYER_TRADE_EXCHANGE_H_
#define MS_SRC_MULTIPLAYER_TRADE_EXCHANGE_H_

#include <map>
#include <vector>

#include "src/character/character.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// Whether the bag can hold `received` once `given` has left it. The two are
// counted together: giving three equips frees three slots, so a bag that's full
// right now can still take three back.
//
// Also false for an item missing from this build's catalog, since it couldn't
// go in the bag anyway.
bool HasRoomForTrade(const CharacterInstance& character,
                     const std::map<std::string, ItemPrototype>& items,
                     const TradeOffer& given, const TradeOffer& received);

// Removes `given` from the character and adds `received`. `given_equips` are
// the offered equip-tab rows, in any order: the message says what an item is,
// and the bag needs to be told which of its own to give.
//
// Call HasRoomForTrade first. This does exactly what it's told: anything the
// bag can't hold is lost, just like a drop into a full bag.
void ApplyTrade(CharacterInstance& character,
                const std::map<std::string, EquipPrototype>& equips,
                const std::map<std::string, ItemPrototype>& items,
                const std::vector<int>& given_equips, const TradeOffer& given,
                const TradeOffer& received);

}  // namespace ms

#endif  // MS_SRC_MULTIPLAYER_TRADE_EXCHANGE_H_
