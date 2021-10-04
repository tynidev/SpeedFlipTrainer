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

struct Color
{
	char red, green, blue = 0;
	float opacity = 0;
};

struct Line : Color
{
	int width = 2;
};

struct MeterRange : Color
{
	int low, high = 0;
};

struct MeterMarking : Line
{
	int value = 0;
};

Vector2 RenderMeter(CanvasWrapper canvas, Vector2 startPos, Vector2 reqBoxSize, Color base, Line border, int totalUnits, std::list<MeterRange> ranges, std::list<MeterMarking> markings, bool vertical)
{
	int unitWidth = vertical ? reqBoxSize.Y / totalUnits : reqBoxSize.X / totalUnits;
	Vector2 boxSize = vertical ? Vector2{ reqBoxSize.X, unitWidth * totalUnits } : Vector2{ unitWidth * totalUnits, reqBoxSize.Y };

	// Draw base meter base color
	canvas.SetColor(base.red, base.green, base.blue, (char)(100 * base.opacity));
	canvas.SetPosition(startPos);
	canvas.FillBox(boxSize);

	// Draw meter ranges
	for (const MeterRange& range : ranges)
	{
		canvas.SetColor(range.red, range.green, range.blue, (char)(255 * range.opacity));

		auto l = range.low;
		if (l < 0)
			l = 0;
		if (l > totalUnits)
			l = totalUnits;

		auto h = range.high;
		if (h < 0)
			h = 0;
		if (h > totalUnits)
			h = totalUnits;

		if (l > h)
			l = h;

		if (vertical)
		{
			auto position = Vector2{ startPos.X, (int)(startPos.Y + boxSize.Y - (h * unitWidth)) };
			auto size = Vector2{ boxSize.X, (int)((h - l) * unitWidth) };
			canvas.SetPosition(position);
			canvas.FillBox(size);
		}
		else
		{
			if (l != 0)
				l -= 1;
			if (h != totalUnits)
				h -= 1;
			auto position = Vector2{ (int)(startPos.X + (l * unitWidth)), startPos.Y };
			auto size = Vector2{ (int)((h - l) * unitWidth), boxSize.Y };
			canvas.SetPosition(position);
			canvas.FillBox(size);
		}
	}

	// Draw meter markings
	for (const MeterMarking& marking : markings)
	{
		canvas.SetColor(marking.red, marking.green, marking.blue, (char)(255 * marking.opacity));

		auto value = marking.value - 1;
		if (value < 0)
			value = 0;
		if (value > totalUnits)
			value = totalUnits;

		if (vertical)
		{
			float y = startPos.Y + boxSize.Y - ((value + 1) * unitWidth);
			auto begin = Vector2{ startPos.X,             (int)y };
			auto end =   Vector2{ startPos.X + boxSize.X, (int)y };
			canvas.DrawLine(begin, end, marking.width);
		}
		else
		{
			float x = startPos.X + (value * unitWidth) + (marking.width / 2);
			float y = startPos.Y - (marking.width / 2);
			auto begin = Vector2{ (int)x, (int)y             };
			auto end =   Vector2{ (int)x, (int)(y + boxSize.Y) };
			canvas.DrawLine(begin, end, marking.width);
		}
	}

	// Draw meter border
	if (border.width > 0)
	{
		canvas.SetColor(border.red, border.green, border.blue, (char)(255 * border.opacity));
		canvas.SetPosition(startPos.minus(Vector2{ border.width / 2, border.width / 2 }));
		canvas.DrawBox(boxSize.minus(Vector2{ border.width * -1, border.width * -1 }));
	}

	return boxSize;
}

