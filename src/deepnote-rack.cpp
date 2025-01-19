#include "plugin.hpp"
#include "voice/deepnotevoice.hpp"
#include "voice/frequencytable.hpp"
#include "ranges/range.hpp"
#include "logger.hpp"
#include <random>
#include <string>
#include <vector>

using simd::float_4;

template <class TModule>
struct RootNoteDisplay : LedDisplay 
{
	TModule* module;

	void drawLayer(const DrawArgs& args, int layer) override 
	{
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));

		if (layer == 1) {
			const std::string rootNoteString = module ? module->currentRootNote() : "";
			std::string fontPath = asset::system("res/fonts/ShareTechMono-Regular.ttf");
			std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
			if (!font)
				return;
			nvgFontSize(args.vg, 24);
			nvgFontFaceId(args.vg, font->handle);
			nvgFillColor(args.vg, SCHEME_YELLOW);
			nvgText(args.vg, 4.0, 20.0, rootNoteString.c_str(), NULL);
		}

		nvgResetScissor(args.vg);
		LedDisplay::drawLayer(args, layer);
	}
};


template <class TModule>
struct CurveDisplay : LedDisplay 
{
	TModule* module;

	void drawLayer(const DrawArgs& args, int layer) override 
	{
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));

		if (layer == 1) {
			nvgBeginPath(args.vg);

			const auto shaper = deepnote::BezierUnitShaper(
				deepnote::nt::ControlPoint1(module ? module->params[TModule::CP1_PARAM].getValue() : 0.8f),
				deepnote::nt::ControlPoint2(module ? module->params[TModule::CP2_PARAM].getValue() : 0.5f)
			);

			for (int x = 0; x < box.size.x; x++) {
				const float y = shaper( x * (1.f / box.size.x));
				if (x == 0) {
					nvgMoveTo(args.vg, x, box.size.y - (y * box.size.y));
				} else {
					nvgLineTo(args.vg, x, box.size.y - (y * box.size.y));
				}
			}

			nvgLineCap(args.vg, NVG_ROUND);
			nvgMiterLimit(args.vg, 2.f);
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStrokeColor(args.vg, SCHEME_YELLOW);
			nvgStroke(args.vg);
		}

		nvgResetScissor(args.vg);
		LedDisplay::drawLayer(args, layer);
	}
};


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
	dsp::BooleanTrigger resetBoolean;
	dsp::SchmittTrigger resetSchmitt;

	enum ParamId {
		DETUNE_TRIM_PARAM,
		DETUNE_PARAM,
		TARGET_TRIM_PARAM,
		TARGET_PARAM,
		RATE_TRIM_PARAM,
		RATE_PARAM,
		CP1_PARAM,
		CP2_PARAM,
		RESET_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		DETUNE_INPUT,
		TARGET_INPUT,
		_1VOCT_INPUT,
		RATE_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		TRIGGER_OUTPUT,
		OUTPUT_OUTPUT,
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		RESET_LIGHT,
		LIGHTS_LEN
	};

	Deepnote_rack() {
		const float sample_rate = 48000.f;
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(DETUNE_TRIM_PARAM, 0.f, 1.f, 0.f, "Detune Trim");
		configParam(DETUNE_PARAM, 0.f, 2.f, 0.5f, "Detune", " Hz");
		configParam(TARGET_TRIM_PARAM, 0.f, 1.f, 0.f, "Root Note Trim");
		configParam(TARGET_PARAM, 0, 11, 0, "Root Note");
		getParamQuantity(TARGET_PARAM)->snapEnabled = true;
		configParam(RATE_TRIM_PARAM, 0.f, 1.f, 0.f, "Animation Rate Trin");
		configParam(RATE_PARAM, 0.05f, 10.0f, 1.f, "Animation Rate Mulitplier");
		configParam(CP1_PARAM, 0.f, 1.f, 0.8f, "Control Point 1");
		configParam(CP2_PARAM, 0.f, 1.f, 0.5f, "Control Point 2");
		configParam(RESET_PARAM, 0.f, 1.f, 0.f, "Reset");

		configInput(DETUNE_INPUT, "Detune");
		configInput(TARGET_INPUT, "Root Note CV");
		configInput(_1VOCT_INPUT, "Root Note 1V/Oct");
		configInput(RATE_INPUT, "Animation Rate");
		configInput(RESET_INPUT, "Reset");

		configOutput(TRIGGER_OUTPUT, "Trigger");
		configOutput(OUTPUT_OUTPUT, "Output");
		configOutput(GATE_OUTPUT, "Gate");

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


	//
	//	TODO: add handlers for things like onReset
	//


	void process(const ProcessArgs& args) override {	
		const auto detune = types::DetuneHz(getValueFromInputCombo(DETUNE_PARAM, DETUNE_INPUT, DETUNE_TRIM_PARAM));
		const auto animationMultiplier = types::AnimationMultiplier(getValueFromInputCombo(RATE_PARAM, RATE_INPUT, RATE_TRIM_PARAM));
		const auto frequencyTableIndex = inputs[_1VOCT_INPUT].isConnected() ? getFrequencyTableIndexFrom1VOct() : getFrequencyTableIndexFromTargetParam();
		const auto cp1 = types::ControlPoint1(params[CP1_PARAM].getValue());
		const auto cp2 = types::ControlPoint2(params[CP2_PARAM].getValue());
		const deepnote::NoopTrace traceFunctor;
		//const RackTraceType traceFunctor;
		auto output{0.f};
		auto index{0};
		bool voiceInFlight{false};

		// Handle reset button and trigger
		resetSchmitt.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		bool reset = resetSchmitt.isHigh() || params[RESET_PARAM].getValue() > 0.f;

		const auto indexChanged = voiceFrequencies.setCurrentIndex(frequencyTableIndex);

		if (outputs[OUTPUT_OUTPUT].isConnected())
		{
			
			for (auto& voice : trioVoices) 
			{
				const auto _atTarget = voice.IsAtTarget();

				if (reset)
				{
					voice.ResetStartFrequency(voiceFrequencies.getResetFrequency(types::VoiceIndex(index)));
				}
				output += processVoice(
								voice, 
								detune,
								indexChanged,
								voiceFrequencies.getFrequency(types::VoiceIndex(index)),
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
				index++;
			}
			for (auto& voice : duoVoices) 
			{
				if (reset)
				{
					voice.ResetStartFrequency(voiceFrequencies.getResetFrequency(types::VoiceIndex(index)));
				}

				const auto _atTarget = voice.IsAtTarget();
				output += processVoice(
								voice, 
								detune,
								indexChanged,
								voiceFrequencies.getFrequency(types::VoiceIndex(index)),
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
				index++;
			}

			outputs[OUTPUT_OUTPUT].setVoltage(output * 5.f);
		}
		
		if (voiceInFlight) 
		{
			outputs[GATE_OUTPUT].setVoltage(0.f);
		} else 
		{
			outputs[GATE_OUTPUT].setVoltage(10.f);
		}

		bool fall = this->triggerPulse.process(args.sampleTime);
		outputs[TRIGGER_OUTPUT].setVoltage(fall ? 10.f : 0.f);

		lights[RESET_LIGHT].setSmoothBrightness(reset, args.sampleTime);
	}


	float getValueFromInputCombo(const ParamId param_id, const InputId input_id, const ParamId trim_id) 
	{
		const float param = params[param_id].getValue();
		const float voltage = inputs[input_id].getVoltage();
		const float trim = params[trim_id].getValue();
		return param + voltage / 10.f * trim;
	}


	types::FrequencyTableIndex getFrequencyTableIndexFrom1VOct()  
	{
		const float voct_voltage = inputs[_1VOCT_INPUT].getVoltage();
		return types::FrequencyTableIndex(((voct_voltage - (int)voct_voltage) / 0.083f) + 0.5f);
	}


	types::FrequencyTableIndex getFrequencyTableIndexFromTargetParam()  
	{
		const float target = params[TARGET_PARAM].getValue();
		const float target_voltage = inputs[TARGET_INPUT].getVoltage();
		const float target_trim = params[TARGET_TRIM_PARAM].getValue();
		return types::FrequencyTableIndex(target + (((target_voltage * target_trim)  / 10.f) * 11));
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


	std::string currentRootNote() const {
		constexpr size_t NUM_NOTES = 12;
		const std::string notes[NUM_NOTES] = {
			"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
		};
		return notes[voiceFrequencies.getCurrentIndex() % NUM_NOTES];
	}
};


struct Deepnote_rackWidget : ModuleWidget {
	Deepnote_rackWidget(Deepnote_rack* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/deepnote-rack.svg")));

		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		
		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.619, 27.1)), module, Deepnote_rack::DETUNE_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.937, 27.1)), module, Deepnote_rack::DETUNE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.619, 48.8)), module, Deepnote_rack::TARGET_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.937, 48.8)), module, Deepnote_rack::TARGET_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.619, 70.721)), module, Deepnote_rack::RATE_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.937, 70.721)), module, Deepnote_rack::RATE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(14.436, 83.667)), module, Deepnote_rack::CP1_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.363, 83.667)), module, Deepnote_rack::CP2_PARAM));

		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(25.91, 94.501)), module, Deepnote_rack::RESET_PARAM, Deepnote_rack::RESET_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 27.1)), module, Deepnote_rack::DETUNE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 48.8)), module, Deepnote_rack::TARGET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 59.729)), module, Deepnote_rack::_1VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 70.721)), module, Deepnote_rack::RATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.269, 94.501)), module, Deepnote_rack::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(11.106, 110.769)), module, Deepnote_rack::TRIGGER_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.715, 110.769)), module, Deepnote_rack::OUTPUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25.91, 110.977)), module, Deepnote_rack::GATE_OUTPUT));

		RootNoteDisplay<Deepnote_rack>* rootDisplay = createWidget<RootNoteDisplay<Deepnote_rack>>(mm2px(Vec(20.0, 34.0)));
		rootDisplay->box.size = mm2px(Vec(12.00, 9.00));
		rootDisplay->module = module;
		addChild(rootDisplay);

		CurveDisplay<Deepnote_rack>* curveDisplay = createWidget<CurveDisplay<Deepnote_rack>>(mm2px(Vec(20.0, 79.0)));
		curveDisplay->box.size = mm2px(Vec(12.00, 9.00));
		curveDisplay->module = module;
		addChild(curveDisplay);


	}
};


Model* modelDeepnote_rack = createModel<Deepnote_rack, Deepnote_rackWidget>("deepnote-rack");
