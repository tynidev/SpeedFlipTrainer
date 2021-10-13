#include "pch.h"
#include "SpeedFlipTrainer.h"
#include "ImGuiFileDialog.h"
#include "BotAttempt.h"

// Plugin Settings Window code here
std::string SpeedFlipTrainer::GetPluginName() {
	return "SpeedFlipTrainer";
}

// Render the plugin settings here
// This will show up in bakkesmod when the plugin is loaded at
//  f2 -> plugins -> SpeedFlipTrainer
void SpeedFlipTrainer::RenderSettings() {
	ImGui::TextUnformatted("A plugin to help give training metrics when learning how to do a speedflip in Musty's training pack: A503-264C-A7EB-D282");

	CVarWrapper enableCvar = cvarManager->getCvar("sf_enabled");
	if (!enableCvar) return;

	bool enabled = enableCvar.getBoolValue();

	if (ImGui::Checkbox("Enable plugin", &enabled))
		enableCvar.setValue(enabled);
	if (ImGui::IsItemHovered()) 
		ImGui::SetTooltip("Enable/Disable Speeflip trainer plugin");

	// ------------------------ ANGLE ----------------------------------
	ImGui::Separator();
	{
		CVarWrapper cvar = cvarManager->getCvar("sf_show_angle");
		if (!cvar) return;

		bool value = cvar.getBoolValue();

		if (ImGui::Checkbox("Show dodge angle", &value))
			cvar.setValue(value);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Show meter for the dodge angle.");
	}

	CVarWrapper leftAngleCvar = cvarManager->getCvar("sf_left_angle");
	if (!leftAngleCvar) return;

	int leftAngle = leftAngleCvar.getIntValue();
	if (ImGui::SliderInt("Optimal left angle", &leftAngle, -70, -15))
		leftAngleCvar.setValue(leftAngle);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The optimal angle at which to dodge left.");

	CVarWrapper rightAngleCvar = cvarManager->getCvar("sf_right_angle");
	if (!rightAngleCvar) return;

	int rightAngle = rightAngleCvar.getIntValue();
	if (ImGui::SliderInt("Optimal right angle", &rightAngle, 15, 70))
		rightAngleCvar.setValue(rightAngle);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The optimal angle at which to dodge right.");
	// ------------------------ FLIP CANCEL ----------------------------------
	ImGui::Separator();
	{
		CVarWrapper cvar = cvarManager->getCvar("sf_show_flip");
		if (!cvar) return;

		bool value = cvar.getBoolValue();

		if (ImGui::Checkbox("Show time to flip cancel", &value))
			cvar.setValue(value);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Show meter for the time to flip cancel.");
	}

	CVarWrapper cancelCvar = cvarManager->getCvar("sf_cancel_threshold");
	if (!cancelCvar) return;

	int cancel = cancelCvar.getIntValue();
	if (ImGui::SliderInt("Flip cancel threshold", &cancel, 1, 15))
		cancelCvar.setValue(cancel);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The number of physics tick to perform flip cancel under.");


	// ------------------------ FIRST JUMP ----------------------------------
	ImGui::Separator();
	{
		CVarWrapper cvar = cvarManager->getCvar("sf_show_jump");
		if (!cvar) return;

		bool value = cvar.getBoolValue();

		if (ImGui::Checkbox("Show time to first jump", &value))
			cvar.setValue(value);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Show meter for time to first jump");
	}

	//CVarWrapper jumpLowCVar = cvarManager->getCvar("sf_jump_low");
	//if (!jumpLowCVar) return;
	//CVarWrapper jumpHighCVar = cvarManager->getCvar("sf_jump_high");
	//if (!jumpHighCVar) return;

	//int jumpPos[2];
	//jumpPos[0] = jumpLowCVar.getIntValue();
	//jumpPos[1] = jumpHighCVar.getIntValue();
	//if (ImGui::SliderInt2("First jump time", jumpPos, 10, 120))
	//{
	//	if (jumpPos[0] < jumpPos[1])
	//	{
	//		jumpLowCVar.setValue(jumpPos[0]);
	//		jumpHighCVar.setValue(jumpPos[1]);
	//	}
	//	else
	//	{
	//		jumpLowCVar.setValue(jumpPos[1]);
	//		jumpHighCVar.setValue(jumpPos[0]);
	//	}
	//}
	//if (ImGui::IsItemHovered())
	//	ImGui::SetTooltip("The low and high thresholds for the first jump of the flip.");

	// ------------------------ POSITION ----------------------------------
	ImGui::Separator();
	{
		CVarWrapper cvar = cvarManager->getCvar("sf_show_position");
		if (!cvar) return;

		bool value = cvar.getBoolValue();

		if (ImGui::Checkbox("Show horizontal position", &value))
			cvar.setValue(value);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Show meter for the horizontal position.");
	}

	// ------------------------ SPEED SETTINGS ----------------------------------
	ImGui::Separator();
	CVarWrapper changeSpeedCvar = cvarManager->getCvar("sf_change_speed");
	if (!changeSpeedCvar) return;

	bool changeSpeed = changeSpeedCvar.getBoolValue(); 

	if (ImGui::Checkbox("Change game speed on hit/miss", &changeSpeed))
		changeSpeedCvar.setValue(changeSpeed);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("If checked this will alter the game speed on consecutive hits or misses.");

	CVarWrapper remSpeedCvar = cvarManager->getCvar("sf_remember_speed");
	if (!remSpeedCvar) return;

	bool remSpeed = remSpeedCvar.getBoolValue();

	if (ImGui::Checkbox("Remember last game speed:", &remSpeed))
		remSpeedCvar.setValue(remSpeed);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Remember last game speed achieved on next load of training session.");

	CVarWrapper hitsCvar = cvarManager->getCvar("sf_num_hits");
	if (!hitsCvar) return;

	int hit = hitsCvar.getIntValue();
	if (ImGui::SliderInt("Number hits/misses before speed change", &hit, 1, 30))
		hitsCvar.setValue(hit);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Number of consecutive hits/misses before speed change.");

	CVarWrapper speedIncCvar = cvarManager->getCvar("sf_speed_increment");
	if (!speedIncCvar) return;

	float speedInc = speedIncCvar.getFloatValue();
	if (ImGui::SliderFloat("Value of speed increment", &speedInc, 0.001, 0.999))
		speedIncCvar.setValue(speedInc);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The value to add or subtract from game speed.");
}


