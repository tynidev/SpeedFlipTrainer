#include "pch.h"	
#include "SpeedFlipTrainer.h"

#define M_PI 3.14159265358979323846

BAKKESMOD_PLUGIN(SpeedFlipTrainer, "Speedflip trainer", plugin_version, PLUGINTYPE_CUSTOM_TRAINING)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

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

	//LOG("X: {0} Y: {1}", dd.X, dd.Y);
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

void SpeedFlipTrainer::Measure(CarWrapper car, std::shared_ptr<GameWrapper> gameWrapper)
{
	int currentPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
	int currentTick = currentPhysicsFrame - startingPhysicsFrame;

	if (!firstJump && car.GetbJumped())
	{
		firstJump = true;
		firstJumpTick = currentTick;
		LOG("First jump: " + to_string(currentTick) + " ticks");
	}

	if (!secondJump && car.GetbDoubleJumped())
	{
		secondJump = true;
		secondJumpTick = currentTick;
		LOG("Second jump: " + to_string(currentTick - firstJumpTick) + " ticks");
	}

	ControllerInput input = car.GetInput();
	if (secondJump && !filpCancel && input.Pitch > 0.8)
	{
		filpCancel = true;
		flipCancelTicks = currentTick - secondJumpTick;
		LOG("Flip Cancel: " + to_string(flipCancelTicks) + " ticks");
	}

	if (!dodged && car.IsDodging())
	{
		dodged = true;
		auto dodge = car.GetDodgeComponent();
		if (!dodge.IsNull())
		{
			dodgeAngle = ComputeDodgeAngle(dodge);
			clock_time time = ComputeClockTime(dodgeAngle);
			string msg = fmt::format("Dodge Angle: {0:#03d} deg or {1:#02d}:{2:#02d} PM", dodgeAngle, time.hour_hand, time.min_hand);
			LOG(msg);
		}
	}

	if (!supersonic && car.GetbSuperSonic())
	{
		supersonic = true;
		LOG("Supersonic: " + to_string(currentTick) + " ticks");
	}
}

void SpeedFlipTrainer::Perform(shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci)
{
	int currentPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
	int tick = currentPhysicsFrame - startingPhysicsFrame;

	ci->Throttle = 1;
	ci->ActivateBoost = 1;
	ci->HoldingBoost = 1;

	int j1 = 38;
	int j1Ticks = 11;
	int cancelTicks = 1;
	int ticksBeforeFlipAdjust = 59;
	int adjustTicks = 16;

	if (tick <= j1)
	{
		ci->Steer = 0.01;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;
	}
	else if (tick <= j1 + j1Ticks)
	{
		// First jump
		ci->Jump = 1;
		ci->Jumped = 1;

		ci->Steer = 1;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;
	}
	else if (tick <= j1 + j1Ticks + 1)
	{
		// Stop jumping
		ci->Jump = 0;
		ci->Jumped = 0;
	}
	else if (tick <= j1 + j1Ticks + 1 + cancelTicks)
	{
		// Dodge
		ci->Jump = 1;
		ci->Jumped = 1;

		ci->Steer = -0.3;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;

		ci->Pitch = -0.70;
		ci->DodgeForward = -1 * ci->Pitch;
	}
	else if (tick <= j1 + j1Ticks + 1 + cancelTicks + ticksBeforeFlipAdjust)
	{
		// Cancel flip
		ci->Jump = 0;
		ci->Jumped = 0;

		ci->Steer = 0;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;

		ci->Pitch = 1;
		ci->DodgeForward = -1 * ci->Pitch;
	}
	else if (tick <= j1 + j1Ticks + 1 + cancelTicks + ticksBeforeFlipAdjust + adjustTicks)
	{
		ci->Steer = -0.3;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;

		ci->Pitch = 0.3;
		ci->DodgeForward = -1 * ci->Pitch;
	}
	else if (tick <= j1 + j1Ticks + 1 + cancelTicks + ticksBeforeFlipAdjust + adjustTicks + 40)
	{
		ci->Steer = 0;
		ci->Yaw = ci->Steer;
		ci->DodgeStrafe = ci->Steer;

		ci->Roll = -1;

		ci->Pitch = 1;
		ci->DodgeForward = -1 * ci->Pitch;
	}
	else if (tick > j1 + j1Ticks + 1 + cancelTicks + ticksBeforeFlipAdjust + adjustTicks + 40)
	{
		ci->Roll = 0;

		ci->Pitch = 0;
		ci->DodgeForward = -1 * ci->Pitch;
	}

	gameWrapper->OverrideParams(ci, sizeof(ControllerInput));
}

