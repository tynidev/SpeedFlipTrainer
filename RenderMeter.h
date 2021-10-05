#pragma once

struct Color
{
	char red, green, blue = 0;
	float opacity = 0;
};

struct Line : Color
{
	int width = 2;
};

struct MeterRange : Color
{
	int low, high = 0;
};

struct MeterMarking : Line
{
	int value = 0;
};

Vector2 RenderMeter(CanvasWrapper canvas, Vector2 startPos, Vector2 reqBoxSize, Color base, Line border, int totalUnits, std::list<MeterRange> ranges, std::list<MeterMarking> markings, bool vertical);
