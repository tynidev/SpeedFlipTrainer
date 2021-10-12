#include "pch.h"	
#include "SpeedFlipTrainer.h"
#include "RenderMeter.h"
#include <array>

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

	ControllerInput input = car.GetInput();
	attempt.Record(currentTick, input);

	auto loc = car.GetLocation();
	attempt.traveledY += abs(loc.Y - attempt.positionY);
	attempt.positionY = loc.Y;

	if (!attempt.jumped && car.GetbJumped())
	{
		attempt.jumped = true;
		attempt.jumpTick = currentTick;
		LOG("First jump: " + to_string(currentTick) + " ticks");
	}

	if (!attempt.dodged && car.IsDodging())
	{
		attempt.dodged = true;
		attempt.dodgedTick = currentTick;
		auto dodge = car.GetDodgeComponent();
		if (!dodge.IsNull())
		{
			attempt.dodgeAngle = ComputeDodgeAngle(dodge);
			clock_time time = ComputeClockTime(attempt.dodgeAngle);
			string msg = fmt::format("Dodge Angle: {0:#03d} deg or {1:#02d}:{2:#02d} PM", attempt.dodgeAngle, time.hour_hand, time.min_hand);
			LOG(msg);
		}
	}

	if (input.Throttle != 1)
		attempt.ticksNotPressingThrottle++;
	if (input.ActivateBoost != 1)
		attempt.ticksNotPressingBoost++;

	if (!attempt.flipCanceled  && attempt.dodged && input.Pitch > 0.8)
	{
		attempt.flipCanceled = true;
		attempt.flipCancelTick = currentTick;
		LOG("Flip Cancel: " + to_string(attempt.flipCancelTick - attempt.dodgedTick) + " ticks");
	}
}

void SpeedFlipTrainer::Hook()
{
	if (loaded)
		return;
	loaded = true;

	LOG("Hooking");

	if (*rememberSpeed)
		_globalCvarManager->getCvar("sv_soccar_gamespeed").setValue(*speed);

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.SetVehicleInput",
		[this](CarWrapper car, void* params, std::string eventname) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;

		if (car.IsNull())
			return;

		if (*rememberSpeed)
			*(this->speed) = _globalCvarManager->getCvar("sv_soccar_gamespeed").getFloatValue();

		auto input = (ControllerInput*)params;

		if(mode == SpeedFlipTrainerMode::Bot)
			Perform(gameWrapper, input);
		else if (mode == SpeedFlipTrainerMode::Replay)
			Replay(&replayAttempt, gameWrapper, input);

		float timeLeft = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		if (startingPhysicsFrame < 0 && timeLeft < initialTime)
		{
			startingPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
		}

		if (initialTime <= 0 || timeLeft == initialTime)
			return;

		if ((timeStarted && !attempt.exploded && !attempt.hit) || !timeStarted)
		{
			if (!timeStarted)
			{
				timeStarted = true;
				attempt = Attempt();
			}

			Measure(car, gameWrapper);
		}
	});

	gameWrapper->HookEvent("Function TAGame.Car_TA.EventHitBall", 
		[this](std::string eventname) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining() || attempt.hit)
			return;

		auto currentPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
		attempt.ticksToBall = currentPhysicsFrame - startingPhysicsFrame;

		if (attempt.ticksToBall <= ticksBeforeTimeExpired)
		{
			LOG("HIT BALL: {0} ticks", attempt.ticksToBall);
			LOG("# Ticks under: {0}", ticksBeforeTimeExpired - attempt.ticksToBall);
			attempt.hit = true;
		}
		else if (attempt.ticksToBall > ticksBeforeTimeExpired)
		{
			LOG("Too slow: {0} ticks", attempt.ticksToBall);
			LOG("# Ticks over: {0}", attempt.ticksToBall - ticksBeforeTimeExpired);
			attempt.hit = false;
		}
	});

	gameWrapper->HookEvent("Function TAGame.Ball_TA.Explode", 
		[this](std::string eventName) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;
		attempt.exploded = true;
	});

	gameWrapper->HookEventPost("Function Engine.Controller.Restart", 
		[this](std::string eventName) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;

		//gameWrapper->GetCurrentGameState().SetGameTimeRemaining(2.14);
		initialTime = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		ticksBeforeTimeExpired = initialTime * 120;
		startingPhysicsFrame = -1;

		timeStarted = false;

		if (attempt.hit && !attempt.exploded)
		{
			consecutiveHits++;
			consecutiveMiss = 0;
		}
		else
		{
			consecutiveHits = 0;
			consecutiveMiss++;
		}

		if (*saveToFile && attempt.inputs.size() > 0)
		{
			auto path = attempt.GetFilename(dataDir);
			attempt.WriteInputsToFile(path);
			LOG("Saving attempt to: {0}", path.string());
		}

		auto speedCvar = _globalCvarManager->getCvar("sv_soccar_gamespeed");
		float speed = speedCvar.getFloatValue();

		if (*changeSpeed)
		{
			if (consecutiveHits != 0 && consecutiveHits % (*numHitsChangedSpeed) == 0)
			{
				gameWrapper->LogToChatbox(to_string(consecutiveHits) + " " + (consecutiveHits > 1 ? "hits" : "hit") + " in a row");
				speed += *speedIncrement;
				speedCvar.setValue(speed);
				string msg = fmt::format("Game speed + {0:.3f} = {1:.3f}", *speedIncrement, speed);
				gameWrapper->LogToChatbox(msg);
			}
			else if (consecutiveMiss != 0 && consecutiveMiss % (*numHitsChangedSpeed) == 0)
			{
				gameWrapper->LogToChatbox(to_string(consecutiveMiss) + " " + (consecutiveMiss > 1 ? "misses" : "miss") + " in a row");
				speed -= *speedIncrement;
				if (speed <= 0)
					speed = 0;
				speedCvar.setValue(speed);
				string msg = fmt::format("Game speed - {0:.3f} = {1:.3f}", *speedIncrement, speed);
				gameWrapper->LogToChatbox(msg);
			}
		}
	});
}

