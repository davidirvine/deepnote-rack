#include "plugin.hpp"
#include "voice/deepnotevoice.hpp"
#include "voice/frequencytable.hpp"
#include "ranges/range.hpp"
#include "logger.hpp"
#include <random>
#include <string>
#include <vector>

template <class TModule>
struct RootNoteDisplay : LedDisplay
{
	TModule *module;

	void drawLayer(const DrawArgs &args, int layer) override
	{
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));

		if (layer == 1)
		{
			const std::string root_note_string = module ? module->current_root_note() : "";
			std::string font_path = asset::system("res/fonts/ShareTechMono-Regular.ttf");
			std::shared_ptr<Font> font = APP->window->loadFont(font_path);
			if (!font)
				return;
			nvgFontSize(args.vg, 24);
			nvgFontFaceId(args.vg, font->handle);
			nvgFillColor(args.vg, SCHEME_BLUE);
			nvgText(args.vg, 4.0, 20.0, root_note_string.c_str(), NULL);
		}

		nvgResetScissor(args.vg);
		LedDisplay::drawLayer(args, layer);
	}
};

template <class TModule>
struct CurveDisplay : LedDisplay
{
	TModule *module;

	void drawLayer(const DrawArgs &args, int layer) override
	{
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));

		if (layer == 1)
		{
			nvgBeginPath(args.vg);

			const auto shaper = deepnote::BezierUnitShaper(
				deepnote::nt::ControlPoint1(module ? module->params[TModule::CP1_PARAM].getValue() : 0.8f),
				deepnote::nt::ControlPoint2(module ? module->params[TModule::CP2_PARAM].getValue() : 0.5f));

			for (int x = 0; x < box.size.x; x++)
			{
				const float y = shaper(x * (1.f / box.size.x));
				if (x == 0)
				{
					nvgMoveTo(args.vg, x, box.size.y - (y * box.size.y));
				}
				else
				{
					nvgLineTo(args.vg, x, box.size.y - (y * box.size.y));
				}
			}

			nvgLineCap(args.vg, NVG_ROUND);
			nvgMiterLimit(args.vg, 2.f);
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStrokeColor(args.vg, SCHEME_BLUE);
			nvgStroke(args.vg);
		}

		nvgResetScissor(args.vg);
		LedDisplay::drawLayer(args, layer);
	}
};

struct StdLibRandomFloatGenerator
{
	//  This isn't the fastest random number generator but we don't require a fast one
	float operator()(float low, float high) const
	{
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_real_distribution<> dis(low, high);
		return dis(gen);
	}
};

struct RackTraceType
{
	void operator()(const deepnote::TraceValues &values) const
	{
		INFO("%.4f, %.4f, %d, %d, %.4f, %.4f, %.4f, %.4f, %.4f",
			 values.start_freq,
			 values.target_freq,
			 values.in_state,
			 values.out_state,
			 values.animation_lfo_value,
			 values.shaped_animation_value,
			 values.animation_freq,
			 values.current_frequency,
			 values.osc_value);
	}
};

namespace types = deepnote::nt;

using DuoVoiceType = deepnote::DeepnoteVoice<2>;
using TrioVoiceType = deepnote::DeepnoteVoice<3>;

const types::OscillatorFrequencyRange START_FREQ_RANGE(deepnote::Range(types::RangeLow(200.f), types::RangeHigh(400.f)));
const deepnote::Range ANIMATION_RATE_RANGE{types::RangeLow(0.05f), types::RangeHigh(1.5f)};

struct DeepnoteRack : Module
{

	TrioVoiceType trio_voices[deepnote::NUM_TRIO_VOICES];
	DuoVoiceType duo_voices[deepnote::NUM_DUO_VOICES];
	deepnote::FrequencyTable voice_frequencies;
	dsp::PulseGenerator trigger_pulse;
	dsp::SchmittTrigger reset_schmitt;

