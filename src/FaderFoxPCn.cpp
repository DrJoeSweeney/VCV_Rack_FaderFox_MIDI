#include "plugin.hpp"
#include <cmath>
#include <cstdint>

using namespace rack;

// Grid limits
static const int MAX_ROWS = 8;
static const int MAX_COLS = 16;
static const int MAX_PORTS = MAX_ROWS * MAX_COLS;

// Panel geometry.
// Rack 2 requires all modules to be exactly 3U tall (380 px); width is free.
static const float RACK_HEIGHT = 380.f;
static const float HEADER_H = 66.f;
static const float ROW_LABEL_W = 20.f;
static const float MARGIN = 12.f;
static const float BOTTOM_H = 26.f;  // column labels + bottom margin
static const float GRID_AREA = RACK_HEIGHT - HEADER_H - BOTTOM_H;  // 288 px of grid space
static const float GRID_X = MARGIN + ROW_LABEL_W;

// Square cell size: 48 px until the grid no longer fits, then shrink to fit the row count.
static float cellSize(int rows) {
	return std::min(48.f, GRID_AREA / rows);
}
static float gridY(int rows) {
	float cell = cellSize(rows);
	return HEADER_H + (GRID_AREA - rows * cell) / 2.f;
}
static float panelWidth(int rows, int cols) {
	return GRID_X + cols * cellSize(rows) + MARGIN;
}

// Voltage ranges (b). Index 0 = default (0-10 V).
static const int NUM_RANGES = 6;
static const float RANGE_MIN[NUM_RANGES] = {0.f, 0.f, 0.f, -10.f, -5.f, -1.f};
static const float RANGE_MAX[NUM_RANGES] = {10.f, 5.f, 1.f, 10.f, 5.f, 1.f};
static const char* RANGE_NAMES[NUM_RANGES] = {"0-10 V", "0-5 V", "0-1 V", "+/-10 V", "+/-5 V", "+/-1 V"};

// Colors
static NVGcolor colMix(NVGcolor a, NVGcolor b, float t) {
	t = clamp(t, 0.f, 1.f);
	return nvgRGB(
		(int)(a.r * 255.f + (b.r - a.r) * 255.f * t),
		(int)(a.g * 255.f + (b.g - a.g) * 255.f * t),
		(int)(a.b * 255.f + (b.b - a.b) * 255.f * t));
}

static std::shared_ptr<Font> fontRegular;
static std::shared_ptr<Font> fontTitle;
static std::shared_ptr<Font> fontLed;
static std::shared_ptr<Font> fontHand;

static void loadFonts() {
	if (!fontRegular)
		fontRegular = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
	if (!fontTitle)
		fontTitle = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
	if (!fontLed)
		fontLed = APP->window->loadFont(asset::system("res/fonts/DSEG7ClassicMini-Bold.ttf"));
	if (!fontHand)
		fontHand = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/ShadowsIntoLight.ttf"));
}

// The module
struct FaderFoxPCnModule : Module {
	midi::InputQueue midiInput;

	int rows = 6;
	int cols = 12;
	int channel = 0;        // 0 = omni, 1..16 = filter (c)
	int rangeDefault = 0;   // range index given to newly learned ports
	int learnPort = -1;     // port currently in learn mode (d)

	int8_t mapping[MAX_PORTS];   // MIDI CC per port, -1 = blank
	uint8_t rangeIdx[MAX_PORTS]; // voltage range per port
	std::string label[MAX_PORTS]; // optional handwritten label per port (replaces CC number)
	float value[MAX_PORTS];     // smoothed CC value 0..1
	float target[MAX_PORTS];
	double changedAt[MAX_PORTS];  // last value change (activity glow)
	double mappedAt[MAX_PORTS];   // learn-map time (green flash)

	// Red LED display state (f)
	int lastCC = -1;
	int lastValue = 0;
	double lastActivity = -100.0;

	FaderFoxPCnModule() {
		config(0, 0, MAX_PORTS, 0);
		for (int i = 0; i < MAX_PORTS; i++) {
			mapping[i] = -1;
			rangeIdx[i] = 0;
			value[i] = 0.f;
			target[i] = 0.f;
			changedAt[i] = -100.0;
			mappedAt[i] = -100.0;
			configOutput(i, "Unmapped (click to learn)");
		}
		// Default to the first available MIDI driver/device so the module works out of the box
		std::vector<int> driverIds = midi::getDriverIds();
		if (!driverIds.empty())
			midiInput.setDriverId(driverIds.front());
		midiInput.setDeviceId(0);
	}

