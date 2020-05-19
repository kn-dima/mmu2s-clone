﻿/*********************************************************************************************************
* MMU2 Clone Controller Version
**********************************************************************************************************
*
* Actual Code developed by Jeremy Briffaut
* Initial Code developed by Chuck Kozlowski
*/

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "application.h"

#include "config.h"
#include "msc_sd.h"

/*************** */
char cstr[16];
#include "print.h"
IOPrint ioprint;
/*************** */

int command = 0;

// absolute position of bearing stepper motor
// this line calibrated for my unit
int idlerPosCoord[6] = {14, 33, 55, 78, 101, 133};

// absolute position of selector stepper motor
// this line calibrated for my unit with cutted T8 POM nut instead of Prusa nut
int selectorPosCoord[6] = {30, 377, 714, 1066, 1418, 1888};

int loadLengthAfterFinda = 20;
int extruderFeedLen[5] = {600, 600, 600, 600, 600};

//stepper direction
#define CW 0
#define CCW 1

// used for 3 states of the idler stepper motor (
#define INACTIVE 0	// parked
#define ACTIVE 1	  // not parked
#define QUICKPARKED 2 // quick parked

#define STOP_AT_EXTRUDER 1
#define IGNORE_STOP_AT_EXTRUDER 0

//int trackToolChanges = 0;
int extruderMotorStatus = INACTIVE;

int repeatTCmdFlag = INACTIVE; // used by the 'C' command processor to avoid processing multiple 'C' commands

int idlerCoord = 0; // this tracks the roller bearing position (top motor on the MMU)
int selectorPos = 0;  // keep track of filament selection (0,1,2,3,4))
int ejectPos = 0; // store ejected filament position for 'R' command
int dummy[100];
int idlerPos = 0;

int firstTimeFlag = 0;
int earlyCommands = 0; // forcing communications with the mk3 at startup

//int toolChangeCount = 0;

char receivedChar;
boolean newData = false;
int idlerStatus = INACTIVE;
int selectorStatus = INACTIVE;
String lastCommand = "Z\r\n";

/*****************************************************
 *
 * Init the MMU, pin, Serial, ...
 *
 *****************************************************/
void Application::setup()
{
	/************/
	ioprint.setup();
	/************/

	pinMode(idlerDirPin, OUTPUT);
	pinMode(idlerStepPin, OUTPUT);
	pinMode(idlerEnablePin, OUTPUT);

	pinMode(findaPin, INPUT);					// MMU pinda Filament sensor
	pinMode(filamentSwitch, INPUT);				// extruder Filament sensor
	pinMode(colorSelectorEnstop, INPUT_PULLUP); // enstop switch sensor

	pinMode(extruderEnablePin, OUTPUT);
	pinMode(extruderDirPin, OUTPUT);
	pinMode(extruderStepPin, OUTPUT);

	pinMode(colorSelectorEnablePin, OUTPUT);
	pinMode(colorSelectorDirPin, OUTPUT);
	pinMode(colorSelectorStepPin, OUTPUT);

	pinMode(yminLED, OUTPUT); // green LED used for debug purposes
	pinMode(zminLED, OUTPUT); // green LED used for debug purposes
	pinMode(bedLED, OUTPUT); // green LED used for debug purposes
	pinMode(h0LED, OUTPUT); // green LED used for debug purposes
	pinMode(fanLED, OUTPUT); // green LED used for debug purposes

	println_log(F("finished setting up input and output pins"));

	// Turn OFF all three stepper motors (heat protection)
	digitalWrite(idlerEnablePin, DISABLE);		   // DISABLE the roller bearing motor (motor #1)
	digitalWrite(extruderEnablePin, DISABLE);	  //  DISABLE the extruder motor  (motor #2)
	digitalWrite(colorSelectorEnablePin, DISABLE); // DISABLE the color selector motor  (motor #3)

	delay(200);

	// Initialize stepper
	println_log(F("Syncing the Idler Selector Assembly")); // do this before moving the selector motor
	initIdlerPosition();								   // reset the roller bearing position

	println_log(F("Syncing the Filament Selector Assembly"));
	if (!isFilamentLoadedPinda())
	{
		initColorSelector(); // reset the color selector if there is NO filament present
	}
	else
	{
		println_log(F("Unable to clear the Color Selector, please remove filament"));
	}

    MSC_SD_init();

	println_log(MMU2_VERSION);

	Serial1.begin(115200); // Hardware serial interface (mmu<->printer board)
	delay(100);

	println_log(F("Sending START command to mk3 controller board"));

	// ***************************************
	// THIS NEXT COMMAND IS CRITICAL ... IT TELLS THE MK3 controller that an MMU is present
	// ***************************************
	Serial1.print(F("\nstart\n")); // attempt to tell the mk3 that the mmu is present

	println_log(F("Inialialization Complete, let's multicolor print ...."));

} // end of init() routine

/*****************************************************
 * 
 * infinite loop - core of the program
 *
 *****************************************************/
void Application::loop()
{
	// wait for 100 milliseconds
	delay(1);
	// check the serial interface for input commands from the mk3
	checkSerialInterface();
    MarlinMSC.loop();

#ifdef SERIAL_DEBUG
	// check for keyboard input
	checkDebugSerialInterface();
#endif

} // end of infinite loop

