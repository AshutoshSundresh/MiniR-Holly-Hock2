#pragma once
#include <vector>
#include <string>
#include <sdk/os/lcd.hpp>

class Console {
public:
    void Init();
    void Draw();
    
    // Input handling
    void AppendInput(const char* str);
    void Backspace();
    void Enter(); // Completes line or adds newline
    void ClearInput();
    
    // Output
    void Print(const char* str);
    void PrintLine(const char* str);
    
    std::string GetInputBuffer() const { return input_accumulator + current_line; }
    
    // Command Interface
    bool HasPendingCommand() const { return !pending_command.empty(); }
    std::string PopPendingCommand() { 
        std::string s = pending_command; 
        pending_command = ""; 
        return s; 
    }

private:
    std::vector<std::string> scrollback;
    std::string input_accumulator; // For multi-line pending statements
    std::string current_line;
    std::string pending_command; // Ready to execute
    const char* prompt = "> ";
    
    int scroll_offset = 0;
    static const int MAX_LINES = 100;
    static const int LINES_PER_SCREEN = 12; // Adjusted for font size
    
    void RenderText(int x, int y, const char* str, bool highlight = false);
};