	void unmapPort(int p) {
		if (p < 0 || p >= MAX_PORTS || mapping[p] < 0)
			return;
		mapping[p] = -1;
		value[p] = 0.f;
		target[p] = 0.f;
	}

	void clearAll() {
		for (int i = 0; i < MAX_PORTS; i++)
			unmapPort(i);
		learnPort = -1;
	}

	void setRangeAll(int r) {
		rangeDefault = r;
		for (int i = 0; i < rows * cols; i++)
			rangeIdx[i] = (uint8_t) r;
	}

	void setRangeRow(int row, int r) {
		for (int c = 0; c < cols; c++)
			rangeIdx[row * cols + c] = (uint8_t) r;
	}

	void processMessage(const midi::Message& msg) {
		if (msg.getStatus() != 0xb)
			return;  // CC only
		int ch = msg.getChannel();
		if (channel != 0 && ch != channel)
			return;
		int cc = msg.getNote();
		int v = msg.getValue();
		if (cc < 0 || cc > 127)
			return;

		double now = system::getTime();
		lastCC = cc;
		lastValue = v;
		lastActivity = now;

		if (learnPort >= 0 && learnPort < rows * cols) {
			// If this CC is already mapped elsewhere, move it to the newly selected port (d)
			for (int q = 0; q < rows * cols; q++) {
				if (q != learnPort && mapping[q] == cc)
					unmapPort(q);
			}
			mapping[learnPort] = (int8_t) cc;
			rangeIdx[learnPort] = (uint8_t) rangeDefault;
			target[learnPort] = v / 127.f;
			changedAt[learnPort] = now;
			mappedAt[learnPort] = now;
			learnPort = -1;
		}
		else {
			for (int p = 0; p < rows * cols; p++) {
				if (mapping[p] == cc) {
					target[p] = v / 127.f;
					changedAt[p] = now;
				}
			}
		}
	}

