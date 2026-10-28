#include "pch.h"	
#include "SpeedFlipTrainer.h"
#include "RenderMeter.h"
#include "UiTheme.h"
#include "UiDraw.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "BotAttempt.h"

#define _USE_MATH_DEFINES
#include <math.h>

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

float distance(Vector a, Vector b)
{
	// Calculating distance
	return sqrt(pow(a.X - b.X, 2) +
		pow(a.Y - b.Y, 2));
}

float distance3(Vector a, Vector b)
{
	const float dx = a.X - b.X;
	const float dy = a.Y - b.Y;
	const float dz = a.Z - b.Z;
	return sqrt(dx * dx + dy * dy + dz * dz);
}

// 100ms @ 120Hz physics
static constexpr int kHitExplodeGraceTicks = 12;
// Ball must travel at least this far (UU) after touch to count as a real hit
static constexpr float kBallMoveAfterHitUU = 40.f;

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

	// After a touch, watch for real ball displacement (filters ghost hits)
	if (attempt.hit && !attempt.ballMovedAfterHit && attempt.hasBallPosAtHit)
	{
		auto ball = gameWrapper->GetGameEventAsServer().GetBall();
		if (!ball.IsNull())
		{
			const float moved = distance3(ball.GetLocation(), attempt.ballPosAtHit);
			if (moved >= kBallMoveAfterHitUU)
			{
				attempt.ballMovedAfterHit = true;
				LOG("Ball moved {0:.0f} UU after touch — confirmed displacement", moved);
			}
		}
	}
}

void SpeedFlipTrainer::Hook()
{
	if (loaded)
		return;
	loaded = true;

	StartTrainingSession();

	LOG("Hooking");
	gameWrapper->RegisterDrawable(bind(&SpeedFlipTrainer::RenderMeters, this, std::placeholders::_1));

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
			PlayBot(gameWrapper, input);
		else if (mode == SpeedFlipTrainerMode::Replay)
			PlayAttempt(&replayAttempt, gameWrapper, input);

		// Has time started counting down?
		float timeLeft = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		int currentFrame = gameWrapper->GetEngine().GetPhysicsFrame();
		if (initialTime <= 0 || timeLeft == initialTime) // if we have no time OR countdown hasn't started just return
			return;
		if (startingPhysicsFrame < 0 && timeLeft < initialTime) // if this is the first frame the countdown has begun
		{
			startingPhysicsFrame = currentFrame;

			attempt = Attempt();
			sessionAttemptFinalized = false;

			if (!car.IsOnGround())
				attempt.startedInAir = true;

			if (!input->ActivateBoost)
				attempt.startedNoBoost = true;
		}

		// Keep measuring after a hit until explode/reset — early RecordCarHit
		// must not freeze the attempt before dodge/cancel are recorded.
		if (!attempt.exploded)
		{
			Measure(car, gameWrapper);
		}
	});

	gameWrapper->HookEvent("Function TAGame.Ball_TA.RecordCarHit",
		[this](std::string eventname) {
		// Allow hit even after Explode — event order can vary in the pack.
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining() || attempt.hit)
			return;
		if (startingPhysicsFrame < 0)
			return;

		const int currentTick = gameWrapper->GetEngine().GetPhysicsFrame() - startingPhysicsFrame;
		attempt.ticksToBall = gameWrapper->GetLocalCar().GetLastBallImpactFrame() - startingPhysicsFrame;
		attempt.timeToBall = initialTime - gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		attempt.hit = true;
		attempt.hitTick = currentTick;

		auto ball = gameWrapper->GetGameEventAsServer().GetBall();
		if (!ball.IsNull())
		{
			attempt.ballPosAtHit = ball.GetLocation();
			attempt.hasBallPosAtHit = true;
		}

		// Explode already happened near this touch → reject
		if (attempt.exploded && attempt.explodeTick >= 0)
		{
			const int dt = std::abs(attempt.explodeTick - attempt.hitTick);
			if (dt <= kHitExplodeGraceTicks)
			{
				attempt.explodedWithin100msOfHit = true;
				LOG("Touch within {0} ticks of explode — hit rejected", dt);
			}
		}

		CaptureInputTimelineReview();
		LOG("Ball touch at tick {0} ({1:.3f}s)", attempt.hitTick, attempt.timeToBall);
	});

	gameWrapper->HookEvent("Function TAGame.Ball_TA.Explode", 
		[this](std::string eventName) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;

		auto ball = gameWrapper->GetGameEventAsServer().GetBall();
		auto car = gameWrapper->GetGameEventAsServer().GetLocalPrimaryPlayer();

		auto distanceToBall = distance(ball.GetLocation(), car.GetLocation());
		auto meters = (distanceToBall / 100.0) - 4.8; // account for car distance
		LOG("Distance to ball = {0:.1f}m", meters);

		attempt.explodeTick = (startingPhysicsFrame >= 0)
			? (gameWrapper->GetEngine().GetPhysicsFrame() - startingPhysicsFrame)
			: -1;

		attempt.exploded = true;

		// One last ball-position sample before it's gone
		if (attempt.hit && !attempt.ballMovedAfterHit && attempt.hasBallPosAtHit && !ball.IsNull())
		{
			const float moved = distance3(ball.GetLocation(), attempt.ballPosAtHit);
			if (moved >= kBallMoveAfterHitUU)
			{
				attempt.ballMovedAfterHit = true;
				LOG("Ball moved {0:.0f} UU at explode — confirmed displacement", moved);
			}
		}

		// Touch then explode within 100ms → not a real hit
		if (attempt.hit && attempt.hitTick >= 0 && attempt.explodeTick >= 0)
		{
			const int dt = attempt.explodeTick - attempt.hitTick;
			if (dt >= 0 && dt <= kHitExplodeGraceTicks)
			{
				attempt.explodedWithin100msOfHit = true;
				LOG("Ball exploded {0} ticks ({1}ms) after touch — hit rejected", dt, (int)(dt * 1000.0 / 120.0));
			}
		}

		CaptureInputTimelineReview();
	});

	gameWrapper->HookEventPost("Function Engine.Controller.Restart", 
		[this](std::string eventName) {
		if (!*enabled || !loaded || !gameWrapper->IsInCustomTraining())
			return;

		// Freeze timeline for review if we somehow missed hit/explode capture
		CaptureInputTimelineReview();
		// Finalize sticks + ratio only here, using the final hit/explode state
		FinalizeAttemptForSession(attempt);

		//gameWrapper->GetCurrentGameState().SetGameTimeRemaining(2.14);
		initialTime = gameWrapper->GetCurrentGameState().GetGameTimeRemaining();
		ticksBeforeTimeExpired = initialTime * 120;
		startingPhysicsFrame = -1;

		if (attempt.IsConfirmedBallHit())
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

	cvarManager->registerCvar("sf_show_angle", "0", "Show angle meter (disabled — covered by stick box).", true, false, 0, false, 0, true).bindTo(showAngleMeter);
	cvarManager->getCvar("sf_show_angle").setValue(false);
	*showAngleMeter = false;
	cvarManager->registerCvar("sf_show_position", "1", "Show horizontal position meter.", true, false, 0, false, 0, true).bindTo(showPositionMeter);
	cvarManager->registerCvar("sf_show_jump", "1", "Show jump meter.", true, false, 0, false, 0, true).bindTo(showJumpMeter);
	cvarManager->registerCvar("sf_show_flip", "1", "Show flip cancel meter.", true, false, 0, false, 0, true).bindTo(showFlipMeter);
	cvarManager->registerCvar("sf_show_inputs", "0", "Show left input timeline overlay (disabled).", true, false, 0, false, 0, true).bindTo(showInputTimeline);
	cvarManager->getCvar("sf_show_inputs").setValue(false);
	*showInputTimeline = false;
	cvarManager->registerCvar("sf_show_stick_paths", "1", "Show dodge/air stick path boxes.", true, false, 0, false, 0, true).bindTo(showStickPaths);
	cvarManager->registerCvar("sf_show_scorecard", "1", "Show horizontal attempt scorecard after each try.", true, false, 0, false, 0, true).bindTo(showAttemptScorecard);
	cvarManager->registerCvar("sf_show_ratio", "1", "Show live success ratio overlay.", true, false, 0, false, 0, true).bindTo(showSuccessRatio);

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

		LoadSessionHistory();
		PromoteCurrentSessionCheckpoint();

		attemptFileDialog.workingDirectory = dataDir / "attempts";
		if (!std::filesystem::exists(attemptFileDialog.workingDirectory))
			std::filesystem::create_directories(attemptFileDialog.workingDirectory);
		attemptFileDialog.name = "Select replay attempt";

		botFileDialog.workingDirectory = dataDir / "bots";
		if (!std::filesystem::exists(botFileDialog.workingDirectory))
			std::filesystem::create_directories(botFileDialog.workingDirectory);
		botFileDialog.name = "Select bot file";
	}

}