void SpeedFlipTrainer::Hook()
{
	if (loaded)
		return;
	loaded = true;

	if (*rememberSpeed)
	{
		auto speedCvar = _globalCvarManager->getCvar("sv_soccar_gamespeed");
		speedCvar.setValue(*speed);
	}

	LOG("Hooking");

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.SetVehicleInput",
		[this](CarWrapper cw, void* params, std::string eventname) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;
		CarWrapper car = gameWrapper->GetLocalCar();

		if (car.IsNull() || !car.GetbIsMoving())
			return;

		float timeLeft = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		if (startingPhysicsFrame < 0 && timeLeft < initialTime)
		{
			startingPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
		}

		//Perform(gameWrapper, (ControllerInput*)params);

		if (initialTime <= 0 || timeLeft == initialTime)
			return;

		Measure(car, gameWrapper);

	});

	gameWrapper->HookEvent("Function TAGame.Car_TA.EventHitBall", 
		[this](std::string eventname) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;

		auto currentPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
		timeToBallTicks = currentPhysicsFrame - startingPhysicsFrame;

		if (timeToBallTicks <= 240)
		{
			LOG("HIT BALL: {0} ticks", timeToBallTicks);
			LOG("# Ticks under: {0}", 240 - timeToBallTicks);
			hit = true;
		}
		else if (timeToBallTicks > 240)
		{
			LOG("Too slow: {0} ticks", timeToBallTicks);
			LOG("# Ticks over: {0}", timeToBallTicks - 240);
			hit = false;
		}
	});

	gameWrapper->HookEvent("Function TAGame.Ball_TA.Explode", 
		[this](std::string eventName) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;

		hit = false;
		exploded = true;
	});

	gameWrapper->HookEventPost("Function Engine.Controller.Restart", 
		[this](std::string eventName) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;

		//gameWrapper->GetCurrentGameState().SetGameTimeRemaining(2.14);
		initialTime = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		ticksBeforeTimeExpired = initialTime * 120;
		startingPhysicsFrame = -1;

		secondJumpTick = 0;
		
		firstJump = false;
		secondJump = false;
		dodged = false;
		filpCancel = false;
		supersonic = false;

		if (hit && !exploded)
		{
			consecutiveHits++;
			consecutiveMiss = 0;
		}
		else
		{
			consecutiveHits = 0;
			consecutiveMiss++;
		}
		exploded = false;
		hit = false;

		if (*changeSpeed)
		{
			if (consecutiveHits != 0 && consecutiveHits % (*numHitsChangedSpeed) == 0)
			{
				gameWrapper->LogToChatbox(to_string(consecutiveHits) + " " + (consecutiveHits > 1 ? "hits" : "hit") + " in a row");
				auto speedCvar = _globalCvarManager->getCvar("sv_soccar_gamespeed");
				float speed = speedCvar.getFloatValue();
				speed += *speedIncrement;
				speedCvar.setValue(speed);
				if (*rememberSpeed)
					*(this->speed) = speed;
				string msg = fmt::format("Game speed + {0:.3f} = {1:.3f}", *speedIncrement, speed);
				gameWrapper->LogToChatbox(msg);
			}
			else if (consecutiveMiss != 0 && consecutiveMiss % (*numHitsChangedSpeed) == 0)
			{
				gameWrapper->LogToChatbox(to_string(consecutiveMiss) + " " + (consecutiveMiss > 1 ? "misses" : "miss") + " in a row");
				auto speedCvar = _globalCvarManager->getCvar("sv_soccar_gamespeed");
				float speed = speedCvar.getFloatValue();
				speed -= *speedIncrement;
				if (speed <= 0)
					speed = 0;
				speedCvar.setValue(speed);
				if (*rememberSpeed)
					*(this->speed) = speed;
				string msg = fmt::format("Game speed - {0:.3f} = {1:.3f}", *speedIncrement, speed);
				gameWrapper->LogToChatbox(msg);
			}
		}
	});
}

