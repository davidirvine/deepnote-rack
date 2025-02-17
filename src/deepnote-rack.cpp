#include "plugin.hpp"
#include "voice/deepnotevoice.hpp"
#include "voice/frequencytable.hpp"
#include "ranges/range.hpp"
#include "logger.hpp"
#include <array>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <sstream>
#include <vector>

namespace nt = deepnote::nt;

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


float get_random_float(float low, float high)
{
	// This isn't the fastest random number generator
	// but we don't require a fast one
	static std::random_device rd;
	static std::mt19937 gen(rd());
	std::uniform_real_distribution<> dis(low, high);
	return dis(gen);
}


struct RackTraceType
{
	RackTraceType() 
	{
		oss << std::fixed << std::setprecision(4);
	}

	template <typename T, typename... Args>
	void operator()(const T &first, Args &&... rest)
	{
		oss << first << ", ";
		(*this)(rest...);
	}
	
	template <typename T>
	void operator()(const T &value)
	{
		oss << value << std::endl;
		INFO(oss.str());
		oss.str("");
		oss.clear();
	}

private:
	std::ostringstream oss;
};


// Voices
const int NUM_OSC_DUO = 2;
const int NUM_DUO_VOICES = 5;

const int NUM_OSC_TRIO = 3;
const int NUM_TRIO_VOICES = 4;

using DuoVoiceType = deepnote::DeepnoteVoice<NUM_OSC_DUO>;
std::array<DuoVoiceType, NUM_DUO_VOICES> duo_voices;

using TrioVoiceType = deepnote::DeepnoteVoice<NUM_OSC_TRIO>;
std::array<TrioVoiceType, NUM_TRIO_VOICES> trio_voices;

const int FREQ_TABLE_WIDTH = NUM_TRIO_VOICES + NUM_DUO_VOICES;
const int FREQ_TABLE_HEIGHT = 13;

// Generate a random frequency with in a range of frequencies
// This function is stored in the the first row of the frequency table allowing
// the drone to start with a new random "chord" each time it is started.
deepnote::FrequencyFunc random_start_freq = []()
{
	const auto low = nt::RangeLow(200.f);
	const auto high = nt::RangeHigh(400.f);
	return nt::OscillatorFrequency(get_random_float(low.get(), high.get()));
};

// Return the fucntion that returns a fixed frequency
deepnote::FrequencyFunc freq(const float f)
{
	return deepnote::FrequencyFunc([f]()
								  { return nt::OscillatorFrequency(f); });
};