void SpeedFlipTrainer::onUnload()
{
	if (!loaded)
		return;
	loaded = false;

	EndTrainingSession();
	ClearSessionStickPatterns();

	LOG("Unhooking");
	gameWrapper->UnhookEvent("Function TAGame.Car_TA.SetVehicleInput");
	gameWrapper->UnhookEvent("Function TAGame.Ball_TA.RecordCarHit");
	gameWrapper->UnhookEvent("Function TAGame.Ball_TA.Explode");
	gameWrapper->UnhookEventPost("Function Engine.Controller.Restart"); 
	gameWrapper->UnregisterDrawables();
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

	// Bottom dodge-angle bar removed (covered by dodge stick box)

	if(*showPositionMeter)
		RenderPositionMeter(canvas, SCREENWIDTH, SCREENHEIGHT);

	if(*showFlipMeter)
		RenderFlipCancelMeter(canvas, SCREENWIDTH, SCREENHEIGHT);

	if(*showJumpMeter)
		RenderFirstJumpMeter(canvas, SCREENWIDTH, SCREENHEIGHT);

	// Input timeline removed from overlay (kept in code unused; user prefers stick + scorecard)

	if (*showStickPaths)
		RenderStickPathBoxes(canvas, SCREENWIDTH, SCREENHEIGHT);

	if (*showAttemptScorecard)
		RenderAttemptScorecard(canvas, SCREENWIDTH, SCREENHEIGHT);

	if (*showSuccessRatio)
		RenderSuccessRatio(canvas, SCREENWIDTH, SCREENHEIGHT);
}

void SpeedFlipTrainer::CaptureInputTimelineReview()
{
	if (attempt.inputs.empty())
		return;
	inputTimelineReview = attempt;
}

bool SpeedFlipTrainer::IsMechanicsAllGreen(const Attempt& a) const
{
	if (a.startedInAir)
		return false;
	return GradeBoostThrottle(a) == CheckGrade::Good
		&& GradeJumpTiming(a) == CheckGrade::Good
		&& GradeDodgeAngle(a) == CheckGrade::Good
		&& GradeCancelTiming(a) == CheckGrade::Good;
}

void SpeedFlipTrainer::ClearSessionStickPatterns()
{
	successStickTrails.clear();
	greenFailStickTrails.clear();
	sessionAttemptFinalized = false;
}

std::filesystem::path SpeedFlipTrainer::SessionHistoryPath() const
{
	return dataDir / "session_history.txt";
}

std::filesystem::path SpeedFlipTrainer::SessionCurrentPath() const
{
	return dataDir / "session_current.txt";
}

std::string SpeedFlipTrainer::FormatSessionTimestamp()
{
	using clock = std::chrono::system_clock;
	const auto now = clock::now();
	const std::time_t t = clock::to_time_t(now);
	std::tm localTm{};
#if defined(_WIN32)
	localtime_s(&localTm, &t);
#else
	localtime_r(&t, &localTm);
#endif
	std::ostringstream oss;
	oss << std::put_time(&localTm, "%Y-%m-%d %H:%M");
	return oss.str();
}

float SpeedFlipTrainer::SessionRatio(const SessionStats& s)
{
	if (s.validAttempts <= 0)
		return 0.f;
	return 100.f * (float)s.successes / (float)s.validAttempts;
}

void SpeedFlipTrainer::LoadSessionHistory()
{
	sessionHistory.clear();
	const auto path = SessionHistoryPath();
	if (!std::filesystem::exists(path))
		return;

	std::ifstream in(path);
	if (!in)
		return;

	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#')
			continue;

		// "YYYY-MM-DD HH:MM  successes  valid"
		std::istringstream iss(line);
		std::string date, time;
		SessionStats s;
		if (!(iss >> date >> time >> s.successes >> s.validAttempts))
			continue;
		s.startedAt = date + " " + time;
		if (s.validAttempts < 0) s.validAttempts = 0;
		if (s.successes < 0) s.successes = 0;
		if (s.successes > s.validAttempts) s.successes = s.validAttempts;
		sessionHistory.push_back(s);
	}
	LOG("Loaded {0} training session(s) from history", sessionHistory.size());
}

void SpeedFlipTrainer::SaveSessionHistory() const
{
	if (dataDir.empty())
		return;

	std::ofstream out(SessionHistoryPath(), std::ios::trunc);
	if (!out)
	{
		LOG("Failed to write session history");
		return;
	}

	out << "# started_at  successes  valid_attempts\n";
	for (const auto& s : sessionHistory)
		out << s.startedAt << "  " << s.successes << "  " << s.validAttempts << "\n";
}

void SpeedFlipTrainer::SaveCurrentSessionCheckpoint() const
{
	if (dataDir.empty() || !trainingSessionActive)
		return;

	std::ofstream out(SessionCurrentPath(), std::ios::trunc);
	if (!out)
		return;
	out << currentSession.startedAt << "  " << currentSession.successes << "  " << currentSession.validAttempts << "\n";
}

void SpeedFlipTrainer::PromoteCurrentSessionCheckpoint()
{
	const auto path = SessionCurrentPath();
	if (!std::filesystem::exists(path))
		return;

	std::ifstream in(path);
	if (!in)
		return;

	std::string date, time;
	SessionStats s;
	if (!(in >> date >> time >> s.successes >> s.validAttempts))
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
		return;
	}
	s.startedAt = date + " " + time;
	std::error_code ec;
	std::filesystem::remove(path, ec);

	if (s.validAttempts > 0)
	{
		sessionHistory.push_back(s);
		SaveSessionHistory();
		LOG("Recovered interrupted session {0}: {1}/{2}", s.startedAt, s.successes, s.validAttempts);
	}
}

void SpeedFlipTrainer::StartTrainingSession()
{
	PromoteCurrentSessionCheckpoint();

	currentSession = SessionStats{};
	currentSession.startedAt = FormatSessionTimestamp();
	trainingSessionActive = true;
	SaveCurrentSessionCheckpoint();
	LOG("Training session started {0}", currentSession.startedAt);
}

