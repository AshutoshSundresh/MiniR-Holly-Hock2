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

	// Put your app's code here!

	//Example for fillScreen(color);
	fillScreen(color(0,0,0));

	//Example for Debug_Printf(x,y,invert_color,0,format_string) //(small text)
	Debug_Printf(10,1,false,0,"MiniR v0.1");

	//Example for Debug_PrintString(string, invert_color) //(big text)
	Debug_SetCursorPosition(2,2);
	Debug_PrintString("Initializing...",0);

	//use this command to actually update the screen 
	LCD_Refresh();

	//Example for getKey
	while(true){
		uint32_t key1, key2;	//First create variables
		getKey(&key1, &key2);	//then read the keys
		
        // Simple exit condition for now
		if(testKey(key1, key2, KEY_CLEAR)){ 
			break;
		}
	}

	calcEnd(); //restore screen and do stuff
}