/***************************************************************************************************************
 ***************************************************************************************************************
 * 
 * SERIAL
 * 
 ***************************************************************************************************************
 **************************************************************************************************************/

/*****************************************************
 * 
 * Serial read until new line
 * 
 *****************************************************/
String ReadSerialStrUntilNewLine()
{
	String str = "";
	char c;
	while (Serial.available())
	{
		c = char(Serial.read());
		str += c;
	}
	if ((c == '\n') || (c == '\r'))
	{
		return str;
	}
	return "";
}

/*****************************************************
 *
 * Handle command from the Printer
 * 
 *****************************************************/
void checkSerialInterface()
{
	int cnt;
	String inputLine;
	int index;

	index = 0;
	if ((cnt = Serial1.available()) > 0)
	{

		inputLine = Serial1.readString(); // fetch the command from the mmu2 serial input interface

		if (inputLine[0] != 'P')
		{
			print_log(F("MMU Command: "));
			println_log(inputLine);
		}
	process_more_commands: // parse the inbound command
		unsigned char c1, c2;

		c1 = inputLine[index++]; // fetch single characer from the input line
		c2 = inputLine[index++]; // fetch 2nd character from the input line
		inputLine[index++];		 // carriage return

		// process commands coming from the mk3 controller
		//***********************************************************************************
		// Commands still to be implemented:
		// X0 (MMU Reset)
		// F0 (Filament type select),
		//***********************************************************************************
		switch (c1)
		{
		case 'T':
			if (toolChange(c2 - 0x30))
			{
				Serial1.print(F("ok\n")); // send command acknowledge back to mk3 controller
			}
			break;
		case 'L':
			if (loadToFindaAndUnloadFromSelector(c2 - 0x30))
				Serial1.print(F("ok\n"));
			break;
		case 'M':
			//todo: M command
			//! M0 set TMC2130 to normal mode
			//! M1 set TMC2130 to stealth mode
			//response "ok\n"
			break;
		case 'U':
			if (unloadFilament())
			{
				Serial1.print(F("ok\n"));
			}
			break;
		case 'X':
			//todo: X0 command
			//! X0 MMU reset
			break;
		case 'P':
			// check FINDA status
			if (!isFilamentLoadedPinda())
			{
				Serial1.print(F("0"));
			}
			else
			{
				Serial1.print(F("1"));
			}
			Serial1.print(F("ok\n"));
			break;
		case 'S':
			// request for firmware version
			switch (c2)
			{
			case '0':
				println_log(F("S: Sending back OK to MK3"));
				Serial1.print(F("ok\n"));
				break;
			case '1':
				println_log(F("S: FW Version Request"));
				Serial1.print(FW_VERSION);
				Serial1.print(F("ok\n"));
				break;
			case '2':
				println_log(F("S: Build Number Request"));
				println_log(F("Initial Communication with MK3 Controller: Successful"));
				Serial1.print(FW_BUILDNR);
				Serial1.print(F("ok\n"));
				break;
			//! S3 Read drive errors
			default:
				println_log(F("S: Unable to process S Command"));
				break;
			}
			break;
		case 'F':
			//todo: 'F' command is acknowledged but no processing goes on at the moment
			// will be useful for flexible material down the road
			//"F%d %d"
			//! F<nr.> \<type\> filament type. <nr.> filament number, \<type\> 0, 1 or 2. Does nothing.
			println_log(F("Filament Type Selected: "));
			println_log(c2);
			Serial1.print(F("ok\n")); // send back OK to the mk3
			break;
		case 'C':
			// move filament from selector ALL the way to printhead
			if (filamentLoadWithBondTechGear())
				Serial1.print(F("ok\n"));
			break;
		
		case 'E':
			ejectFilament(c2 - 0x30);
			Serial1.print(F("ok\n"));
			break;

		case 'R':
			recoverAfterEject();
			Serial1.print(F("ok\n"));
			break;

		case 'W': //todo: W0 Wait for user click
			break;

		case 'K': //todo: K<nr.> cut filament
			//response "ok\n"
			break;

		default:
			print_log(F("ERROR: unrecognized command from the MK3 controller"));
			//Serial1.print(F("ok\n"));
		} // end of switch statement

	} // end of cnt > 0 check

	if (index < cnt)
	{
		goto process_more_commands;
	}
	// }  // check for early commands
}

