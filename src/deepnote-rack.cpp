#include "plugin.hpp"
#include "voice/deepnotevoice.hpp"
#include "voice/frequencytable.hpp"
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
		INFO("%.4f, %.4f, %d, %d, %.4f, %.4f, %.4f, %.4f, %.4f", 
			values.startFreq, 
			values.targetFreq, 
			values.in_state, 
			values.out_state, 
			values.animationLfo_value, 
			values.shapedAnimationValue,
			values.animationFreq, 
			values.currentFrequency, 
			values.oscValue
		);
	}
};

namespace types = deepnote::nt;

using DuoVoiceType = deepnote::DeepnoteVoice<2>;
using TrioVoiceType = deepnote::DeepnoteVoice<3>;

const types::OscillatorFrequencyRange START_FREQ_RANGE(deepnote::Range(types::RangeLow(200.f), types::RangeHigh(400.f)));
const deepnote::Range ANIMATION_RATE_RANGE{types::RangeLow(0.05f), types::RangeHigh(1.5f)};

struct Deepnote_rack : Module {

	TrioVoiceType trioVoices[deepnote::NUM_TRIO_VOICES];
	DuoVoiceType duoVoices[deepnote::NUM_DUO_VOICES];
	deepnote::FrequencyTable voiceFrequencies;
	dsp::PulseGenerator triggerPulse;

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

		configParam(DETUNE_PARAM, 0.f, 2.f, 0.5f, "detune", " Hz");
		configParam(TARGET_PARAM, 0, 12, 0, "target");
		configParam(RATE_PARAM, 0.05f, 10.0f, 1.f, "rate_multiplier");
		configParam(CP1_PARAM, 0.f, 1.f, 0.8f, "control_point_1");
		configParam(CP2_PARAM, 0.f, 1.f, 0.5f, "control_point_2");

		configInput(DETUNE_CV_INPUT, "");
		configInput(TARGET_CV_INPUT, "");
		configInput(ROOT_CV_INPUT, "");
		configOutput(OUTPUT_OUTPUT, "");
		configOutput(GATE_OUTPUT, "");

		const StdLibRandomFloatGenerator random;
		auto index{0};
		voiceFrequencies.initialize(START_FREQ_RANGE, random);

		for (auto& voice : trioVoices) {
			voice.Init(
				voiceFrequencies.getFrequency(types::VoiceIndex(index++)),
				types::SampleRate(sample_rate), 
				types::OscillatorFrequency(random(ANIMATION_RATE_RANGE.GetLow().get(), ANIMATION_RATE_RANGE.GetHigh().get())), 
				random
			);
		}

		for (auto& voice : duoVoices) {
			voice.Init(
				voiceFrequencies.getFrequency(types::VoiceIndex(index++)),
				types::SampleRate(sample_rate), 
				types::OscillatorFrequency(random(ANIMATION_RATE_RANGE.GetLow().get(), ANIMATION_RATE_RANGE.GetHigh().get())), 
				random
			);
		}
	}

	void process(const ProcessArgs& args) override {
		const auto detune = types::DetuneHz(params[DETUNE_PARAM].getValue());
		const auto frequencyTableIndex = types::FrequencyTableIndex(params[TARGET_PARAM].getValue());
		const auto animationMultiplier = types::AnimationMultiplier(params[RATE_PARAM].getValue());
		const auto cp1 = types::ControlPoint1(params[CP1_PARAM].getValue());
		const auto cp2 = types::ControlPoint2(params[CP2_PARAM].getValue());
		const deepnote::NoopTrace traceFunctor;
		//const RackTraceType traceFunctor;
		auto output{0.f};
		auto index{0};
		bool voiceInFlight{false};

		const auto indexChanged = voiceFrequencies.setCurrentIndex(frequencyTableIndex);

		for (auto& voice : trioVoices) 
		{
			const auto _atTarget = voice.IsAtTarget();
			output += processVoice(
							voice, 
							detune,
							indexChanged,
							voiceFrequencies.getFrequency(types::VoiceIndex(index++)),
							animationMultiplier, 
							cp1, 
							cp2, 
							traceFunctor);
			
			if (!voice.IsAtTarget()) 
			{
				voiceInFlight = true;
			}

			if (voice.IsAtTarget() && !_atTarget) 
			{
				this->triggerPulse.trigger(1e-3f);
			}
		}
		for (auto& voice : duoVoices) 
		{
			const auto _atTarget = voice.IsAtTarget();
			output += processVoice(
							voice, 
							detune,
							indexChanged,
							voiceFrequencies.getFrequency(types::VoiceIndex(index++)),
							animationMultiplier, 
							cp1, 
							cp2, 
							traceFunctor);
			
			//	Gate is high when all voices are at target: !voiceInFlight
			if (!voice.IsAtTarget()) 
			{
				voiceInFlight = true;
			}

			//	the trigger fires when 1 or more voices arrive at target
			if (voice.IsAtTarget() && !_atTarget) 
			{
				this->triggerPulse.trigger(1e-3f);
			}
		}

		outputs[OUTPUT_OUTPUT].setVoltage(output * 5.f);

		if (voiceInFlight) 
		{
			outputs[GATE_OUTPUT].setVoltage(0.f);
		} else 
		{
			outputs[GATE_OUTPUT].setVoltage(10.f);
		}

		//bool fall = this->triggerPulse.process(args.sampleTime);
		//outputs[FALL_OUTPUT].setVoltage(fall ? 10.f : 0.f, c);
	}

	template<typename VoiceType, typename TraceFunctor>
	float processVoice(VoiceType& voice, const types::DetuneHz& detune, const bool indexChanged, 
		const types::OscillatorFrequency& targetFrequency, const types::AnimationMultiplier& animationMultiplier,
		const types::ControlPoint1& cp1, const types::ControlPoint2& cp2, const TraceFunctor& traceFunctor) const
	{
		voice.computeDetune(detune);
		if (indexChanged)
		{
			voice.SetTargetFrequency(targetFrequency);
		}
		return voice.Process(animationMultiplier, cp1, cp2, traceFunctor);
	}
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