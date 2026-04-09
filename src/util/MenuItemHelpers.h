#pragma once

#include <I18n.h>

#include <functional>
#include <type_traits>
#include <vector>

// Generic helper for constructing separator rows in menu item vectors.
// The item type must have an `action`, an `isSeparator`, and either a `labelId` or `nameId` member.
template <typename ItemType>
inline ItemType makeSeparatorMenuItem(StrId labelId) {
  ItemType item{};
  if constexpr (std::is_member_object_pointer_v<decltype(&ItemType::action)>) {
    item.action = static_cast<decltype(item.action)>(0);
  }
  if constexpr (std::is_member_object_pointer_v<decltype(&ItemType::labelId)>) {
    item.labelId = labelId;
  } else if constexpr (std::is_member_object_pointer_v<decltype(&ItemType::nameId)>) {
    item.nameId = labelId;
  } else {
    static_assert(sizeof(ItemType) == 0,
                  "makeSeparatorMenuItem requires ItemType with labelId or nameId member");
  }
  item.isSeparator = true;
  return item;
}

// Generic helper for creating a selectable predicate for menu lists.
// The item type must have an `isSeparator` member.
template <typename ItemType>
inline std::function<bool(int)> makeSelectablePredicate(const std::vector<ItemType>& items) {
  return
      [&items](int index) { return index >= 0 && index < static_cast<int>(items.size()) && !items[index].isSeparator; };
}

template <typename ItemType>
inline std::function<bool(int)> makeSelectablePredicate(const std::vector<ItemType>& items, int indexOffset,
                                                        bool firstIndexSelectable) {
  return [&items, indexOffset, firstIndexSelectable](int index) {
    if (firstIndexSelectable && index == 0) {
      return true;
    }

    const int itemIndex = index - indexOffset;
    return itemIndex >= 0 && itemIndex < static_cast<int>(items.size()) && !items[itemIndex].isSeparator;
  };
}
