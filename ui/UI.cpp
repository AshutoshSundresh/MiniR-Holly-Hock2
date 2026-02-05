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
        MiniString src = console.PopPendingCommand();
        while (!src.empty() && (src[src.size() - 1] == '\n' || src[src.size() - 1] == '\r')) {
            // emulate pop_back
            // (size is decreased inside helper)
            // easier: create a trimmed copy
            break;
        }
        // Simple trim of trailing newline/CR
        while (!src.empty()) {
            char last = src[src.size() - 1];
            if (last == '\n' || last == '\r') {
                // manual pop_back
                // (MiniString::push_back/clear used elsewhere; here we reconstruct)
                // Not super efficient but fine for small lines:
                MiniString trimmed;
                for (std::size_t i = 0; i + 1 < src.size(); ++i) {
                    trimmed.push_back(src[i]);
                }
                src = trimmed;
            } else {
                break;
            }
        }
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
                    MiniString msg("Error: ");
                    msg += res->sym_name;
                    console.PrintLine(msg.c_str());
                } else if (res && res->type != RType::NIL) {
                    MiniString out = Evaluator::ToString(res);
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
