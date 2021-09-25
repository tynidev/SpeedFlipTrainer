#include "pch.h"	
#include "SpeedFlipTrainer.h"

#define M_PI 3.14159265358979323846

BAKKESMOD_PLUGIN(SpeedFlipTrainer, "Speedflip trainer", plugin_version, PLUGINTYPE_CUSTOM_TRAINING)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

int lastDodgeAngle = 0;
float startTime = 0;
bool loggedSupersonic = false;

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
	time.min_hand = ((int)angle % (360 / 12)) * (60.0 / 30.0);

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

	LOG("Dodge Degree Angle: {0:#03d}", (int)dodgeAngle);
	LOG("Dodge Clock Angle: {0:#02d}:{1:#02d}", time.hour_hand, time.min_hand);
}

void SpeedFlipTrainer::onLoad()
{
	_globalCvarManager = cvarManager;
	LOG("Speedflip Plugin loaded!");

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.SetVehicleInput",
		[this](CarWrapper car, void* params, std::string eventname) {
			if (!gameWrapper->IsInGame() || car.IsNull())
				return;

			if (car.IsDodging())
				HandleDodge(car.GetDodgeComponent());

			if (car.GetbSuperSonic() && !loggedSupersonic)
			{
				loggedSupersonic = true;
				LOG("Time to supersonic: {0}s", startTime - gameWrapper->GetCurrentGameState().GetGameTimeRemaining());
			}
		}
	);

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.EventHitBall",
		[this](CarWrapper caller, void* params, std::string eventname) {
		
		LOG("Time to hit: {0}s", startTime - gameWrapper->GetCurrentGameState().GetGameTimeRemaining());
	});

	gameWrapper->HookEventPost("Function Engine.Controller.Restart", 
		[this](std::string eventName) {
		startTime = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		lastDodgeAngle = 0;
		loggedSupersonic = false;
	});
}

void SpeedFlipTrainer::onUnload()
{
}