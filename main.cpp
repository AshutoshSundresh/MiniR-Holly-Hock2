#include <appdef.hpp>
#include <sdk/calc/calc.hpp>
#include <sdk/os/lcd.hpp>
#include <sdk/os/debug.hpp>

/*
 * Fill this section in with some information about your app.
 * All fields are optional - so if you don't need one, take it out.
 */
APP_NAME("MiniR")
APP_DESCRIPTION("A mini R REPL for Hollyhock 2")
APP_AUTHOR("Ashutosh Sundresh")
APP_VERSION("0.1.0")

extern "C"
void main() {
	calcInit(); //backup screen and init some variables

	UI ui;
	ui.Init();

	//use this command to actually update the screen 
	LCD_Refresh();

	while(true){
		ui.Update();
		ui.Draw();
		LCD_Refresh();
		
		// Optional: break on specific key combination handled inside UI or here
		// For now, let's keep it running. 
		// If we need a way to exit:
		uint32_t k1, k2; 
		getKey(&k1, &k2);
		if(testKey(k1, k2, KEY_CLEAR)) break;
	}

	calcEnd(); //restore screen and do stuff
}