void SpeedFlipTrainer::onLoad()
{
	_globalCvarManager = cvarManager;

	cvarManager->registerCvar("sf_enabled", "1", "Enabled speedflip training.", true, false, 0, false, 0, true).bindTo(enabled);
	cvarManager->getCvar("sf_enabled").addOnValueChanged([this](string oldVal, CVarWrapper cvar)
	{
		LOG("sf_enabled = {0}", *enabled);
		if (!*enabled)
		{
			onUnload();
		}
	});

	cvarManager->registerCvar("sf_save_attempts", "0", "Save attmempts to a file.", true, false, 0, false, 0, true).bindTo(saveToFile);

	cvarManager->registerCvar("sf_change_speed", "0", "Change game speed on consecutive hits and misses.", true, false, 0, false, 0, true).bindTo(changeSpeed);
	cvarManager->registerCvar("sf_speed", "1.0", "Change game speed on consecutive hits and misses.", true, false, 0.0, false, 1.0, true).bindTo(speed);
	cvarManager->registerCvar("sf_remember_speed", "1", "Remember last set speed.", true, true, 1, true, 1, true).bindTo(rememberSpeed);
	cvarManager->registerCvar("sf_num_hits", "3", "Number of hits/misses before the speed increases/decreases.", true, true, 0, true, 50, true).bindTo(numHitsChangedSpeed);
	cvarManager->registerCvar("sf_speed_increment", "0.05", "Speed increase/decrease increment.", true, true, 0.001, true, 0.999, true).bindTo(speedIncrement);

	cvarManager->registerCvar("sf_left_angle", "-30", "Optimal left angle", true, true, -78, true, -12, true).bindTo(optimalLeftAngle);
	cvarManager->registerCvar("sf_right_angle", "30", "Optimal right angle", true, true, 12, true, 78, true).bindTo(optimalRightAngle);
	cvarManager->registerCvar("sf_cancel_threshold", "10", "Optimal flip cancel threshold.", true, true, 1, true, 15, true).bindTo(flipCancelThreshold);

	cvarManager->registerCvar("sf_show_angle", "1", "Show angle meter.", true, false, 0, false, 0, true).bindTo(showAngleMeter);
	cvarManager->registerCvar("sf_show_position", "1", "Show horizontal position meter.", true, false, 0, false, 0, true).bindTo(showPositionMeter);
	cvarManager->registerCvar("sf_show_jump", "1", "Show jump meter.", true, false, 0, false, 0, true).bindTo(showJumpMeter);
	cvarManager->registerCvar("sf_show_flip", "1", "Show flip cancel meter.", true, false, 0, false, 0, true).bindTo(showFlipMeter);

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

		dataDir = gameWrapper->GetDataFolder().append("speedflip");
		if (!std::filesystem::exists(dataDir))
			std::filesystem::create_directories(dataDir);
		fileDialog.workingDirectory = dataDir;
	}

	gameWrapper->RegisterDrawable(bind(&SpeedFlipTrainer::RenderMeters, this, std::placeholders::_1));
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