void checkDebugSerialInterface()
{
	String kbString;

	kbString = ReadSerialStrUntilNewLine();
	if (kbString == "") return;

	if ((kbString[0] == '\r') || (kbString[0] == '\n')) kbString = lastCommand;

	switch(kbString[0])
	{
		case '-':
			prevTool();
			break;
		case '+':
			nextTool();
			break;
		case 'A':
			toolChangeCycleA();
			break;
		case 'C':
			filamentLoadWithBondTechGear();
			break;
		case 'D':
			toolChangeCycleD();
			break;
		case 'I':
			if ((kbString[1] >= '0') && (kbString[1] <= '5'))
			{
				println_log(F("Move idler."));
				moveIdler(kbString[1] - 0x30);
			}
			else if (kbString[1] == '+')
			{
				println_log(F("Adjust idler position closer."));
				idlerturnamount(1, CW);
				idlerPosCoord[idlerPos] += 1;
			}
			else if (kbString[1] == '-')
			{
				println_log(F("Adjust idler position further."));
				idlerturnamount(1, CCW);
				idlerPosCoord[idlerPos] -= 1;
			}
			break;
		case 'L':
			if (loadToFindaAndUnloadFromSelector(kbString[1] - 0x30))
				println_log(F("ok\n"));
			break;
		case 'P':
			println_log(F("Park selector and idler"));
			park();
			break;
		case 'S':
			if ((kbString[1] >= '0') && (kbString[1] <= '5'))
			{
				println_log(F("Move selector."));
				moveSelector(kbString[1] - 0x30);
			}
			else if (kbString[1] == '+')
			{
				println_log(F("Adjust selector position to right."));
				csTurnAmount(5, CCW);
				selectorPosCoord[selectorPos] += 5;
			}
			else if (kbString[1] == '-')
			{
				println_log(F("Adjust selector position to left."));
				csTurnAmount(5, CW);
				selectorPosCoord[selectorPos] -= 5;
			}
			break;
		case 'T':
			if (kbString[1] == '+')
			{
				println_log(F("Increase loading length."));
				feedFilament(STEPSPERMM, IGNORE_STOP_AT_EXTRUDER);
				extruderFeedLen[selectorPos] += 1;
			}
			else if (kbString[1] == '-')
			{
				println_log(F("Decrease loading length."));
				feedFilament(-STEPSPERMM, IGNORE_STOP_AT_EXTRUDER);
				extruderFeedLen[selectorPos] -= 1;
			}
			else
			{
				if (toolChange(kbString[1] - 0x30))
					println_log(F("ok\n"));
			}
			break;
		case 'U':
			unloadFilament(); //unload the filament
			break;
		case 'Z':
			printStatus();
			break;
		default:
			printHelp();
			break;
	}
	lastCommand = kbString;
}

/*****************************************************
 *
 * this routine is the common routine called for fixing the filament issues (loading or unloading)
 *
 *****************************************************/
void fixTheProblem(String statement)
{
	println_log(F(""));
	println_log(F("********************* ERROR ************************"));
	println_log(statement); // report the error to the user
	println_log(F("********************* ERROR ************************"));
	println_log(F("Clear the problem and then hit any key to continue "));
	println_log(F(""));
	println_log(F("PINDA | EXTRUDER"));
	isFilamentLoadedPinda() ? print_log(F("ON    | ")) : print_log(F("OFF   | "));
	isFilamentLoadedtoExtruder() ? println_log(F("ON")) : println_log(F("OFF"));
	println_log(F(""));
	//FIXME
	// IF POSSIBLE : 
	// SYNC COLORSELECTOR
	// SYNC IDLER
	parkIdler();								   // park the idler stepper motor
	digitalWrite(colorSelectorEnablePin, DISABLE); // turn off the selector stepper motor

#ifdef SERIAL_DEBUG
	while (!Serial.available())
	{
		//  wait until key is entered to proceed  (this is to allow for operator intervention)
	}
	Serial.readString(); // clear the keyboard buffer
#endif

	unParkIdler();								  // put the idler stepper motor back to its' original position
	digitalWrite(colorSelectorEnablePin, ENABLE); // turn ON the selector stepper motor
	delay(1);									  // wait for 1 millisecond
}

/***************************************************************************************************************
 ***************************************************************************************************************
 * 
 * COLOR SELECTOR
 * 
 ***************************************************************************************************************
 **************************************************************************************************************/

/*****************************************************
 *
 * Select the color : selection (0..4)
 * 
 *****************************************************/

/*****************************************************
 *
 *
 *****************************************************/
void deActivateColorSelector()
{
//FIXME : activate it by default
#ifdef TURNOFFSELECTORMOTOR
	digitalWrite(colorSelectorEnablePin, DISABLE); // turn off the color selector stepper motor  (nice to do, cuts down on CURRENT utilization)
	delay(1);
	colorSelectorStatus = INACTIVE;
#endif
}

/*****************************************************
 *
 *
 *****************************************************/
void activateColorSelector()
{
	digitalWrite(colorSelectorEnablePin, ENABLE);
	delay(1);
	selectorStatus = ACTIVE;
}

void moveSelector(int newPos)
{
	if ((newPos < 0) || (newPos > 5))
	{
		print_log(F("moveSelector():  Error, invalid position"));
		return;
	}
loop:
	if (isFilamentLoadedPinda())
	{
		fixTheProblem("colorSelector(): Error, filament is present between the MMU2 and the MK3 Extruder:  UNLOAD FILAMENT!!");
		goto loop;
	}

	int oldCoord = selectorPosCoord[selectorPos];
	int newCoord = selectorPosCoord[newPos];
	if (newCoord >= oldCoord)
	{
		csTurnAmount((newCoord - oldCoord), CCW);
	}
	else
	{
		csTurnAmount((oldCoord - newCoord), CW);
	}
	selectorPos = newPos;
} // end of colorSelector routine()

