#include "pch.h"
#include "RenderMeter.h"

Vector2 RenderMeter(CanvasWrapper canvas, Vector2 startPos, Vector2 reqBoxSize, Color base, Line border, int totalUnits, std::list<MeterRange> ranges, std::list<MeterMarking> markings, bool vertical)
{
	int unitWidth = vertical ? reqBoxSize.Y / totalUnits : reqBoxSize.X / totalUnits;
	Vector2 boxSize = vertical ? Vector2{ reqBoxSize.X, unitWidth * totalUnits } : Vector2{ unitWidth * totalUnits, reqBoxSize.Y };

	// Draw base meter base color
	canvas.SetColor(base.red, base.green, base.blue, (char)(100 * base.opacity));
	canvas.SetPosition(startPos);
	canvas.FillBox(boxSize);

	// Draw meter ranges
	for (const MeterRange& range : ranges)
	{
		canvas.SetColor(range.red, range.green, range.blue, (char)(255 * range.opacity));

		auto l = range.low;
		if (l < 0)
			l = 0;
		if (l > totalUnits)
			l = totalUnits;

		auto h = range.high;
		if (h < 0)
			h = 0;
		if (h > totalUnits)
			h = totalUnits;

		if (l > h)
			l = h;

		if (vertical)
		{
			auto position = Vector2{ startPos.X, (int)(startPos.Y + boxSize.Y - (h * unitWidth)) };
			auto size = Vector2{ boxSize.X, (int)((h - l) * unitWidth) };
			canvas.SetPosition(position);
			canvas.FillBox(size);
		}
		else
		{
			if (l != 0)
				l -= 1;
			if (h != totalUnits)
				h -= 1;
			auto position = Vector2{ (int)(startPos.X + (l * unitWidth)), startPos.Y };
			auto size = Vector2{ (int)((h - l) * unitWidth), boxSize.Y };
			canvas.SetPosition(position);
			canvas.FillBox(size);
		}
	}

	// Draw meter markings
	for (const MeterMarking& marking : markings)
	{
		canvas.SetColor(marking.red, marking.green, marking.blue, (char)(255 * marking.opacity));

		auto value = marking.value - 1;
		if (value < 0)
			value = 0;
		if (value > totalUnits)
			value = totalUnits;

		if (vertical)
		{
			float y = startPos.Y + boxSize.Y - ((value + 1) * unitWidth);
			auto begin = Vector2{ startPos.X,             (int)y };
			auto end = Vector2{ startPos.X + boxSize.X, (int)y };
			canvas.DrawLine(begin, end, marking.width);
		}
		else
		{
			float x = startPos.X + (value * unitWidth) + (marking.width / 2);
			float y = startPos.Y - (marking.width / 2);
			auto begin = Vector2{ (int)x, (int)y };
			auto end = Vector2{ (int)x, (int)(y + boxSize.Y + 1) };
			canvas.DrawLine(begin, end, marking.width);
		}
	}

	// Draw meter border
	if (border.width > 0)
	{
		canvas.SetColor(border.red, border.green, border.blue, (char)(255 * border.opacity));
		canvas.SetPosition(startPos.minus(Vector2{ border.width / 2, border.width / 2 }));
		canvas.DrawBox(boxSize.minus(Vector2{ border.width * -1, border.width * -1 }));
	}

	return boxSize;
}