	void process(const ProcessArgs& args) override {
		midi::Message msg;
		while (midiInput.tryPop(&msg, args.frame)) {
			processMessage(msg);
		}

		float c = 1.f - std::exp(-args.sampleTime / 0.002f);  // ~2 ms smoothing
		int active = rows * cols;
		for (int i = 0; i < MAX_PORTS; i++) {
			if (i < active && mapping[i] >= 0) {
				float minV = RANGE_MIN[rangeIdx[i]];
				float maxV = RANGE_MAX[rangeIdx[i]];
				value[i] += (target[i] - value[i]) * c;
				if (std::fabs(target[i] - value[i]) < 1e-4f)
					value[i] = target[i];
				outputs[i].setVoltage(minV + value[i] * (maxV - minV));
			}
			else {
				value[i] = 0.f;
				outputs[i].setVoltage(0.f);
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "rows", json_integer(rows));
		json_object_set_new(rootJ, "cols", json_integer(cols));
		json_object_set_new(rootJ, "channel", json_integer(channel));
		json_object_set_new(rootJ, "rangeDefault", json_integer(rangeDefault));
		json_t* mapJ = json_array();
		json_t* rngJ = json_array();
		for (int i = 0; i < MAX_PORTS; i++) {
			json_array_append_new(mapJ, json_integer((int) mapping[i]));
			json_array_append_new(rngJ, json_integer((int) rangeIdx[i]));
		}
		json_object_set_new(rootJ, "mapping", mapJ);
		json_object_set_new(rootJ, "ranges", rngJ);
		json_t* lblJ = json_array();
		for (int i = 0; i < MAX_PORTS; i++)
			json_array_append_new(lblJ, json_string(label[i].c_str()));
		json_object_set_new(rootJ, "labels", lblJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* j;
		if ((j = json_object_get(rootJ, "rows")))
			rows = clamp((int) json_integer_value(j), 1, MAX_ROWS);
		if ((j = json_object_get(rootJ, "cols")))
			cols = clamp((int) json_integer_value(j), 1, MAX_COLS);
		if ((j = json_object_get(rootJ, "channel")))
			channel = clamp((int) json_integer_value(j), 0, 16);
		if ((j = json_object_get(rootJ, "mapping"))) {
			size_t n = json_array_size(j);
			for (size_t i = 0; i < n && i < MAX_PORTS; i++) {
				int cc = (int) json_integer_value(json_array_get(j, i));
				mapping[i] = (cc >= 0 && cc <= 127) ? (int8_t) cc : (int8_t) -1;
			}
		}
		// Per-port ranges
		json_t* rangesJ = json_object_get(rootJ, "ranges");
		if (rangesJ) {
			size_t n = json_array_size(rangesJ);
			for (size_t i = 0; i < n && i < MAX_PORTS; i++)
				rangeIdx[i] = (uint8_t) clamp((int) json_integer_value(json_array_get(rangesJ, i)), 0, NUM_RANGES - 1);
		}
		if ((j = json_object_get(rootJ, "rangeDefault"))) {
			rangeDefault = clamp((int) json_integer_value(j), 0, NUM_RANGES - 1);
		}
		else if ((j = json_object_get(rootJ, "range"))) {
			// Legacy: global range from before per-port ranges (old table indices)
			static const int oldToNew[5] = {2, 1, 0, 4, 3};
			rangeDefault = oldToNew[clamp((int) json_integer_value(j), 0, 4)];
			if (!rangesJ)
				setRangeAll(rangeDefault);
		}
		if ((j = json_object_get(rootJ, "labels"))) {
			size_t n = json_array_size(j);
			for (size_t i = 0; i < n && i < MAX_PORTS; i++) {
				const char* s = json_string_value(json_array_get(j, i));
				label[i] = s ? s : "";
			}
		}
		learnPort = -1;
	}
};

// A single encoder/jack cell: one output port with FaderFox-style looks (a, d, f)
struct FaderFoxPort : PortWidget {
	FaderFoxPCnModule* mod;
	int idx;

	FaderFoxPort(FaderFoxPCnModule* mod, int idx) {
		this->mod = mod;
		this->idx = idx;
		box.size = math::Vec(48.f, 48.f);
		module = mod;  // required by the cable-drawing machinery
		type = engine::Port::OUTPUT;
		portId = idx;
	}

	bool isMapped() {
		return mod && mod->mapping[idx] >= 0;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			// Never touch mappings; let it bubble up to open the module context menu
			return;
		}
		if (mod && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && !isMapped()) {
			// Blank ports: click toggles MIDI learn (d)
			mod->learnPort = (mod->learnPort == idx) ? -1 : idx;
		}
		PortWidget::onButton(e);
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		// Double-click a mapped port to unlearn it
		if (mod && isMapped()) {
			mod->unmapPort(idx);
			e.consume(this);
		}
	}

	void onDragStart(const DragStartEvent& e) override {
		// Blank ports are inert: no cables can be dragged out of them
		if (!isMapped())
			return;
		PortWidget::onDragStart(e);
	}

	void onDragDrop(const DragDropEvent& e) override {
		// Blank ports reject cable drops
		if (!isMapped())
			return;
		PortWidget::onDragDrop(e);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		loadFonts();
		float cx = box.size.x * 0.5f;
		float cy = box.size.y * 0.5f;
		float s = box.size.x / 48.f;  // cells shrink when many rows are configured
		double now = system::getTime();

		bool mapped = mod && mod->mapping[idx] >= 0;
		bool learning = mod && mod->learnPort == idx;
		float v = mod ? mod->value[idx] : 0.f;
		float flash = mod ? clamp(1.f - (float) (now - mod->mappedAt[idx]) / 0.8f, 0.f, 1.f) : 0.f;
		float glow = mod ? clamp(1.f - (float) (now - mod->changedAt[idx]) / 0.3f, 0.f, 1.f) : 0.f;
		float pulse = 0.5f + 0.5f * std::sin(now * 7.5);

		// Accent color: yellow while learning, green flash right after mapping, white-grey when mapped
		NVGcolor accent = nvgRGB(0xd7, 0xdc, 0xe2);
		if (mapped)
			accent = colMix(accent, nvgRGB(0x35, 0xd4, 0x59), flash);
		if (learning)
			accent = colMix(nvgRGB(0x49, 0x4d, 0x54), nvgRGB(0xff, 0xd2, 0x3f), 0.35f + 0.65f * pulse);

		// Potentiometer travel: 7 o'clock to 5 o'clock, clockwise over the top (300 deg)
		const float ARC_START = 240.f;  // degrees, value = 0 (7 o'clock)
		const float ARC_SWEEP = 300.f;  // degrees of travel to value = 1 (5 o'clock)

		// Value arc (mapped only)
		if (mapped) {
			float minV = RANGE_MIN[mod->rangeIdx[idx]];
			float maxV = RANGE_MAX[mod->rangeIdx[idx]];
			float zeroV = (0.f - minV) / (maxV - minV);
			float arcR = 17.5f * s;

			// Track
			nvgBeginPath(vg);
			bool first = true;
			for (int seg = 0; seg <= 36; seg++) {
				float deg = ARC_START - ARC_SWEEP * seg / 36.f;
				float rad = deg * M_PI / 180.f;
				float x = cx + arcR * std::cos(rad);
				float y = cy - arcR * std::sin(rad);
				if (first) { nvgMoveTo(vg, x, y); first = false; } else { nvgLineTo(vg, x, y); }
			}
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 16));
			nvgStrokeWidth(vg, 2.5f * s);
			nvgLineCap(vg, NVG_ROUND);
			nvgStroke(vg);

			// Filled portion: from range start (or zero for bipolar) to value
			float start = (minV < 0.f) ? zeroV : 0.f;
			float end = v;
			if (std::fabs(end - start) > 0.002f) {
				nvgBeginPath(vg);
				first = true;
				int steps = std::max(2, (int) (36.f * std::fabs(end - start)));
				for (int seg = 0; seg <= steps; seg++) {
					float f = start + (end - start) * seg / steps;
					float deg = ARC_START - ARC_SWEEP * f;
					float rad = deg * M_PI / 180.f;
					float x = cx + arcR * std::cos(rad);
					float y = cy - arcR * std::sin(rad);
					if (first) { nvgMoveTo(vg, x, y); first = false; } else { nvgLineTo(vg, x, y); }
				}
				nvgStrokeColor(vg, nvgRGBAf(accent.r, accent.g, accent.b, 0.75f + 0.25f * glow));
				nvgStrokeWidth(vg, 2.5f * s);
				nvgLineCap(vg, NVG_ROUND);
				nvgStroke(vg);
			}
		}

