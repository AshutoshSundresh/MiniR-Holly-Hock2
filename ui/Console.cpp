#include "Console.hpp"
#include <sdk/os/debug.hpp>

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
    // Basic "Enter" just pushes to scrollback for now (until we have runtime)
    // Real logic: Append to accumulator, check if complete.
    // For now: push prompt + line to scrollback, clear line.
    
    std::string full = prompt + current_line;
    scrollback.push_back(full);
    
    // Check command (mock runtime)
    // If we had the runtime, we would call it here.
    // For now, if line is empty, do nothing interesting.
    
    current_line = "";
    // Scroll to bottom
    if ((int)scrollback.size() > LINES_PER_SCREEN) {
        scroll_offset = scrollback.size() - LINES_PER_SCREEN;
    }
}

void Console::ClearInput() {
    current_line = "";
}

void Console::Print(const char* str) {
    // Append to last line or push new? 
    // Simplified: PrintLine
    PrintLine(str);
}

void Console::PrintLine(const char* str) {
    scrollback.push_back(str);
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
    std::string line_view = prompt + current_line;
    
    // Blinking cursor simulation? 
    line_view += "_";
    
    Debug_Printf(0, input_row, false, 0, "%s", line_view.c_str());
}