/*****************************************************
 *
 * this is the selector motor with the lead screw (final stage of the MMU2 unit)
 * 
 *****************************************************/
void csTurnAmount(int steps, int dir)
{

	digitalWrite(colorSelectorEnablePin, ENABLE); // turn on the color selector motor
	digitalWrite(colorSelectorDirPin, dir); // set the direction for the Color Extruder Stepper Motor
	// FIXME ??? NEEDED ???
	// wait 1 milliseconds
	delayMicroseconds(1500); // changed from 500 to 1000 microseconds on 10.6.18, changed to 1500 on 10.7.18)

	for (uint16_t i = 0; i <= (steps * STEPSIZE); i++)
	{
		digitalWrite(colorSelectorStepPin, HIGH);
		delayMicroseconds(PINHIGH); // delay for 10 useconds
		digitalWrite(colorSelectorStepPin, LOW);
		delayMicroseconds(PINLOW);					// delay for 10 useconds
		delayMicroseconds(SELECTORMOTORDELAY); // wait for 60 useconds
		//add enstop
		if ((digitalRead(colorSelectorEnstop) == LOW) && (dir == CCW))
			break;
	}

#ifdef TURNOFFSELECTORMOTOR
	digitalWrite(colorSelectorEnablePin, DISABLE); // turn off the color selector motor
#endif
}

/*****************************************************
 *
 * Home the Color Selector
 * perform this function only at power up/reset
 * 
 *****************************************************/
void initColorSelector()
{

	digitalWrite(colorSelectorEnablePin, ENABLE);		   // turn on the stepper motor
	delay(1);											   // wait for 1 millisecond
	csTurnAmount(selectorPosCoord[5], CW);                   // move to the left
	csTurnAmount(selectorPosCoord[5], CCW); // move all the way to the right
	digitalWrite(colorSelectorEnablePin, DISABLE);		   // turn off the stepper motor
	selectorPos = 5;
}

/*****************************************************
 *
 * Re-Sync Color Selector
 * this function is performed by the 'T' command after so many moves to make sure the colorselector is synchronized
 *
 *****************************************************/
void syncColorSelector()
{
	digitalWrite(colorSelectorEnablePin, ENABLE); // turn on the selector stepper motor
	delay(1);									  // wait for 1 millecond

	print_log(F("syncColorSelelector()   current Filament selection: "));
	println_log(selectorPos);

	csTurnAmount(selectorPosCoord[selectorPos], CW);	// move all the way to the left
	csTurnAmount(selectorPosCoord[5], CCW); 			// move all the way to the right
														//FIXME : turn off motor ???
														//digitalWrite(colorSelectorEnablePin, DISABLE); // turn off the stepper motor
}

/***************************************************************************************************************
 ***************************************************************************************************************
 * 
 * IDLER
 * 
 ***************************************************************************************************************
 **************************************************************************************************************/

/*****************************************************
 *
 * Home the idler
 * perform this function only at power up/reset
 *
 *****************************************************/
void initIdlerPosition()
{

	digitalWrite(idlerEnablePin, ENABLE); // turn on the roller bearing motor
	delay(1);
	idlerturnamount(idlerPosCoord[5], CCW);
	idlerturnamount(idlerPosCoord[5], CW); // move the bearings out of the way
	digitalWrite(idlerEnablePin, DISABLE); // turn off the idler roller bearing motor
	idlerPos = 5;
}

/*****************************************************
 *
 * this routine drives the 5 position bearings (aka idler, on the top of the MMU2 carriage)
 * newPosChar 0..4 -> the position
 * newPosChar 5 -> parking position
 *****************************************************/
void moveIdler(int newPos)
{
	if ((newPos < 0) || (newPos > 5))
	{
		println_log(F("moveIdler() ERROR, invalid filament selection"));
		return;
	}

	digitalWrite(extruderEnablePin, ENABLE);

	int travelSteps = idlerPosCoord[newPos] - idlerPosCoord[idlerPos];
	if (travelSteps < 0)
		idlerturnamount(-travelSteps, CCW); // turn idler to appropriate position
	else
		idlerturnamount(travelSteps, CW); // turn idler to appropriate position

	idlerPos = newPos;
}

/*****************************************************
 *
 * turn the idler stepper motor
 * 
 *****************************************************/
void idlerturnamount(int steps, int dir)
{
	digitalWrite(idlerEnablePin, ENABLE); // turn on motor
	digitalWrite(idlerDirPin, dir);
	delay(1); // wait for 1 millisecond

	// these command actually move the IDLER stepper motor
	for (uint16_t i = 0; i < steps * STEPSIZE; i++)
	{
		digitalWrite(idlerStepPin, HIGH);
		delayMicroseconds(PINHIGH); // delay for 10 useconds
		digitalWrite(idlerStepPin, LOW);
		//delayMicroseconds(PINLOW);               // delay for 10 useconds
		delayMicroseconds(IDLERMOTORDELAY);
	}
} // end of idlerturnamount() routine