void SpeedFlipTrainer::RenderFirstJumpMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight)
{
	int totalUnits = *jumpHigh - *jumpLow;
	int halfMark = totalUnits / 2;
	int ticks = firstJumpTick - ((*jumpHigh - (halfMark)) - halfMark);
	if (ticks < 0)
	{
		ticks = 0;
	}
	else if (ticks > totalUnits)
	{
		ticks = totalUnits;
	}

	float opacity = 1.0;
	Vector2 reqSize = Vector2{ (int)(screenWidth * 2 / 100.f), (int)(screenHeight * 56 / 100.f) };
	Vector2 startPos = Vector2{ (int)(screenWidth * 75 / 100.f) + 2 * reqSize.X, (int)(screenHeight * 25 / 100.f) };
	
	struct Color baseColor = { (char)255, (char)255, (char)255, opacity };
	struct Line border = { (char)255, (char)255, (char)255, opacity, 2 };

	std::list<MeterRange> ranges;
	float go = 1, ro = 1, yo = 1;
	if (ticks >= halfMark - 10 && ticks <= halfMark + 10)
	{
		ranges.push_back({ (char)50,(char)255, (char)50, go, (int)(halfMark - 10), (int)(halfMark + 10) });
	}
	else if (ticks >= halfMark - 15 && ticks <= halfMark + 15)
	{
		ranges.push_back({ (char)255, (char)255, (char)50, yo, (int)(halfMark - 15), (int)(halfMark - 15) });
	}
	else
	{
		ranges.push_back({ (char)255,(char)50, (char)50, ro, 0, totalUnits });
	}

	std::list<MeterMarking> markings;
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, (int)(halfMark - 15) });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, (int)(halfMark - 10) });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, (int)(halfMark + 10) });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, (int)(halfMark + 15) });
	markings.push_back({ (char)0, (char)0, (char)0, 0.6, (int)reqSize.Y/totalUnits, (int)ticks });

	auto boxSize = RenderMeter(canvas, startPos, reqSize, baseColor, border, totalUnits, ranges, markings, true);

	//draw label
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{ startPos.X - 15, (int)(startPos.Y + boxSize.Y + 10) });
	canvas.DrawString("First Jump");
	canvas.SetPosition(Vector2{ startPos.X - 4, (int)(startPos.Y + boxSize.Y + 10 + 15) });
	auto ms = (int)(firstJumpTick * 1.0 / 120.0 * 1000.0 / 1.0);
	canvas.DrawString(to_string(ms) + " ms");
}

void SpeedFlipTrainer::RenderFlipCancelMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight)
{
	float opacity = 1.0;
	Vector2 reqSize = Vector2{ (int)(screenWidth * 2 / 100.f), (int)(screenHeight * 55 / 100.f) };
	Vector2 startPos = Vector2{ (int)(screenWidth * 75 / 100.f), (int)(screenHeight * 25 / 100.f) };

	int totalUnits = *flipCancelThreshold;
	int unitWidth = reqSize.Y / totalUnits;

	struct Color baseColor = { (char)255, (char)255, (char)255, opacity };
	struct Line border = { (char)255, (char)255, (char)255, opacity, 2 };

	// flip cancel time range
	std::list<MeterRange> ranges;
	auto ticks = flipCancelTicks > totalUnits ? totalUnits : flipCancelTicks;
	struct Color meterColor;
	if (ticks <= (int)(totalUnits * 0.6f))
		meterColor = { (char)50, (char)255, (char)50, 0.7 }; // green
	else if (ticks <= (int)(totalUnits * 0.8f))
		meterColor = { (char)255, (char)255, (char)50, 0.7 }; // yellow
	else
		meterColor = { (char)255, (char)50, (char)50, 0.7 }; // red
	ranges.push_back({ meterColor.red, meterColor.green, meterColor.blue, 1, 0, ticks });

	std::list<MeterMarking> markings;
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, ((int)(totalUnits * 0.6f)) });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, ((int)(totalUnits * 0.8f)) });
	//markings.push_back({ (char)0, (char)0, (char)0, 0.6, 10, ticks });

	auto boxSize = RenderMeter(canvas, startPos, reqSize, baseColor, border, totalUnits, ranges, markings, true);

	//draw label
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{startPos.X - 15, (int)(startPos.Y + boxSize.Y + 10)});
	canvas.DrawString("Flip Cancel");
	canvas.SetPosition(Vector2{ startPos.X - 4, (int)(startPos.Y + boxSize.Y + 10 + 15) });
	auto ms = (int)(flipCancelTicks * 1.0 / 120.0 * 1000.0 / 1.0);
	canvas.DrawString(to_string(ms) + " ms");
}

