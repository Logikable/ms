#include "src/item/stack_tabs.h"

#include <vector>

#include "src/item/item.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

bool Shows(StackView view, ItemKind kind) {
  switch (view) {
    case StackView::kTokens:
      return kind == ITEM_KIND_TOKEN;
    case StackView::kSoulShards:
      return kind == ITEM_KIND_SOUL_SHARD;
    case StackView::kEtc:
      // Every currency has a kind and a home of its own; an ordinary drop
      // names none, and Etc is where those go.
      return kind == ITEM_KIND_UNSPECIFIED;
  }
  return false;
}

}  // namespace

std::vector<int> StacksIn(const std::vector<StackableItem>& stacks,
                          StackView view) {
  std::vector<int> rows;
  for (int i = 0; i < static_cast<int>(stacks.size()); ++i) {
    if (Shows(view, stacks[i].prototype().kind())) {
      rows.push_back(i);
    }
  }
  return rows;
}

bool HoldsCurrency(const std::vector<StackableItem>& stacks) {
  for (const StackableItem& stack : stacks) {
    ItemKind kind = stack.prototype().kind();
    if (kind == ITEM_KIND_TOKEN || kind == ITEM_KIND_SOUL_SHARD) {
      return true;
    }
  }
  return false;
}

}  // namespace ms
