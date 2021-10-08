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

void Attempt::WriteInputsToFile(std::filesystem::path filepath)
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
		os << kv.first << " "
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

void Attempt::ReadInputsFromFile(std::filesystem::path filepath)
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