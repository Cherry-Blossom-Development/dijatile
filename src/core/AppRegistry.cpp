#include <algorithm>

#include "App.h"

namespace app_registry {

namespace {
// Function-local so it exists before any REGISTER_APP static initializer runs.
std::vector<App *> &list() {
  static std::vector<App *> apps;
  return apps;
}
bool sorted = false;
}  // namespace

void add(App *app) {
  list().push_back(app);
  sorted = false;
}

const std::vector<App *> &all() {
  if (!sorted) {
    std::sort(list().begin(), list().end(), [](App *a, App *b) {
      if (a->kind() != b->kind()) return a->kind() < b->kind();
      return strcmp(a->name(), b->name()) < 0;
    });
    sorted = true;
  }
  return list();
}

App *byNetId(uint16_t id) {
  for (App *a : list()) {
    if (a->netId() == id) return a;
  }
  return nullptr;
}

App *byName(const char *name) {
  for (App *a : list()) {
    if (strcmp(a->name(), name) == 0) return a;
  }
  return nullptr;
}

}  // namespace app_registry
