#include "pch.h"	
#include "SpeedFlipTrainer.h"

#define M_PI 3.14159265358979323846

BAKKESMOD_PLUGIN(SpeedFlipTrainer, "Plugin to help while training to perfect speedflip", plugin_version, PLUGINTYPE_CUSTOM_TRAINING)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

float lastAngle = 0.0;

void SpeedFlipTrainer::onLoad()
{
	_globalCvarManager = cvarManager;
	LOG("Speedflip Plugin loaded!");

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.SetVehicleInput",
		[this](CarWrapper caller, void* params, std::string eventname) {

		DodgeComponentWrapper dodge = caller.GetDodgeComponent();
		if (dodge.IsNull())
			return;

		Vector dd = dodge.GetDodgeDirection();
		if (dd.X == 0 && dd.Y == 0)
			return;

		float rads = atan2f(dd.Y, dd.X);
		float deg = rads * (180 / M_PI);

		if (lastAngle != deg)
		{
			lastAngle = deg;

			float degF = deg;
			if (degF < 0)
			{
				degF = 360 + degF;
			}

			int hour = (int)(degF * (12.0/360.0));
			int min = ((int)degF % (360/12)) * (60.0/30);

			LOG("Dodge Deg Angle: {0:#03d}", (int)deg);
			LOG("Dodge Clock Angle: {0:#02d}:{1:#02d}", hour, min);
		}
	});
	
	//cvarManager->registerNotifier("my_aweseome_notifier", [&](std::vector<std::string> args) {
	//	cvarManager->log("Hello notifier!");
	//}, "", 0);

	//auto cvar = cvarManager->registerCvar("template_cvar", "hello-cvar", "just a example of a cvar");
	//auto cvar2 = cvarManager->registerCvar("template_cvar2", "0", "just a example of a cvar with more settings", true, true, -10, true, 10 );

	//cvar.addOnValueChanged([this](std::string cvarName, CVarWrapper newCvar) {
	//	cvarManager->log("the cvar with name: " + cvarName + " changed");
	//	cvarManager->log("the new value is:" + newCvar.getStringValue());
	//});

	//cvar2.addOnValueChanged(std::bind(&SpeedFlipTrainer::YourPluginMethod, this, _1, _2));

	// enabled decleared in the header
	//enabled = std::make_shared<bool>(false);
	//cvarManager->registerCvar("TEMPLATE_Enabled", "0", "Enable the TEMPLATE plugin", true, true, 0, true, 1).bindTo(enabled);

	//cvarManager->registerNotifier("NOTIFIER", [this](std::vector<std::string> params){FUNCTION();}, "DESCRIPTION", PERMISSION_ALL);
	//cvarManager->registerCvar("CVAR", "DEFAULTVALUE", "DESCRIPTION", true, true, MINVAL, true, MAXVAL);//.bindTo(CVARVARIABLE);
	//gameWrapper->HookEvent("FUNCTIONNAME", std::bind(&TEMPLATE::FUNCTION, this));
	//gameWrapper->HookEventWithCallerPost<ActorWrapper>("FUNCTIONNAME", std::bind(&SpeedFlipTrainer::FUNCTION, this, _1, _2, _3));
	//gameWrapper->RegisterDrawable(bind(&TEMPLATE::Render, this, std::placeholders::_1));


	//gameWrapper->HookEvent("Function TAGame.Ball_TA.Explode", [this](std::string eventName) {
	//	cvarManager->log("Your hook got called and the ball went POOF");
	//});
	// You could also use std::bind here
	//gameWrapper->HookEvent("Function TAGame.Ball_TA.Explode", std::bind(&SpeedFlipTrainer::YourPluginMethod, this);
}

void SpeedFlipTrainer::onUnload()
{
}