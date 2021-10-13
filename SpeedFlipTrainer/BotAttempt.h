#pragma once
class BotAttempt
{
public:
	double dodgeAngle = 0;
	float initialSteer = 0;
	int beforeJump = 0;
	int jumpDuration = 0;
	int beforeCancelAdjust = 0;
	float adjustAmmount = 0;
	int adjustDuration = 0;
	int cancelSpeed = 0;
	int airRollDuration = 0;

	void Become26Bot();
	void Become45Bot();
	void ReadInputsFromFile(std::filesystem::path filepath);
	void Play(ControllerInput* ci, int tick);
};