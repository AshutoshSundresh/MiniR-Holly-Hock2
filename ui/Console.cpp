#include "Console.hpp"
#include <sdk/os/debug.hpp>

namespace {
    // Very small “is input complete?” heuristic for on-device multiline:
    // track (), {}, [] balance and ignore anything inside single/double quotes.
    // This is not a full R parser, but it handles the common { ... } case.
    int BalanceDelims(const MiniString& s) {
        int paren = 0, brace = 0, bracket = 0;
        bool in_single = false, in_double = false;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (!in_double && c == '\'' ) { in_single = !in_single; continue; }
            if (!in_single && c == '"'  ) { in_double = !in_double; continue; }
            if (in_single || in_double) continue;
            switch (c) {
                case '(': paren++; break;
                case ')': paren--; break;
                case '{': brace++; break;
                case '}': brace--; break;
                case '[': bracket++; break;
                case ']': bracket--; break;
                default: break;
            }
        }
        // treat any negative as complete (parser will error)
        return (paren > 0) + (brace > 0) + (bracket > 0);
    }
}

void Console::Init() {
    scrollback.clear();
    input_accumulator = "";
    current_line = "";
    scrollback.push_back("Welcome to MiniR!");
    scrollback.push_back("Type commands...");
}

void Console::AppendInput(const char* str) {
    current_line += str;
}

void Console::Backspace() {
    if (!current_line.empty()) {
        current_line.pop_back();
    }
}

void Console::Enter() {
    const bool was_at_bottom = (scroll_offset >= (int)scrollback.size() - LINES_PER_SCREEN);

    const char* active_prompt = input_accumulator.empty() ? "> " : "+ ";
    MiniString line(active_prompt);
    line += current_line;
    scrollback.push_back(line);

    // Enforce MAX_LINES
    while ((int)scrollback.size() > MAX_LINES) {
        scrollback.erase(scrollback.begin());
        if (scroll_offset > 0) scroll_offset--;
    }

    // Accumulate multi-line input
    MiniString combined = input_accumulator;
    combined += current_line;
    combined += "\n";

    if (!current_line.empty() || !input_accumulator.empty()) {
        if (BalanceDelims(combined) > 0) {
            input_accumulator = combined;
            pending_command.clear();
        } else {
            pending_command = combined;
            input_accumulator.clear();
        }
    }

    current_line.clear();

    // Auto-follow bottom if we were already there
    if (was_at_bottom && (int)scrollback.size() > LINES_PER_SCREEN) {
        scroll_offset = (int)scrollback.size() - LINES_PER_SCREEN;
    }
}

void Console::ClearInput() {
    current_line = "";
}

void Console::Clear() {
    scrollback.clear();
    scrollback.push_back("(cleared)");
    input_accumulator.clear();
    current_line.clear();
    pending_command.clear();
    scroll_offset = 0;
}

void Console::Print(const char* str) {
    // Append to last line or push new? 
    // Simplified: PrintLine
    PrintLine(str);
}

void Console::PrintLine(const char* str) {
    const bool was_at_bottom = (scroll_offset >= (int)scrollback.size() - LINES_PER_SCREEN);
    scrollback.push_back(str);
    while ((int)scrollback.size() > MAX_LINES) {
        scrollback.erase(scrollback.begin());
        if (scroll_offset > 0) scroll_offset--;
    }
    if (was_at_bottom && (int)scrollback.size() > LINES_PER_SCREEN) {
        scroll_offset = (int)scrollback.size() - LINES_PER_SCREEN;
    }
}

void Console::Draw() {
    // Draw scrollback
    // Assuming screen text grid starts at 0,0 and goes down
    int y_grid = 1; 
    
    int start_idx = scroll_offset;
    if (start_idx < 0) start_idx = 0;
    
    int lines_drawn = 0;
    for (int i = start_idx; i < (int)scrollback.size() && lines_drawn < LINES_PER_SCREEN; ++i) {
        Debug_Printf(0, y_grid++, false, 0, "%s", scrollback[i].c_str());
        lines_drawn++;
    }
    
    // Draw Input Line at bottom of console area
    // (Or just below last log)
    // Let's pin it to a fixed row, say row 10
    int input_row = LINES_PER_SCREEN + 2;
    const char* active_prompt = input_accumulator.empty() ? "> " : "+ ";
    MiniString line_view(active_prompt);
    line_view += current_line;
    
    // Blinking cursor simulation? 
    line_view += "_";
    
    Debug_Printf(0, input_row, false, 0, "%s", line_view.c_str());
}