// The rows contain the frequencies for the target "chord" to be played by the drone.
// There is one frequency per voice in the drone in each column.
// The first row will contain a random start "chord".
// Subsequent rows will contain the the chord rooted at all 12 notes in the
// chromatic scale.
//
// 1 = C, 2 = C#, 3 = D, 4 = D#, 5 = E, 6 = F, 7 = F#, 8 = G, 9 = G#, 10 = A, 11 = A#, 12 = B
//
// See tools/freqtable-builder.py for the generation of the values in the table
deepnote::FrequencyTable<FREQ_TABLE_HEIGHT, FREQ_TABLE_WIDTH> target_freq_table({{
	{random_start_freq, random_start_freq, random_start_freq, random_start_freq, random_start_freq, random_start_freq, random_start_freq, random_start_freq, random_start_freq},
	{freq(1244.51f), freq(1046.50f), freq(587.33f), freq(523.25f), freq(392.00f), freq(130.81f), freq(98.00f), freq(65.41f), freq(32.70f)},
	{freq(1318.51f), freq(1108.73f), freq(622.25f), freq(554.37f), freq(415.30f), freq(138.59f), freq(103.83f), freq(69.30f), freq(34.65f)},
	{freq(1396.91f), freq(1174.66f), freq(659.26f), freq(587.33f), freq(440.00f), freq(146.83f), freq(110.00f), freq(73.42f), freq(36.71f)},
	{freq(1479.98f), freq(1244.51f), freq(698.46f), freq(622.25f), freq(466.16f), freq(155.56f), freq(116.54f), freq(77.78f), freq(38.89f)},
	{freq(1567.98f), freq(1318.51f), freq(739.99f), freq(659.26f), freq(493.88f), freq(164.81f), freq(123.47f), freq(82.41f), freq(41.20f)},
	{freq(1661.22f), freq(1396.91f), freq(783.99f), freq(698.46f), freq(523.25f), freq(174.61f), freq(130.81f), freq(87.31f), freq(43.65f)},
	{freq(1760.00f), freq(1479.98f), freq(830.61f), freq(739.99f), freq(554.37f), freq(185.00f), freq(138.59f), freq(92.50f), freq(46.25f)},
	{freq(1864.66f), freq(1567.98f), freq(880.00f), freq(783.99f), freq(587.33f), freq(196.00f), freq(146.83f), freq(98.00f), freq(49.00f)},
	{freq(1975.53f), freq(1661.22f), freq(932.33f), freq(830.61f), freq(622.25f), freq(207.65f), freq(155.56f), freq(103.83f), freq(51.91f)},
	{freq(2093.00f), freq(1760.00f), freq(987.77f), freq(880.00f), freq(659.26f), freq(220.00f), freq(164.81f), freq(110.00f), freq(55.00f)},
	{freq(2217.46f), freq(1864.66f), freq(1046.50f), freq(932.33f), freq(698.46f), freq(233.08f), freq(174.61f), freq(116.54f), freq(58.27f)},
	{freq(2349.32f), freq(1975.53f), freq(1108.73f), freq(987.77f), freq(739.99f), freq(246.94f), freq(185.00f), freq(123.47f), freq(61.74f)}}});

// Generate a random animation frequency
nt::OscillatorFrequency random_animation_freq()
{
	const auto low = nt::RangeLow(0.5f);
	const auto high = nt::RangeHigh(1.5f);
	return nt::OscillatorFrequency(get_random_float(low.get(), high.get()));
}



struct DeepnoteRack : Module
{
	std::array<TrioVoiceType, NUM_TRIO_VOICES> trio_voices;
	std::array<DuoVoiceType, NUM_DUO_VOICES> duo_voices;
	dsp::PulseGenerator trigger_pulse;
	dsp::SchmittTrigger reset_schmitt;
	nt::FrequencyTableIndex frequency_table_index{0};

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

		auto voice_index{0};
		const auto start_table_index{0};

		for (auto &voice : trio_voices)
		{
			voice.init(
				target_freq_table.get(nt::FrequencyTableIndex(start_table_index), nt::VoiceIndex(voice_index++)),
				nt::SampleRate(sample_rate),
				random_animation_freq());
		}