bool SpeedFlipTrainer::IsMustysPack(TrainingEditorWrapper tw)
{
	if (!tw.IsNull())
	{
		GameEditorSaveDataWrapper data = tw.GetTrainingData();
		if (!data.IsNull())
		{
			TrainingEditorSaveDataWrapper td = data.GetTrainingData();
			if (!td.IsNull())
			{
				if (!td.GetCode().IsNull())
				{
					auto code = td.GetCode().ToString();
					if (code == "A503-264C-A7EB-D282")
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

void SpeedFlipTrainer::onLoad()
{
	_globalCvarManager = cvarManager;

	cvarManager->registerCvar("sf_enabled", "1", "Enabled speedflip training.", true, false, 0, false, 0, false).bindTo(enabled);
	cvarManager->getCvar("sf_enabled").addOnValueChanged([this](string oldVal, CVarWrapper cvar)
	{
		LOG("sf_enabled = {0}", *enabled);
		if (!*enabled)
		{
			onUnload();
		}
	});

	cvarManager->registerCvar("sf_change_speed", "1", "Change game speed on consecutive hits and misses.", true, false, 0, false, 0, true).bindTo(changeSpeed);
	cvarManager->registerCvar("sf_speed", "1.0", "Change game speed on consecutive hits and misses.", true, false, 0.0, false, 1.0, true).bindTo(speed);
	cvarManager->registerCvar("sf_remember_speed", "1", "Remember last set speed.", true, true, 1, true, 1, true).bindTo(rememberSpeed);
	cvarManager->registerCvar("sf_num_hits", "3", "Number of hits/misses before the speed increases/decreases.", true, true, 0, true, 50, true).bindTo(numHitsChangedSpeed);
	cvarManager->registerCvar("sf_speed_increment", "0.05", "Speed increase/decrease increment.", true, true, 0.001, true, 0.999, true).bindTo(speedIncrement);

	cvarManager->registerCvar("sf_left_angle", "-30", "Optimal left angle", true, true, -78, true, -12, true).bindTo(optimalLeftAngle);
	cvarManager->registerCvar("sf_right_angle", "30", "Optimal right angle", true, true, 12, true, 78, true).bindTo(optimalRightAngle);
	cvarManager->registerCvar("sf_cancel_threshold", "10", "Optimal flip cancel threshold.", true, true, 1, true, 15, true).bindTo(flipCancelThreshold);

	//cvarManager->registerCvar("sf_jump_low", "40", "Low threshold for first jump of speedflip.", true, true, 10, true, 110, false).bindTo(jumpLow);
	//cvarManager->registerCvar("sf_jump_high", "90", "High threshold for first jump of speedflip.", true, true, 20, true, 120, false).bindTo(jumpHigh);

	if (*enabled)
	{
		gameWrapper->HookEventWithCaller<ActorWrapper>("Function TAGame.GameEvent_TrainingEditor_TA.LoadRound", [this](ActorWrapper cw, void* params, std::string eventName) {
			if (!*enabled || !IsMustysPack((TrainingEditorWrapper)cw.memory_address))
				return;
			Hook();
		});

		gameWrapper->HookEventWithCaller<ActorWrapper>("Function TAGame.GameEvent_TrainingEditor_TA.Destroyed", [this](ActorWrapper cw, void* params, std::string eventName) {
			if(loaded)
				onUnload();
		});
	}

	gameWrapper->RegisterDrawable(bind(&SpeedFlipTrainer::Render, this, std::placeholders::_1));
}

void SpeedFlipTrainer::onUnload()
{
	if (!loaded)
		return;
	loaded = false;

	LOG("Unhooking");
	gameWrapper->UnhookEvent("Function TAGame.Car_TA.SetVehicleInput");
	gameWrapper->UnhookEvent("Function TAGame.Car_TA.EventHitBall");
	gameWrapper->UnhookEvent("Function TAGame.Ball_TA.Explode");
	gameWrapper->UnhookEventPost("Function Engine.Controller.Restart");
}

void SpeedFlipTrainer::Render(CanvasWrapper canvas)
{
	bool training = gameWrapper->IsInCustomTraining();

	if (!*enabled || !loaded || !training) return;

	float SCREENWIDTH = canvas.GetSize().X;
	float SCREENHEIGHT = canvas.GetSize().Y;
	float RELWIDTH = SCREENWIDTH / 1080.f;
	float RELHEIGHT = SCREENHEIGHT / 1080.f;

	RenderAngleMeter(canvas, SCREENWIDTH, SCREENHEIGHT, RELWIDTH, RELHEIGHT);
	RenderFirstJumpMeter(canvas, SCREENWIDTH, SCREENHEIGHT, RELWIDTH, RELHEIGHT);
	RenderFlipCancelMeter(canvas, SCREENWIDTH, SCREENHEIGHT, RELWIDTH, RELHEIGHT);
}

void SpeedFlipTrainer::RenderFirstJumpMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight)
{
	float halfHeight = screenHeight / 2.f;
	float halfWidth = screenWidth / 2.f;

	float heightPerc = 50 / 100.f;
	float widthPerc = 60 / 100.f;

	float boxWidth = 60.f * widthPerc;
	float boxHeight = screenHeight * heightPerc;

	int totalUnits = *jumpHigh - *jumpLow;
	float unitWidth = boxHeight / totalUnits;

	Vector2 startPos = { (int)(halfWidth + (halfWidth * widthPerc) - 0.75 * boxWidth), (int)(halfHeight - halfHeight * heightPerc) };
	Vector2 boxSize = { boxWidth, boxHeight };

	//draw base color
	float opacity = 1.0;
	canvas.SetColor(255, 255, 255, (char)(100 * opacity));
	canvas.SetPosition(startPos);
	canvas.FillBox(boxSize);

	//draw outline
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(startPos.minus(Vector2{ 1,1 }));
	canvas.DrawBox(boxSize.minus(Vector2{ -2,-2 }));

	auto bestBot = totalUnits / 2 - 10;
	auto bestTop = totalUnits / 2 + 10;
	auto goodBot = totalUnits / 2 - 15;
	auto goodTop = totalUnits / 2 + 15;

	//draw label
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{ startPos.X - 15, (int)(startPos.Y + boxHeight + 10) });
	canvas.DrawString("First Jump");
	canvas.SetPosition(Vector2{ startPos.X - 4, (int)(startPos.Y + boxHeight + 10 + 15) });
	auto ms = (int)(firstJumpTick * 1.0 / 120.0 * 1000.0 / 1.0);
	canvas.DrawString(to_string(ms) + " ms");

	canvas.SetColor(255, 255, 50, (char)(255 * 0.5)); // yellow
	canvas.SetPosition(Vector2{ startPos.X + 1, (int)(startPos.Y + boxHeight - (goodTop * unitWidth)) });
	canvas.FillBox(Vector2{ (int)boxWidth - 1, (int)((goodTop -goodBot) * unitWidth) });

	canvas.SetColor(50, 255, 50, (char)(255 * 0.5)); // green
	canvas.SetPosition(Vector2{ startPos.X + 1, (int)(startPos.Y + boxHeight - (bestTop * unitWidth)) });
	canvas.FillBox(Vector2{ (int)boxWidth - 1, (int)((bestTop - bestBot) * unitWidth) });

	//draw target guids
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.DrawLine(Vector2{ startPos.X, (int)(startPos.Y + boxHeight - ((int)(totalUnits / 2 - 10) * unitWidth)) }, Vector2{ startPos.X + (int)boxWidth + 1, (int)(startPos.Y + boxHeight - ((int)(totalUnits / 2 - 10) * unitWidth)) }, 3);
	canvas.DrawLine(Vector2{ startPos.X, (int)(startPos.Y + boxHeight - ((int)(totalUnits / 2 + 10) * unitWidth)) }, Vector2{ startPos.X + (int)boxWidth + 1, (int)(startPos.Y + boxHeight - ((int)(totalUnits / 2 + 10) * unitWidth)) }, 3);

	canvas.DrawLine(Vector2{ startPos.X, (int)(startPos.Y + boxHeight - ((int)(totalUnits / 2 - 15) * unitWidth)) }, Vector2{ startPos.X + (int)boxWidth + 1, (int)(startPos.Y + boxHeight - ((int)(totalUnits / 2 - 15) * unitWidth)) }, 3);
	canvas.DrawLine(Vector2{ startPos.X, (int)(startPos.Y + boxHeight - ((int)(totalUnits / 2 + 15) * unitWidth)) }, Vector2{ startPos.X + (int)boxWidth + 1, (int)(startPos.Y + boxHeight - ((int)(totalUnits / 2 + 15) * unitWidth)) }, 3);

	// draw first jump time
	auto ticks = firstJumpTick - ((*jumpHigh - (totalUnits/2)) - totalUnits / 2);

	if (ticks < 0)
	{
		ticks = 0;
	}
	else if (ticks > totalUnits)
	{
		ticks = totalUnits;
	}

	canvas.SetColor(0, 0, 0, (char)(255 * 0.8));
	canvas.SetPosition(Vector2{ startPos.X + 1, (int)(startPos.Y + boxHeight - ((ticks + 1) * unitWidth)) });
	canvas.DrawLine(Vector2{ startPos.X + 1, (int)(startPos.Y + boxHeight - ((int)(ticks + 1) * unitWidth)) }, Vector2{ startPos.X + (int)boxWidth, (int)(startPos.Y + boxHeight - ((int)(ticks + 1) * unitWidth)) }, unitWidth);
}

void SpeedFlipTrainer::RenderFlipCancelMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight)
{
	float halfHeight = screenHeight / 2.f;
	float halfWidth = screenWidth / 2.f;

	float heightPerc = 50 / 100.f;
	float widthPerc = 60 / 100.f;

	float boxWidth = 60.f * widthPerc;
	float boxHeight = screenHeight * heightPerc;
	
	int totalUnits = *flipCancelThreshold;
	float unitWidth = boxHeight / totalUnits;

	Vector2 startPos = { (int)(halfWidth + (halfWidth * widthPerc) - 3 * boxWidth), (int)(halfHeight - halfHeight * heightPerc) };
	Vector2 boxSize = { boxWidth, boxHeight };

	//draw base color
	float opacity = 1.0;
	canvas.SetColor(255, 255, 255, (char)(100 * opacity));
	canvas.SetPosition(startPos);
	canvas.FillBox(boxSize);

	//draw outline
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(startPos.minus(Vector2{ 1,1 }));
	canvas.DrawBox(boxSize.minus(Vector2{ -2,-2 }));

	//draw label
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{startPos.X - 15, (int)(startPos.Y + boxHeight + 10)});
	canvas.DrawString("Flip Cancel");
	canvas.SetPosition(Vector2{ startPos.X - 4, (int)(startPos.Y + boxHeight + 10 + 15) });
	auto ms = (int)(flipCancelTicks * 1.0 / 120.0 * 1000.0 / 1.0);
	canvas.DrawString(to_string(ms) + " ms");

	int threshold = (totalUnits * 0.9f);

	// draw flip cancel time
	auto ticks = flipCancelTicks > totalUnits ? totalUnits : flipCancelTicks;
	if(ticks <= (int)(totalUnits * 0.6f))
		canvas.SetColor(50, 255, 50, (char)(255 * 0.7)); // green
	else if (ticks <= (int)(totalUnits * 0.8f))
		canvas.SetColor(255, 255, 50, (char)(255 * 0.7)); // yellow
	else
		canvas.SetColor(255, 50, 50, (char)(255 * 0.7)); // red
	canvas.SetPosition(Vector2{ startPos.X + 1, (int)(startPos.Y + boxHeight - (ticks * unitWidth)) });
	canvas.FillBox(Vector2{ boxSize.X - 2,  (int)(ticks * unitWidth) });

	//draw optimal cancel threshold
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.DrawLine(Vector2{ startPos.X, (int)(startPos.Y + boxHeight - ((int)(totalUnits * 0.6f) * unitWidth)) }, Vector2{ startPos.X + (int)boxWidth + 1, (int)(startPos.Y + boxHeight - ((int)(totalUnits * 0.6f) * unitWidth)) }, 3);
	canvas.DrawLine(Vector2{ startPos.X, (int)(startPos.Y + boxHeight - ((int)(totalUnits * 0.8f) * unitWidth)) }, Vector2{ startPos.X + (int)boxWidth + 1, (int)(startPos.Y + boxHeight - ((int)(totalUnits * 0.8f) * unitWidth)) }, 3);
}

void SpeedFlipTrainer::RenderAngleMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight)
{
	float halfHeight = screenHeight / 2.f;
	float halfWidth = screenWidth / 2.f;

	float heightPerc = 90 / 100.f;
	float widthPerc = 60 / 100.f;

	float boxHeight = 40.f * relativeHeight;

	Vector2 startPosition = { (int)(halfWidth - halfWidth * widthPerc), (int)(screenHeight * heightPerc) };
	Vector2 boxSize = { (int)(screenWidth * widthPerc), boxHeight };

	//draw base color
	float opacity = 1.0;
	canvas.SetColor(255, 255, 255, (char)(100 * opacity));
	canvas.SetPosition(startPosition);
	canvas.FillBox(boxSize);

	//draw label
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{ startPosition.X + boxSize.X + 5, startPosition.Y + 10 });
	canvas.DrawString("Dodge Angle");

	//draw angle
	canvas.SetPosition(Vector2{ startPosition.X + boxSize.X + 5, startPosition.Y + 25 });
	canvas.DrawString(to_string(dodgeAngle) + " DEG");

	//draw time to ball
	if (consecutiveHits >= 0)
	{
		canvas.SetColor(255, 255, 255, (char)(255 * opacity));
		canvas.SetPosition(Vector2{ startPosition.X + (int)(boxSize.X/2) - 75, startPosition.Y - 25 });

		auto ms = timeToBallTicks * 1.0 / 120.0;
		string msg = fmt::format("Time to ball: {0:.4f}s", ms);
		canvas.DrawString(msg, 1, 1);
	}

	//draw outline
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(startPosition.minus(Vector2{ 1,1 }));
	canvas.DrawBox(boxSize.minus(Vector2{ -2,-2 }));

	//draw supersonic thresholds
	float unitWidth = (float)boxSize.X / 180.0;

	float mid = startPosition.X + (float)boxSize.X * 0.5;

	// Fill in ranges
	float pos = mid + (int)(*optimalRightAngle * unitWidth);
	float neg = mid + (int)(*optimalLeftAngle * unitWidth);

	// neg ranges
	canvas.SetColor(255, 255, 50, (char)(255 * 0.5)); // yellow
	canvas.SetPosition(Vector2{ (int)(neg - (13 * unitWidth)), startPosition.Y + 1 });
	canvas.FillBox(Vector2{ (int)(25 * unitWidth), boxSize.Y - 2 });

	canvas.SetColor(50, 255, 50, (char)(255 * 0.5));
	canvas.SetPosition(Vector2{ (int)(neg - (7 * unitWidth)), startPosition.Y + 1 });
	canvas.FillBox(Vector2{ (int)(13 * unitWidth), boxSize.Y - 2 });

	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.DrawLine(Vector2{ (int)(mid + ((*optimalLeftAngle - 7) * unitWidth) - (3 / 2)) , startPosition.Y }, Vector2{ (int)(mid + ((*optimalLeftAngle - 7) * unitWidth) - (3 / 2)), startPosition.Y + (int)boxHeight }, 3);
	canvas.DrawLine(Vector2{ (int)(mid + ((*optimalLeftAngle + 7) * unitWidth) - (3 / 2) - unitWidth) , startPosition.Y }, Vector2{ (int)(mid + ((*optimalLeftAngle + 7) * unitWidth) - (3 / 2) - unitWidth), startPosition.Y + (int)boxHeight }, 3);
	canvas.DrawLine(Vector2{ (int)(mid + ((*optimalLeftAngle - 12) * unitWidth) - (3 / 2) - unitWidth) , startPosition.Y }, Vector2{ (int)(mid + ((*optimalLeftAngle - 12) * unitWidth) - (3 / 2) - unitWidth), startPosition.Y + (int)boxHeight }, 3);
	canvas.DrawLine(Vector2{ (int)(mid + ((*optimalLeftAngle + 12) * unitWidth) - (3 / 2)) , startPosition.Y }, Vector2{ (int)(mid + ((*optimalLeftAngle + 12) * unitWidth) - (3 / 2)), startPosition.Y + (int)boxHeight }, 3);

	// pos ranges
	canvas.SetColor(255, 255, 50, (char)(255 * 0.5)); // yellow
	canvas.SetPosition(Vector2{ (int)(pos - (13 * unitWidth) + 1), startPosition.Y + 1 });
	canvas.FillBox(Vector2{ (int)(25 * unitWidth) - 1, boxSize.Y - 2 });

	canvas.SetColor(50, 255, 50, (char)(255 * 0.5));
	canvas.SetPosition(Vector2{ (int)(pos - (7 * unitWidth)), startPosition.Y + 1 });
	canvas.FillBox(Vector2{ (int)(13 * unitWidth), boxSize.Y - 2 });

	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.DrawLine(Vector2{ (int)(mid + ((*optimalRightAngle + 7) * unitWidth) - (3 / 2) - unitWidth) , startPosition.Y }, Vector2{ (int)(mid + ((*optimalRightAngle + 7) * unitWidth) - (3 / 2) - unitWidth), startPosition.Y + (int)boxHeight }, 3);
	canvas.DrawLine(Vector2{ (int)(mid + ((*optimalRightAngle - 7) * unitWidth) - (3 / 2)), startPosition.Y }, Vector2{ (int)(mid + ((*optimalRightAngle - 7) * unitWidth) - (3 / 2)), startPosition.Y + (int)boxHeight }, 3);
	canvas.DrawLine(Vector2{ (int)(mid + ((*optimalRightAngle + 12) * unitWidth) - (3 / 2)), startPosition.Y }, Vector2{ (int)(mid + ((*optimalRightAngle + 12) * unitWidth) - (3 / 2)), startPosition.Y + (int)boxHeight }, 3);
	canvas.DrawLine(Vector2{ (int)(mid + ((*optimalRightAngle - 12) * unitWidth) - unitWidth), startPosition.Y }, Vector2{ (int)(mid + ((*optimalRightAngle - 12) * unitWidth) - unitWidth), startPosition.Y + (int)boxHeight }, 3);

	// Cap angle at 90
	if (dodgeAngle > 90) dodgeAngle = 90;
	if (dodgeAngle < -90) dodgeAngle = -90;

	canvas.SetColor(0, 0, 0, (char)(255 * 0.8));

	//int compareAngle = dodgeAngle < 0 ? *optimalLeftAngle : *optimalRightAngle;
	//if (dodgeAngle >= compareAngle - 7 && dodgeAngle <= compareAngle + 7)
	//	canvas.SetColor(40, 200, 40, (char)(255 * opacity)); // green
	//else if (dodgeAngle >= compareAngle - 12 && dodgeAngle <= compareAngle + 12)
	//	canvas.SetColor(255, 255, 50, (char)(255 * opacity)); // yellow
	//else
	//	canvas.SetColor(255, 50, 50, (char)(255 * opacity)); // red
	// Draw Doge angle
	float x = mid + (dodgeAngle * unitWidth) - (unitWidth / 2);
	canvas.DrawLine(Vector2{ (int)x , (int)(startPosition.Y - (unitWidth / 2) + 2)}, Vector2{ (int)x, startPosition.Y + (int)(boxHeight - (unitWidth/2) + 1)}, unitWidth);
}