		// Knob body
		float knobR = 15.f * s;
		NVGpaint body = nvgLinearGradient(vg, cx, cy - knobR, cx, cy + knobR,
			mapped ? nvgRGB(0x4d, 0x52, 0x5a) : nvgRGB(0x37, 0x3a, 0x3f),
			mapped ? nvgRGB(0x27, 0x2a, 0x2f) : nvgRGB(0x1f, 0x21, 0x25));
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, knobR);
		nvgFillPaint(vg, body);
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(14, 15, 17, 255));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);

		// Pointer
		if (mapped) {
			float deg = ARC_START - ARC_SWEEP * v;
			float rad = deg * M_PI / 180.f;
			float ca = std::cos(rad), sa = std::sin(rad);
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx + 6.5f * s * ca, cy - 6.5f * s * sa);
			nvgLineTo(vg, cx + 11.5f * s * ca, cy - 11.5f * s * sa);
			nvgStrokeColor(vg, accent);
			nvgStrokeWidth(vg, 2.2f * s);
			nvgLineCap(vg, NVG_ROUND);
			nvgStroke(vg);
		}

		// Learning edge glow
		if (learning) {
			nvgBeginPath(vg);
			nvgCircle(vg, cx, cy, knobR);
			nvgStrokeColor(vg, nvgRGBA(255, 210, 63, (int) (50 + 90 * pulse)));
			nvgStrokeWidth(vg, 1.5f);
			nvgStroke(vg);
		}

		// Jack socket (the "port plug")
		float jackR = 6.5f * s;
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, jackR);
		nvgFillColor(vg, accent);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, 4.9f * s);
		nvgFillColor(vg, nvgRGB(0x08, 0x09, 0x0b));
		nvgFill(vg);
		// Center dot: dull grey when blank, standard bright when mapped, pulsing when learning
		NVGcolor dot;
		if (learning)
			dot = nvgRGBA(0xff, 0xd2, 0x3f, (int) (140 + 115 * pulse));
		else if (mapped)
			dot = nvgRGB(0xe8, 0xeb, 0xef);
		else
			dot = nvgRGB(0x43, 0x47, 0x4e);
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, 1.9f * s);
		nvgFillColor(vg, dot);
		nvgFill(vg);

		// Handwritten label strip (replaces the CC number) or CC number label
		if (mod && !mod->label[idx].empty() && (fontHand || fontRegular)) {
			float sw = box.size.x - 6.f;
			float sh = 12.f * s;
			float sx = 3.f, sy = cy + 17.5f * s;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, sx, sy, sw, sh, 2.f * s);
			nvgFillColor(vg, nvgRGB(0xf1, 0xef, 0xe9));  // paper strip
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 70));
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);
			nvgScissor(vg, sx + 1.f, sy, sw - 2.f, sh);
			nvgFontFaceId(vg, fontHand ? fontHand->handle : fontRegular->handle);
			nvgFontSize(vg, 10.5f * s);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, nvgRGB(0x14, 0x12, 0x0e));
			nvgText(vg, sx + 2.5f, sy + sh * 0.55f, mod->label[idx].c_str(), NULL);
			nvgResetScissor(vg);
		}
		else if (mapped && fontRegular) {
			nvgFontFaceId(vg, fontRegular->handle);
			nvgFontSize(vg, 8.f * s);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGBA(0xc8, 0xce, 0xd5, (int) (180 + 75 * glow)));
			nvgText(vg, cx, cy + 20.5f * s, string::f("%d", mod->mapping[idx]).c_str(), NULL);
		}
	}
};

