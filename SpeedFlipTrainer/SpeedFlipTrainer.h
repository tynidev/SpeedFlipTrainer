#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"

#include "version.h"
#include "Attempt.h"
constexpr auto plugin_version = stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH) "." stringify(VERSION_BUILD);

using namespace std;

class SpeedFlipTrainer: public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginSettingsWindow, public BakkesMod::Plugin::PluginWindow
{
public: 

private:

	// Whether plugin is enabled
	shared_ptr<bool> enabled = make_shared<bool>(true);

	// Whether to show various meters
	shared_ptr<bool> showAngleMeter = make_shared<bool>(true);
	shared_ptr<bool> showPositionMeter = make_shared<bool>(true);
	shared_ptr<bool> showFlipMeter = make_shared<bool>(true);
	shared_ptr<bool> showJumpMeter = make_shared<bool>(true);

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

	// Use bot to perform flip
	shared_ptr<bool> perform = make_shared<bool>(false);

	// Replay stored flip
	shared_ptr<bool> replay = make_shared<bool>(false);

	// Whether plugin is loaded
	bool loaded = false;

	// Whether countdown has started
	bool timeStarted = false;

	// Assists in determining when time started counting down
	float initialTime = 0;

	int startingPhysicsFrame = 0 * -1;
	int ticksBeforeTimeExpired = (int)(initialTime * 120);

	filesystem::path dataDir;

	Attempt attempt;
	Attempt previousAttempt;

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
	void Perform(shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci);

	// Replays saved speedflip attempt
	void Replay(Attempt* a, shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci);

	// Render functions to render various meters and measured values on screen
	void RenderMeters(CanvasWrapper canvas);
	void RenderAngleMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderFlipCancelMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderFirstJumpMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderPositionMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);

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
};

