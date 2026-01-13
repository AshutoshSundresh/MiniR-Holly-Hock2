#include "Keyboard.hpp"
#include <sdk/os/lcd.hpp>
#include <sdk/os/debug.hpp>

// Constants for layout
// Assuming 320x240 screen, but strictly following Hollyhock SDK
// Debug_Printf chars are ~6x8 or 8x8.
// We'll draw simple rectangles.

#define KB_START_Y 120
#define KEY_HEIGHT 20
#define KEY_MARGIN 2

// Helper for drawing rects (since SDK only has line/triangle/setPixel)
void DrawRect(int x, int y, int w, int h, int color_outline, int color_fill) {
    // Top
    line(x, y, x + w, y, color_outline);
    // Bottom
    line(x, y + h, x + w, y + h, color_outline);
    // Left
    line(x, y, x, y + h, color_outline);
    // Right
    line(x + w, y, x + w, y + h, color_outline);
    
    // Fill (simple dumb fill for now, optimize later)
    /*
    if (color_fill >= 0) {
        for(int i=y+1; i<y+h; i++) {
            line(x+1, i, x+w-1, i, color_fill);
        }
    }
    */
}

void Keyboard::Init() {
    current_tab = Tab::ABC;
    shifted = false;
    cursor_r = 0;
    cursor_c = 0;
}

const KeyGrid& Keyboard::GetCurrentGrid() {
    switch(current_tab) {
        case Tab::ABC: return Layouts::GetABC(shifted);
        case Tab::Num123: return Layouts::Get123();
        case Tab::SYM: return Layouts::GetSYM();
    }
    return Layouts::GetABC(false);
}

void Keyboard::ClampCursor() {
    const auto& grid = GetCurrentGrid();
    if (cursor_r < 0) cursor_r = grid.size() - 1;
    if (cursor_r >= (int)grid.size()) cursor_r = 0;
    
    const auto& row = grid[cursor_r];
    if (cursor_c < 0) cursor_c = row.size() - 1;
    if (cursor_c >= (int)row.size()) cursor_c = 0;
}

void Keyboard::Draw() {
    const auto& grid = GetCurrentGrid();
    
    int y = KB_START_Y;
    int screen_w = 320; // Approx
    
    for (int r = 0; r < (int)grid.size(); ++r) {
        const auto& row = grid[r];
        int num_keys = 0;
        int total_weight = 0;
        for(const auto& k : row) total_weight += k.width_mult;
        
        int key_unit_w = (screen_w - (KEY_MARGIN * 2)) / total_weight;
        int x = KEY_MARGIN;
        
        for (int c = 0; c < (int)row.size(); ++c) {
            const auto& key = row[c];
            int w = key.width_mult * key_unit_w;
            
            bool is_selected = (r == cursor_r && c == cursor_c);
            int color = is_selected ? 0b1111100000000000 : 0b1111111111111111; // Red vs White
            int text_bg = is_selected ? 1 : 0; // Invert if selected
            
            DrawRect(x, y, w - KEY_MARGIN, KEY_HEIGHT - KEY_MARGIN, color, -1);
            
            // Draw Label
            // Centering math (approximate)
            int tx = x + (w/2) - (strlen(key.label) * 4); 
            int ty = y + (KEY_HEIGHT/2) - 4;
            
            // Debug_PrintString is global grid based, hard to map to pixels exactly
            // We'll use Debug_Printf for pixel-ish positioning if available, 
            // but SDK header only shows Debug_Printf(x, y...) where x,y are likely grid.
            // Let's assume standard SDK behavior: Text is separate layer or difficult to mix.
            // But we can try Debug_Printf which takes x,y.
            // Assuming x,y in Debug_Printf are character coords (columns/rows).
            
            // Map pixel (x, y) to char coords (approx 8x12 font?)
            int char_x = x / 6; // guess
            int char_y = y / 12; // guess
            
            // Implementation detail: for now, we rely on the grid drawing 
            // and assume the user can see which key is selected by the rectangle color.
            // Printing text inside might overlap if we aren't careful.
            
            // HACK: Just print text at computed grid positions
             Debug_Printf(char_x, char_y, text_bg, 0, "%s", key.label);
            
            x += w;
        }
        y += KEY_HEIGHT;
    }
}

bool Keyboard::HandleInput(uint32_t key1, uint32_t key2, Console& console) {
    if (testKey(key1, key2, KEY_UP)) {
        cursor_r--; ClampCursor(); 
        // Try to keep relative column position approx? For now just clamp.
        return true;
    }
    if (testKey(key1, key2, KEY_DOWN)) {
        cursor_r++; ClampCursor(); return true;
    }
    if (testKey(key1, key2, KEY_LEFT)) {
        cursor_c--; ClampCursor(); return true;
    }
    if (testKey(key1, key2, KEY_RIGHT)) {
        cursor_c++; ClampCursor(); return true;
    }
    
    if (testKey(key1, key2, KEY_EXE)) {
        const auto& grid = GetCurrentGrid();
        ExecuteKey(grid[cursor_r][cursor_c], console);
        return true;
    }
    
    // Shift/Alpha as shortcut to tabs?
    if (testKey(key1, key2, KEY_SHIFT)) {
        shifted = !shifted;
        return true;
    }
    
    return false;
}

void Keyboard::ExecuteKey(const Key& key, Console& console) {
    if (key.type == KeyType::Character) {
        console.AppendInput(key.value);
    } else if (key.type == KeyType::TabSwitch) {
        if (key.command_id == CMD_TAB_ABC) current_tab = Tab::ABC;
        else if (key.command_id == CMD_TAB_123) current_tab = Tab::Num123;
        else if (key.command_id == CMD_TAB_SYM) current_tab = Tab::SYM;
        
        // Reset cursor to 0,0 on tab switch to avoid out of bounds
        cursor_r = 0; cursor_c = 0;
    } else if (key.type == KeyType::Command) {
        switch(key.command_id) {
            case CMD_ENTER: console.Enter(); break;
            case CMD_BACKSPACE: console.Backspace(); break;
            case CMD_SPACE: console.AppendInput(" "); break;
            case CMD_SHIFT: shifted = !shifted; break;
        }
    }
}