// Panel background: FaderFox-style chassis, title, red LED display (a, e, f)
struct FaderFoxBackground : Widget {
	FaderFoxPCnModule* mod;

	void drawDigitString(NVGcontext* vg, float x, float y, const char* str, float alpha) {
		if (!fontLed)
			return;
		nvgFontFaceId(vg, fontLed->handle);
		nvgFontSize(vg, 20.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		// ghost pass for glow
		nvgFontBlur(vg, 3.f);
		nvgFillColor(vg, nvgRGBA(0xff, 0x35, 0x20, (int) (90 * alpha)));
		nvgText(vg, x, y, str, NULL);
		nvgFontBlur(vg, 1.f);
		nvgFillColor(vg, nvgRGBA(0xff, 0x45, 0x2e, (int) (255 * alpha)));
		nvgText(vg, x, y, str, NULL);
		nvgFontBlur(vg, 0.f);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		loadFonts();
		float w = box.size.x;
		float h = box.size.y;
		int rows = mod ? mod->rows : 6;
		int cols = mod ? mod->cols : 12;
		float cell = cellSize(rows);
		float gy = gridY(rows);
		double now = system::getTime();

		// Chassis
		NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, h, nvgRGB(0x22, 0x24, 0x27), nvgRGB(0x15, 0x16, 0x19));
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, w, h);
		nvgFillPaint(vg, bg);
		nvgFill(vg);