// Do ImGui rendering here
void SpeedFlipTrainer::Render()
{
	if (!ImGui::Begin(menuTitle_.c_str(), &isWindowOpen_, ImGuiWindowFlags_None))
	{
		// Early out if the window is collapsed, as an optimization.
		ImGui::End();
		return;
	}

	if (ImGui::Button("Enable manual mode"))
	{
		mode = SpeedFlipTrainerMode::Manual;
		LOG("MODE = Manual");
	}
	ImGui::SameLine();
	if (ImGui::Button("Save last attempt"))
	{
		auto path = attempt.GetFilename(dataDir);
		attempt.WriteInputsToFile(path);
		LOG("Saved attempt to: {0}", path.string());
	}

	if (ImGui::Button("Replay last attempt"))
	{
		mode = SpeedFlipTrainerMode::Replay;
		LOG("MODE = Replay");
		replayAttempt = attempt;
	}
	ImGui::SameLine();
	if (ImGui::Button("Load replay attempt"))
	{
		attemptFileDialog.open = true;
	}
	if (attemptFileDialog.open && attemptFileDialog.ShowFileDialog(ImGui::FileDialogType::SelectFile))
	{
		try 
		{
			auto a = Attempt();
			a.ReadInputsFromFile(attemptFileDialog.selected);
			mode = SpeedFlipTrainerMode::Replay;
			LOG("MODE = Replay");
			replayAttempt = a;
			LOG("Loaded attempt from file: {0}", attemptFileDialog.selected.string());
		}
		catch (...)
		{
			LOG("Failed to read attempt from file: {0}", attemptFileDialog.selected.string());
		}
	}

	if (ImGui::Button("Load -26 Bot"))
	{
		bot.Become26Bot();
		mode = SpeedFlipTrainerMode::Bot;
		LOG("MODE = Bot");
	}
	ImGui::SameLine();
	if (ImGui::Button("Load -45 Bot"))
	{
		bot.Become45Bot();
		mode = SpeedFlipTrainerMode::Bot;
		LOG("MODE = Bot");
	}
	ImGui::SameLine();
	if (ImGui::Button("Load Bot"))
	{
		botFileDialog.open = true;
	}
	if (botFileDialog.open && botFileDialog.ShowFileDialog(ImGui::FileDialogType::SelectFile))
	{
		try
		{
			bot.ReadInputsFromFile(botFileDialog.selected);
			LOG("Loaded bot from file: {0}", botFileDialog.selected.string());
			mode = SpeedFlipTrainerMode::Bot;
			LOG("MODE = Bot");
		}
		catch (...)
		{
			LOG("Failed to read bot from file: {0}", botFileDialog.selected.string());
		}
	}

	ImGui::End();

	if (!isWindowOpen_)
	{
		cvarManager->executeCommand("togglemenu " + GetMenuName());
	}
}

// Name of the menu that is used to toggle the window.
std::string SpeedFlipTrainer::GetMenuName()
{
	return "SpeedFlipTrainer";
}

// Title to give the menu
std::string SpeedFlipTrainer::GetMenuTitle()
{
	return menuTitle_;
}

// Don't call this yourself, BM will call this function with a pointer to the current ImGui context
void SpeedFlipTrainer::SetImGuiContext(uintptr_t ctx)
{
	ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

// Should events such as mouse clicks/key inputs be blocked so they won't reach the game
bool SpeedFlipTrainer::ShouldBlockInput()
{
	return ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
}

// Return true if window should be interactive
bool SpeedFlipTrainer::IsActiveOverlay()
{
	return true;
}

// Called when window is opened
void SpeedFlipTrainer::OnOpen()
{
	isWindowOpen_ = true;
}

// Called when window is closed
void SpeedFlipTrainer::OnClose()
{
	isWindowOpen_ = false;
}