/***************************************************************************************************************
 ***************************************************************************************************************
 * 
 * EXTRUDER
 * 
 ***************************************************************************************************************
 **************************************************************************************************************/

/*****************************************************
 *
 * this routine feeds filament by the amount of steps provided
 * stoptoextruder when mk3 switch detect it (only if switch is before mk3 gear)
 * 144 steps = 1mm of filament (using the current mk8 gears in the MMU2)
 *
 *****************************************************/
void feedFilament(int steps, int stoptoextruder)
{
	if (steps > 0)
	{
		digitalWrite(extruderDirPin, CW); // set the direction of the MMU2 extruder motor
	}
	else
	{
		digitalWrite(extruderDirPin, CCW); // set the direction of the MMU2 extruder motor
	}
	delay(1);
	for (unsigned int i = 0; i <= abs(steps); i++)
	{
		digitalWrite(extruderStepPin, HIGH);
		delayMicroseconds(PINHIGH); // delay for 10 useconds
		digitalWrite(extruderStepPin, LOW);
		delayMicroseconds(PINLOW); // delay for 10 useconds

		delayMicroseconds(EXTRUDERMOTORDELAY); // wait for 400 useconds
		//delay(delayValue);           // wait for 30 milliseconds
		if ((stoptoextruder) && isFilamentLoadedtoExtruder())
			break;
	}
}

/***************************************************************************************************************
 ***************************************************************************************************************
 * 
 * FILAMENT SENSORS
 * 
 ***************************************************************************************************************
 **************************************************************************************************************/

/*****************************************************
 *
 * Check if Filament is loaded into MMU pinda
 *
 *****************************************************/
int isFilamentLoadedPinda()
{
	int findaStatus;
	findaStatus = digitalRead(findaPin);
	return (findaStatus);
}

/*****************************************************
 *
 * Check if Filament is loaded into extruder
 *
 *****************************************************/
bool isFilamentLoadedtoExtruder()
{
	int fStatus;
	fStatus = digitalRead(filamentSwitch);
	return (fStatus == filamentSwitchON);
}

/***************************************************************************************************************
 ***************************************************************************************************************
 * 
 * LOAD / UNLOAD FILAMENT
 * 
 ***************************************************************************************************************
 **************************************************************************************************************/

/*****************************************************
 *
 * Load the Filament using the FINDA and go back to MMU
 * 
 *****************************************************/
bool loadFilamentToFinda()
{
	unsigned long startTime, currentTime;

	digitalWrite(extruderEnablePin, ENABLE);

	startTime = millis();

loop:
	currentTime = millis();
	if ((currentTime - startTime) > 10000)
	{ // 10 seconds worth of trying to load the filament
		println_log(F("loadFilamentToFinda ERROR:   timeout error, filament is not loaded to the FINDA sensor"));
		return false;
	}

	// go 144 steps (1 mm) and then check the finda status
	feedFilament(STEPSPERMM, STOP_AT_EXTRUDER);

	// keep feeding the filament until the pinda sensor triggers
	if (!isFilamentLoadedPinda())
		goto loop;
	return true;
}

/*****************************************************
 *
 * unload Filament using the FINDA sensor and push it in the MMU
 * 
 *****************************************************/
bool unloadFilament()
{
	unsigned long startTime, currentTime, startTime1;
	// if the filament is already unloaded, do nothing
	if (!isFilamentLoadedPinda())
	{
		println_log(F("unloadFilamentToFinda():  filament already unloaded"));
		return true;
	}

	moveIdler(selectorPos);

	digitalWrite(extruderEnablePin, ENABLE); // turn on the extruder motor

	startTime = millis();
	startTime1 = millis();

loop:

	currentTime = millis();

	// read the filament switch (on the top of the mk3 extruder)
	if (isFilamentLoadedtoExtruder())
	{
		// filament Switch is still ON, check for timeout condition
		if ((currentTime - startTime1) > 2000)
		{ // has 2 seconds gone by ?
			println_log(F("unloadFilamentToFinda(): UNLOAD FILAMENT ERROR: filament not unloading properly, stuck in mk3 head"));
			return false;
		}
	}
	else
	{
		// check for timeout waiting for FINDA sensor to trigger
		if ((currentTime - startTime) > TIMEOUT_LOAD_UNLOAD)
		{
			moveIdler(5);
			// 10 seconds worth of trying to unload the filament
			println_log(F("unloadFilamentToFinda(): UNLOAD FILAMENT ERROR: filament is not unloading properly, stuck between mk3 and mmu2"));
			return false;
		}
	}

	feedFilament(-STEPSPERMM, IGNORE_STOP_AT_EXTRUDER); // 1mm and then check the pinda status

	// keep unloading until we hit the FINDA sensor
	if (isFilamentLoadedPinda())
	{
		goto loop;
	}

	// back the filament away from the selector by UNLOAD_LENGTH_BACK_COLORSELECTOR mm
	digitalWrite(extruderDirPin, CCW);
	feedFilament(-STEPSPERMM * UNLOAD_LENGTH_BACK_COLORSELECTOR, IGNORE_STOP_AT_EXTRUDER);
	moveIdler(5);
	return true;
}