		// Corner screws (bolt-in flourish)
		float screwXY[4][2] = {{9.f, 9.f}, {w - 9.f, 9.f}, {9.f, h - 9.f}, {w - 9.f, h - 9.f}};
		for (int i = 0; i < 4; i++) {
			float sx = screwXY[i][0], sy = screwXY[i][1];
			// body
			NVGpaint screw = nvgLinearGradient(vg, sx - 3.f, sy - 3.f, sx + 3.f, sy + 3.f,
				nvgRGB(0x33, 0x35, 0x39), nvgRGB(0x14, 0x15, 0x18));
			nvgBeginPath(vg);
			nvgCircle(vg, sx, sy, 4.2f);
			nvgFillPaint(vg, screw);
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 160));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
			// slot, rotated at a fixed machined angle
			float rad = 35.f * M_PI / 180.f;
			float ca = std::cos(rad), sa = std::sin(rad);
			nvgBeginPath(vg);
			nvgMoveTo(vg, sx - 2.8f * ca, sy - 2.8f * sa);
			nvgLineTo(vg, sx + 2.8f * ca, sy + 2.8f * sa);
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 200));
			nvgStrokeWidth(vg, 1.1f);
			nvgLineCap(vg, NVG_ROUND);
			nvgStroke(vg);
			// subtle top-left glint
			nvgBeginPath(vg);
			nvgArc(vg, sx, sy, 3.2f, M_PI * 1.1f, M_PI * 1.6f, NVG_CW);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 26));
			nvgStrokeWidth(vg, 0.9f);
			nvgStroke(vg);
		}

		// Header divider
		nvgBeginPath(vg);
		nvgRect(vg, 10, HEADER_H - 9, w - 20, 1);
		nvgFillColor(vg, nvgRGBA(255, 255, 255, 14));
		nvgFill(vg);

		// Title (e): FaderFox PCn where n = number of columns
		if (fontTitle) {
			nvgFontFaceId(vg, fontTitle->handle);
			nvgFontSize(vg, 20.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGB(0xec, 0xee, 0xf0));
			nvgText(vg, 16, 11, string::f("FaderFox PC%d", cols).c_str(), NULL);
		}
		if (fontRegular) {
			nvgFontFaceId(vg, fontRegular->handle);
			nvgFontSize(vg, 9.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGB(0x79, 0x80, 0x89));
			nvgText(vg, 16, 40, string::f("%d ROWS x %d COLS  MIDI CC MATRIX", rows, cols).c_str(), NULL);
		}

		// Red LED display (f)
		float bw = 136.f, bh = 46.f;
		float bx = w - MARGIN - bw, by = 9.f;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, bx, by, bw, bh, 3.f);
		nvgFillColor(vg, nvgRGB(0x09, 0x0a, 0x0b));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(0x2e, 0x31, 0x36));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);

		bool learning = mod && mod->learnPort >= 0;
		bool recent = mod && (now - mod->lastActivity) < 0.2;
		float alpha = recent ? 1.f : 0.82f;
		float g1x = bx + 16.f, g2x = bx + 72.f, digy = by + 32.f;

		if (fontRegular) {
			nvgFontFaceId(vg, fontRegular->handle);
			nvgFontSize(vg, 7.f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGB(0x6e, 0x3a, 0x33));
			nvgText(vg, g1x + 18.f, by + 6, "CC", NULL);
			nvgText(vg, g2x + 18.f, by + 6, "VALUE", NULL);
		}

		const char* ccStr = "---";
		const char* valStr = "---";
		std::string ccBuf, valBuf;
		if (mod) {
			if (learning) {
				float p = 0.5f + 0.5f * std::sin(now * 7.5);
				alpha *= 0.45f + 0.55f * p;
				ccStr = "Lrn";
				if (mod->lastCC >= 0) {
					valBuf = string::f("%03d", mod->lastValue);
					valStr = valBuf.c_str();
				}
			}
			else if (mod->lastCC >= 0) {
				ccBuf = string::f("%03d", mod->lastCC);
				valBuf = string::f("%03d", mod->lastValue);
				ccStr = ccBuf.c_str();
				valStr = valBuf.c_str();
			}
		}
		drawDigitString(vg, g1x, digy, ccStr, alpha);
		drawDigitString(vg, g2x, digy, valStr, alpha);

		// MIDI activity dot + LEARN indicator, left of the display
		bool activity = mod && (now - mod->lastActivity) < 0.15;
		nvgBeginPath(vg);
		nvgCircle(vg, bx - 13.f, by + 9.f, 3.f);
		nvgFillColor(vg, activity ? nvgRGB(0xff, 0x35, 0x20) : nvgRGB(0x3a, 0x15, 0x12));
		nvgFill(vg);
		if (learning && fontRegular) {
			nvgFontFaceId(vg, fontRegular->handle);
			nvgFontSize(vg, 9.f);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGB(0xff, 0xd2, 0x3f));
			nvgText(vg, bx - 8.f, by + 20, "LEARN", NULL);
		}

		// Grid labels
		if (fontRegular) {
			nvgFontFaceId(vg, fontRegular->handle);
			nvgFontSize(vg, 8.f);
			nvgFillColor(vg, nvgRGB(0x66, 0x6c, 0x74));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			for (int c = 0; c < cols; c++) {
				float x = GRID_X + c * cell + cell * 0.5f;
				nvgText(vg, x, gy + rows * cell + 6, string::f("%d", c + 1).c_str(), NULL);
			}
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			for (int r = 0; r < rows; r++) {
				float y = gy + r * cell + cell * 0.5f;
				nvgText(vg, GRID_X - 7.f, y, string::f("%d", r + 1).c_str(), NULL);
			}
		}

		// Cell plates (very subtle)
		nvgBeginPath(vg);
		for (int r = 0; r < rows; r++) {
			for (int c = 0; c < cols; c++) {
				nvgRoundedRect(vg, GRID_X + c * cell + 2, gy + r * cell + 2, cell - 4, cell - 4, 4.f);
			}
		}
		nvgFillColor(vg, nvgRGBA(255, 255, 255, 5));
		nvgFill(vg);
	}
};

// "About FaderFox" popup
struct FaderFoxAboutBox : widget::OpaqueWidget {
	struct LinkButton : ui::Button {
		void onAction(const ActionEvent& e) override {
			system::openBrowser("https://faderfox.de/");
		}
	};

