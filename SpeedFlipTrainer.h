#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"

#include "version.h"
constexpr auto plugin_version = stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH) "." stringify(VERSION_BUILD);

using namespace std;

class SpeedFlipTrainer: public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginSettingsWindow
{
public: 

private:

	// Whether plugin is enabled
	shared_ptr<bool> enabled = make_shared<bool>(true);

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

	// Whether plugin is loaded
	bool loaded = false;

	// Whether countdown has started
	bool timeStarted = false;

	// Assists in determining when time started counting down
	float initialTime = 0;

	int startingPhysicsFrame = 0 * -1;
	int ticksBeforeTimeExpired = (int)(initialTime * 120);

	// Variables to measure the first jump
	int jumpTick = 0; // ticks before first jump occured
	bool jumped = false;

	// Variables to measure the flip cancel
	int flipCancelTicks = 0;
	bool flipCanceled = false;

	// Variables to measure the dodge angle
	int dodgeAngle = 0;
	int dodgedTick = 0;
	bool dodged = false;

	// Variable to keep track of Y position
	float positionY = -1.1;
	float traveledY = 0;

	// Number of ticks taken to reach the ball
	int timeToBallTicks = 0;

	// Boolean values to keep track of what happened on reset
	bool hit = false;
	bool exploded = false;

	// Consecutive hits and misses
	int consecutiveHits = 0;
	int consecutiveMiss = 0;

	//Boilerplate
	virtual void onLoad();
	virtual void onUnload();

	// Inherited via PluginSettingsWindow
	void RenderSettings() override;
	std::string GetPluginName() override;
	void SetImGuiContext(uintptr_t ctx) override;

	// Hooks the necessary game functions to operate the plugin
	void Hook();

	// Determines whether Musty's training pack is loaded
	bool IsMustysPack(TrainingEditorWrapper tw);

	// Measures the speedflip being performed
	void Measure(CarWrapper car, shared_ptr<GameWrapper> gameWrapper);

	// Programmed bot to perform the speed flip
	void Perform(shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci);

	// Render functions to render various meters and measured values on screen
	void Render(CanvasWrapper canvas);
	void RenderAngleMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderFlipCancelMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderFirstJumpMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
	void RenderPositionMeter(CanvasWrapper canvas, float screenWidth, float screenHeight);
};