void SpeedFlipTrainer::EndTrainingSession()
{
	if (!trainingSessionActive)
		return;

	// Last attempt may not have hit Restart yet
	FinalizeAttemptForSession(attempt);

	trainingSessionActive = false;

	if (currentSession.validAttempts > 0)
	{
		sessionHistory.push_back(currentSession);
		SaveSessionHistory();
		LOG("Training session ended {0}: {1}/{2} ({3:.1f}%)",
			currentSession.startedAt,
			currentSession.successes,
			currentSession.validAttempts,
			SessionRatio(currentSession));
	}

	std::error_code ec;
	std::filesystem::remove(SessionCurrentPath(), ec);
	currentSession = SessionStats{};
}

void SpeedFlipTrainer::FinalizeAttemptForSession(const Attempt& a)
{
	if (!trainingSessionActive || sessionAttemptFinalized)
		return;
	// Skip overshoots / resets that never committed to a speedflip
	if (!a.jumped || !a.dodged)
		return;
	if (a.inputs.size() < 2)
		return;

	sessionAttemptFinalized = true;

	// Success = touch + ball actually moved + no explode within 100ms of touch
	const bool success = a.IsConfirmedBallHit();
	currentSession.validAttempts++;
	if (success)
		currentSession.successes++;
	SaveCurrentSessionCheckpoint();

	LOG("Attempt result: {0} (hit={1} moved={2} quickExplode={3})",
		success ? "SUCCESS" : "FAIL",
		a.hit, a.ballMovedAfterHit, a.explodedWithin100msOfHit);

	constexpr size_t kMaxTrails = 40;
	StickTrailSnapshot snap;
	snap.inputs = a.inputs;
	AnalyzeJumpEdges(a, snap.firstJumpTick, snap.secondJumpTick, snap.cancelTick);
	snap.dodged = a.dodged;
	snap.dodgedTick = a.dodgedTick;
	snap.dodgeAngle = a.dodgeAngle;

	if (success)
	{
		successStickTrails.push_back(std::move(snap));
		if (successStickTrails.size() > kMaxTrails)
			successStickTrails.erase(successStickTrails.begin());
		LOG("Session pattern: success trail #{0} (ratio {1}/{2})",
			successStickTrails.size(), currentSession.successes, currentSession.validAttempts);
	}
	else if (a.exploded && !a.IsConfirmedBallHit() && IsMechanicsAllGreen(a))
	{
		greenFailStickTrails.push_back(std::move(snap));
		if (greenFailStickTrails.size() > kMaxTrails)
			greenFailStickTrails.erase(greenFailStickTrails.begin());
		LOG("Session pattern: all-green fail trail #{0}", greenFailStickTrails.size());
	}
}

const Attempt* SpeedFlipTrainer::GetInputTimelineSource() const
{
	const bool live = startingPhysicsFrame >= 0 && !attempt.hit && !attempt.exploded;
	if (live)
		return &attempt;
	return &inputTimelineReview;
}

void SpeedFlipTrainer::AnalyzeJumpEdges(const Attempt& src, int& firstJumpTick, int& secondJumpTick, int& cancelTick) const
{
	firstJumpTick = -1;
	secondJumpTick = -1;
	cancelTick = -1;

	bool jumpWasDown = false;
	for (const auto& kv : src.inputs)
	{
		const bool jumpDown = kv.second.Jump != 0;
		if (jumpDown && !jumpWasDown)
		{
			if (firstJumpTick < 0)
				firstJumpTick = kv.first;
			else if (secondJumpTick < 0)
				secondJumpTick = kv.first;
		}
		jumpWasDown = jumpDown;

		// Cancel = first strong nose-up pitch after the second jump press
		if (cancelTick < 0 && secondJumpTick >= 0 && kv.first >= secondJumpTick && kv.second.Pitch > 0.8f)
			cancelTick = kv.first;
	}
}

void SpeedFlipTrainer::RenderInputTimeline(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	const Attempt* src = GetInputTimelineSource();
	if (src == nullptr || src->inputs.empty())
		return;

	const int maxTick = src->inputs.rbegin()->first;
	if (maxTick <= 0)
		return;

	const float panelWidth = screenWidth * 0.16f;
	const float labelWidth = 58.0f;
	const float marginX = 8.0f;
	// Leave room below for stick-path boxes
	const float marginY = screenHeight * 0.12f;
	const float panelHeight = screenHeight * 0.30f;
	const float laneGap = 6.0f;
	const int laneCount = 4;
	const float laneHeight = (panelHeight - laneGap * (laneCount - 1)) / laneCount;
	const float chartLeft = marginX + labelWidth;
	const float chartWidth = panelWidth - labelWidth - 4.0f;
	if (chartWidth < 20.0f)
		return;

	auto tickToX = [&](int tick) -> int {
		float t = (float)tick / (float)maxTick;
		if (t < 0.f) t = 0.f;
		if (t > 1.f) t = 1.f;
		return (int)(chartLeft + t * chartWidth);
	};

	const char* labels[4] = { "Throttle", "Boost", "Jump", "Pitch" };
	const bool live = startingPhysicsFrame >= 0 && !attempt.hit && !attempt.exploded;

	// Panel background
	canvas.SetColor(20, 20, 20, 160);
	canvas.SetPosition(Vector2{ (int)marginX, (int)marginY });
	canvas.FillBox(Vector2{ (int)panelWidth, (int)(panelHeight + 28) });

	canvas.SetColor(220, 220, 220, 255);
	canvas.SetPosition(Vector2{ (int)(marginX + 4), (int)(marginY - 16) });
	canvas.DrawString(live ? "Inputs (live)" : "Inputs (last)", 1, 1, true, false);

	for (int lane = 0; lane < laneCount; ++lane)
	{
		const float laneTop = marginY + lane * (laneHeight + laneGap);
		const int y0 = (int)laneTop;
		const int h = (int)laneHeight;

		// Lane track
		canvas.SetColor(60, 60, 60, 180);
		canvas.SetPosition(Vector2{ (int)chartLeft, y0 });
		canvas.FillBox(Vector2{ (int)chartWidth, h });

		canvas.SetColor(200, 200, 200, 255);
		canvas.SetPosition(Vector2{ (int)(marginX + 2), y0 + (int)(h * 0.35f) });
		canvas.DrawString(labels[lane], 1, 1, true, false);

		if (lane == 3)
		{
			// Pitch center line
			canvas.SetColor(120, 120, 120, 200);
			int midY = y0 + h / 2;
			canvas.DrawLine(Vector2{ (int)chartLeft, midY }, Vector2{ (int)(chartLeft + chartWidth), midY }, 1);
		}

		// Sample columns across the chart (compress full attempt into panel width)
		const int columns = (int)chartWidth;
		for (int col = 0; col < columns; ++col)
		{
			int tick = (int)((col / chartWidth) * maxTick);
			auto it = src->inputs.lower_bound(tick);
			if (it == src->inputs.end())
				--it;
			else if (it != src->inputs.begin())
			{
				auto prev = it;
				--prev;
				if (abs(prev->first - tick) < abs(it->first - tick))
					it = prev;
			}

			const ControllerInput& in = it->second;
			const int x = (int)(chartLeft + col);

			if (lane == 0)
			{
				// Throttle 0..1 fill from bottom
				float v = in.Throttle;
				if (v < 0.f) v = 0.f;
				if (v > 1.f) v = 1.f;
				if (v > 0.05f)
				{
					int barH = (int)(h * v);
					if (barH < 1) barH = 1;
					canvas.SetColor(80, 180, 255, 220);
					canvas.SetPosition(Vector2{ x, y0 + h - barH });
					canvas.FillBox(Vector2{ 1, barH });
				}
			}
			else if (lane == 1)
			{
				if (in.ActivateBoost || in.HoldingBoost)
				{
					canvas.SetColor(255, 180, 40, 230);
					canvas.SetPosition(Vector2{ x, y0 + 1 });
					canvas.FillBox(Vector2{ 1, h - 2 });
				}
			}
			else if (lane == 2)
			{
				if (in.Jump)
				{
					canvas.SetColor(80, 255, 120, 230);
					canvas.SetPosition(Vector2{ x, y0 + 1 });
					canvas.FillBox(Vector2{ 1, h - 2 });
				}
			}
			else
			{
				// Pitch: positive (nose up / cancel) above center, negative below
				float p = in.Pitch;
				if (p > 0.05f)
				{
					int barH = (int)((h / 2.f) * (p > 1.f ? 1.f : p));
					if (barH < 1) barH = 1;
					canvas.SetColor(255, 90, 90, 230);
					canvas.SetPosition(Vector2{ x, y0 + h / 2 - barH });
					canvas.FillBox(Vector2{ 1, barH });
				}
				else if (p < -0.05f)
				{
					float mag = -p;
					if (mag > 1.f) mag = 1.f;
					int barH = (int)((h / 2.f) * mag);
					if (barH < 1) barH = 1;
					canvas.SetColor(180, 100, 255, 200);
					canvas.SetPosition(Vector2{ x, y0 + h / 2 });
					canvas.FillBox(Vector2{ 1, barH });
				}
			}
		}

		// Lane border
		canvas.SetColor(180, 180, 180, 160);
		canvas.SetPosition(Vector2{ (int)chartLeft, y0 });
		canvas.DrawBox(Vector2{ (int)chartWidth, h });
	}

	// Time axis labels
	const int endMs = (int)(maxTick / 120.0 * 1000.0);
	canvas.SetColor(200, 200, 200, 255);
	canvas.SetPosition(Vector2{ (int)chartLeft, (int)(marginY + panelHeight + 6) });
	canvas.DrawString("0ms", 1, 1, true, false);

	string endLabel = to_string(endMs) + "ms";
	canvas.SetPosition(Vector2{ tickToX(maxTick) - (int)(endLabel.size() * 6), (int)(marginY + panelHeight + 6) });
	canvas.DrawString(endLabel, 1, 1, true, false);
}

