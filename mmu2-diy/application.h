#ifndef APPLICATION_H
#define APPLICATION_H

#include <Arduino.h>

extern int isFilamentLoadedPinda();
extern bool isFilamentLoadedtoExtruder();

extern void initIdlerPosition();
extern void checkSerialInterface();
extern void checkDebugSerialInterface();
extern String ReadSerialStrUntilNewLine();
extern void initColorSelector();
extern void filamentLoadToMK3();
extern bool filamentLoadWithBondTechGear();
extern void toolChange(int newPos);
extern void quickParkIdler();
extern void quickUnParkIdler();
extern void unParkIdler();
extern bool unloadFilament();
extern void parkIdler();
extern void activateColorSelector();
extern void deActivateColorSelector();
extern void moveIdler(int newPos);
extern void moveSelector(int newPos);
extern bool loadFilamentToFinda();
extern void fixTheProblem(String statement);
extern void csTurnAmount(int steps, int direction);
extern void feedFilament(unsigned int steps, int stoptoextruder);
extern void idlerturnamount(int steps, int dir);
extern void syncColorSelector();
extern void printHelp();
extern void printStatus();
extern void toolChangeCycleA();
extern void toolChangeCycleD();
extern void park();
extern void prevTool();
extern void nextTool();
extern bool loadFilament();

class Application
{
public:
	Application();

	void setup();
	void loop();
};

#endif // APPLICATION_H