	FaderFoxAboutBox() {
		box.size = math::Vec(390.f, 238.f);
		LinkButton* link = new LinkButton;
		link->text = "Visit FaderFox";
		link->box = math::Rect(math::Vec(390.f - 124.f, 238.f - 40.f), math::Vec(108.f, 26.f));
		addChild(link);
	}

	void step() override {
		OpaqueWidget::step();
		if (getParent())
			box.pos = getParent()->box.size.div(2).minus(box.size.div(2));
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		loadFonts();
		float w = box.size.x;
		float h = box.size.y;

		// dialog box
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, w, h, 8.f);
		nvgFillColor(vg, nvgRGB(0x22, 0x24, 0x28));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(0x0b, 0x0c, 0x0e));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);

		if (fontTitle) {
			nvgFontFaceId(vg, fontTitle->handle);
			nvgFontSize(vg, 15.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGB(0xec, 0xee, 0xf0));
			nvgText(vg, 18, 14, "About FaderFox", NULL);
		}
		nvgBeginPath(vg);
		nvgRect(vg, 18, 38, w - 36, 1);
		nvgFillColor(vg, nvgRGBA(255, 255, 255, 14));
		nvgFill(vg);

		if (fontRegular) {
			nvgFontFaceId(vg, fontRegular->handle);
			nvgFontSize(vg, 12.f);
			nvgFillColor(vg, nvgRGB(0xcf, 0xd3, 0xd8));
			nvgTextBox(vg, 18, 48, w - 36,
				"This module makes connecting the FaderFox PC12 and similar controllers to VCV Rack fast and easy.\n\n"
				"If you have never seen FaderFox's MIDI controllers, you are missing out. Built like tanks, but smooth to play, there are no better performance machines.\n\n"
				"I am not associated with FaderFox in any way - just a fan of this tiny, craft-driven company.",
				NULL);
		}
	}
};

// Module widget: dynamic rows/cols grid, fixed 3U height (e)
struct FaderFoxPCnWidget : ModuleWidget {
	FaderFoxPCnModule* mod;
	FaderFoxPort* ports[MAX_PORTS];
	FaderFoxBackground* background;
	int appliedRows = -1;
	int appliedCols = -1;
	int8_t lastMapping[MAX_PORTS];  // for tooltip text sync
	int8_t lastRange[MAX_PORTS];

	FaderFoxPCnWidget(FaderFoxPCnModule* module) {
		setModule(module);
		this->mod = module;

		background = new FaderFoxBackground;
		background->mod = module;
		setPanel(background);

		for (int i = 0; i < MAX_PORTS; i++) {
			ports[i] = new FaderFoxPort(module, i);
			addOutput(ports[i]);
			lastMapping[i] = -2;  // force initial tooltip update
			lastRange[i] = -2;
		}
		applyGrid();
	}

	void applyGrid() {
		int rows = mod ? mod->rows : 6;
		int cols = mod ? mod->cols : 12;
		if (rows == appliedRows && cols == appliedCols)
			return;
		appliedRows = rows;
		appliedCols = cols;

		float cell = cellSize(rows);
		float gy = gridY(rows);
		math::Vec sz(panelWidth(rows, cols), RACK_HEIGHT);
		box.size = sz;
		background->box = math::Rect(math::Vec(0.f, 0.f), sz);

		for (int i = 0; i < MAX_PORTS; i++) {
			if (i < rows * cols) {
				int r = i / cols;
				int c = i % cols;
				ports[i]->visible = true;
				ports[i]->box.size = math::Vec(cell, cell);
				ports[i]->box.pos = math::Vec(GRID_X + c * cell, gy + r * cell);
			}
			else {
				ports[i]->visible = false;
			}
		}
		if (mod && mod->learnPort >= rows * cols)
			mod->learnPort = -1;
	}

