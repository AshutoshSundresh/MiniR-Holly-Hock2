#include "UI.hpp"
#include <sdk/os/lcd.hpp>

void UI::Init() {
    console.Init();
    keyboard.Init();
    keyboard_active = true;
}

void UI::Update() {
    // Read keys
    uint32_t key1, key2;
    getKey(&key1, &key2);
    
    if (keyboard_active) {
        keyboard.HandleInput(key1, key2, console);
    } else {
        // Handle scroll keys for console?
    }
    
    // F-Keys to toggle keyboard?
    // Not implemented yet
}

void UI::Draw() {
     // Clear screen done in main usually, but we can do part of it
     // fillScreen(color(0,0,0)); // Let's avoid clearing everything if slow
     
     // Draw Console (Top half)
     console.Draw();
     
     // Draw Keyboard (Bottom half)
     if (keyboard_active) {
         keyboard.Draw();
     }
}
