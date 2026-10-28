#pragma once

// SpeedFlip "Training Pack HUD" — Rocket League–style coherent overlay palette.
namespace Ui
{
	struct Rgba
	{
		unsigned char r, g, b, a;
	};

	struct Rgb
	{
		unsigned char r, g, b;
		constexpr Rgba withAlpha(unsigned char a) const { return { r, g, b, a }; }
	};

	// Surfaces
	inline constexpr Rgb PanelFill       { 11,  20,  36  }; // #0B1424
	inline constexpr Rgb PanelFillInset  { 16,  28,  48  }; // #101C30
	inline constexpr Rgb PanelBorder     { 92,  115, 148 }; // #5C7394
	inline constexpr Rgb PanelBorderLive { 143, 182, 232 }; // #8FB6E8
	inline constexpr Rgb PanelTopLine    { 255, 255, 255 }; // alpha ~28
	inline constexpr Rgb Grid            { 42,  59,  87  }; // #2A3B57
	inline constexpr Rgb Scrim           { 5,   10,  20  };

	// Text
	inline constexpr Rgb TextPrimary     { 242, 246, 255 }; // #F2F6FF
	inline constexpr Rgb TextSecondary   { 159, 176, 204 }; // #9FB0CC
	inline constexpr Rgb TextMuted       { 111, 129, 159 }; // #6F819F
	inline constexpr Rgb TextOnAccent    { 8,   16,  30  };

	// Team accents
	inline constexpr Rgb RLBlue          { 30,  150, 255 }; // #1E96FF
	inline constexpr Rgb RLBlueDeep      { 15,  95,  168 };
	inline constexpr Rgb RLOrange        { 255, 122, 26  }; // #FF7A1A
	inline constexpr Rgb RLOrangeDeep    { 184, 80,  11  };

	// Status meaning (everywhere): green = OK · yellow = barely · red = not OK
	inline constexpr Rgb StatusGood      { 40,  230,  95  }; // OK
	inline constexpr Rgb StatusWarn      { 255, 200,  30  }; // barely
	inline constexpr Rgb StatusBad       { 235,  55,  55  }; // not OK
	inline constexpr Rgb StatusNone      { 74,  90,  120 };

	// Stick trail phases (event timeline — not pass/fail)
	inline constexpr Rgb TrailPre        { 111, 129, 159 };
	inline constexpr Rgb TrailJ1J2       { 30,  150, 255 };
	inline constexpr Rgb TrailJ2C        { 255, 122, 26  };
	inline constexpr Rgb TrailAfterC     { 242, 246, 255 };

	// Angle zones = same meaning as status (OK / barely)
	inline constexpr Rgb ZoneIdeal       { 40,  230,  95  }; // StatusGood
	inline constexpr Rgb ZoneAcceptable  { 255, 200,  30  }; // StatusWarn

	// Meters
	inline constexpr Rgb MeterTrough     { 16,  28,  48  };
	inline constexpr Rgb MeterTick       { 159, 176, 204 };

	// Stick trail max length (2s @ 120 Hz)
	inline constexpr int StickTrailMaxTick = 240;

	// Legacy aliases used during migration
	inline constexpr Rgb Bg = PanelFill;
	inline constexpr Rgb Border = PanelBorder;
	inline constexpr Rgb Text = TextPrimary;
	inline constexpr Rgb TextDim = TextSecondary;
	inline constexpr Rgb TextDark = TextOnAccent;
	inline constexpr Rgb Blue = RLBlue;
	inline constexpr Rgb Orange = RLOrange;
	inline constexpr Rgb Good = StatusGood;
	inline constexpr Rgb Ok = StatusWarn;
	inline constexpr Rgb Bad = StatusBad;
	inline constexpr Rgb MeterBase = MeterTrough;
	inline constexpr Rgb MeterMark = TextPrimary;
	inline constexpr Rgb PhasePre = TrailPre;
	inline constexpr Rgb PhaseJ12 = TrailJ1J2;
	inline constexpr Rgb PhaseJ2C = TrailJ2C;
	inline constexpr Rgb PhaseHold = TrailAfterC;
	inline constexpr Rgb AngleZone = ZoneIdeal;
}
