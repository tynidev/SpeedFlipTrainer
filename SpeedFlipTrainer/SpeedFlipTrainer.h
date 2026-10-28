#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"
#include "ImGuiFileDialog.h"
#include "BotAttempt.h"

#include "version.h"
#include "Attempt.h"
#include "UiTheme.h"
#include <map>
#include <vector>
constexpr auto plugin_version = stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH) "." stringify(VERSION_BUILD);

using namespace std;

enum class SpeedFlipTrainerMode
{
	Replay,
	Bot,
	Manual
};

class SpeedFlipTrainer: public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginSettingsWindow, public BakkesMod::Plugin::PluginWindow
{
public: 

private:

	SpeedFlipTrainerMode mode = SpeedFlipTrainerMode::Manual;

	// Whether plugin is enabled
	shared_ptr<bool> enabled = make_shared<bool>(true);

	// Whether to show various meters
	shared_ptr<bool> showAngleMeter = make_shared<bool>(true);
	shared_ptr<bool> showPositionMeter = make_shared<bool>(true);
	shared_ptr<bool> showFlipMeter = make_shared<bool>(true);
	shared_ptr<bool> showJumpMeter = make_shared<bool>(true);
	shared_ptr<bool> showInputTimeline = make_shared<bool>(false);
	shared_ptr<bool> showStickPaths = make_shared<bool>(true);
	shared_ptr<bool> showAttemptScorecard = make_shared<bool>(true);
	shared_ptr<bool> showSuccessRatio = make_shared<bool>(true);

	// Whether the speed should be changed on consecutive hits and misses
	shared_ptr<bool> changeSpeed = make_shared<bool>(false);

	// What the last speed was
	shared_ptr<float> speed = make_shared<float>(1.0);

	// Whether we should remember the game speed between plugin loads
	shared_ptr<bool> rememberSpeed = make_shared<bool>(true);

	// Number of hits/misses before the game speed is changed
	shared_ptr<int> numHitsChangedSpeed = make_shared<int>(3);

	// Amount to change the game speed by
	shared_ptr<float> speedIncrement = make_shared<float>(0.1);
	
	// Optimal left angle for dodge
	shared_ptr<int> optimalLeftAngle = make_shared<int>(-30);

	// Optimal right angle for dodge
	shared_ptr<int> optimalRightAngle = make_shared<int>(30);

	// Physics ticks the flip canceled should be performed under
	shared_ptr<int> flipCancelThreshold = make_shared<int>(13);

	// Physics ticks range during which first jump should be performed
	shared_ptr<int> jumpLow = make_shared<int>(40);
	shared_ptr<int> jumpHigh = make_shared<int>(90);

	// Save attempts to file?
	shared_ptr<bool> saveToFile = make_shared<bool>(false);

	// Whether plugin is loaded
	bool loaded = false;

	// Assists in determining when time started counting down
	float initialTime = 0;

	int startingPhysicsFrame = -1;
	int ticksBeforeTimeExpired = (int)(initialTime * 120);

	Attempt attempt;
	Attempt replayAttempt;
	// Frozen copy of the last finished attempt for the left input timeline
	Attempt inputTimelineReview;

	BotAttempt bot;

	// Session stick patterns (cleared when leaving training)
	struct StickTrailSnapshot
	{
		std::map<int, ControllerInput> inputs;
		int firstJumpTick = -1;
		int secondJumpTick = -1;
		int cancelTick = -1;
		int dodgedTick = -1;
		int dodgeAngle = 0;
		bool dodged = false;
	};
	std::vector<StickTrailSnapshot> successStickTrails;
	std::vector<StickTrailSnapshot> greenFailStickTrails;
	bool sessionAttemptFinalized = false;

	// Training session success tracking (persisted across game launches)
	struct SessionStats
	{
		std::string startedAt; // "YYYY-MM-DD HH:MM"
		int validAttempts = 0; // jumped + dodged
		int successes = 0;     // hit ball
	};
	SessionStats currentSession;
	std::vector<SessionStats> sessionHistory;
	bool trainingSessionActive = false;

	// Consecutive hits and misses
	int consecutiveHits = 0;
	int consecutiveMiss = 0;

	//Boilerplate
	virtual void onLoad();
	virtual void onUnload();

	// Inherited via PluginSettingsWindow
	void RenderSettings() override;
	std::string GetPluginName() override;

	// Hooks the necessary game functions to operate the plugin
	void Hook();

	// Determines whether Musty's training pack is loaded
	bool IsMustysPack(TrainingEditorWrapper tw);

	// Measures the speedflip being performed
	void Measure(CarWrapper car, shared_ptr<GameWrapper> gameWrapper);

	// Programmed bot to perform the speed flip
	void PlayBot(shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci);

	// Replays saved speedflip attempt
	void PlayAttempt(Attempt* a, shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci);

	// Render functions to render various meters and measured values on screen
	void RenderMeters(CanvasWrapper canvas);
	void RenderAngleMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderFlipCancelMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderFirstJumpMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderPositionMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderInputTimeline(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderStickPathBoxes(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void DrawStickBox(
		CanvasWrapper canvas,
		int left, int top, int size, int headerH, int pad,
		const char* title,
		bool live,
		const std::vector<StickTrailSnapshot>* sessionTrails,
		Ui::Rgb sessionColor,
		unsigned char sessionAlpha,
		const Attempt* current,
		bool drawZones,
		int markerRadius) const;
	void RenderAttemptScorecard(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderSuccessRatio(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void CaptureInputTimelineReview();
	void ClearSessionStickPatterns();
	bool IsMechanicsAllGreen(const Attempt& a) const;

	void StartTrainingSession();
	void EndTrainingSession();
	void FinalizeAttemptForSession(const Attempt& a);
	void LoadSessionHistory();
	void SaveSessionHistory() const;
	void SaveCurrentSessionCheckpoint() const;
	void PromoteCurrentSessionCheckpoint();
	std::filesystem::path SessionHistoryPath() const;
	std::filesystem::path SessionCurrentPath() const;
	static std::string FormatSessionTimestamp();
	static float SessionRatio(const SessionStats& s);
	const Attempt* GetInputTimelineSource() const;
	void AnalyzeJumpEdges(const Attempt& src, int& firstJumpTick, int& secondJumpTick, int& cancelTick) const;

	enum class CheckGrade { Good, Ok, Bad, Unknown };
	CheckGrade GradeJumpTiming(const Attempt& a) const;
	CheckGrade GradeDodgeAngle(const Attempt& a) const;
	CheckGrade GradeCancelTiming(const Attempt& a) const;
	CheckGrade GradeBoostThrottle(const Attempt& a) const;

	// Inherited via PluginWindow
	bool isWindowOpen_ = false;
	bool isMinimized_ = false;
	std::string menuTitle_ = "Speedflip Trainer";

	virtual void Render() override;
	virtual std::string GetMenuName() override;
	virtual std::string GetMenuTitle() override;
	virtual void SetImGuiContext(uintptr_t ctx) override;
	virtual bool ShouldBlockInput() override;
	virtual bool IsActiveOverlay() override;
	virtual void OnOpen() override;
	virtual void OnClose() override;

	filesystem::path dataDir;
	ImGui::FileDialog attemptFileDialog;
	ImGui::FileDialog botFileDialog;
};

