#include "plugin.hpp"
#include "voice/deepnotevoice.hpp"
#include "ranges/range.hpp"
#include "logger.hpp"
#include <random>
#include <string>
#include <vector>


struct StdLibRandomFloatGenerator {
	//  This isn't the fastest random number generator but we don't require a fast one
    float operator()(float low, float high) const {
		std::random_device rd;
		std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(low, high);
        return dis(gen);
    }
};

struct RackTraceType 
{
    void operator()(const deepnote::TraceValues& values) const
    {
		INFO("%.4f, %.4f, %.4f, %d, %d, %.4f, %.4f, %.4f, %.4f, %.4f", 
			values.startRange.GetLow(), 
			values.startRange.GetHigh(), 
			values.targetFreq, 
			values.in_state, 
			values.out_state, 
			values.animationLfo_value, 
			values.shapedAnimationValue,
			values.animationFreq, 
			values.frequency, 
			values.oscValue
		);
	}
};

using DuoVoiceType = deepnote::DeepnoteVoice<2>;
using TrioVoiceType = deepnote::DeepnoteVoice<3>;

const deepnote::Range START_FREQ_RANGE{deepnote::RangeLow(200.f), deepnote::RangeHigh(400.f)};
const deepnote::Range ANIMATION_RATE_RANGE{deepnote::RangeLow(0.05f), deepnote::RangeHigh(1.5f)};

struct Deepnote_rack : Module {
	enum ParamId {
		DETUNE_PARAM,
		CUTOFF_PARAM,
		DIRECTION_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	Deepnote_rack() {
		const float sample_rate = 48000.f;
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(DETUNE_PARAM, 0.f, 1.f, 0.5f, "detune", " Hz");
		configParam(CUTOFF_PARAM, 500.f, 15000.f, 10000.0f, "cutoff", " Hz");
		
		configSwitch(DIRECTION_PARAM, 0.f, 1.f, 0.f, "direction", std::vector<std::string>{"start", "target"});
		
		configOutput(OUTPUT_OUTPUT, "");

		const StdLibRandomFloatGenerator random;
		for (auto& voice : trioVoices) {
			voice.Init(sample_rate, random(ANIMATION_RATE_RANGE.GetLow(), ANIMATION_RATE_RANGE.GetHigh()), random);
		}

		for (auto& voice : duoVoices) {
			voice.Init(sample_rate, random(ANIMATION_RATE_RANGE.GetLow(), ANIMATION_RATE_RANGE.GetHigh()), random);
			voice.TransitionToTarget();
		}

		//filter.Init(sample_rate);
		//filter.SetRes(0.0f);
	}

	void process(const ProcessArgs& args) override {
		const float valueVolume = 1.0f;
		const auto valueDetune = params[DETUNE_PARAM].getValue();
		const auto valueCutoff = params[CUTOFF_PARAM].getValue();
		const auto valueDirection = params[DIRECTION_PARAM].getValue();
		const deepnote::NoopTrace traceFunctor;

		auto output{0.f};
		for (auto& voice : trioVoices) {
			voice.computeDetune(valueDetune);
			(valueDirection < 1.f) ? voice.TransitionToTarget() : voice.TransitionToStart();
			output += (voice.Process(traceFunctor) * valueVolume);
		}
		for (auto& voice : duoVoices) {
			voice.computeDetune(valueDetune);
			(valueDirection < 1.f) ? voice.TransitionToTarget() : voice.TransitionToStart();
			//voice.SetAnimationRate(randomFunctor(animationRateRange.GetLow(), animationRateRange.GetHigh()));
			output += (voice.Process(traceFunctor) * valueVolume);


		}

		outputs[OUTPUT_OUTPUT].setVoltage(output * 5.f);
	}


	TrioVoiceType trioVoices[4] = {
		{ START_FREQ_RANGE, 1396.91 },
		{ START_FREQ_RANGE, 1174.66 },
		{ START_FREQ_RANGE, 659.25 },
		{ START_FREQ_RANGE, 587.33 }
	};

	DuoVoiceType duoVoices[5] = {
		{ START_FREQ_RANGE, 440.00 },
		{ START_FREQ_RANGE, 146.83 },
		{ START_FREQ_RANGE, 110.0 },
		{ START_FREQ_RANGE, 73.42 },
		{ START_FREQ_RANGE, 36.71 }
	};
};


struct Deepnote_rackWidget : ModuleWidget {
	Deepnote_rackWidget(Deepnote_rack* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/deepnote-rack.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(Vec(21.208, 35.39)), module, Deepnote_rack::DETUNE_PARAM));
		addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(Vec(21.208, 67.859)), module, Deepnote_rack::CUTOFF_PARAM));
		addParam(createParamCentered<BefacoPush>(mm2px(Vec(21.208, 96.787)), module, Deepnote_rack::DIRECTION_PARAM));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(21.208, 116.803)), module, Deepnote_rack::OUTPUT_OUTPUT));
	}


};


Model* modelDeepnote_rack = createModel<Deepnote_rack, Deepnote_rackWidget>("deepnote-rack");