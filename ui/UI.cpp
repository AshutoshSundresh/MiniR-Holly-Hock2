#include "UI.hpp"
#include <sdk/os/lcd.hpp>
#include "../runtime/Lexer.hpp"
#include "../runtime/Parser.hpp"
#include "../runtime/Evaluator.hpp"

void UI::Init() {
    console.Init();
    keyboard.Init();
    keyboard_active = true;
    
    // Init Runtime
    global_env = std::make_shared<RValue>(RType::ENV);
    Evaluator::InitGlobalEnv(global_env);
}

void UI::Update() {
    // Read keys
    uint32_t key1, key2;
    getKey(&key1, &key2);
    
    if (keyboard_active) {
        if (keyboard.HandleInput(key1, key2, console)) {
             // Input handled
        }
    }
    
    // Check for pending command
    if (console.HasPendingCommand()) {
        std::string src = console.PopPendingCommand();
        while (!src.empty() && (src.back() == '\n' || src.back() == '\r')) src.pop_back();
        if (src == "cls") {
            console.Clear();
        } else {
            Lexer lex(src);
            auto tokens = lex.Tokenize();
            Parser parser(tokens);
            RValuePtr ast = parser.Parse();
            if (parser.HasError()) {
                console.PrintLine(("Error: " + parser.GetError()).c_str());
            } else {
                RValuePtr res = Evaluator::Eval(ast, global_env);
                if (res && res->type == RType::ERROR) {
                    console.PrintLine(("Error: " + res->sym_name).c_str());
                } else if (res && res->type != RType::NIL) {
                    std::string out = Evaluator::ToString(res);
                    if (!out.empty()) console.PrintLine(out.c_str());
                }
            }
        }
    }
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