void SpeedFlipTrainer::DrawStickBox(
	CanvasWrapper canvas,
	int left, int top, int size, int headerH, int pad,
	const char* title,
	bool live,
	const std::vector<StickTrailSnapshot>* sessionTrails,
	Ui::Rgb sessionColor,
	unsigned char sessionAlpha,
	const Attempt* current,
	bool drawZones,
	int markerRadius) const
{
	const bool hasCurrent = current != nullptr && current->inputs.size() >= 2;
	const bool hasSession = sessionTrails != nullptr && !sessionTrails->empty();
	if (!hasCurrent && !hasSession)
		return;

	Ui::DrawPanel(canvas, left, top, size, size + headerH, live);
	Ui::DrawInsetWell(canvas, left + 1, top + 1, size - 2, headerH - 1);
	Ui::Set(canvas, Ui::TextSecondary, 230);
	canvas.SetPosition(Vector2{ left + 4, top + (headerH > 16 ? 5 : 2) });
	canvas.DrawString(title, 1, 1, true, false);

	if (hasSession)
	{
		string count = "x" + to_string(sessionTrails->size());
		Ui::Set(canvas, sessionColor, 220);
		canvas.SetPosition(Vector2{ left + size - 4 - (int)count.size() * 6, top + (headerH > 16 ? 5 : 2) });
		canvas.DrawString(count, 1, 1, true, false);
	}

	const int plotX = left + pad;
	const int plotY = top + headerH + pad;
	const int plotS = size - pad * 2;
	if (plotS < 8)
		return;
	Ui::DrawInsetWell(canvas, plotX, plotY, plotS, plotS);

	const int cx = plotX + plotS / 2;
	const int cy = plotY + plotS / 2;
	const float half = plotS * 0.45f;

	auto angleToPoint = [&](float angleDeg, float radius) -> Vector2 {
		const float rad = angleDeg * (float)M_PI / 180.0f;
		return Vector2{ (int)(cx + sinf(rad) * radius), (int)(cy - cosf(rad) * radius) };
	};

	if (drawZones)
	{
		auto drawZone = [&](int angleDeg) {
			Ui::Set(canvas, Ui::ZoneAcceptable, 40);
			for (float a = angleDeg - 15.f; a <= angleDeg + 15.f; a += 2.f)
				canvas.DrawLine(Vector2{ cx, cy }, angleToPoint(a, half), 2);
			Ui::Set(canvas, Ui::ZoneIdeal, 55);
			for (float a = angleDeg - 8.f; a <= angleDeg + 8.f; a += 2.f)
				canvas.DrawLine(Vector2{ cx, cy }, angleToPoint(a, half), 2);
		};
		drawZone(*optimalLeftAngle);
		drawZone(*optimalRightAngle);
	}

	Ui::Set(canvas, Ui::Grid, 90);
	canvas.DrawLine(Vector2{ plotX + 2, cy }, Vector2{ plotX + plotS - 2, cy }, 1);
	canvas.DrawLine(Vector2{ cx, plotY + 2 }, Vector2{ cx, plotY + plotS - 2 }, 1);

	auto toPoint = [&](const ControllerInput& in) -> Vector2 {
		float xAxis = in.DodgeStrafe;
		float yAxis = in.DodgeForward;
		if (xAxis < -1.f) xAxis = -1.f;
		if (xAxis > 1.f) xAxis = 1.f;
		if (yAxis < -1.f) yAxis = -1.f;
		if (yAxis > 1.f) yAxis = 1.f;
		return Vector2{ (int)(cx + xAxis * half), (int)(cy - yAxis * half) };
	};

	auto drawTrail = [&](const std::map<int, ControllerInput>& inputs, Ui::Rgb col, unsigned char alpha, int thickness) {
		if (inputs.size() < 2)
			return;
		auto it = inputs.begin();
		auto prev = it;
		++it;
		Ui::Set(canvas, col, alpha);
		for (; it != inputs.end(); ++it)
		{
			if (prev->first > Ui::StickTrailMaxTick || it->first > Ui::StickTrailMaxTick)
				break;
			canvas.DrawLine(toPoint(prev->second), toPoint(it->second), thickness);
			prev = it;
		}
	};

	auto drawMarkerAt = [&](const std::map<int, ControllerInput>& inputs, int tick, Ui::Rgb ring, const char* label, int rad, bool withLabel) {
		if (tick < 0 || tick > Ui::StickTrailMaxTick || rad < 2)
			return;
		auto found = inputs.find(tick);
		if (found == inputs.end())
		{
			found = inputs.lower_bound(tick);
			if (found == inputs.end())
				return;
		}
		Vector2 p = toPoint(found->second);
		Ui::Set(canvas, Ui::PanelFill, 255);
		for (int dy = -rad + 1; dy <= rad - 1; ++dy)
			for (int dx = -rad + 1; dx <= rad - 1; ++dx)
				if (dx * dx + dy * dy <= (rad - 1) * (rad - 1))
				{
					canvas.SetPosition(Vector2{ p.X + dx, p.Y + dy });
					canvas.FillBox(Vector2{ 1, 1 });
				}
		Ui::Set(canvas, ring, 255);
		for (int dy = -rad; dy <= rad; ++dy)
			for (int dx = -rad; dx <= rad; ++dx)
			{
				int d2 = dx * dx + dy * dy;
				if (d2 <= rad * rad && d2 >= (rad - 2) * (rad - 2))
				{
					canvas.SetPosition(Vector2{ p.X + dx, p.Y + dy });
					canvas.FillBox(Vector2{ 1, 1 });
				}
			}
		if (withLabel)
		{
			Ui::Set(canvas, ring, 255);
			const int textW = (int)strlen(label) * 6;
			canvas.SetPosition(Vector2{ p.X - textW / 2, p.Y - 5 });
			canvas.DrawString(label, 1, 1, true, false);
		}
	};

	if (hasSession)
	{
		for (const auto& trail : *sessionTrails)
		{
			drawTrail(trail.inputs, sessionColor, sessionAlpha, 1);

			const bool labelEvents = sessionTrails->size() == 1;
			drawMarkerAt(trail.inputs, trail.firstJumpTick, Ui::RLBlue, "J1", markerRadius, labelEvents);
			drawMarkerAt(trail.inputs, trail.secondJumpTick, Ui::RLOrange, "J2", markerRadius, labelEvents);
			drawMarkerAt(trail.inputs, trail.cancelTick, Ui::TextPrimary, "C", markerRadius, labelEvents);

			if (trail.dodged && trail.dodgedTick <= Ui::StickTrailMaxTick)
			{
				Vector2 inner = angleToPoint((float)trail.dodgeAngle, half * 0.55f);
				Vector2 outer = angleToPoint((float)trail.dodgeAngle, half);
				Ui::Set(canvas, sessionColor, (unsigned char)(sessionAlpha + 40 > 255 ? 255 : sessionAlpha + 40));
				canvas.DrawLine(inner, outer, 1);
			}
		}
	}

	if (hasCurrent)
	{
		int firstJumpTick = -1, secondJumpTick = -1, cancelTick = -1;
		AnalyzeJumpEdges(*current, firstJumpTick, secondJumpTick, cancelTick);

		auto phaseColor = [&](int tick) -> Ui::Rgb {
			if (firstJumpTick < 0 || tick < firstJumpTick) return Ui::TrailPre;
			if (secondJumpTick < 0 || tick < secondJumpTick) return Ui::TrailJ1J2;
			if (cancelTick < 0 || tick < cancelTick) return Ui::TrailJ2C;
			return Ui::TrailAfterC;
		};

		auto it = current->inputs.begin();
		auto prev = it;
		++it;
		for (; it != current->inputs.end(); ++it)
		{
			if (prev->first > Ui::StickTrailMaxTick || it->first > Ui::StickTrailMaxTick)
				break;
			Ui::Rgb col = phaseColor(it->first);
			unsigned char a = (col.r == Ui::TrailAfterC.r && col.g == Ui::TrailAfterC.g) ? 150 : 230;
			Ui::Set(canvas, col, a);
			canvas.DrawLine(toPoint(prev->second), toPoint(it->second), 2);
			prev = it;
		}

		if (current->dodged && current->dodgedTick <= Ui::StickTrailMaxTick)
		{
			Vector2 inner = angleToPoint((float)current->dodgeAngle, half * 0.55f);
			Vector2 outer = angleToPoint((float)current->dodgeAngle, half);
			Ui::Set(canvas, Ui::TextPrimary, 220);
			canvas.DrawLine(inner, outer, 2);
			canvas.SetPosition(Vector2{ outer.X - 3, outer.Y - 3 });
			canvas.FillBox(Vector2{ 6, 6 });
		}

		drawMarkerAt(current->inputs, firstJumpTick, Ui::RLBlue, "J1", markerRadius, true);
		drawMarkerAt(current->inputs, secondJumpTick, Ui::RLOrange, "J2", markerRadius, true);
		drawMarkerAt(current->inputs, cancelTick, Ui::TextPrimary, "C", markerRadius, true);
	}
}