	enum ParamId
	{
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
	enum InputId
	{
		DETUNE_INPUT,
		TARGET_INPUT,
		_1VOCT_INPUT,
		RATE_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId
	{
		TRIGGER_OUTPUT,
		OUTPUT_OUTPUT,
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId
	{
		RESET_LIGHT,
		LIGHTS_LEN
	};

	DeepnoteRack()
	{
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
		voice_frequencies.initialize(START_FREQ_RANGE, random);

		for (auto &voice : trio_voices)
		{
			voice.init(
				voice_frequencies.get_frequency(types::VoiceIndex(index++)),
				types::SampleRate(sample_rate),
				types::OscillatorFrequency(random(ANIMATION_RATE_RANGE.get_low().get(), ANIMATION_RATE_RANGE.get_high().get())),
				random);
		}

		for (auto &voice : duo_voices)
		{
			voice.init(
				voice_frequencies.get_frequency(types::VoiceIndex(index++)),
				types::SampleRate(sample_rate),
				types::OscillatorFrequency(random(ANIMATION_RATE_RANGE.get_low().get(), ANIMATION_RATE_RANGE.get_high().get())),
				random);
		}
	}

	//
	//	TODO: add handlers for things like onReset
	//

	void process(const ProcessArgs &args) override
	{
		const auto detune = types::DetuneHz(get_value_from_input_combo(DETUNE_PARAM, DETUNE_INPUT, DETUNE_TRIM_PARAM));
		const auto animation_multiplier = types::AnimationMultiplier(get_value_from_input_combo(RATE_PARAM, RATE_INPUT, RATE_TRIM_PARAM));
		const auto frequency_table_index = inputs[_1VOCT_INPUT].isConnected() ? get_frequency_table_index_from_1VOct() : get_frequency_table_index_from_target_param();
		const auto cp1 = types::ControlPoint1(params[CP1_PARAM].getValue());
		const auto cp2 = types::ControlPoint2(params[CP2_PARAM].getValue());
		const deepnote::NoopTrace trace_functor;
		// const RackTraceType trace_functor;
		auto output{0.f};
		auto index{0};
		bool voice_in_flight{false};

		// Handle reset button and trigger
		reset_schmitt.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		bool reset = reset_schmitt.isHigh() || params[RESET_PARAM].getValue() > 0.f;

		const auto index_changed = voice_frequencies.set_current_index(frequency_table_index);

		if (outputs[OUTPUT_OUTPUT].isConnected())
		{

			for (auto &voice : trio_voices)
			{
				const auto is_at_target_pre = voice.is_at_target();

				if (reset)
				{
					voice.reset_start_frequency(voice_frequencies.get_reset_frequency(types::VoiceIndex(index)));
				}
				output += process_voice(
					voice,
					detune,
					index_changed,
					voice_frequencies.get_frequency(types::VoiceIndex(index)),
					animation_multiplier,
					cp1,
					cp2,
					trace_functor);

				if (!voice.is_at_target())
				{
					voice_in_flight = true;
				}

				if (voice.is_at_target() && !is_at_target_pre)
				{
					this->trigger_pulse.trigger(1e-3f);
				}
				index++;
			}
			for (auto &voice : duo_voices)
			{
				if (reset)
				{
					voice.reset_start_frequency(voice_frequencies.get_reset_frequency(types::VoiceIndex(index)));
				}

				const auto is_at_target_pre = voice.is_at_target();
				output += process_voice(
					voice,
					detune,
					index_changed,
					voice_frequencies.get_frequency(types::VoiceIndex(index)),
					animation_multiplier,
					cp1,
					cp2,
					trace_functor);

				//	Gate is high when all voices are at target: !voiceInFlight
				if (!voice.is_at_target())
				{
					voice_in_flight = true;
				}

				//	the trigger fires when 1 or more voices arrive at target
				if (voice.is_at_target() && !is_at_target_pre)
				{
					this->trigger_pulse.trigger(1e-3f);
				}
				index++;
			}

			outputs[OUTPUT_OUTPUT].setVoltage(output * 5.f);
		}

		if (voice_in_flight)
		{
			outputs[GATE_OUTPUT].setVoltage(0.f);
		}
		else
		{
			outputs[GATE_OUTPUT].setVoltage(10.f);
		}

		bool fall = this->trigger_pulse.process(args.sampleTime);
		outputs[TRIGGER_OUTPUT].setVoltage(fall ? 10.f : 0.f);

		lights[RESET_LIGHT].setSmoothBrightness(reset, args.sampleTime);
	}

	float get_value_from_input_combo(const ParamId param_id, const InputId input_id, const ParamId trim_id)
	{
		const float param = params[param_id].getValue();
		const float voltage = inputs[input_id].getVoltage();
		const float trim = params[trim_id].getValue();
		return param + voltage / 10.f * trim;
	}

	types::FrequencyTableIndex get_frequency_table_index_from_1VOct()
	{
		const float voct_voltage = inputs[_1VOCT_INPUT].getVoltage();
		return types::FrequencyTableIndex(((voct_voltage - (int)voct_voltage) / 0.083f) + 0.5f);
	}

	types::FrequencyTableIndex get_frequency_table_index_from_target_param()
	{
		const float target = params[TARGET_PARAM].getValue();
		const float target_voltage = inputs[TARGET_INPUT].getVoltage();
		const float target_trim = params[TARGET_TRIM_PARAM].getValue();
		return types::FrequencyTableIndex(target + (((target_voltage * target_trim) / 10.f) * 11));
	}

	template <typename VoiceType, typename TraceFunctor>
	float process_voice(VoiceType &voice, const types::DetuneHz &detune, const bool index_changed,
						const types::OscillatorFrequency &target_frequency, const types::AnimationMultiplier &animation_multiplier,
						const types::ControlPoint1 &cp1, const types::ControlPoint2 &cp2, const TraceFunctor &trace_functor) const
	{
		voice.compute_detune(detune);
		if (index_changed)
		{
			voice.set_target_frequency(target_frequency);
		}
		return voice.process(animation_multiplier, cp1, cp2, trace_functor);
	}

	std::string current_root_note() const
	{
		constexpr size_t NUM_NOTES = 12;
		const std::string notes[NUM_NOTES] = {
			"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		return notes[voice_frequencies.get_current_index() % NUM_NOTES];
	}
};

struct DeepnoteRackWidget : ModuleWidget
{
	DeepnoteRackWidget(DeepnoteRack *module)
	{
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/deepnote-rack.svg")));

		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.619, 27.1)), module, DeepnoteRack::DETUNE_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.937, 27.1)), module, DeepnoteRack::DETUNE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.619, 48.8)), module, DeepnoteRack::TARGET_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.937, 48.8)), module, DeepnoteRack::TARGET_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.619, 70.721)), module, DeepnoteRack::RATE_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.937, 70.721)), module, DeepnoteRack::RATE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(14.436, 83.667)), module, DeepnoteRack::CP1_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.363, 83.667)), module, DeepnoteRack::CP2_PARAM));

		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(25.91, 94.501)), module, DeepnoteRack::RESET_PARAM, DeepnoteRack::RESET_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 27.1)), module, DeepnoteRack::DETUNE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 48.8)), module, DeepnoteRack::TARGET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 59.729)), module, DeepnoteRack::_1VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 70.721)), module, DeepnoteRack::RATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.269, 94.501)), module, DeepnoteRack::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(11.106, 110.769)), module, DeepnoteRack::TRIGGER_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.715, 110.769)), module, DeepnoteRack::OUTPUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25.91, 110.977)), module, DeepnoteRack::GATE_OUTPUT));

		RootNoteDisplay<DeepnoteRack> *root_display = createWidget<RootNoteDisplay<DeepnoteRack>>(mm2px(Vec(20.0, 34.0)));
		root_display->box.size = mm2px(Vec(12.00, 9.00));
		root_display->module = module;
		addChild(root_display);

		CurveDisplay<DeepnoteRack> *curve_display = createWidget<CurveDisplay<DeepnoteRack>>(mm2px(Vec(20.0, 79.0)));
		curve_display->box.size = mm2px(Vec(12.00, 9.00));
		curve_display->module = module;
		addChild(curve_display);
	}
};

Model *modelDeepnoteRack = createModel<DeepnoteRack, DeepnoteRackWidget>("deepnote-rack");
