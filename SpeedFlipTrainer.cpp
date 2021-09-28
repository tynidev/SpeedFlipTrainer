#include "pch.h"	
#include "SpeedFlipTrainer.h"

#define M_PI 3.14159265358979323846

BAKKESMOD_PLUGIN(SpeedFlipTrainer, "Speedflip trainer", plugin_version, PLUGINTYPE_CUSTOM_TRAINING)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

float initialTime = 0;
float firstJumpTime = 0;
float secondJumpTime = 0;

bool firstJump = false;
bool secondJump = false;
bool filpCancel = false;
bool dodged = false;
bool supersonic = false;

int ticks = 0;

struct clock_time {
	int hour_hand;
	int min_hand;
};

int ComputeDodgeAngle(DodgeComponentWrapper dodge)
{
	if (dodge.IsNull())
		return 0;

	Vector dd = dodge.GetDodgeDirection();
	if (dd.X == 0 && dd.Y == 0)
		return 0;

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

double HandleDodge(DodgeComponentWrapper dodge)
{
	if (dodge.IsNull())
		return 0;

	int dodgeAngle = ComputeDodgeAngle(dodge);

	clock_time time = ComputeClockTime(dodgeAngle);

	LOG("Dodge Angle: {0:#03d} deg or {1:#02d}:{2:#02d} PM", dodgeAngle, time.hour_hand, time.min_hand);

	return dodgeAngle;
}

void Measure(CarWrapper car, std::shared_ptr<GameWrapper> gameWrapper, float timeLeft)
{
	//Vector velocity = car.GetVelocity();
	//float speed = velocity.magnitude();
	//float mph = speed / 44.704;
	//float kph = speed * 0.036;

	if (!firstJump && car.GetbJumped())
	{
		firstJump = true;
		firstJumpTime = timeLeft;
		LOG("First jump: {0}s", initialTime - firstJumpTime);
	}

	if (!secondJump && car.GetbDoubleJumped())
	{
		secondJump = true;
		secondJumpTime = timeLeft;
		LOG("Second jump: {0}s", firstJumpTime - timeLeft);
	}

	ControllerInput input = car.GetInput();
	if (secondJump && !filpCancel && input.Pitch > 0.8)
	{
		filpCancel = true;
		LOG("Flip Cancel: {0}s", secondJumpTime - timeLeft);
	}

	if (!dodged && car.IsDodging())
	{
		dodged = true;
		float angle = HandleDodge(car.GetDodgeComponent());
		//if (abs(angle) > 40)
		//{
		//	LOG("RESET");
		//	gameWrapper->ExecuteUnrealCommand("Function TAGame.GameInfo_GameEditor_TA.PlayerResetTraining 1");
		//}
	}

	if (!supersonic && car.GetbSuperSonic())
	{
		supersonic = true;
		LOG("Supersonic: {0}s", initialTime - timeLeft);
	}
}

void Perform(std::shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci, int tick)
{
	ci->Throttle = 1;
	ci->ActivateBoost = 1;
	ci->HoldingBoost = 1;

	int j1 = 59;
	int j1Time = 11;
	int timeBeforeAdjust = 59;
	int adjustTime = 16;

	if (tick <= j1)
	{
		ci->Steer = 0.01;
		ci->Yaw = 0.01;
		ci->DodgeStrafe = 0.01;
	}
	else if (tick <= j1 + j1Time)
	{
		// First jump
		ci->Jump = 1;
		ci->Jumped = 1;

		ci->Steer = 1;
		ci->Yaw = 1;
		ci->DodgeStrafe = 1;
	}
	else if (tick <= j1 + j1Time + 1)
	{
		// Stop jumping
		ci->Jump = 0;
		ci->Jumped = 0;
	}
	else if (tick <= j1 + j1Time + 1 + 1)
	{
		// Dodge
		ci->Jump = 1;
		ci->Jumped = 1;
		
		ci->Steer = -0.3;
		ci->Yaw = -0.3;
		ci->DodgeStrafe = -0.3;

		ci->Pitch = -0.70;
		ci->DodgeForward = 0.70;
	}
	else if (tick <= j1 + j1Time + 1 + 1 + timeBeforeAdjust)
	{
		// Cancel flip
		ci->Jump = 0;
		ci->Jumped = 0;

		ci->Steer = 0;
		ci->Yaw = 0;
		ci->DodgeStrafe = 0;

		ci->Pitch = 1;
		ci->DodgeForward = -1;
	}
	else if (tick <= j1 + j1Time + 1 + 1 + timeBeforeAdjust + adjustTime)
	{
		ci->Steer = -0.3;
		ci->Yaw = -0.3;
		ci->DodgeStrafe = -0.3;

		ci->Pitch = 0.3;
		ci->DodgeForward = -0.3;
	}
	else if (tick <= j1 + j1Time + 1 + 1 + timeBeforeAdjust + adjustTime + 40)
	{
		ci->Steer = 0;
		ci->Yaw = 0;
		ci->DodgeStrafe = 0;

		ci->Roll = -1;

		ci->Pitch = 1;
		ci->DodgeForward = -1;
	}
	else if (tick > j1 + j1Time + 1 + 1 + timeBeforeAdjust + adjustTime + 40)
	{
		ci->Roll = 0;

		ci->Pitch = 0;
		ci->DodgeForward = 0;
	}

	gameWrapper->OverrideParams(ci, sizeof(ControllerInput));
}

void SpeedFlipTrainer::onLoad()
{
	if (!gameWrapper->IsInGame() || !gameWrapper->IsInCustomTraining())
		return;

	_globalCvarManager = cvarManager;
	LOG("Speedflip Plugin loaded!");

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.SetVehicleInput",
		[this](CarWrapper cw, void* params, std::string eventname) {

			CarWrapper car = gameWrapper->GetLocalCar();
			if (car.IsNull() || !car.GetbIsMoving())
				return;

			float timeLeft = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
			if (timeLeft < initialTime)
				ticks++;

			//Perform(gameWrapper, (ControllerInput*)params, ticks);

			if (initialTime <= 0 || timeLeft == initialTime)
				return;

			Measure(car, gameWrapper, timeLeft);
		}
	);

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.EventHitBall",
		[this](CarWrapper caller, void* params, std::string eventname) {
		float timeLeft = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		if (timeLeft <= 0)
		{
			LOG("Phantom touch! {0} ticks", ticks);
		}
		else if (timeLeft < 2.0)
		{
			LOG("HIT BALL!!");
			LOG("Time: {0}s", initialTime - timeLeft);
			LOG("Ticks: {0}", ticks);
			LOG("Remaining Ticks {0}", 241 - ticks);
		}
	});

	gameWrapper->HookEvent("Function TAGame.Ball_TA.Explode", [this](std::string eventName) {
		LOG("Ticks {0}", ticks);
	});

	gameWrapper->HookEventPost("Function Engine.Controller.Restart", 
		[this](std::string eventName) {
		ticks = 0;
		
		initialTime = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		firstJumpTime = 0;
		secondJumpTime = 0;
		
		firstJump = false;
		secondJump = false;
		dodged = false;
		filpCancel = false;
		supersonic = false;
	});
}

void SpeedFlipTrainer::onUnload()
{
}

