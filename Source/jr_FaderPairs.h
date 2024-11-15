#pragma once

#include <JuceHeader.h>
#include <vector>
#include "jr_Oscillators.h"

class FaderPair
{
public:
	FaderPair() {};

	void init(float _sampleRate, float _maxLevel = 1.0f, bool _silenced = false) {
		silenced = _silenced;

		masterGain.reset(_sampleRate, rampTime);
		masterGain.setCurrentAndTargetValue(0.0f);
		masterGain.setTargetValue(silenced ? 0.0f : 1.0f);
		avgLevel.reset(_sampleRate, rampTime);
		maxLevel.reset(_sampleRate, rampTime);

		lfo.setSampleRate(_sampleRate);
		maxLevel.setTargetValue(_maxLevel);
		avgLevel.setTargetValue(_maxLevel / 2.0f);

		lfoBaseFreq = FaderPair::getRandomLfoFrequency();
		lfo.setFrequency(lfoBaseFreq);

		initOscs(_sampleRate);
	}

	static void initFreqs(float _minOscFreq, float _maxOscFreq, float _minLfoFreq = 0.01f, float _maxLfoFreq = 0.1f) {
		FaderPair::minOscFreq = _minOscFreq;
		FaderPair::maxOscFreq = _maxOscFreq;
		FaderPair::minLfoFreq = _minLfoFreq;
		FaderPair::maxLfoFreq = _maxLfoFreq;
	}

	float process() {

		if (masterGain.getCurrentValue() == 0.0f && waitingToRestart) {
			waitingToRestart = false;
			start();
		}

		// process next sampleOut
		float delta = processLevels(); // avgLevel gets incremented in here
		float sampleOut{};
		sampleOut += oscs.at(0).process() * (avgLevel.getCurrentValue() + delta);
		sampleOut += oscs.at(1).process() * (avgLevel.getCurrentValue() - delta);
		return sampleOut * masterGain.getNextValue();
	}

	void silence() {
		if (!silenced) {
			masterGain.setTargetValue(0);
			silenced = true;
		}
		waitingToRestart = false;
	}

	/**
	* unsilences a voice - if it has already been silenced, will wait until the master reaches 0 before resetting frequencies and volume
	*/
	void start() {
		if (masterGain.getCurrentValue() != 0.0f && silenced) {
			// if the voice is silenced but has not yet been faded completely, wait
			waitingToRestart = true;
		}
		else if (masterGain.getCurrentValue() == 0.0f) {
			// if voice is already silenced reset it and start
			resetFrequencies();
			masterGain.setTargetValue(1.0f);
			silenced = false;
		}
		// if voice is not currently silenced do nothing
	}

	void restart() {
		waitingToRestart = true;
		masterGain.setTargetValue(0.0f);
	}

	bool isSilenced() {
		return silenced && !waitingToRestart;
	}

	void setMaxLevel(float _maxLevel) {
		maxLevel.setTargetValue(_maxLevel);
		avgLevel.setTargetValue(_maxLevel / 2.0f);
	}

	void setLfoRate(float _rate) {
		if (_rate < 0.0f) {
			_rate = 0.0f;
		}
		else if (_rate > 1.0f) {
			_rate = 1.0f;
		}
		lfoRate = _rate;
		lfo.setFrequency(lfoBaseFreq + (lfoSpread * lfoRate));
	}

private:
	void initOscs(float _sampleRate) {
		for (int i{}; i < 2; i++) {
			SineOsc osc = SineOsc();
			osc.setSampleRate(_sampleRate);
			osc.setFrequency(FaderPair::getRandomOscFrequency());
			oscs.push_back(osc);
		}
	}

	void resetFrequencies() {
		for (auto& osc : oscs) {
			osc.setFrequency(FaderPair::getRandomOscFrequency());
		}
		lfo.setFrequency(FaderPair::getRandomLfoFrequency());
	}

	void resetOsc(int index) {
		if (index < 0 || index >= oscs.size()) {
			return;
		}
		oscs.at(index).setFrequency(FaderPair::getRandomOscFrequency());
	}

	float processLevels() {
		maxLevel.getNextValue();
		float lfoVal = lfo.process();
		if (lfoVal >= 1.0f) {
			resetOsc(1);
		}
		else if (lfoVal <= -1.0f) {
			resetOsc(0);
		}
		return lfoVal * avgLevel.getNextValue();
	}

