#include "pch.h"
#include "BotAttempt.h"

#define _USE_MATH_DEFINES
#include <math.h>

using namespace std;

void BotAttempt::Become26Bot()
{
	dodgeAngle = -26;
	initialSteer = 0.03;
	beforeJump = 59;
	jumpDuration = 11;
	beforeCancelAdjust = 59;
	adjustAmmount = 0.75;
	adjustDuration = 16;
	cancelSpeed = 5;
	airRollDuration = 40;
}

void BotAttempt::Become45Bot()
{
	dodgeAngle = -45;
	initialSteer = 0.1;
	beforeJump = 59;
	jumpDuration = 11;
	beforeCancelAdjust = 59;
	adjustAmmount = 0.7;
	adjustDuration = 16;
	cancelSpeed = 5;
	airRollDuration = 40;
}

void BotAttempt::Play(ControllerInput* ci, int tick)
{
	ci->Throttle = 1;
	ci->ActivateBoost = 1;
	ci->HoldingBoost = 1;

	if (tick <= beforeJump)
	{
		ci->Steer = initialSteer;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;
	}
	else if (tick <= beforeJump + jumpDuration)
	{
		// First jump
		ci->Jump = 1;
		ci->Jumped = 1;

		ci->Steer = 1;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;
	}
	else if (tick <= beforeJump + jumpDuration + 1)
	{
		// Stop jumping
		ci->Jump = 0;
		ci->Jumped = 0;
	}
	else if (tick <= beforeJump + jumpDuration + 1 + cancelSpeed)
	{
		// Dodge
		ci->Jump = 1;
		ci->Jumped = 1;

		double rads = dodgeAngle * M_PI / 180;

		ci->Steer = sin(rads);
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;

		ci->Pitch = -1 * cos(rads);
		ci->DodgeForward = -1 * ci->Pitch;
	}
	else if (tick <= beforeJump + jumpDuration + 1 + cancelSpeed + beforeCancelAdjust)
	{
		// Cancel flip
		ci->Jump = 0;
		ci->Jumped = 0;

		ci->Steer = 0;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;

		ci->Pitch = 1;
		ci->DodgeForward = -1;
	}
	else if (tick <= beforeJump + jumpDuration + 1 + cancelSpeed + beforeCancelAdjust + adjustDuration)
	{
		ci->Steer = -1 * adjustAmmount;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;

		ci->Pitch = adjustAmmount;
		ci->DodgeForward = -1 * ci->Pitch;
	}
	else if (tick <= beforeJump + jumpDuration + 1 + cancelSpeed + beforeCancelAdjust + adjustDuration + airRollDuration)
	{
		ci->Steer = 0;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;

		ci->Roll = -1;

		ci->Pitch = 1;
		ci->DodgeForward = -1 * ci->Pitch;
	}
	else if (tick > beforeJump + jumpDuration + 1 + cancelSpeed + beforeCancelAdjust + adjustDuration + airRollDuration)
	{
		ci->Roll = 0;

		ci->Pitch = 0;
		ci->DodgeForward = 0;
	}
}
void ReadInputsFromFile(std::filesystem::path filepath)
{
	// TODO
}