void SpeedFlipTrainer::RenderStickPathBoxes(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	const Attempt* src = GetInputTimelineSource();
	const bool hasCurrent = src != nullptr && src->inputs.size() >= 2;
	const bool hasSession = !successStickTrails.empty() || !greenFailStickTrails.empty();
	if (!hasCurrent && !hasSession)
		return;

	const bool live = startingPhysicsFrame >= 0 && !attempt.hit && !attempt.exploded;
	const int left = (int)(screenWidth * 0.02f);
	const int top = (int)(screenHeight * 0.24f);
	const int mainSize = (int)(screenWidth * 0.15f);
	const int mainHeader = 22;
	const int mainPad = 10;

	DrawStickBox(
		canvas, left, top, mainSize, mainHeader, mainPad,
		"DODGE STICK", live,
		nullptr, Ui::StatusGood, 0,
		hasCurrent ? src : nullptr,
		true, 8);

	const int miniSize = (int)(screenWidth * 0.072f);
	const int miniHeader = 14;
	const int miniPad = 4;
	const int gap = 6;
	const int miniTop = top + mainSize + mainHeader + gap;
	const int okLeft = left;
	const int failLeft = left + miniSize + gap;

	DrawStickBox(
		canvas, okLeft, miniTop, miniSize, miniHeader, miniPad,
		"OK", false,
		&successStickTrails, Ui::StatusGood, 90,
		nullptr,
		true, 4);

	DrawStickBox(
		canvas, failLeft, miniTop, miniSize, miniHeader, miniPad,
		"FAIL", false,
		&greenFailStickTrails, Ui::StatusBad, 90,
		nullptr,
		true, 4);
}

SpeedFlipTrainer::CheckGrade SpeedFlipTrainer::GradeJumpTiming(const Attempt& a) const
{
	if (!a.jumped)
		return CheckGrade::Bad;

	const int totalUnits = *jumpHigh - *jumpLow;
	const int halfMark = totalUnits / 2;
	const int meterOriginTick = *jumpHigh - halfMark - halfMark;
	int units = a.jumpTick - meterOriginTick;
	if (units < 0) units = 0;
	if (units > totalUnits) units = totalUnits;

	if (units >= halfMark - 10 && units <= halfMark + 5)
		return CheckGrade::Good;
	if (units >= halfMark - 15 && units <= halfMark + 10)
		return CheckGrade::Ok;
	return CheckGrade::Bad;
}

SpeedFlipTrainer::CheckGrade SpeedFlipTrainer::GradeDodgeAngle(const Attempt& a) const
{
	if (!a.dodged)
		return CheckGrade::Bad;

	const int greenRange = 8;
	const int yellowRange = 15;
	auto nearTarget = [&](int target) {
		int d = abs(a.dodgeAngle - target);
		if (d <= greenRange) return CheckGrade::Good;
		if (d <= yellowRange) return CheckGrade::Ok;
		return CheckGrade::Bad;
	};

	CheckGrade left = nearTarget(*optimalLeftAngle);
	CheckGrade right = nearTarget(*optimalRightAngle);
	if (left == CheckGrade::Good || right == CheckGrade::Good)
		return CheckGrade::Good;
	if (left == CheckGrade::Ok || right == CheckGrade::Ok)
		return CheckGrade::Ok;
	return CheckGrade::Bad;
}

SpeedFlipTrainer::CheckGrade SpeedFlipTrainer::GradeCancelTiming(const Attempt& a) const
{
	if (!a.dodged || !a.flipCanceled)
		return CheckGrade::Bad;

	const int ticks = a.flipCancelTick - a.dodgedTick;
	const int threshold = *flipCancelThreshold;
	if (ticks <= (int)(threshold * 0.6f))
		return CheckGrade::Good;
	if (ticks <= (int)(threshold * 0.9f))
		return CheckGrade::Ok;
	return CheckGrade::Bad;
}