	void step() override {
		ModuleWidget::step();
		applyGrid();
		// Keep port tooltips in sync with assignments and ranges
		if (mod) {
			for (int i = 0; i < MAX_PORTS; i++) {
				if (lastMapping[i] != mod->mapping[i] || lastRange[i] != (int8_t) mod->rangeIdx[i]) {
					lastMapping[i] = mod->mapping[i];
					lastRange[i] = (int8_t) mod->rangeIdx[i];
					if (mod->mapping[i] >= 0)
						mod->configOutput(i, string::f("CC %d  (%s)", (int) mod->mapping[i], RANGE_NAMES[mod->rangeIdx[i]]));
					else
						mod->configOutput(i, string::f("Unmapped (click to learn)  (%s)", RANGE_NAMES[mod->rangeIdx[i]]));
				}
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		FaderFoxPCnModule* m = mod;
		if (!m)
			return;

		// (c) MIDI driver / device / channel
		menu->addChild(createSubmenuItem("MIDI driver", "", [=](Menu* menu) {
			for (int driverId : midi::getDriverIds()) {
				midi::Driver* driver = midi::getDriver(driverId);
				if (!driver)
					continue;
				menu->addChild(createCheckMenuItem(driver->getName(), "",
					[=]() { return m->midiInput.driverId == driverId; },
					[=]() {
						m->midiInput.setDriverId(driverId);
						m->midiInput.setDeviceId(0);
					}));
			}
		}));

		menu->addChild(createSubmenuItem("MIDI device", "", [=](Menu* menu) {
			midi::Driver* driver = midi::getDriver(m->midiInput.driverId);
			if (!driver) {
				menu->addChild(createMenuLabel("No MIDI driver"));
				return;
			}
			std::vector<int> deviceIds = driver->getInputDeviceIds();
			if (deviceIds.empty()) {
				menu->addChild(createMenuLabel("(no input devices)"));
				return;
			}
			for (int deviceId : deviceIds) {
				std::string name = driver->getInputDeviceName(deviceId);
				menu->addChild(createCheckMenuItem(name, "",
					[=]() { return m->midiInput.deviceId == deviceId; },
					[=]() { m->midiInput.setDeviceId(deviceId); }));
			}
		}));

		menu->addChild(createSubmenuItem("Channel filter", "", [=](Menu* menu) {
			menu->addChild(createCheckMenuItem("Omni (all channels)", "",
				[=]() { return m->channel == 0; },
				[=]() { m->channel = 0; }));
			for (int ch = 1; ch <= 16; ch++) {
				menu->addChild(createCheckMenuItem(string::f("%d", ch), "",
					[=]() { return m->channel == ch; },
					[=]() { m->channel = ch; }));
			}
		}));

		menu->addChild(new MenuSeparator);

		// (b) voltage ranges: en-masse setters
		menu->addChild(createSubmenuItem("Output voltage range", "", [=](Menu* menu) {
			menu->addChild(createSubmenuItem("All ports", "", [=](Menu* menu) {
				for (int i = 0; i < NUM_RANGES; i++) {
					menu->addChild(createCheckMenuItem(RANGE_NAMES[i], "",
						[=]() { return m->rangeDefault == i; },
						[=]() { m->setRangeAll(i); }));
				}
			}));
			menu->addChild(createSubmenuItem("Row", "", [=](Menu* menu) {
				for (int r = 0; r < m->rows; r++) {
					menu->addChild(createSubmenuItem(string::f("Row %d", r + 1), "", [=](Menu* menu) {
						for (int i = 0; i < NUM_RANGES; i++) {
							menu->addChild(createMenuItem(RANGE_NAMES[i], "",
								[=]() { m->setRangeRow(r, i); }));
						}
					}));
				}
			}));
		}));

		// (a, e) grid size
		menu->addChild(createSubmenuItem("Rows", string::f("%d", m->rows), [=](Menu* menu) {
			for (int r = 1; r <= MAX_ROWS; r++) {
				menu->addChild(createCheckMenuItem(string::f("%d", r), "",
					[=]() { return m->rows == r; },
					[=]() { m->rows = r; }));
			}
		}));
		menu->addChild(createSubmenuItem("Columns", string::f("%d", m->cols), [=](Menu* menu) {
			for (int c = 1; c <= MAX_COLS; c++) {
				menu->addChild(createCheckMenuItem(string::f("%d", c), "",
					[=]() { return m->cols == c; },
					[=]() { m->cols = c; }));
			}
		}));

		menu->addChild(new MenuSeparator);

		menu->addChild(createMenuItem("Clear all mappings", "", [=]() { m->clearAll(); }));

		menu->addChild(new MenuSeparator);

		menu->addChild(createMenuItem("About FaderFox", "", [=]() {
			ui::MenuOverlay* overlay = new ui::MenuOverlay;
			overlay->addChild(new FaderFoxAboutBox);
			APP->scene->addChild(overlay);
		}));
	}
};

Model* modelFaderFoxPCn = createModel<FaderFoxPCnModule, FaderFoxPCnWidget>("FaderFoxPCn");