	SineOsc lfo;											// LFO to control mix level of faders
	juce::SmoothedValue<float> maxLevel;											// the maximum combined level of both faders
	juce::SmoothedValue<float> avgLevel;						
	std::vector<SineOsc > oscs;
	float rampTime{ 0.1f };									// time in seconds for fading in and out
	juce::SmoothedValue<float> masterGain{ 0.0f };			// master gain for fading in and out
	bool silenced{ false };
	bool waitingToRestart{ false };							// true if the voice is waiting to reach 0 master gain before restarting
	float lfoRate{ 0.0f };									// rate to modify the LFO freq by (0-1)
	float lfoBaseFreq{};
	float lfoSpread{ 1.0f };
	
	// shared static variables and methods

	static inline juce::Random random;									// used for generating random frequency
	static inline float minLfoFreq;								// minimum lfo frequency when generating random in Hz
	static inline float maxLfoFreq;								// range when picking a random frequency in Hz
	static inline float minOscFreq;								// minimum lfo frequency when generating random in Hz
	static inline float maxOscFreq;							// range when picking a random frequency in Hz

	static float getRandomOscFrequency() {
		return (FaderPair::random.nextFloat() * (FaderPair::maxOscFreq - FaderPair::minOscFreq)) + FaderPair::minOscFreq;
	}

	static float getRandomLfoFrequency() {
		return (FaderPair::random.nextFloat() * (FaderPair::maxLfoFreq - FaderPair::minLfoFreq)) + FaderPair::minLfoFreq;
	}
};

class FaderPairs
{
public:
	FaderPairs() {};

	void init(size_t numPairs, float _sampleRate, size_t maxNumPairs, float minOscFreq = 120.0f, float maxOscFreq = 1200.0f) {
		sampleRate = _sampleRate;
		gain.reset(sampleRate, 0.1f);

		FaderPair::initFreqs(minOscFreq, maxOscFreq);

		const float maxLevel = 0.5f / (float)numPairs;
		for (int i{}; i < maxNumPairs; i++) {
			pairs.push_back(FaderPair());
			pairs.at(i).init(sampleRate, maxLevel, i >= numPairs);
		}
		numActivePairs = numPairs;

		setGainOffset();
	}

	float process() {
		float sampleOut{};
		for (auto& pair : pairs) {
			sampleOut += pair.process();
		}
		return sampleOut * gain.getNextValue();
	}

	void setNumPairs(size_t numPairs) {
		if (numPairs < 0) {
			numPairs = 0;
		}
		else if (numPairs > maxNumPairs) {
			numPairs = maxNumPairs;
		}
		float maxLevel = 1.0f / (float)numPairs;
		if (numPairs < pairs.size()) {
			for (int i{}; i < pairs.size(); i++) {
				if (i < numPairs) {
					pairs.at(i).start();
				}
				else {
					pairs.at(i).silence();
				}
				pairs.at(i).setMaxLevel(maxLevel);
			}
		}

		numActivePairs = numPairs;
		setGainOffset();
	}

	void setLfoRate(float _rate) {
		for (auto& pair : pairs) {
			pair.setLfoRate(_rate);
		}
	}

	void setOscFreqRange(float _minHz, float _maxHz) {
		FaderPair::initFreqs(setAndConstrain(_minHz, 80.0f, 2000.0f), setAndConstrain(_maxHz, 80.0f, 2000.0f));
	}

private:
	void setGainOffset() {
		gainOffset = (numActivePairs - 1) / 7.0f;
		if (gainOffset > 1.0f) {
			gainOffset = 1.0f;
		}

		// gain offset is now 0.0f if numActiveParis is 2, or 1.0f if numActivePairs is 14 or greater
		gain.setTargetValue(0.6f + 0.4f * gainOffset);
	}

	float setAndConstrain(float newValue, float min, float max) {
		if (newValue > max) {
			return max;
		}
		else if (newValue < min) {
			return min;
		}
		return newValue;
	}

	std::vector<FaderPair> pairs;
	float sampleRate;
	size_t numActivePairs;
	size_t maxNumPairs;
	float gainOffset;							// offset to manage gain difference between few voices and many voices
	juce::SmoothedValue<float> gain{ 0.0f };
};