SpeedFlipTrainer::CheckGrade SpeedFlipTrainer::GradeBoostThrottle(const Attempt& a) const
{
	const int worst = (a.ticksNotPressingBoost > a.ticksNotPressingThrottle)
		? a.ticksNotPressingBoost
		: a.ticksNotPressingThrottle;
	if (worst <= 3)
		return CheckGrade::Good;
	if (worst <= 12)
		return CheckGrade::Ok;
	return CheckGrade::Bad;
}

void SpeedFlipTrainer::RenderSuccessRatio(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	const int valid = currentSession.validAttempts;
	const int hits = currentSession.successes;
	const float pct = SessionRatio(currentSession);

	string line;
	if (valid <= 0)
		line = "Session  --  (jump+dodge to count)";
	else
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "Session  %d/%d  %.0f%%", hits, valid, pct);
		line = buf;
	}

	const int panelW = (int)(screenWidth * 0.22f);
	const int panelH = (int)(screenHeight * 0.028f);
	const int panelX = (int)((screenWidth - panelW) / 2);
	const int panelY = (int)(screenHeight * 0.105f);

	Ui::Rgb border = Ui::StatusNone;
	if (valid > 0)
	{
		if (pct >= 99.f) border = Ui::StatusGood;
		else if (pct >= 70.f) border = Ui::StatusWarn;
		else border = Ui::StatusBad;
	}
	Ui::DrawPanel(canvas, panelX, panelY, panelW, panelH, border, 255);

	Ui::Set(canvas, valid > 0 ? border : Ui::TextSecondary, 255);
	const int textW = (int)line.size() * 6;
	canvas.SetPosition(Vector2{ panelX + (panelW - textW) / 2, panelY + panelH / 2 - 5 });
	canvas.DrawString(line, 1, 1, true, false);
}

void SpeedFlipTrainer::RenderAttemptScorecard(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	// Only show between attempts (frozen review), not during the live take
	const bool live = startingPhysicsFrame >= 0 && !attempt.hit && !attempt.exploded;
	if (live)
		return;

	const Attempt& a = inputTimelineReview;
	if (a.inputs.empty() && !a.jumped && !a.dodged && !a.hit && !a.exploded)
		return;

	struct Item {
		CheckGrade grade;
		const char* label;
		int kind; // 0 ground, 1 boost, 2 jump, 3 angle, 4 cancel, 5 ball
	};

	CheckGrade ground = a.startedInAir ? CheckGrade::Bad : CheckGrade::Good;

	Item items[5] = {
		{ ground, "Ground", 0 },
		{ GradeBoostThrottle(a), "Inputs", 1 },
		{ GradeJumpTiming(a), "Jump", 2 },
		{ GradeDodgeAngle(a), "Angle", 3 },
		{ GradeCancelTiming(a), "Cancel", 4 },
	};

	const int count = 5;
	const int cellW = (int)(screenWidth * 0.055f);
	const int cellH = (int)(screenHeight * 0.038f);
	const int gap = 4;
	const int totalW = count * cellW + (count - 1) * gap;
	const int startX = (int)((screenWidth - totalW) / 2);
	const int startY = (int)(screenHeight * 0.058f);

	auto statusRgb = [](CheckGrade g) -> Ui::Rgb {
		if (g == CheckGrade::Good) return Ui::StatusGood;
		if (g == CheckGrade::Ok) return Ui::StatusWarn;
		if (g == CheckGrade::Unknown) return Ui::StatusNone;
		return Ui::StatusBad;
	};

	auto drawIcon = [&](int kind, int icx, int icy, Ui::Rgb col) {
		Ui::Set(canvas, col, 255);
		if (kind == 0)
		{
			canvas.SetPosition(Vector2{ icx - 8, icy + 3 });
			canvas.FillBox(Vector2{ 16, 2 });
			canvas.SetPosition(Vector2{ icx - 5, icy - 4 });
			canvas.FillBox(Vector2{ 10, 6 });
		}
		else if (kind == 1)
		{
			canvas.SetPosition(Vector2{ icx - 5, icy - 5 });
			canvas.DrawBox(Vector2{ 10, 10 });
			canvas.DrawLine(Vector2{ icx, icy - 1 }, Vector2{ icx, icy + 4 }, 2);
		}
		else if (kind == 2)
		{
			canvas.DrawLine(Vector2{ icx, icy + 5 }, Vector2{ icx, icy - 5 }, 2);
			canvas.DrawLine(Vector2{ icx, icy - 5 }, Vector2{ icx - 4, icy }, 2);
			canvas.DrawLine(Vector2{ icx, icy - 5 }, Vector2{ icx + 4, icy }, 2);
		}
		else if (kind == 3)
		{
			canvas.DrawLine(Vector2{ icx - 6, icy + 4 }, Vector2{ icx + 6, icy - 4 }, 2);
			canvas.SetPosition(Vector2{ icx + 3, icy - 6 });
			canvas.FillBox(Vector2{ 4, 4 });
		}
		else if (kind == 4)
		{
			canvas.DrawLine(Vector2{ icx - 5, icy + 4 }, Vector2{ icx, icy - 5 }, 2);
			canvas.DrawLine(Vector2{ icx, icy - 5 }, Vector2{ icx + 5, icy + 4 }, 2);
		}
		else
		{
			canvas.SetPosition(Vector2{ icx - 5, icy - 5 });
			canvas.FillBox(Vector2{ 10, 10 });
		}
	};

	for (int i = 0; i < count; ++i)
	{
		const int x = startX + i * (cellW + gap);
		const int y = startY;
		Ui::Rgb st = statusRgb(items[i].grade);

		// Status-colored border (green / yellow / red)
		Ui::DrawPanel(canvas, x, y, cellW, cellH, st, 255);

		Ui::Set(canvas, st, 60);
		canvas.SetPosition(Vector2{ x + 2, y + 2 });
		canvas.FillBox(Vector2{ cellW - 4, cellH - 4 });

		Ui::Set(canvas, st, 255);
		canvas.SetPosition(Vector2{ x + 4, y + 4 });
		canvas.FillBox(Vector2{ 3, cellH - 8 });

		drawIcon(items[i].kind, x + 16, y + cellH / 2 - 1, st);

		Ui::Set(canvas, Ui::TextPrimary, 255);
		canvas.SetPosition(Vector2{ x + 26, y + cellH / 2 - 5 });
		canvas.DrawString(items[i].label, 1, 1, true, false);
	}
}