		for (auto &voice : duo_voices)
		{
			voice.init(
				target_freq_table.get(nt::FrequencyTableIndex(start_table_index), nt::VoiceIndex(voice_index++)),
				nt::SampleRate(sample_rate),
				random_animation_freq());
		}
	}

	//
	//	TODO: add handlers for things like onReset
	//

	void process(const ProcessArgs &args) override
	{


		const auto detune = nt::DetuneHz(get_value_from_input_combo(DETUNE_PARAM, DETUNE_INPUT, DETUNE_TRIM_PARAM));
		const auto animation_multiplier = nt::AnimationMultiplier(get_value_from_input_combo(RATE_PARAM, RATE_INPUT, RATE_TRIM_PARAM));
		const auto new_freq_table_index = inputs[_1VOCT_INPUT].isConnected() ? 
			get_frequency_table_index_from_1VOct() : get_frequency_table_index_from_target_param();
		const auto cp1 = nt::ControlPoint1(params[CP1_PARAM].getValue());
		const auto cp2 = nt::ControlPoint2(params[CP2_PARAM].getValue());
		const deepnote::NoopTrace trace_functor;
		// const RackTraceType trace_functor;
		auto output{0.f};
		auto index{0};
		bool voice_in_flight{false};

		// Handle reset button and trigger
		reset_schmitt.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		bool reset = reset_schmitt.isHigh() || params[RESET_PARAM].getValue() > 0.f;

		const auto index_changed = (frequency_table_index.get() != new_freq_table_index.get());
		frequency_table_index = new_freq_table_index;

		if (outputs[OUTPUT_OUTPUT].isConnected())
		{

			for (auto &voice : trio_voices)
			{
				const auto is_at_target_pre = voice.is_at_target();

				if (reset)
				{
					voice.set_start_frequency(target_freq_table.get(nt::FrequencyTableIndex(0), nt::VoiceIndex(index)));
				}
				output += process_voice(
					voice,
					detune,
					index_changed,
					target_freq_table.get(frequency_table_index, nt::VoiceIndex(index)),
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
					voice.set_start_frequency(target_freq_table.get(nt::FrequencyTableIndex(0), nt::VoiceIndex(index)));
				}

				const auto is_at_target_pre = voice.is_at_target();
				output += process_voice(
					voice,
					detune,
					index_changed,
					target_freq_table.get(frequency_table_index, nt::VoiceIndex(index)),
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

	nt::FrequencyTableIndex get_frequency_table_index_from_1VOct()
	{
		const float voct_voltage = inputs[_1VOCT_INPUT].getVoltage();
		return nt::FrequencyTableIndex(((voct_voltage - (int)voct_voltage) / 0.083f) + 0.5f);
	}

	nt::FrequencyTableIndex get_frequency_table_index_from_target_param()
	{
		const float target = params[TARGET_PARAM].getValue();
		const float target_voltage = inputs[TARGET_INPUT].getVoltage();
		const float target_trim = params[TARGET_TRIM_PARAM].getValue();
		return nt::FrequencyTableIndex(target + (((target_voltage * target_trim) / 10.f) * 11));
	}

	template <typename VoiceType, typename TraceFunctor>
	float process_voice(VoiceType &voice, const nt::DetuneHz &detune, const bool index_changed,
						const nt::OscillatorFrequency &target_frequency, const nt::AnimationMultiplier &animation_multiplier,
						const nt::ControlPoint1 &cp1, const nt::ControlPoint2 &cp2, const TraceFunctor &trace_functor) const
	{
		voice.set_detune(detune);
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
		return notes[frequency_table_index.get() % NUM_NOTES];
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

		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.177, 33.641)), module, DeepnoteRack::TARGET_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.496, 33.641)), module, DeepnoteRack::TARGET_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.177, 52.208)), module, DeepnoteRack::DETUNE_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.496, 52.208)), module, DeepnoteRack::DETUNE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(20.619, 70.721)), module, DeepnoteRack::RATE_TRIM_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(41.937, 70.721)), module, DeepnoteRack::RATE_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(14.436, 83.667)), module, DeepnoteRack::CP1_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(37.363, 83.667)), module, DeepnoteRack::CP2_PARAM));

		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(25.91, 94.501)), module, DeepnoteRack::RESET_PARAM, DeepnoteRack::RESET_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.181, 28.349)), module, DeepnoteRack::TARGET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.181, 42.982)), module, DeepnoteRack::_1VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.181, 57.499)), module, DeepnoteRack::DETUNE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.623, 70.721)), module, DeepnoteRack::RATE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.106, 94.501)), module, DeepnoteRack::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(11.106, 110.769)), module, DeepnoteRack::TRIGGER_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.715, 110.769)), module, DeepnoteRack::OUTPUT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25.91, 110.977)), module, DeepnoteRack::GATE_OUTPUT));

		RootNoteDisplay<DeepnoteRack> *root_display = createWidget<RootNoteDisplay<DeepnoteRack>>(mm2px(Vec(20.0, 18.0)));
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
