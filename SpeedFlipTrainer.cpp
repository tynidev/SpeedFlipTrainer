#include "pch.h"	
#include "SpeedFlipTrainer.h"

#define M_PI 3.14159265358979323846

BAKKESMOD_PLUGIN(SpeedFlipTrainer, "Speedflip trainer", plugin_version, PLUGINTYPE_CUSTOM_TRAINING)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

int lastDodgeAngle = 0;

float initialTime = 0;
float firstJumpTime = 0;
float secondJumpTime = 0;

bool firstJump = false;
bool secondJump = false;
bool pressDown = false;
bool dodged = false;
bool supersonic = false;

struct clock_time {
	int hour_hand;
	int min_hand;
};

int ComputeDodgeAngle(int previousAngle, DodgeComponentWrapper dodge)
{
	if (dodge.IsNull())
		return previousAngle;

	Vector dd = dodge.GetDodgeDirection();
	if (dd.X == 0 && dd.Y == 0)
		return previousAngle;

	return (int)(atan2f(dd.Y, dd.X) * (180 / M_PI));
}

clock_time ComputeClockTime(int angle)
{
	if (angle < 0)
	{
		angle = 360 + angle;
	}
	clock_time time;

	time.hour_hand = (int)(angle * (12.0 / 360.0));
	time.min_hand = (angle % (360 / 12)) * (60 / 30);

	return time;
}

void HandleDodge(DodgeComponentWrapper dodge)
{
	if (dodge.IsNull())
		return;

	int dodgeAngle = ComputeDodgeAngle(lastDodgeAngle, dodge);

	if (dodgeAngle == lastDodgeAngle)
		return;

	lastDodgeAngle = dodgeAngle;

	clock_time time = ComputeClockTime(dodgeAngle);

	LOG("Dodge Angle: {0:#03d} deg or {1:#02d}:{2:#02d} PM", dodgeAngle, time.hour_hand, time.min_hand);
}

void SpeedFlipTrainer::onLoad()
{
	_globalCvarManager = cvarManager;
	LOG("Speedflip Plugin loaded!");

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.SetVehicleInput",
		[this](CarWrapper car, void* params, std::string eventname) {
			if (!gameWrapper->IsInGame() || car.IsNull())
				return;

			if (!firstJump && car.GetbJumped())
			{
				firstJump = true;
				firstJumpTime = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
				LOG("Time to first jump: {0}s", initialTime - firstJumpTime);
			}

			if (!secondJump && car.GetbDoubleJumped())
			{
				secondJump = true;
				secondJumpTime = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
				LOG("Time to second jump: {0}s", firstJumpTime - gameWrapper->GetCurrentGameState().GetGameTimeRemaining());
			}

			ControllerInput input = car.GetInput();
			if (secondJump && !pressDown && input.Pitch > 0.8)
			{
				pressDown = true;
				LOG("Time to press down: {0}s", secondJumpTime - gameWrapper->GetCurrentGameState().GetGameTimeRemaining());
			}

			if (!dodged && car.IsDodging())
			{
				dodged = true;
				HandleDodge(car.GetDodgeComponent());
			}

			if (!supersonic && car.GetbSuperSonic())
			{
				supersonic = true;
				LOG("Time to supersonic: {0}s", initialTime - gameWrapper->GetCurrentGameState().GetGameTimeRemaining());
			}
		}
	);

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.EventHitBall",
		[this](CarWrapper caller, void* params, std::string eventname) {
		LOG("Time to hit: {0}s", initialTime - gameWrapper->GetCurrentGameState().GetGameTimeRemaining());
	});

	gameWrapper->HookEventPost("Function Engine.Controller.Restart", 
		[this](std::string eventName) {
		initialTime = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		lastDodgeAngle = 0;
		supersonic = false;
		firstJump = false;
		secondJump = false;
		dodged = false;	
		pressDown = false;
		firstJumpTime = 0;
		secondJumpTime = 0;
	});
}

void SpeedFlipTrainer::onUnload()
{
}

