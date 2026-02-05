#pragma once
#include "../runtime/Containers.hpp"
 
enum class KeyType { Character, Command, TabSwitch };
 
struct Key {
    const char* label;
    const char* value;
    KeyType type;
    int width_mult;
    int command_id;
};
 
using KeyGrid = MiniVector<MiniVector<Key>>;

enum {
    CMD_NONE = 0,
    CMD_ENTER,
    CMD_BACKSPACE,
    CMD_SPACE,
    CMD_SHIFT,
    CMD_TAB_ABC,
    CMD_TAB_123,
    CMD_TAB_SYM,
};

namespace Layouts {
    const KeyGrid& GetABC(bool shifted);
    const KeyGrid& Get123();
    const KeyGrid& GetSYM();
}