void SpeedFlipTrainer::RenderPositionMeter(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	int range = 200;
	int relLocation = (-1 * attempt.positionY) + range;
	int totalUnits = range * 2;

	const int panelW = (int)(screenWidth * 0.48f);
	const int panelH = (int)(screenHeight * 0.022f);
	const int panelX = (int)((screenWidth - panelW) / 2);
	const int panelY = (int)(screenHeight * 0.022f);

	Ui::DrawPanel(canvas, panelX, panelY, panelW, panelH, false);
	Ui::DrawInsetWell(canvas, panelX + 4, panelY + 4, panelW - 8, panelH - 8);

	const int wellX = panelX + 4;
	const int wellY = panelY + 4;
	const int wellW = panelW - 8;
	const int wellH = panelH - 8;
	const int unitW = wellW / totalUnits;

	// Ideal band (quiet)
	// Same meaning: green = OK lane, yellow = barely
	Ui::Set(canvas, Ui::StatusWarn, 45);
	canvas.SetPosition(Vector2{ wellX + (range - 160) * unitW, wellY });
	canvas.FillBox(Vector2{ 320 * unitW, wellH });
	Ui::Set(canvas, Ui::StatusGood, 70);
	canvas.SetPosition(Vector2{ wellX + (range - 80) * unitW, wellY });
	canvas.FillBox(Vector2{ 160 * unitW, wellH });

	int tickX = wellX + relLocation * unitW;
	if (tickX < wellX) tickX = wellX;
	if (tickX > wellX + wellW - 3) tickX = wellX + wellW - 3;
	Ui::Rgb posCol = Ui::StatusBad;
	if (relLocation >= range - 80 && relLocation <= range + 80)
		posCol = Ui::StatusGood;
	else if (relLocation >= range - 160 && relLocation <= range + 160)
		posCol = Ui::StatusWarn;
	Ui::Set(canvas, posCol, 255);
	canvas.SetPosition(Vector2{ tickX, wellY });
	canvas.FillBox(Vector2{ 3, wellH });

	Ui::Set(canvas, Ui::TextMuted, 200);
	canvas.SetPosition(Vector2{ panelX + 8, panelY + panelH + 2 });
	canvas.DrawString("Start position", 1, 1, true, false);

	auto speedCvar = _globalCvarManager->getCvar("sv_soccar_gamespeed");
	float speed = speedCvar.getFloatValue();
	if (fabsf(speed - 1.0f) > 0.001f)
	{
		string msg = fmt::format("Game speed {}%", (int)(speed * 100));
		Ui::Set(canvas, Ui::TextPrimary, 255);
		canvas.SetPosition(Vector2{ panelX + panelW - (int)msg.size() * 7, panelY + panelH + 2 });
		canvas.DrawString(msg, 1, 1, true, false);
	}

	// Warnings bottom-left column
	int warnY = (int)(screenHeight * 0.88f);
	int ms = (int)(attempt.ticksNotPressingBoost / 120.0 * 1000.0);
	if (ms != 0)
	{
		Ui::Set(canvas, Ui::StatusWarn, 255);
		canvas.SetPosition(Vector2{ (int)(screenWidth * 0.02f), warnY });
		canvas.FillBox(Vector2{ 4, 12 });
		canvas.SetPosition(Vector2{ (int)(screenWidth * 0.02f) + 8, warnY });
		canvas.DrawString(fmt::format("Boost gap {}ms", ms), 1, 1, true, false);
		warnY += 16;
	}
	ms = (int)(attempt.ticksNotPressingThrottle / 120.0 * 1000.0);
	if (ms != 0)
	{
		Ui::Set(canvas, Ui::StatusWarn, 255);
		canvas.SetPosition(Vector2{ (int)(screenWidth * 0.02f), warnY });
		canvas.FillBox(Vector2{ 4, 12 });
		canvas.SetPosition(Vector2{ (int)(screenWidth * 0.02f) + 8, warnY });
		canvas.DrawString(fmt::format("Throttle gap {}ms", ms), 1, 1, true, false);
		warnY += 16;
	}
	if (attempt.startedInAir)
	{
		Ui::Set(canvas, Ui::StatusBad, 255);
		canvas.SetPosition(Vector2{ (int)(screenWidth * 0.02f), warnY });
		canvas.FillBox(Vector2{ 4, 12 });
		canvas.SetPosition(Vector2{ (int)(screenWidth * 0.02f) + 8, warnY });
		canvas.DrawString("Wait for car to settle on ground", 1, 1, true, false);
		warnY += 16;
	}
	if (attempt.startedNoBoost)
	{
		Ui::Set(canvas, Ui::StatusBad, 255);
		canvas.SetPosition(Vector2{ (int)(screenWidth * 0.02f), warnY });
		canvas.FillBox(Vector2{ 4, 12 });
		canvas.SetPosition(Vector2{ (int)(screenWidth * 0.02f) + 8, warnY });
		canvas.DrawString("Hold boost before countdown starts", 1, 1, true, false);
	}
}

void SpeedFlipTrainer::RenderFirstJumpMeter(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	const int totalUnits = *jumpHigh - *jumpLow;
	const int halfMark = totalUnits / 2;
	const int meterOriginTick = *jumpHigh - halfMark - halfMark;

	const int panelW = (std::max)(12, (int)(screenWidth * 0.018f));
	const int panelH = (int)(screenHeight * 0.48f);
	const int panelX = (int)(screenWidth * 0.965f);
	const int panelY = (int)(screenHeight * 0.22f);

	Ui::DrawPanel(canvas, panelX, panelY, panelW, panelH, false);
	Ui::Set(canvas, Ui::TextSecondary, 230);
	{
		const char* title = "JUMP";
		const int titleW = (int)strlen(title) * 6;
		canvas.SetPosition(Vector2{ panelX + (panelW - titleW) / 2, panelY - 14 });
		canvas.DrawString(title, 1, 1, true, false);
	}

	const int troughX = panelX + 2;
	const int troughY = panelY + 2;
	const int troughW = panelW - 4;
	const int troughH = panelH - 4;
	Ui::DrawInsetWell(canvas, troughX, troughY, troughW, troughH);

	const int warnLo = halfMark - 15;
	const int goodLo = halfMark - 10;
	const int goodHi = halfMark + 5;
	const int warnHi = halfMark + 10;
	auto unitsToY = [&](int u) {
		float t = (float)u / (float)totalUnits;
		return troughY + troughH - (int)(t * troughH);
	};
	auto fillBand = [&](int lo, int hi, Ui::Rgb col, unsigned char a) {
		int yHi = unitsToY(hi);
		int yLo = unitsToY(lo);
		Ui::Set(canvas, col, a);
		canvas.SetPosition(Vector2{ troughX, yHi });
		canvas.FillBox(Vector2{ troughW, yLo - yHi });
	};
	fillBand(warnLo, goodLo, Ui::StatusWarn, 70);
	fillBand(goodLo, goodHi, Ui::StatusGood, 90);
	fillBand(goodHi, warnHi, Ui::StatusWarn, 70);

	auto gradeForUnits = [&](int units) -> Ui::Rgb {
		if (units >= goodLo && units <= goodHi) return Ui::StatusGood;
		if (units >= warnLo && units <= warnHi) return Ui::StatusWarn;
		return Ui::StatusBad;
	};

	int displayTick = -1;
	if (attempt.jumped)
		displayTick = attempt.jumpTick;
	else if (startingPhysicsFrame >= 0 && !attempt.hit && !attempt.exploded)
		displayTick = gameWrapper->GetEngine().GetPhysicsFrame() - startingPhysicsFrame;

	if (displayTick >= 0)
	{
		int units = displayTick - meterOriginTick;
		if (units < 0) units = 0;
		if (units > totalUnits) units = totalUnits;

		Ui::Rgb fill = gradeForUnits(units);
		int fillH = (int)((float)units / (float)totalUnits * troughH);
		Ui::Set(canvas, fill, 255);
		canvas.SetPosition(Vector2{ troughX, troughY + troughH - fillH });
		canvas.FillBox(Vector2{ troughW, fillH });

		string msg = to_string((int)(displayTick * 1000.0 / 120.0)) + "ms";
		Ui::Set(canvas, Ui::TextPrimary, 255);
		canvas.SetPosition(Vector2{ panelX - 6, panelY + panelH + 4 });
		canvas.DrawString(msg, 1, 1, true, false);
	}
}

