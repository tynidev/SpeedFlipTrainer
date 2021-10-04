#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"

#include "version.h"
constexpr auto plugin_version = stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH) "." stringify(VERSION_BUILD);

using namespace std;

class SpeedFlipTrainer: public BakkesMod::Plugin::BakkesModPlugin, public BakkesMod::Plugin::PluginSettingsWindow/*, public BakkesMod::Plugin::PluginWindow*/
{
public: 

private:
	shared_ptr<bool> changeSpeed = make_shared<bool>(true);
	shared_ptr<float> speed = make_shared<float>(1.0);
	shared_ptr<bool> rememberSpeed = make_shared<bool>(true);
	shared_ptr<int> numHitsChangedSpeed = make_shared<int>(3);
	shared_ptr<float> speedIncrement = make_shared<float>(0.1);
	shared_ptr<bool> enabled = make_shared<bool>(true);
	shared_ptr<int> optimalLeftAngle = make_shared<int>(-30);
	shared_ptr<int> optimalRightAngle = make_shared<int>(30);
	shared_ptr<int> flipCancelThreshold = make_shared<int>(10);
	shared_ptr<int> jumpLow = make_shared<int>(40);
	shared_ptr<int> jumpHigh = make_shared<int>(90);

	float initialTime = 0;
	bool loaded = false;

	int startingPhysicsFrame = 0 * -1;
	int ticksBeforeTimeExpired = (int)(initialTime * 120);

	int firstJumpTick = 0;
	int secondJumpTick = 0;

	bool firstJump = false;
	bool secondJump = false;
	bool filpCancel = false;
	bool dodged = false;
	bool supersonic = false;

	int consecutiveHits = 0;
	bool hit = false;
	bool exploded = false;
	int consecutiveMiss = 0;

	int dodgeAngle = 0;
	int flipCancelTicks = 0;
	int timeToBallTicks = 0;

	//Boilerplate
	virtual void onLoad();
	virtual void onUnload();

	// Inherited via PluginSettingsWindow
	void RenderSettings() override;
	std::string GetPluginName() override;
	void SetImGuiContext(uintptr_t ctx) override;

	void Hook();
	bool IsMustysPack(TrainingEditorWrapper tw);
	void Measure(CarWrapper car, shared_ptr<GameWrapper> gameWrapper);
	void Perform(shared_ptr<GameWrapper> gameWrapper, ControllerInput* ci);
	void Render(CanvasWrapper canvas);
	void RenderAngleMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight);
	void RenderFlipCancelMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight);
	void RenderFirstJumpMeter(CanvasWrapper canvas, float screenWidth, float screenHeight, float relativeWidth, float relativeHeight);

	// Inherited via PluginWindow
	/*

	bool isWindowOpen_ = false;
	bool isMinimized_ = false;
	std::string menuTitle_ = "SpeedFlipTrainer";

	virtual void Render() override;
	virtual std::string GetMenuName() override;
	virtual std::string GetMenuTitle() override;
	virtual void SetImGuiContext(uintptr_t ctx) override;
	virtual bool ShouldBlockInput() override;
	virtual bool IsActiveOverlay() override;
	virtual void OnOpen() override;
	virtual void OnClose() override;
	
	*/
};