void SpeedFlipTrainer::RenderMeters(CanvasWrapper canvas)
{
	bool training = gameWrapper->IsInCustomTraining();

	if (!*enabled || !loaded || !training) return;

	float SCREENWIDTH = canvas.GetSize().X;
	float SCREENHEIGHT = canvas.GetSize().Y;

	if(*showAngleMeter)
		RenderAngleMeter(canvas, SCREENWIDTH, SCREENHEIGHT);

	if(*showPositionMeter)
		RenderPositionMeter(canvas, SCREENWIDTH, SCREENHEIGHT);

	if(*showFlipMeter)
		RenderFlipCancelMeter(canvas, SCREENWIDTH, SCREENHEIGHT);

	if(*showJumpMeter)
		RenderFirstJumpMeter(canvas, SCREENWIDTH, SCREENHEIGHT);
}

void SpeedFlipTrainer::RenderPositionMeter(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	float mid = -1.1;
	int range = 200;
	int relLocation = (-1 * attempt.positionY) + range;
	int totalUnits = range * 2;

	float opacity = 1.0;
	Vector2 reqSize = Vector2{ (int)(screenWidth * 70 / 100.f), (int)(screenHeight * 4 / 100.f) };
	int unitWidth = reqSize.X / totalUnits;

	Vector2 boxSize = Vector2{ unitWidth * totalUnits, reqSize.Y };
	Vector2 startPos = Vector2{ (int)((screenWidth/2) - (boxSize.X/2)), (int)(screenHeight * 10 / 100.f) };

	struct Color baseColor = { (char)255, (char)255, (char)255, opacity };
	struct Line border = { (char)255, (char)255, (char)255, opacity, 2 };

	std::list<MeterRange> ranges;
	if (timeStarted)
	{
		float go = 1, ro = 1, yo = 1;
		if (relLocation >= range - 80 && relLocation <= range + 80)
		{
			ranges.push_back({ (char)50, (char)255, (char)50, go, range - 80, range + 80 });
		}
		else if (relLocation >= range - 160 && relLocation <= range + 160)
		{
			ranges.push_back({ (char)255, (char)255, (char)50, yo,  range - 160, range + 160 });
			ranges.push_back({ (char)255, (char)255, (char)50, yo,  range - 160, range + 160 });
		}
		else
		{
			ranges.push_back({ (char)255,(char)50, (char)50, ro, 0, totalUnits });
		}
	}

	std::list<MeterMarking> markings;
	markings.push_back({ (char)255, (char)255, (char)255, opacity, unitWidth, range - 80 });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, unitWidth, range + 80 });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, unitWidth, range - 160 });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, unitWidth, range + 160 });
	markings.push_back({ (char)0, (char)0, (char)0, 0.6, unitWidth*2, relLocation });

	RenderMeter(canvas, startPos, reqSize, baseColor, border, totalUnits, ranges, markings, false);



	//draw speed label	
	auto speedCvar = _globalCvarManager->getCvar("sv_soccar_gamespeed");
	float speed = speedCvar.getFloatValue();
	string msg = fmt::format("Game speed: {0}%", (int)(speed * 100));
	int width = (msg.length() * 8.5) - 10;
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{ startPos.X + boxSize.X - width, (int)(startPos.Y - 20) });
	canvas.DrawString(msg);

	int ms = (int)(attempt.ticksNotPressingBoost / 120.0 * 1000.0);
	if(ms != 0)
		canvas.SetColor(255, 255, 50, (char)(255 * opacity));
	else
		canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	//draw time not pressing boost label
	msg = fmt::format("Not pressing Boost: {0}ms", ms);
	width = 200;
	canvas.SetPosition(Vector2{ startPos.X, (int)(startPos.Y + boxSize.Y + 10) });
	canvas.DrawString(msg);

	ms = (int)(attempt.ticksNotPressingThrottle / 120.0 * 1000.0);
	if (ms != 0)
		canvas.SetColor(255, 255, 50, (char)(255 * opacity));
	else
		canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	//draw time not pressing throttle label
	msg = fmt::format("Not pressing Throttle: {0}ms", ms);
	width = 200;
	canvas.SetPosition(Vector2{ startPos.X, (int)(startPos.Y + boxSize.Y + 25) });
	canvas.DrawString(msg);
}