void SpeedFlipTrainer::RenderFlipCancelMeter(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	const int totalUnits = *flipCancelThreshold;
	const int panelW = (std::max)(12, (int)(screenWidth * 0.018f));
	const int panelH = (int)(screenHeight * 0.48f);
	const int panelX = (int)(screenWidth * 0.94f);
	const int panelY = (int)(screenHeight * 0.22f);

	Ui::DrawPanel(canvas, panelX, panelY, panelW, panelH, false);
	Ui::Set(canvas, Ui::TextSecondary, 230);
	{
		const char* title = "CANCEL";
		const int titleW = (int)strlen(title) * 6;
		canvas.SetPosition(Vector2{ panelX + (panelW - titleW) / 2, panelY - 14 });
		canvas.DrawString(title, 1, 1, true, false);
	}

	const int troughX = panelX + 2;
	const int troughY = panelY + 2;
	const int troughW = panelW - 4;
	const int troughH = panelH - 4;
	Ui::DrawInsetWell(canvas, troughX, troughY, troughW, troughH);

	int goodUnits = (int)(totalUnits * 0.6f);
	int warnUnits = (int)(totalUnits * 0.9f);
	auto yFromUnits = [&](int u) {
		return troughY + troughH - (int)((float)u / (float)totalUnits * troughH);
	};
	int yGood = yFromUnits(goodUnits);
	int yWarn = yFromUnits(warnUnits);
	Ui::Set(canvas, Ui::StatusGood, 90);
	canvas.SetPosition(Vector2{ troughX, yGood });
	canvas.FillBox(Vector2{ troughW, troughY + troughH - yGood });
	Ui::Set(canvas, Ui::StatusWarn, 70);
	canvas.SetPosition(Vector2{ troughX, yWarn });
	canvas.FillBox(Vector2{ troughW, yGood - yWarn });

	if (attempt.flipCanceled)
	{
		int ticks = attempt.flipCancelTick - attempt.dodgedTick;
		if (ticks < 0) ticks = 0;
		if (ticks > totalUnits) ticks = totalUnits;

		CheckGrade g = GradeCancelTiming(attempt);
		Ui::Rgb fill = g == CheckGrade::Good ? Ui::StatusGood : (g == CheckGrade::Ok ? Ui::StatusWarn : Ui::StatusBad);
		int fillH = (int)((float)ticks / (float)totalUnits * troughH);
		Ui::Set(canvas, fill, 255);
		canvas.SetPosition(Vector2{ troughX, troughY + troughH - fillH });
		canvas.FillBox(Vector2{ troughW, fillH });

		string msg = to_string((int)(ticks * 1000.0 / 120.0)) + "ms";
		Ui::Set(canvas, Ui::TextPrimary, 255);
		canvas.SetPosition(Vector2{ panelX - 6, panelY + panelH + 4 });
		canvas.DrawString(msg, 1, 1, true, false);
	}
}

void SpeedFlipTrainer::RenderAngleMeter(CanvasWrapper canvas, float screenWidth, float screenHeight)
{
	const int totalUnits = 180;
	const int greenRange = 8;
	const int yellowRange = 15;
	const int lTarget = *optimalLeftAngle + 90;
	const int rTarget = *optimalRightAngle + 90;

	const int panelW = (int)(screenWidth * 0.40f);
	const int panelH = (int)(screenHeight * 0.034f);
	const int panelX = (int)((screenWidth - panelW) / 2);
	const int panelY = (int)(screenHeight * 0.91f);

	Ui::DrawPanel(canvas, panelX, panelY, panelW, panelH, false);
	const int troughX = panelX + 4;
	const int troughY = panelY + 4;
	const int troughW = panelW - 8;
	const int troughH = panelH - 8;
	Ui::DrawInsetWell(canvas, troughX, troughY, troughW, troughH);

	auto unitToX = [&](int u) {
		return troughX + (int)((float)u / (float)totalUnits * troughW);
	};

	auto drawBand = [&](int lo, int hi, Ui::Rgb col, unsigned char alpha) {
		if (hi <= lo) return;
		Ui::Set(canvas, col, alpha);
		canvas.SetPosition(Vector2{ unitToX(lo), troughY });
		canvas.FillBox(Vector2{ unitToX(hi) - unitToX(lo), troughH });
	};

	// Yellow margins, green ideal (both sides)
	drawBand(lTarget - yellowRange, lTarget - greenRange, Ui::StatusWarn, 50);
	drawBand(lTarget - greenRange, lTarget + greenRange, Ui::StatusGood, 70);
	drawBand(lTarget + greenRange, lTarget + yellowRange, Ui::StatusWarn, 50);
	drawBand(rTarget - yellowRange, rTarget - greenRange, Ui::StatusWarn, 50);
	drawBand(rTarget - greenRange, rTarget + greenRange, Ui::StatusGood, 70);
	drawBand(rTarget + greenRange, rTarget + yellowRange, Ui::StatusWarn, 50);

	Ui::Set(canvas, Ui::Grid, 140);
	canvas.DrawLine(Vector2{ unitToX(90), troughY }, Vector2{ unitToX(90), troughY + troughH }, 1);

	if (attempt.dodged)
	{
		int angle = attempt.dodgeAngle;
		if (angle > 90) angle = 90;
		if (angle < -90) angle = -90;
		int adj = 90 + angle;
		CheckGrade g = GradeDodgeAngle(attempt);
		Ui::Rgb tick = attempt.dodged
			? (g == CheckGrade::Good ? Ui::StatusGood : (g == CheckGrade::Ok ? Ui::StatusWarn : Ui::StatusBad))
			: Ui::TextPrimary;
		Ui::Set(canvas, tick, 255);
		int tx = unitToX(adj);
		canvas.SetPosition(Vector2{ tx - 1, troughY });
		canvas.FillBox(Vector2{ 3, troughH });

		string deg = to_string(attempt.dodgeAngle) + " deg";
		Ui::Set(canvas, Ui::TextPrimary, 255);
		canvas.SetPosition(Vector2{ panelX, panelY - 16 });
		canvas.DrawString(deg, 1, 1, true, false);
	}

	if (attempt.hit && attempt.ticksToBall > 0)
	{
		string msg = fmt::format("Ball {:.3f}s", attempt.ticksToBall / 120.0);
		Ui::Set(canvas, Ui::RLBlue, 255);
		canvas.SetPosition(Vector2{ panelX + panelW / 2 - 40, panelY - 16 });
		canvas.DrawString(msg, 1, 1, true, false);
	}

	// Blocking warnings only
	int warnY = panelY + panelH + 6;
	if (attempt.startedInAir)
	{
		Ui::Set(canvas, Ui::StatusBad, 255);
		canvas.SetPosition(Vector2{ panelX, warnY });
		canvas.DrawString("Wait for car to settle on ground", 1, 1, true, false);
		warnY += 14;
	}
	if (attempt.startedNoBoost)
	{
		Ui::Set(canvas, Ui::StatusBad, 255);
		canvas.SetPosition(Vector2{ panelX, warnY });
		canvas.DrawString("Hold boost before countdown starts", 1, 1, true, false);
	}
}

void SpeedFlipTrainer::PlayAttempt(Attempt* a, shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci)
{
	if (a->inputs.size() <= 0)
		return;

	int currentPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
	int tick = currentPhysicsFrame - startingPhysicsFrame;

	a->Play(ci, tick);

	gameWrapper->OverrideParams(ci, sizeof(ControllerInput));
}

void SpeedFlipTrainer::PlayBot(shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci)
{
	int currentPhysicsFrame = gameWrapper->GetEngine().GetPhysicsFrame();
	int tick = currentPhysicsFrame - startingPhysicsFrame;

	bot.Play(ci, tick);

	gameWrapper->OverrideParams(ci, sizeof(ControllerInput));
}