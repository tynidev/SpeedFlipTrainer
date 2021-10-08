#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include <filesystem>

using namespace std;

class Attempt
{
public:
	// Variables to measure the first jump
	int jumpTick = 0;
	bool jumped = false;

	// Variables to measure the flip cancel
	int flipCancelTick = 0;
	bool flipCanceled = false;

	// Variables to measure the dodge angle
	int dodgeAngle = 0;
	int dodgedTick = 0;
	bool dodged = false;

	// Variable to keep track of Y position
	float positionY = -1.1;
	float traveledY = 0;

	// Number of ticks taken to reach the ball
	int ticksToBall = 0;

	// Boolean values to keep track of what happened on reset
	bool hit = false;
	bool exploded = false;

	// Number of ticks not pressing boost or throttle
	int ticksNotPressingBoost = 0;
	int ticksNotPressingThrottle = 0;

	void Record(int tick, ControllerInput input);
	ControllerInput Play(int tick);
	void WriteInputsToFile(std::filesystem::path filepath);
	void ReadInputsFromFile(std::filesystem::path filepath);

	map<int, ControllerInput> inputs;
private:
};