void SpeedFlipTrainer::RenderAngleMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight)
{
	// Cap angle at 90
	int angle = dodgeAngle;
	if (angle > 90) angle = 90;
	if (angle < -90) angle = -90;

	float opacity = 1.0;
	Vector2 reqSize = Vector2{ (int)(screenWidth * 66 / 100.f), (int)(screenHeight * 4 / 100.f) };
	Vector2 startPos = Vector2{ (int)(screenWidth * 17 / 100.f), (int)(screenHeight * 90 / 100.f) };

	int totalUnits = 180;
	int unitWidth = reqSize.X / totalUnits;

	struct Color baseColor = { (char)255, (char)255, (char)255, opacity };
	struct Line border = { (char)255, (char)255, (char)255, opacity, 2 };

	std::list<MeterRange> ranges;
	int compareAngle = angle < 0 ? *optimalLeftAngle : *optimalRightAngle;
	float go = 1, ro = 1, yo = 1;
	if (angle >= compareAngle - 7 && angle <= compareAngle + 7)
	{
		ranges.push_back({ (char)50, (char)255, (char)50, go, 90 + *optimalLeftAngle - 7, 90 + *optimalLeftAngle + 7 });
		ranges.push_back({ (char)50, (char)255, (char)50, go, 90 + *optimalRightAngle - 7, 90 + *optimalRightAngle + 7 });
	}
	else if (angle >= compareAngle - 12 && angle <= compareAngle + 12)
	{
		ranges.push_back({ (char)255, (char)255, (char)50, yo, 90 + *optimalLeftAngle - 12, 90 + *optimalLeftAngle + 12 });
		ranges.push_back({ (char)255, (char)255, (char)50, yo, 90 + *optimalRightAngle - 12, 90 + *optimalRightAngle + 12 });
	}
	else
	{
		ranges.push_back({ (char)255,(char)50, (char)50, ro, 0, totalUnits });
	}

	std::list<MeterMarking> markings;
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, 90 + *optimalLeftAngle + 7});
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, 90 + *optimalLeftAngle + 12});
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, 90 + *optimalLeftAngle - 7 });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, 90 + *optimalLeftAngle - 12 });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, 90 + *optimalRightAngle + 7 });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, 90 + *optimalRightAngle + 12 });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, 90 + *optimalRightAngle - 7 });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, 90 + *optimalRightAngle - 12 });
	markings.push_back({ (char)0, (char)0, (char)0, 0.6, unitWidth, 90 + angle });

	auto boxSize = RenderMeter(canvas, startPos, reqSize, baseColor, border, totalUnits, ranges, markings, false);

	//draw label
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{ startPos.X + boxSize.X + 5, startPos.Y + 10 });
	canvas.DrawString("Dodge Angle");

	//draw angle
	canvas.SetPosition(Vector2{ startPos.X + boxSize.X + 5, startPos.Y + 25 });
	canvas.DrawString(to_string(dodgeAngle) + " DEG");

	//draw time to ball
	if (consecutiveHits >= 0)
	{
		canvas.SetColor(255, 255, 255, (char)(255 * opacity));
		canvas.SetPosition(Vector2{ startPos.X + (int)(boxSize.X/2) - 75, startPos.Y - 25 });

		auto ms = timeToBallTicks * 1.0 / 120.0;
		string msg = fmt::format("Time to ball: {0:.4f}s", ms);
		canvas.DrawString(msg, 1, 1);
	}
}
