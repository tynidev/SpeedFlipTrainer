#include "pch.h"
#include "Attempt.h"

#include <iostream>
#include <fstream>
#include <sstream>

void Attempt::Record(int tick, ControllerInput input)
{
	inputs[tick] = input;
}

ControllerInput Attempt::Play(int tick)
{
	auto it = inputs.find(tick);
	if (it != inputs.end())
		return it->second;
	return ControllerInput();
}

filesystem::path Attempt::GetFilename(filesystem::path dir)
{
	// Get date string
	auto t = time(0);
	auto now = localtime(&t);

	auto month = to_string(now->tm_mon + 1);
	month.insert(month.begin(), 2 - month.length(), '0');

	auto day = to_string(now->tm_mday);
	day.insert(day.begin(), 2 - day.length(), '0');

	auto year = to_string(now->tm_year + 1900);

	auto hour = to_string(now->tm_hour);
	hour.insert(hour.begin(), 2 - hour.length(), '0');

	auto min = to_string(now->tm_min);
	min.insert(min.begin(), 2 - min.length(), '0');

	auto secs = to_string(now->tm_sec);
	secs.insert(secs.begin(), 2 - secs.length(), '0');

	string filename = year + "-" + month + "-" + day + "." + hour + "." + min + "." + secs + ".attempt";
	return dir / filename;
}

void Attempt::WriteInputsToFile(filesystem::path filepath)
{
	ofstream os;
	os.open(filepath, ios::out);
	os << "Tick,"
	   << "ActivateBoost,"
	   << "DodgeForward,"
	   << "DodgeStrafe,"
	   << "Handbrake,"
	   << "HoldingBoost,"
	   << "Jump,"
	   << "Jumped,"
	   << "Pitch,"
	   << "Roll,"
	   << "Steer,"
	   << "Throttle,"
	   << "Yaw" << '\n';
	for (auto& kv : inputs) {
		os << kv.first << ","
		   << kv.second.ActivateBoost << ","
		   << kv.second.DodgeForward << ","
		   << kv.second.DodgeStrafe << ","
		   << kv.second.Handbrake << ","
		   << kv.second.HoldingBoost << ","
		   << kv.second.Jump << ","
		   << kv.second.Jumped << ","
		   << kv.second.Pitch << ","
		   << kv.second.Roll << ","
		   << kv.second.Steer << ","
		   << kv.second.Throttle << ","
		   << kv.second.Yaw << '\n';
	}
	os.close();
}

void Attempt::ReadInputsFromFile(filesystem::path filepath)
{
	ifstream is;
	is.open(filepath, ios::in);

	string line, word;

	inputs.clear(); // clear current inputs

	getline(is, line); // skip header line

	while (getline(is, line)) 
	{
		ControllerInput i;

		// Parse line
		stringstream s(line);
		getline(s, word, ',');
		int tick = stoi(word);
		getline(s, word, ',');
		i.ActivateBoost = stoul(word);
		getline(s, word, ',');
		i.DodgeForward = stof(word);
		getline(s, word, ',');
		i.DodgeStrafe = stof(word);
		getline(s, word, ',');
		i.Handbrake = stoul(word);
		getline(s, word, ',');
		i.HoldingBoost = stoul(word);
		getline(s, word, ',');
		i.Jump = stoul(word);
		getline(s, word, ',');
		i.Jumped = stoul(word);
		getline(s, word, ',');
		i.Pitch = stof(word);
		getline(s, word, ',');
		i.Roll = stof(word);
		getline(s, word, ',');
		i.Steer = stof(word);
		getline(s, word, ',');
		i.Throttle = stof(word);
		getline(s, word, ',');
		i.Yaw = stof(word);

		inputs[tick] = i;
	}

	is.close();
}