/***************************************************************************************************************
 ***************************************************************************************************************
 * 
 * PARK / UNPARK IDLER
 * 
 ***************************************************************************************************************
 **************************************************************************************************************/

/*****************************************************
 *
 * move the filament Roller pulleys away from the filament
 *
 *****************************************************/
void parkIdler()
{
	int newSetting;

	digitalWrite(idlerEnablePin, ENABLE);
	delay(1);

	newSetting = idlerPosCoord[5] - idlerCoord;
	idlerCoord = idlerPosCoord[5]; // record the current roller status  (CSK)

	idlerturnamount(newSetting, CW); // move the bearing roller out of the way
	idlerStatus = INACTIVE;
}

/*****************************************************
 *
 *
 *****************************************************/
void unParkIdler()
{
	int rollerSetting;

	digitalWrite(idlerEnablePin, ENABLE); // turn on (enable) the roller bearing motor
	delay(1);							  // wait for 10 useconds

	rollerSetting = idlerPosCoord[5] - idlerPosCoord[selectorPos];
	idlerCoord = idlerPosCoord[selectorPos]; // update the idler bearing position

	idlerturnamount(rollerSetting, CCW); // restore the old position
	idlerStatus = ACTIVE;				// mark the idler as active

	digitalWrite(extruderEnablePin, ENABLE); // turn on (enable) the extruder stepper motor as well
}

/*****************************************************
 *
 * attempt to disengage the idler bearing after a 'T' command instead of parking the idler
 * this is trying to save significant time on re-engaging the idler when the 'C' command is activated
 *
 *****************************************************/
void quickParkIdler()
{
	return;
	/* 
	digitalWrite(idlerEnablePin, ENABLE); // turn on the idler stepper motor
	delay(1);
	
	idlerturnamount(IDLERSTEPSIZE, CW);

	idlerCoord = idlerCoord + IDLERSTEPSIZE; // record the current position of the IDLER bearing
	idlerStatus = QUICKPARKED;								 // use this new state to show the idler is pending the 'C0' command

	//FIXME : Turn off idler ?
	//digitalWrite(idlerEnablePin, DISABLE);    // turn off the roller bearing stepper motor  (nice to do, cuts down on CURRENT utilization)
	digitalWrite(extruderEnablePin, DISABLE); // turn off the extruder stepper motor as well
	*/
}

/*****************************************************
 *
 * this routine is called by the 'C' command to re-engage the idler bearing
 *
 * FIXME: needed ?
 *****************************************************/
void quickUnParkIdler()
{
	return;
	/*
	int rollerSetting;

	rollerSetting = idlerCoord - IDLERSTEPSIZE; // go back IDLERSTEPSIZE units (hopefully re-enages the bearing

	idlerturnamount(IDLERSTEPSIZE, CCW); // restore old position

	print_log(F("quickunparkidler(): oldBearingPosition"));
	println_log(idlerCoord);

	idlerCoord = rollerSetting - IDLERSTEPSIZE; // keep track of the idler position

	idlerStatus = ACTIVE; // mark the idler as active
	*/
}

/***************************************************************************************************************
 ***************************************************************************************************************
 * 
 * TOOLCHANGE / LOAD TO MK3 / LOAD TO BONTECH GEAR
 * 
 ***************************************************************************************************************
 **************************************************************************************************************/

/*****************************************************
 *
 * (T) Tool Change Command - this command is the core command used my the mk3 to drive the mmu2 filament selection
 *
 *****************************************************/
bool toolChange(int newPos)
{
	if ((newPos < 0) || (newPos > 4))
	{
		println_log(F("T: Invalid filament Selection"));
		return false;
	}

	if (!unloadFilament())
	{
		moveIdler(5); // park idler
		println_log(F("T: Filament unloading error"));
		return false;
	}

	moveIdler(newPos);
	moveSelector(newPos);
	if (!loadFilamentToFinda())
	{
		moveIdler(5); // park idler
		return false;
	}
	if (!filamentLoadToMK3())
	{
		moveIdler(5); // park idler
		return false;
	}
	return true;
} // end of ToolChange processing

/*****************************************************
 *
 * this routine is executed as part of the 'T' Command (Load Filament)
 *
 *****************************************************/