void SpeedFlipTrainer::RenderFirstJumpMeter(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	int totalUnits = *jumpHigh - *jumpLow;
	int halfMark = totalUnits / 2;
	
	float opacity = 1.0;
	Vector2 reqSize = Vector2{ (int)(screenWidth * 2 / 100.f), (int)(screenHeight * 56 / 100.f) };
	int unitWidth = reqSize.Y / totalUnits;

	Vector2 boxSize = Vector2{ reqSize.X, unitWidth * totalUnits };
	Vector2 startPos = Vector2{ (int)((screenWidth * 75 / 100.f) + 2.5 * reqSize.X), (int)((screenHeight * 80 / 100.f) - boxSize.Y) };
	
	struct Color baseColor = { (char)255, (char)255, (char)255, opacity };
	struct Line border = { (char)255, (char)255, (char)255, opacity, 2 };

	
	std::list<MeterMarking> markings;
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, (int)(halfMark - 15) });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, (int)(halfMark - 10) });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, (int)(halfMark + 10) });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, (int)(halfMark + 15) });

	std::list<MeterRange> ranges;
	ranges.push_back({ (char)255, (char)255, (char)50, 0.2, (int)(halfMark - 15), (int)(halfMark - 10) });
	ranges.push_back({ (char)50,(char)255, (char)50, 0.2, (int)(halfMark - 10), (int)(halfMark + 10) });
	ranges.push_back({ (char)255,(char)255, (char)50, 0.2, (int)(halfMark + 10), (int)(halfMark + 15) });

	if (attempt.jumped)
	{
		int ticks = attempt.jumpTick - ((*jumpHigh - (halfMark)) - halfMark);
		if (ticks < 0)
		{
			ticks = 0;
		}
		else if (ticks > totalUnits)
		{
			ticks = totalUnits;
		}

		if (ticks < halfMark - 15)
		{
			ranges.push_back({ (char)255,(char)50, (char)50, 1, 0, halfMark - 15 });
		}
		else if (ticks < halfMark - 10)
		{
			ranges.push_back({ (char)255, (char)255, (char)50, 1, (int)(halfMark - 15), (int)(halfMark - 10) });
		}
		else if (ticks < halfMark + 10)
		{
			ranges.push_back({ (char)50,(char)255, (char)50, 1, (int)(halfMark - 10), (int)(halfMark + 10) });
		}
		else if (ticks < halfMark + 15)
		{
			ranges.push_back({ (char)255,(char)255, (char)50, 1, (int)(halfMark + 10), (int)(halfMark + 15) });
		}
		else
		{
			ranges.push_back({ (char)255,(char)50, (char)50, 1, (int)(halfMark + 15), totalUnits });
		}
		markings.push_back({ (char)0, (char)0, (char)0, 0.6, (int)reqSize.Y / totalUnits, (int)ticks });
	}

	RenderMeter(canvas, startPos, reqSize, baseColor, border, totalUnits, ranges, markings, true);

	//draw label
	string msg = "First Jump";
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{ (int)(startPos.X - 13), (int)(startPos.Y + boxSize.Y + 8) });
	canvas.DrawString(msg);

	auto ms = (int)(attempt.jumpTick * 1.0 / 120.0 * 1000.0 / 1.0);
	msg = to_string(ms) + " ms";
	canvas.SetPosition(Vector2{ startPos.X, (int)(startPos.Y + boxSize.Y + 8 + 15) });
	canvas.DrawString(msg);
}

