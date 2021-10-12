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
};

class BotAttempt26 : public BotAttempt
{
public:
	double dodgeAngle = -26;
	float initialSteer = 0.03;
	int beforeJump = 59;
	int jumpDuration = 11;
	int beforeCancelAdjust = 59;
	float adjustAmmount = 0.75;
	int adjustDuration = 16;
	int cancelSpeed = 4;
	int airRollDuration = 40;
};

class BotAttempt45 : public BotAttempt
{
public:
	double dodgeAngle = -45;
	float initialSteer = 0.1;
	int beforeJump = 59;
	int jumpDuration = 11;
	int beforeCancelAdjust = 59;
	float adjustAmmount = 0.7;
	int adjustDuration = 16;
	int cancelSpeed = 4;
	int airRollDuration = 40;
}; 
