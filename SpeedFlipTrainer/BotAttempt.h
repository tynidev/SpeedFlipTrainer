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

	void Get26Bot()
	{
		dodgeAngle = -26;
		initialSteer = 0.03;
		beforeJump = 59;
		jumpDuration = 11;
		beforeCancelAdjust = 59;
		adjustAmmount = 0.75;
		adjustDuration = 16;
		cancelSpeed = 5;
		airRollDuration = 40;
	}

	void Get45Bot()
	{
		dodgeAngle = -45;
		initialSteer = 0.1;
		beforeJump = 59;
		jumpDuration = 11;
		beforeCancelAdjust = 59;
		adjustAmmount = 0.7;
		adjustDuration = 16;
		cancelSpeed = 5;
		airRollDuration = 40;
	}
};