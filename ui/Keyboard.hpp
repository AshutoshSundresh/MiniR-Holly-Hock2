#pragma once
#include "Layouts.hpp"

class Console;

class Keyboard {
public:
    void Init();
    void Draw();
    bool HandleInput(uint32_t key1, uint32_t key2, Console& console);

private:
    enum class Tab { ABC, Num123, SYM };
    Tab current_tab = Tab::ABC;
    bool shifted = false;
    int cursor_r = 0;
    int cursor_c = 0;

    const KeyGrid& GetCurrentGrid();
    void ClampCursor();
    void ExecuteKey(const Key& key, Console& console);
};
