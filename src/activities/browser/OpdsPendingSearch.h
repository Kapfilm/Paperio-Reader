#pragma once
#include <string>
#include <utility>
namespace opds {
// Consume before invoking a callback that may synchronously fetch another feed.
template<class Search>
bool runPendingSearch(std::string& pending, bool hasTemplate, Search search) {
  if (pending.empty() || !hasTemplate) return false;
  auto query = std::exchange(pending, std::string{});
  search(query);
  return true;
}
}
