
#include "jr_FaderPairs.h"
#include <JuceHeader.h>
#include <vector>
#include "jr_Oscillators.h"

//=========================================//
//************ FaderPair ******************//
//=========================================//

FaderPair::FaderPair() {};

void FaderPair::init(float _sampleRate, float _maxLevel, bool _silenced)
{
	silenced = _silenced;

	masterGain.reset(_sampleRate, rampTime);
	masterGain.setCurrentAndTargetValue(0.0f);
	masterGain.setTargetValue(silenced ? 0.0f : 1.0f);
	avgLevel.reset(_sampleRate, rampTime);
	maxLevel.reset(_sampleRate, rampTime);

	lfo.setSampleRate(_sampleRate);
	maxLevel.setTargetValue(_maxLevel);
	avgLevel.setTargetValue(_maxLevel / 2.0f);

	lfoBaseFreq = random.nextFloat();
	lfo.setFrequency(getLfoFreqFromScale(lfoBaseFreq));

	resetPan(0);
	resetPan(1);

	initOscs(_sampleRate);
}

void FaderPair::initFreqs(float _minOscFreq, float _maxOscFreq, float _minLfoFreq, float _maxLfoFreq)
{
	FaderPair::minOscFreq = _minOscFreq;
	FaderPair::maxOscFreq = _maxOscFreq;
	FaderPair::minLfoFreq = _minLfoFreq;
	FaderPair::maxLfoFreq = _maxLfoFreq;
}

std::pair<float, float> FaderPair::process()
{
	if (oscs.size() == 0)
	{
		return out;
	}

	if (masterGain.getCurrentValue() == 0.0f && waitingToRestart)
	{
		waitingToRestart = false;
		start();
	}

	// process next sampleOut
	float delta = processLevels(); // avgLevel gets incremented in here

	// reset sample out
	out.first = 0.0f;
	out.second = 0.0f;

	// raw values before panning
	float osc1RawOut = oscs.at(0).process() * (avgLevel.getCurrentValue() + delta);
	float osc2RawOut = oscs.at(1).process() * (avgLevel.getCurrentValue() - delta);

	// add panned signals to out pair
	out.first += osc1RawOut * (1.0f - pan.at(0));
	out.second += osc1RawOut * (pan.at(0));
	out.first += osc2RawOut * (1.0f - pan.at(1));
	out.second += osc2RawOut * (pan.at(1));

	// apply master gain
	masterGain.getNextValue();
	out.first *= masterGain.getCurrentValue();
	out.second *= masterGain.getCurrentValue();

	return out;
}

void FaderPair::silence()
{
	if (!silenced)
	{
		masterGain.setTargetValue(0);
		silenced = true;
	}
	waitingToRestart = false;
}

void FaderPair::start()
{
	if (masterGain.getCurrentValue() != 0.0f && silenced)
	{
		// if the voice is silenced but has not yet been faded completely, wait
		waitingToRestart = true;
	}
	else if (masterGain.getCurrentValue() == 0.0f)
	{
		// if voice is already silenced reset it and start
		resetFrequencies();
		masterGain.setTargetValue(1.0f);
		silenced = false;
	}
	// if voice is not currently silenced do nothing
}

void FaderPair::setMaxLevel(float _maxLevel)
{
	maxLevel.setTargetValue(_maxLevel);
	avgLevel.setTargetValue(_maxLevel / 2.0f);
}

void FaderPair::updateLfoFreq()
{
	lfo.setFrequency(getLfoFreqFromScale(lfoBaseFreq));
}

void FaderPair::setStereoWidth(float width)
{
	FaderPair::stereoWidth = width;
}

void FaderPair::setLfoRate(float _rate)
{
	FaderPair::lfoRate = jr::Utils::constrainFloat(_rate);
}

void FaderPair::initOscs(float _sampleRate)
{
	if (oscs.size() != 2)
	{
		for (int i{}; i < 2; i++)
		{
			SineOsc osc = SineOsc();
			osc.setSampleRate(_sampleRate);
			osc.setFrequency(FaderPair::getRandomOscFrequency());
			oscs.push_back(osc);
		}
	}
}

void FaderPair::resetFrequencies()
{
	for (auto& osc : oscs)
	{
		osc.setFrequency(FaderPair::getRandomOscFrequency());
	}
	lfoBaseFreq = random.nextFloat();
	lfo.setFrequency(getLfoFreqFromScale(lfoBaseFreq));
}