bool filamentLoadToMK3()
{
#ifdef FILAMENTSWITCH_BEFORE_EXTRUDER
	int flag;
	int fStatus;
	int filamentDistance;
#endif
	int startTime, currentTime;

	digitalWrite(extruderEnablePin, ENABLE); // turn on the extruder stepper motor (10.14.18)

	startTime = millis();

loop:
	feedFilament(STEPSPERMM, IGNORE_STOP_AT_EXTRUDER); // feed 1 mm of filament into the bowden tube

	currentTime = millis();

	// added this timeout feature on 10.4.18 (2 second timeout)
	if ((currentTime - startTime) > 2000)
	{
		println_log(F("FILAMENT LOAD ERROR:  Filament not detected by FINDA sensor, check the selector head in the MMU2"));
		return false;
	}
	// keep feeding the filament until the pinda sensor triggers
	if (!isFilamentLoadedPinda())
		goto loop;

	// go DIST_MMU_EXTRUDER mm
	feedFilament(STEPSPERMM * extruderFeedLen[selectorPos], STOP_AT_EXTRUDER);

#ifdef FILAMENTSWITCH_BEFORE_EXTRUDER
	// insert until the 2nd filament sensor
	filamentDistance = DIST_MMU_EXTRUDER;
	startTime = millis();
	flag = 0;

	// wait until the filament sensor on the mk3 extruder head (microswitch) triggers
	while (flag == 0)
	{

		currentTime = millis();
		if ((currentTime - startTime) > TIMEOUT_LOAD_UNLOAD)
		{
			fixTheProblem("FILAMENT LOAD ERROR: Filament not detected by the MK3 filament sensor, check the bowden tube for clogging/binding");
			startTime = millis(); // reset the start Time
		}
		feedFilament(STEPSPERMM, STOP_AT_EXTRUDER); // step forward 1 mm
		filamentDistance++;
		// read the filament switch on the mk3 extruder
		if (isFilamentLoadedtoExtruder())
		{
			flag = 1;
			print_log(F("Filament distance traveled (mm): "));
			println_log(filamentDistance);
		}
	}

	// feed filament an additional DIST_EXTRUDER_BTGEAR mm to hit the middle of the bondtech gear
	// go an additional DIST_EXTRUDER_BTGEAR
	feedFilament(STEPSPERMM * DIST_EXTRUDER_BTGEAR, IGNORE_STOP_AT_EXTRUDER);
#endif
	return true;
}

/*****************************************************
 *
 * part of the 'C' command,  does the last little bit to load into the past the extruder gear
 *
 *****************************************************/
bool filamentLoadWithBondTechGear()
{
	// added this code snippet to not process a 'C' command that is essentially a repeat command
	if (repeatTCmdFlag == ACTIVE)
	{
		println_log(F("filamentLoadWithBondTechGear(): filament already loaded and 'C' command already processed"));
		repeatTCmdFlag = INACTIVE;
		return false;
	}

	if (!isFilamentLoadedPinda())
	{
		println_log(F("filamentLoadWithBondTechGear()  Error, filament sensor thinks there is no filament"));
		return false;
	}

	moveIdler(selectorPos);

	digitalWrite(yminLED, HIGH); // turn on the green LED (for debug purposes)
	digitalWrite(extruderEnablePin, ENABLE); // turn on the extruder stepper motor

	// feed the filament from the MMU2 into the bondtech gear
	feedFilament(STEPSPERMM * DIST_EXTRUDER_BTGEAR, IGNORE_STOP_AT_EXTRUDER);
	digitalWrite(yminLED, LOW); // turn off the green LED (for debug purposes)

#ifdef DEBUG
	println_log(F("C Command: parking the idler"));
#endif

	moveIdler(5);

#ifdef FILAMENTSWITCH_ON_EXTRUDER
	//Wait for MMU code in Marlin to load the filament and activate the filament switch
	delay(FILAMENT_TO_MK3_C0_WAIT_TIME);
	if (isFilamentLoadedtoExtruder())
	{
		println_log(F("filamentLoadWithBondTechGear(): Loading Filament to Print Head Complete"));
		return true;
	}
	println_log(F("filamentLoadWithBondTechGear() : FILAMENT LOAD ERROR:  Filament not detected by EXTRUDER sensor, check the EXTRUDER"));
	return false;
#endif

#ifdef DEBUG
	println_log(F("filamentLoadWithBondTechGear(): Loading Filament to Print Head Complete"));
#endif
	return true;
}

void printHelp()
{
	println_log(F("Available commands:"));
	println_log(F("'+' - Select next tool."));
	println_log(F("'-' - Select previous tool."));
	println_log(F("'A' - Tool change in cycle."));
	println_log(F("'C' - Load filament into extruder"));
	delay(100);
	println_log(F("'D' - Load and unload all filaments in cycle."));
	println_log(F("'I0'-'I5' - Move idler to position (5 = park position)."));
	println_log(F("'I+' - Adjust idler position for current slot one step close to selector."));
	println_log(F("'I-' - Adjust idler position for current slot one step further from selector."));
	println_log(F("'L' - Load filament to finda and feed to selector edge."));
	delay(100);
	println_log(F("'P' - Park idler and selector. Last one only if no filament in sensor."));
	println_log(F("'S0'-'S5' - Move selector to position (5 = park position)."));
	println_log(F("'S+' - Adjust selector position five steps right."));
	println_log(F("'S-' - Adjust selector position five steps left."));
	delay(100);
	println_log(F("'T0'-'T4' - Tool change."));
	println_log(F("'T+' - increase loading length."));
	println_log(F("'T-' - decrease loading length."));
	println_log(F("'U' - Unload filament"));
	println_log(F("'Z' - Status"));
}

