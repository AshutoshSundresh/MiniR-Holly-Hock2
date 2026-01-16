#include "Console.hpp"
#include "Keyboard.hpp"
#include "../runtime/RValue.hpp"

class UI {
public:
    void Init();
    void Update(); // Called from main loop
    void Draw();
    
private:
    Console console;
    Keyboard keyboard;
    RValuePtr global_env;
    bool keyboard_active = true;
    
    void HandlePhysicalKeys();
};
