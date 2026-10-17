#include "persistence/RadioStore.h"

// Radio is compiled out of the Pico builds, so its station list has nowhere to go.
namespace awtrix::radiostore {
void save(const std::string&) {}
bool pending() { return false; }
void load(CoreEngine&) {}
}