void FaderPair::resetOsc(int index)
{
	if (index < 0 || index >= oscs.size())
	{
		return;
	}
	oscs.at(index).setFrequency(FaderPair::getRandomOscFrequency());
}

void FaderPair::resetPan(int index)
{
	if (index < 0 || index >= oscs.size())
	{
		return;
	}
	pan.at(index) = 0.5f + (random.nextFloat() - 0.5f) * FaderPair::stereoWidth; // set to 0.5f +/- 0.5f at max or 0.0f at min
}

float FaderPair::getLfoFreqFromScale(float scale)
{
	scale = jr::Utils::constrainFloat(scale);
	return minLfoFreq + ((maxLfoFreq + lfoRate * lfoSpread) - minLfoFreq) * scale;
}

float FaderPair::processLevels()
{
	maxLevel.getNextValue();
	float lfoVal = lfo.process();
	if (lfoVal >= 1.0f)
	{
		resetOsc(1);
		resetPan(1);
	}
	else if (lfoVal <= -1.0f)
	{
		resetOsc(0);
		resetPan(0);
	}
	return lfoVal * avgLevel.getNextValue();
}

float FaderPair::getRandomOscFrequency()
{
	return (FaderPair::random.nextFloat() * (FaderPair::maxOscFreq - FaderPair::minOscFreq)) + FaderPair::minOscFreq;
}

//=========================================//
//************ FaderPairs *****************//
//=========================================//

void FaderPairs::init(size_t numPairs, float _sampleRate, size_t maxNumPairs, float minOscFreq, float maxOscFreq)
{
	sampleRate = _sampleRate;
	gain.reset(sampleRate, 0.1f);

	FaderPair::initFreqs(minOscFreq, maxOscFreq);

	const float maxLevel = 0.5f / (float)numPairs;
	for (int i{}; i < maxNumPairs; i++)
	{
		pairs.push_back(FaderPair());
		pairs.at(i).init(sampleRate, maxLevel, i >= numPairs);
	}
	numActivePairs = numPairs;

	setGainOffset();
}

std::pair<float, float> FaderPairs::process()
{
	out.first = 0.0f;
	out.second = 0.0f;

	float sampleOut{};
	for (auto& pair : pairs)
	{
		auto pairOut = pair.process();
		out.first += pairOut.first;
		out.second += pairOut.second;
	}

	gain.getNextValue();

	out.first *= gain.getCurrentValue();
	out.second *= gain.getCurrentValue();

	return out;
}

void FaderPairs::setNumPairs(size_t numPairs)
{
	if (numPairs < 0)
	{
		numPairs = 0;
	}
	else if (numPairs > maxNumPairs)
	{
		numPairs = maxNumPairs;
	}
	float maxLevel = 1.0f / (float)numPairs;
	if (numPairs < pairs.size())
	{
		for (int i{}; i < pairs.size(); i++)
		{
			if (i < numPairs)
			{
				pairs.at(i).start();
			}
			else
			{
				pairs.at(i).silence();
			}
			pairs.at(i).setMaxLevel(maxLevel);
		}
	}

	numActivePairs = numPairs;
	setGainOffset();
}

void FaderPairs::setLfoRate(float _rate)
{
	FaderPair::setLfoRate(_rate);
	for (auto& pair : pairs)
	{
		pair.updateLfoFreq();
	}
}

void FaderPairs::setOscFreqRange(float _minHz, float _maxHz)
{
	FaderPair::initFreqs(jr::Utils::constrainFloat(_minHz, 80.0f, 2000.0f), jr::Utils::constrainFloat(_maxHz, 80.0f, 2000.0f));
}

void FaderPairs::setStereoWidth(float width)
{
	FaderPair::setStereoWidth(jr::Utils::constrainFloat(width));
}

void FaderPairs::setGainOffset()
{
	gainOffset = (numActivePairs - 1) / 7.0f;
	if (gainOffset > 1.0f)
	{
		gainOffset = 1.0f;
	}

	// gain offset is now 0.0f if numActiveParis is 2, or 1.0f if numActivePairs is 14 or greater
	gain.setTargetValue(0.6f + 0.4f * gainOffset);
}