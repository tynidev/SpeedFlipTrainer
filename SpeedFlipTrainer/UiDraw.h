#pragma once

#include "bakkesmod/wrappers/canvaswrapper.h"
#include "UiTheme.h"

namespace Ui
{
	inline void Set(CanvasWrapper& c, Rgb rgb, unsigned char a = 255)
	{
		c.SetColor(rgb.r, rgb.g, rgb.b, a);
	}

	// Shared panel chrome: fill + border + faint top highlight
	inline void DrawPanel(CanvasWrapper& c, int x, int y, int w, int h, Rgb borderColor, unsigned char borderAlpha)
	{
		Set(c, PanelFill, 230);
		c.SetPosition(Vector2{ x, y });
		c.FillBox(Vector2{ w, h });

		Set(c, borderColor, borderAlpha);
		c.SetPosition(Vector2{ x, y });
		c.DrawBox(Vector2{ w, h });

		Set(c, PanelTopLine, 40);
		c.DrawLine(Vector2{ x + 5, y + 1 }, Vector2{ x + w - 5, y + 1 }, 1);
	}

	inline void DrawPanel(CanvasWrapper& c, int x, int y, int w, int h, bool live = false)
	{
		DrawPanel(c, x, y, w, h, live ? PanelBorderLive : PanelBorder, live ? (unsigned char)240 : (unsigned char)200);
	}

	inline void DrawInsetWell(CanvasWrapper& c, int x, int y, int w, int h)
	{
		Set(c, PanelFillInset, 235);
		c.SetPosition(Vector2{ x, y });
		c.FillBox(Vector2{ w, h });
	}
}