void SpeedFlipTrainer::RenderFlipCancelMeter(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	float opacity = 1.0;
	int totalUnits = *flipCancelThreshold;

	Vector2 reqSize = Vector2{ (int)(screenWidth * 2 / 100.f), (int)(screenHeight * 55 / 100.f) };
	int unitWidth = reqSize.Y / totalUnits;

	Vector2 boxSize = Vector2{ reqSize.X, unitWidth * totalUnits };
	Vector2 startPos = Vector2{ (int)(screenWidth * 75 / 100.f), (int)((screenHeight * 80 / 100.f) - boxSize.Y) };

	struct Color baseColor = { (char)255, (char)255, (char)255, opacity };
	struct Line border = { (char)255, (char)255, (char)255, opacity, 2 };

	auto tickBeforeCancel = attempt.flipCancelTick - attempt.dodgedTick;

	// flip cancel time range
	std::list<MeterRange> ranges;
	if (attempt.flipCanceled)
	{
		auto ticks = tickBeforeCancel > totalUnits ? totalUnits : tickBeforeCancel;
		struct Color meterColor;
		if (ticks <= (int)(totalUnits * 0.6f))
			meterColor = { (char)50, (char)255, (char)50, 0.7 }; // green
		else if (ticks <= (int)(totalUnits * 0.9f))
			meterColor = { (char)255, (char)255, (char)50, 0.7 }; // yellow
		else
			meterColor = { (char)255, (char)50, (char)50, 0.7 }; // red
		ranges.push_back({ meterColor.red, meterColor.green, meterColor.blue, 1, 0, ticks });
	}

	std::list<MeterMarking> markings;
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, ((int)(totalUnits * 0.6f)) });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, ((int)(totalUnits * 0.9f)) });
	//markings.push_back({ (char)0, (char)0, (char)0, 0.6, 10, ticks });

	RenderMeter(canvas, startPos, reqSize, baseColor, border, totalUnits, ranges, markings, true);

	//draw label
	string msg = "Flip Cancel";
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{ (int)(startPos.X - 16), (int)(startPos.Y + boxSize.Y + 8) });
	canvas.DrawString(msg);

	auto ms = (int)(tickBeforeCancel * 1.0 / 120.0 * 1000.0 / 1.0);
	msg = to_string(ms) + " ms";
	canvas.SetPosition(Vector2{ startPos.X, (int)(startPos.Y + boxSize.Y + 8 + 15) });
	canvas.DrawString(msg);
}

void SpeedFlipTrainer::RenderAngleMeter(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	// Cap angle at 90
	int totalUnits = 180;
	
	float opacity = 1.0;
	Vector2 reqSize = Vector2{ (int)(screenWidth * 66 / 100.f), (int)(screenHeight * 4 / 100.f) };
	int unitWidth = reqSize.X / totalUnits;

	Vector2 boxSize = Vector2{ unitWidth * totalUnits, reqSize.Y };
	Vector2 startPos = Vector2{ (int)((screenWidth / 2) - (boxSize.X / 2)), (int)(screenHeight * 90 / 100.f) };

	struct Color baseColor = { (char)255, (char)255, (char)255, opacity };
	struct Line border = { (char)255, (char)255, (char)255, opacity, 2 };

	std::list<MeterRange> ranges;
	std::list<MeterMarking> markings;

	int greenRange = 7, yellowRange = 15;
	int lTarget = *optimalLeftAngle + 90;
	int rTarget = *optimalRightAngle + 90;

	int lyl = lTarget - yellowRange;
	int lgl = lTarget - greenRange;
	int lgh = lTarget + greenRange;
	int lyh = lTarget + yellowRange;

	int ryl = rTarget - yellowRange;
	int rgl = rTarget - greenRange;
	int rgh = rTarget + greenRange;
	int ryh = rTarget + yellowRange;

	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, lgh });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, lyh });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, lgl });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, lyl });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, rgh });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, ryh });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, rgl });
	markings.push_back({ (char)255, (char)255, (char)255, opacity, 3, ryl });

	ranges.push_back({ (char)255, (char)255, (char)50, 0.2, lyl, lgl });
	ranges.push_back({ (char)50, (char)255, (char)50, 0.2, lgl, lgh });
	ranges.push_back({ (char)255, (char)255, (char)50, 0.2, lgh, lyh });
	ranges.push_back({ (char)255, (char)255, (char)50, 0.2, ryl, rgl });
	ranges.push_back({ (char)50, (char)255, (char)50, 0.2, rgl, rgh });
	ranges.push_back({ (char)255, (char)255, (char)50, 0.2, rgh, ryh });

	if (attempt.dodged)
	{
		int angle = attempt.dodgeAngle;
		if (angle > 90) angle = 90;
		if (angle < -90) angle = -90;

		int angleAdjusted = 90 + angle;
		markings.push_back({ (char)0, (char)0, (char)0, 0.6, unitWidth, angleAdjusted });

		if (angleAdjusted < lyl)
		{
			ranges.push_back({ (char)255,(char)50, (char)50, 1, 0, lyl });
		}
		else if (angleAdjusted < lgl)
		{
			ranges.push_back({ (char)255, (char)255, (char)50, 1, lTarget - yellowRange, lTarget - greenRange });
		}
		else if (angleAdjusted < lgh)
		{
			ranges.push_back({ (char)50, (char)255, (char)50, 1, lTarget - greenRange, lTarget + greenRange });
		}
		else if (angleAdjusted < lyh)
		{
			ranges.push_back({ (char)255, (char)255, (char)50, 1, lTarget + greenRange, lTarget + yellowRange });
		}
		else if (angleAdjusted < ryl)
		{
			ranges.push_back({ (char)255,(char)50, (char)50, 1, lTarget + yellowRange, rTarget - yellowRange });
		}
		else if (angleAdjusted < rgl)
		{
			ranges.push_back({ (char)255, (char)255, (char)50, 1, rTarget - yellowRange, rTarget - greenRange });
		}
		else if (angleAdjusted < rgh)
		{
			ranges.push_back({ (char)50, (char)255, (char)50, 1, rTarget - greenRange, rTarget + greenRange });
		}
		else if (angleAdjusted < ryl)
		{
			ranges.push_back({ (char)255, (char)255, (char)50, 1, rTarget + greenRange, rTarget + yellowRange });
		}
		else
		{
			ranges.push_back({ (char)255, (char)50, (char)50, 1, rTarget + yellowRange, totalUnits });
		}
	}

	RenderMeter(canvas, startPos, reqSize, baseColor, border, totalUnits, ranges, markings, false);

	//draw angle label
	canvas.SetColor(255, 255, 255, (char)(255 * opacity));
	canvas.SetPosition(Vector2{ startPos.X, (int)(startPos.Y - 20) });
	canvas.DrawString("Dodge Angle: " + to_string(attempt.dodgeAngle) + " DEG");

	//draw time to ball label
	if (attempt.hit && attempt.ticksToBall > 0)
	{
		auto ms = attempt.ticksToBall * 1.0 / 120.0;
		string msg = fmt::format("Time to ball: {0:.4f}s", ms);

		int width = (msg.length() * 8) - (5 * 3); // 8 pixels per char - 5 pixels per space

		canvas.SetColor(255, 255, 255, (char)(255 * opacity));
		canvas.SetPosition(Vector2{ startPos.X + (int)(boxSize.X / 2) - (width/2), startPos.Y - 20 });
		canvas.DrawString(msg, 1, 1);
	}

	string msg = fmt::format("Horizontal distance traveled: {0:.1f}", attempt.traveledY);
	int width = (msg.length() * 6.6);

	//draw angle label
	if(attempt.traveledY < 225)
		canvas.SetColor(50, 255, 50, (char)(255 * opacity));
	else if(attempt.traveledY < 425)
		canvas.SetColor(255, 255, 50, (char)(255 * opacity));
	else
		canvas.SetColor(255, 50, 50, (char)(255 * opacity));

	canvas.SetPosition(Vector2{ startPos.X + boxSize.X - width, (int)(startPos.Y - 20) });
	canvas.DrawString(msg);
}

