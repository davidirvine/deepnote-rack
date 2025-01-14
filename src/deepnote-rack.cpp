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
		TARGET_PARAM,
		RATE_PARAM,
		CP1_PARAM,
		CP2_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		DETUNE_CV_INPUT,
		TARGET_CV_INPUT,
		ROOT_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUT_OUTPUT,
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	Deepnote_rack() {
		const float sample_rate = 48000.f;
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(DETUNE_PARAM, 0.f, 1.f, 0.5f, "detune", " Hz");
		configParam(TARGET_PARAM, 0.f, 1.f, 0.f, "");
		configParam(RATE_PARAM, 0.f, 1.f, 0.f, "");
		configParam(CP1_PARAM, 0.f, 1.f, 0.f, "");
		configParam(CP2_PARAM, 0.f, 1.f, 0.f, "");

		configInput(DETUNE_CV_INPUT, "");
		configInput(TARGET_CV_INPUT, "");
		configInput(ROOT_CV_INPUT, "");
		configOutput(OUTPUT_OUTPUT, "");
		configOutput(GATE_OUTPUT, "");

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
		const deepnote::NoopTrace traceFunctor;

		auto output{0.f};
		for (auto& voice : trioVoices) {
			voice.computeDetune(valueDetune);
			output += (voice.Process(traceFunctor) * valueVolume);
		}
		for (auto& voice : duoVoices) {
			voice.computeDetune(valueDetune);
			//(valueDirection < 1.f) ? voice.TransitionToTarget() : voice.TransitionToStart();
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

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(22.645, 31.429)), module, Deepnote_rack::DETUNE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(22.645, 53.986)), module, Deepnote_rack::TARGET_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(22.645, 76.072)), module, Deepnote_rack::RATE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(7.906, 91.891)), module, Deepnote_rack::CP1_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(22.758, 91.891)), module, Deepnote_rack::CP2_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(6.318, 31.429)), module, Deepnote_rack::DETUNE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(6.333, 53.986)), module, Deepnote_rack::TARGET_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(6.333, 76.072)), module, Deepnote_rack::ROOT_CV_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(23.439, 112.642)), module, Deepnote_rack::OUTPUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(7.776, 112.851)), module, Deepnote_rack::GATE_OUTPUT));
	}
};


Model* modelDeepnote_rack = createModel<Deepnote_rack, Deepnote_rackWidget>("deepnote-rack");