#include "Layouts.hpp"

namespace Layouts {

    // Helper to create keys
    Key K(const char* label, const char* val = nullptr, int width = 1) {
        return { label, val ? val : label, KeyType::Character, width, CMD_NONE };
    }
    Key Cmd(const char* label, int cmd, int width = 1) {
        return { label, "", KeyType::Command, width, cmd };
    }
    Key TabKey(const char* label, int cmd) {
        return { label, "", KeyType::TabSwitch, 1, cmd };
    }

    // Static storage for grids
    KeyGrid grid_abc;
    KeyGrid grid_abc_shifted;
    KeyGrid grid_123;
    KeyGrid grid_sym;

    void InitABC() {
        if (!grid_abc.empty()) return;
        
        // Row 1: Q W E R T Y U I O P
        grid_abc.push_back({ K("q"), K("w"), K("e"), K("r"), K("t"), K("y"), K("u"), K("i"), K("o"), K("p") });
        // Row 2: A S D F G H J K L
        grid_abc.push_back({ K("a"), K("s"), K("d"), K("f"), K("g"), K("h"), K("j"), K("k"), K("l") });
        // Row 3: Z X C V B N M
        grid_abc.push_back({ K("z"), K("x"), K("c"), K("v"), K("b"), K("n"), K("m") });
        // Row 4: Controls
        grid_abc.push_back({ Cmd("SHIFT", CMD_SHIFT), Cmd("SPACE", CMD_SPACE, 2), Cmd("BKSP", CMD_BACKSPACE), Cmd("ENTER", CMD_ENTER, 2) });
        // Row 5: Tabs
        grid_abc.push_back({ TabKey("ABC", CMD_TAB_ABC), TabKey("123", CMD_TAB_123), TabKey("SYM", CMD_TAB_SYM) });
    }

    void InitABCShifted() {
        if (!grid_abc_shifted.empty()) return;
        grid_abc_shifted.push_back({ K("Q"), K("W"), K("E"), K("R"), K("T"), K("Y"), K("U"), K("I"), K("O"), K("P") });
        grid_abc_shifted.push_back({ K("A"), K("S"), K("D"), K("F"), K("G"), K("H"), K("J"), K("K"), K("L") });
        grid_abc_shifted.push_back({ K("Z"), K("X"), K("C"), K("V"), K("B"), K("N"), K("M") });
        grid_abc_shifted.push_back({ Cmd("shift", CMD_SHIFT), Cmd("SPACE", CMD_SPACE, 2), Cmd("BKSP", CMD_BACKSPACE), Cmd("ENTER", CMD_ENTER, 2) });
        grid_abc_shifted.push_back({ TabKey("ABC", CMD_TAB_ABC), TabKey("123", CMD_TAB_123), TabKey("SYM", CMD_TAB_SYM) });
    }

    void Init123() {
        if (!grid_123.empty()) return;
        // Row 1: 1 2 3 4 5
        grid_123.push_back({ K("1"), K("2"), K("3"), K("4"), K("5") });
        // Row 2: 6 7 8 9 0
        grid_123.push_back({ K("6"), K("7"), K("8"), K("9"), K("0") });
        // Row 3: . , + - e E
        grid_123.push_back({ K("."), K(","), K("+"), K("-"), K("e"), K("E") });
         // Row 4: ( ) [ ] { }
        grid_123.push_back({ K("("), K(")"), K("["), K("]"), K("{"), K("}") });
        // Row 5: Tabs (standard row)
        grid_123.push_back({ TabKey("ABC", CMD_TAB_ABC), TabKey("123", CMD_TAB_123), TabKey("SYM", CMD_TAB_SYM), Cmd("BKSP", CMD_BACKSPACE), Cmd("ENTER", CMD_ENTER) });
    }

    void InitSYM() {
        if (!grid_sym.empty()) return;
        // Row 1: <- = == != < <= > >=
        grid_sym.push_back({ K("<-"), K("="), K("=="), K("!="), K("<"), K("<="), K(">"), K(">=") });
        // Row 2: + - * / ^ : , ;
        grid_sym.push_back({ K("+"), K("-"), K("*"), K("/"), K("^"), K(":"), K(","), K(";") });
        // Row 3: & | ! %% %/% %*% ( )
        grid_sym.push_back({ K("&"), K("|"), K("!"), K("%%"), K("%/%"), K("%*%"), K("("), K(")") });
        // Row 4: [ ] { } " ' # _
        grid_sym.push_back({ K("["), K("]"), K("{"), K("}"), K("\""), K("'"), K("#"), K("_") });
        // Row 5: Tabs
        grid_sym.push_back({ TabKey("ABC", CMD_TAB_ABC), TabKey("123", CMD_TAB_123), TabKey("SYM", CMD_TAB_SYM), Cmd("BKSP", CMD_BACKSPACE), Cmd("ENTER", CMD_ENTER) });
    }

    const KeyGrid& GetABC(bool shifted) {
        if (shifted) { InitABCShifted(); return grid_abc_shifted; }
        InitABC(); return grid_abc;
    }
    const KeyGrid& Get123() { Init123(); return grid_123; }
    const KeyGrid& GetSYM() { InitSYM(); return grid_sym; }
}