void SpeedFlipTrainer::Replay(Attempt* a, shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci)
{
	if (a->inputs.size() <= 0)
		return;

	int currentPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
	int tick = currentPhysicsFrame - startingPhysicsFrame;

	auto it = a->inputs.find(tick);
	if (it == a->inputs.end())
		return;

	ci->ActivateBoost = it->second.ActivateBoost;
	ci->DodgeForward = it->second.DodgeForward;
	ci->DodgeStrafe = it->second.DodgeStrafe;
	ci->Handbrake = it->second.Handbrake;
	ci->HoldingBoost = it->second.HoldingBoost;
	ci->Jump = it->second.Jump;
	ci->Jumped = it->second.Jumped;
	ci->Pitch = it->second.Pitch;
	ci->Roll = it->second.Roll;
	ci->Steer = it->second.Steer;
	ci->Throttle = it->second.Throttle;
	ci->Yaw = it->second.Yaw;

	gameWrapper->OverrideParams(ci, sizeof(ControllerInput));
}

void SpeedFlipTrainer::Perform(shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci)
{
	int currentPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
	int tick = currentPhysicsFrame - startingPhysicsFrame;

	ci->Throttle = 1;
	ci->ActivateBoost = 1;
	ci->HoldingBoost = 1;
	double dodgeAngle = -26;

	float initialSteer = 0.01;

	int beforeJump = 59;
	int jumpDuration = 11;
	int beforeCancelAdjust = 59;
	float adjustAmmount = 0.5;
	int adjustDuration = 16;
	int cancelSpeed = 4;
	int airRollDuration = 40;

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
	else if (tick <= beforeJump + jumpDuration + 1 + 1)
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

	gameWrapper->OverrideParams(ci, sizeof(ControllerInput));
}