void toolChangeCycleA()
{
	println_log(F("Processing 'A' Command. Tool change in cycle."));
	println_log(F("initColorSelector"));
	initColorSelector();
	println_log(F("initIdlerPosition"));
	initIdlerPosition();
	println_log(F("T0"));
	toolChange(0);
	delay(2000);
	println_log(F("T1"));
	toolChange(1);
	delay(2000);
	println_log(F("T2"));
	toolChange(2);
	delay(2000);
	println_log(F("T3"));
	toolChange(3);
	delay(2000);
	println_log(F("T4"));
	toolChange(4);
	delay(2000);
	println_log(F("T0"));
	toolChange(0);
	delay(2000);

	if (idlerStatus == QUICKPARKED)
	{
		quickUnParkIdler(); // un-park the idler from a quick park
	}
	if (idlerStatus == INACTIVE)
	{
		unParkIdler(); // turn on the idler motor
	}
	unloadFilament(); //unload the filament
	parkIdler();			 // park the idler motor and turn it off
}

void toolChangeCycleD()
{
	println_log(F("Processing 'D' Command. Load and unload all filaments in cycle."));
	int cnt = 0;
	while(1)
	for (int i = 0; i < 5; i++)
	{
		moveIdler(i);
		moveSelector(i);
		if (!loadFilament()) return;
		filamentLoadWithBondTechGear();
		//filamentLoadToMK3();
		if (!unloadFilament()) return;
		print_log(F("Cycles count = "));
		println_log(++cnt);
		//delay(5000);
	}
}

void printStatus()
{
	println_log(F("idler positions array"));
	for (int i = 0; i <= 5; i++)
	{
	  	Serial.print(idlerPosCoord[i]);
	  	Serial.print(' ');
	}
 	println_log(' ');

	println_log(F("selector positions array"));
	for (int i = 0; i <= 5; i++)
	{
	  	Serial.print(selectorPosCoord[i]);
	  	Serial.print(' ');
	}
	println_log(' ');

	println_log(F("extruder feed length array"));
	for (int i = 0; i <= 4; i++)
	{
	  	Serial.print(extruderFeedLen[i]);
	  	Serial.print(' ');
	}
 	println_log(' ');

	print_log(F("FINDA status: "));
	int fstatus = digitalRead(findaPin);
	println_log(fstatus);
	print_log(F("colorSelectorEnstop status: "));
	int cdenstatus = digitalRead(colorSelectorEnstop);
	println_log(cdenstatus);
	print_log(F("Extruder endstop status: "));
	fstatus = digitalRead(filamentSwitch);
	Serial.println(fstatus);
	println_log(F("PINDA | EXTRUDER"));
	while (true)
	{
		isFilamentLoadedPinda() ? print_log(F("ON    | ")) : print_log(F("OFF   | "));
		isFilamentLoadedtoExtruder() ? println_log(F("ON")) : println_log(F("OFF"));
		delay(200);
		String str = ReadSerialStrUntilNewLine();
		if (str != "")
		{ 
			break;
		}
	}
}

void park()
{
	initIdlerPosition();								   // reset the roller bearing position

	if (!isFilamentLoadedPinda())
	{
		initColorSelector(); // reset the color selector if there is NO filament present
	}
	else
	{
		println_log(F("Unable to park selector, please remove filament"));
	}
}

void prevTool()
{
	if (selectorPos == 0)
	{
		toolChange(4);
	}
	else
	{
		toolChange(selectorPos - 1);
	}
}

void nextTool()
{
	if (selectorPos >= 4)
	{
		toolChange(0);
	}
	else
	{
		toolChange(selectorPos + 1);
	}
}

bool loadFilament()
{
	if ((selectorPos < 0) || (selectorPos > 4) || (selectorPos != idlerPos))
	{
		println_log(F("loadFilament error: wrong selector or idler position."));
		return false;
	}
	if (isFilamentLoadedPinda())
	{
		println_log(F("loadFilament error: already loaded."));
		return false;
	}
	if (!loadFilamentToFinda())
	{
		return false;
	}
	feedFilament(STEPSPERMM * loadLengthAfterFinda, IGNORE_STOP_AT_EXTRUDER);
	return true;
}

bool loadToFindaAndUnloadFromSelector(int newPos)
{
	if ((newPos < 0) || (newPos > 4))
	{
		println_log(F("Error: Invalid Filament Number Selected"));
		return false;
	}
	if (isFilamentLoadedPinda())
	{
		println_log(F("Error: Filament loaded"));
		return false;
	}
	moveIdler(newPos);
	moveSelector(newPos);
	if (!loadFilamentToFinda())
	{
		return false;
	}
	if (!unloadFilament())
	{
		return false;
	}
	return true;
}

void ejectFilament(int newPos)
{
	ejectPos = newPos;
	const uint8_t selector_position = (newPos <= 2) ? 4 : 0;
	if (isFilamentLoadedPinda())
	{
		unloadFilament();
	}
	moveIdler(newPos);
	moveSelector(selector_position);
	feedFilament(STEPSPERMM * DISTANCE_EJECT, IGNORE_STOP_AT_EXTRUDER);
	moveIdler(5);
}

void recoverAfterEject()
{
	moveIdler(ejectPos);
	feedFilament(-STEPSPERMM * DISTANCE_EJECT, IGNORE_STOP_AT_EXTRUDER);
	moveIdler(5);
}


/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

/*
 * Nothing to do in the constructor
 */
Application::Application()
{
	// nothing to do in the constructor
}
