#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <cariboulite.h>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "cariboulite_source",
    /* Description:     */ "CaribouLite source module for SDR++",
    /* Author:          */ "DavidMichaeli/CaribouLabsLTD",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

const double sampleRates[] = {
    400000,
    500000,
    666000,
    800000,
    1000000,
    1333000,
    2000000,
    4000000
};

#define NUM_SAMPLE_RATES (sizeof(sampleRates) / sizeof(sampleRates[0]))

const char* sampleRatesTxt[] = {
    "400KHz",
    "500MHz",
    "666MHz",
    "800MHz",
    "1MHz",
    "1.333MHz",
    "2MHz",
    "4MHz"
};

const double bandWidths[] = {
    200000,
    250000,
    312500,
    400000,
	500000,
	625000,
	787500,
    1000000,
	1250000,
	1562500,
    2000000,
    2500000
};

#define NUM_BANDSIDTHS (sizeof(bandWidths) / sizeof(bandWidths[0]))

const char* bandWidthsTxt[] = {
    "200KHz",
    "250KHz",
    "312KHz",
    "400KHz",
    "500KHz",
    "625KHz",
    "787KHz",
    "1000KHz",
	"1250KHz",
	"1562KHz",
	"2000KHz",
	"2500KHz"
};

class CaribouLiteSourceModule : public ModuleManager::Instance {
public:
    CaribouLiteSourceModule(std::string name) {
        this->name = name;

        serverMode = (bool)core::args["server"];

        sampleRate = sampleRates[0];

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        strcpy(dbTxt, "--");

        for (int i = 0; i < NUM_SAMPLE_RATES; i++) {
            sampleRateListTxt += sampleRatesTxt[i];
            sampleRateListTxt += '\0';
        }

		for (int i = 0; i < NUM_BANDSIDTHS; i++) {
            bandWidthListTxt += bandWidthsTxt[i];
            bandWidthListTxt += '\0';
        }

        refresh();

        config.acquire();
        if (!config.conf["device"].is_string()) {
            selectedDevName = "";
            config.conf["device"] = "";
        }
        else {
            selectedDevName = config.conf["device"];
        }
        config.release(true);
        selectByName(selectedDevName);

        sigpath::sourceManager.registerSource("CaribouLite", &handler);
    }

    ~CaribouLiteSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("CaribouLite");
		if (cariboulite_is_initialized())
		{
			cariboulite_close();
		}
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void refresh() {
        devNames.clear();
        devListTxt = "";

		// check if any board is connected
		unsigned int serial_number;
		bool detected = cariboulite_detect_connected_board(NULL, NULL, NULL);
		if (detected) 
		{
			devCount = 2; // total two channels (each represented as a device / radio)
			serial_number = cariboulite_get_sn();
		}
		else devCount = 0;

        char buf[1024];
        char sn[1024];
        for (int i = 0; i < devCount; i++) {
            // Gather device info
			char name[64];
			if (cariboulite_get_channel_name((cariboulite_channel_en)i, name, sizeof(name)) == 0)
			{
				// Build name
				sprintf(buf, "[%08X] %d:%s", serial_number, i, name);
				devNames.push_back(buf);
				devListTxt += buf;
				devListTxt += '\0';
			}
        }
    }

    void selectFirst() {
        if (devCount > 0) {
            selectById(0);
        }
    }

    void selectByName(std::string name) {
        for (int i = 0; i < devCount; i++) {
            if (name == devNames[i]) {
                selectById(i);
                return;
            }
        }
        selectFirst();
    }

    void selectById(int id) {
        selectedDevName = devNames[id];

		if (!cariboulite_is_initialized())
		{
			flog::info("Initializing CaribouLite");
			if (cariboulite_init(false, cariboulite_log_level_none) != 0)
			{
				flog::error("Could not open CaribouLite");
				return;
			}
		}

        openDev = cariboulite_get_radio((cariboulite_channel_en)id);
		if (openDev == NULL)
		{
			selectedDevName = "";
            flog::error("The selected id {0} is invalid", id);
            return;
		}
        
        gainList.clear();
        int gains[256];
		int min_gain, max_gain, step_gain;
		cariboulite_radio_get_rx_gain_limits(openDev, &min_gain, &max_gain, &step_gain);
		
		for (int gain = min_gain; gain <= max_gain; gain+= step_gain)
		{
			gainList.push_back(gain);
		}

        bool created = false;
        config.acquire();
        if (!config.conf["devices"].contains(selectedDevName)) {
            created = true;
            config.conf["devices"][selectedDevName]["sampleRate"] = 4000000.0;
			config.conf["devices"][selectedDevName]["bandwidth"] = 2500000.0;
            config.conf["devices"][selectedDevName]["agc"] = agcActive;
            config.conf["devices"][selectedDevName]["gain"] = gainId;
        }
        if (gainId >= gainList.size()) { gainId = gainList.size() - 1; }
        updateGainTxt();

        // Load config
        if (config.conf["devices"][selectedDevName].contains("sampleRate")) {
            int selectedSr = config.conf["devices"][selectedDevName]["sampleRate"];
            for (int i = 0; i < NUM_SAMPLE_RATES; i++) {
                if (sampleRates[i] == selectedSr) {
                    srId = i;
                    sampleRate = selectedSr;
                    break;
                }
            }
        }

		if (config.conf["devices"][selectedDevName].contains("bandwidth")) {
            int selectedBw = config.conf["devices"][selectedDevName]["bandwidth"];
            for (int i = 0; i < NUM_BANDSIDTHS; i++) {
                if (bandWidths[i] == selectedBw) {
                    bwId = i;
                    bandWidth = selectedBw;
                    break;
                }
            }
        }

        if (config.conf["devices"][selectedDevName].contains("agc")) {
            agcActive = config.conf["devices"][selectedDevName]["agc"];
        }

        if (config.conf["devices"][selectedDevName].contains("gain")) {
            gainId = config.conf["devices"][selectedDevName]["gain"];
            updateGainTxt();
        }

        config.release(created);

		// TBD: close the decice?
    }

private:
    std::string getBandwdithScaled(double bw) {
        char buf[1024];
        if (bw >= 1000000.0) {
            sprintf(buf, "%.2lfMHz", bw / 1000000.0);
        }
        else if (bw >= 1000.0) {
            sprintf(buf, "%.1lfKHz", bw / 1000.0);
        }
        else {
            sprintf(buf, "%.1lfHz", bw);
        }
        return std::string(buf);
    }

    static void menuSelected(void* ctx) {
        CaribouLiteSourceModule* _this = (CaribouLiteSourceModule*)ctx;
		_this->refresh();
        core::setInputSampleRate(_this->sampleRate);
        flog::info("CaribouLiteSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        CaribouLiteSourceModule* _this = (CaribouLiteSourceModule*)ctx;
        flog::info("CaribouLiteSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        CaribouLiteSourceModule* _this = (CaribouLiteSourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->selectedDevName == "") {
            flog::error("No device selected");
            return;
        }

		_this->openDev = cariboulite_get_radio((cariboulite_channel_en)_this->devId);
		if (_this->openDev == NULL)
		{
            flog::error("The selected id {0} is invalid", _this->devId);
            return;
		}

		// allocate read buffer
		_this->read_buffer_length = cariboulite_radio_get_native_mtu_size_samples(_this->openDev);
		_this->read_buffer = new cariboulite_sample_complex_int16[_this->read_buffer_length];
		if (_this->read_buffer == NULL)
		{
			flog::error("RX buffer allocation failed");
            return;
		}

        flog::info("CaribouLite Sample Rate: {0}", _this->sampleRate);
		flog::info("CaribouLite Bandwidth: {0}", _this->bandWidth);

		cariboulite_radio_set_rx_bandwidth_flt(_this->openDev, _this->bandWidth);
		cariboulite_radio_set_rx_sample_rate_flt(_this->openDev, _this->sampleRate);
		cariboulite_radio_set_rx_gain_control(_this->openDev, _this->agcActive, _this->gainList[_this->gainId]);
        cariboulite_radio_set_frequency(_this->openDev, true, &_this->freq);

        _this->workerThread = std::thread(&CaribouLiteSourceModule::worker, _this);
        _this->running = true;

		cariboulite_radio_activate_channel(_this->openDev, cariboulite_channel_dir_rx, true);
        flog::info("CaribouLiteSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        CaribouLiteSourceModule* _this = (CaribouLiteSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        _this->stream.stopWriter();
		cariboulite_radio_activate_channel(_this->openDev, cariboulite_channel_dir_rx, false);
        if (_this->workerThread.joinable()) { _this->workerThread.join(); }
        _this->stream.clearWriteStop();

		delete[] _this->read_buffer;
		_this->read_buffer = NULL;
        // TBD remove the device?
        flog::info("CaribouLiteSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        CaribouLiteSourceModule* _this = (CaribouLiteSourceModule*)ctx;
		cariboulite_radio_set_frequency(_this->openDev, true, &_this->freq);
        flog::info("CaribouLiteSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        CaribouLiteSourceModule* _this = (CaribouLiteSourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }

        SmGui::FillWidth();
        SmGui::ForceSync();
        
		if (SmGui::Combo(CONCAT("##_cariboulite_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) 
		{
            _this->selectById(_this->devId);
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["device"] = _this->selectedDevName;
                config.release(true);
            }
        }

        if (SmGui::Combo(CONCAT("##_cariboulite_sr_sel_", _this->name), &_this->srId, _this->sampleRateListTxt.c_str())) 
		{
            _this->sampleRate = sampleRates[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["sampleRate"] = _this->sampleRate;
                config.release(true);
            }
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_cariboulite_refr_", _this->name)/*, ImVec2(refreshBtnWdith, 0)*/)) 
		{
            _this->refresh();
            _this->selectByName(_this->selectedDevName);
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->running) { SmGui::EndDisabled(); }

		/*
        // Rest of cariboulite config here
        SmGui::LeftLabel("Direct Sampling");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_rtlsdr_ds_", _this->name), &_this->directSamplingMode, directSamplingModesTxt)) {
            if (_this->running) {
                rtlsdr_set_direct_sampling(_this->openDev, _this->directSamplingMode);

                // Update gains (fix for librtlsdr bug)
                if (_this->directSamplingMode == false) {
                    rtlsdr_set_agc_mode(_this->openDev, _this->rtlAgc);
                    if (_this->tunerAgc) {
                        rtlsdr_set_tuner_gain_mode(_this->openDev, 0);
                    }
                    else {
                        rtlsdr_set_tuner_gain_mode(_this->openDev, 1);
                        rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]);
                    }
                }
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["directSampling"] = _this->directSamplingMode;
                config.release(true);
            }
        }

        SmGui::LeftLabel("PPM Correction");
        SmGui::FillWidth();
        if (SmGui::InputInt(CONCAT("##_rtlsdr_ppm_", _this->name), &_this->ppm, 1, 10)) {
            _this->ppm = std::clamp<int>(_this->ppm, -1000000, 1000000);
            if (_this->running) {
                rtlsdr_set_freq_correction(_this->openDev, _this->ppm);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["ppm"] = _this->ppm;
                config.release(true);
            }
        }

        if (_this->tunerAgc || _this->gainList.size() == 0) { SmGui::BeginDisabled(); }

        SmGui::LeftLabel("Gain");
        SmGui::FillWidth();
        SmGui::ForceSync();
        // TODO: FIND ANOTHER WAY
        if (_this->serverMode) {
            if (SmGui::SliderInt(CONCAT("##_rtlsdr_gain_", _this->name), &_this->gainId, 0, _this->gainList.size() - 1, SmGui::FMT_STR_NONE)) {
                _this->updateGainTxt();
                if (_this->running) {
                    rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]);
                }
                if (_this->selectedDevName != "") {
                    config.acquire();
                    config.conf["devices"][_this->selectedDevName]["gain"] = _this->gainId;
                    config.release(true);
                }
            }
        }
        else {
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_gain_", _this->name), &_this->gainId, 0, _this->gainList.size() - 1, _this->dbTxt)) {
                _this->updateGainTxt();
                if (_this->running) {
                    rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]);
                }
                if (_this->selectedDevName != "") {
                    config.acquire();
                    config.conf["devices"][_this->selectedDevName]["gain"] = _this->gainId;
                    config.release(true);
                }
            }
        }

        
        if (_this->tunerAgc || _this->gainList.size() == 0) { SmGui::EndDisabled(); }

        if (SmGui::Checkbox(CONCAT("Bias T##_rtlsdr_rtl_biast_", _this->name), &_this->biasT)) {
            if (_this->running) {
                rtlsdr_set_bias_tee(_this->openDev, _this->biasT);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["biasT"] = _this->biasT;
                config.release(true);
            }
        }

        if (SmGui::Checkbox(CONCAT("Offset Tuning##_rtlsdr_rtl_ofs_", _this->name), &_this->offsetTuning)) {
            if (_this->running) {
                rtlsdr_set_offset_tuning(_this->openDev, _this->offsetTuning);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["offsetTuning"] = _this->offsetTuning;
                config.release(true);
            }
        }

        if (SmGui::Checkbox(CONCAT("RTL AGC##_rtlsdr_rtl_agc_", _this->name), &_this->rtlAgc)) {
            if (_this->running) {
                rtlsdr_set_agc_mode(_this->openDev, _this->rtlAgc);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["rtlAgc"] = _this->rtlAgc;
                config.release(true);
            }
        }

        SmGui::ForceSync();
        if (SmGui::Checkbox(CONCAT("Tuner AGC##_rtlsdr_tuner_agc_", _this->name), &_this->tunerAgc)) {
            if (_this->running) {
                if (_this->tunerAgc) {
                    rtlsdr_set_tuner_gain_mode(_this->openDev, 0);
                }
                else {
                    rtlsdr_set_tuner_gain_mode(_this->openDev, 1);
                    rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]);
                }
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["tunerAgc"] = _this->tunerAgc;
                config.release(true);
            }
        }*/
    }

    void worker() 
	{
        while (running)
		{
			int samples_read = cariboulite_radio_read_samples(openDev, read_buffer, NULL, read_buffer_length);
			if (samples_read <= 0) continue;

			for (int i = 0; i < samples_read; i++) 
			{
				stream.writeBuf[i].re = read_buffer[i].i / 4096.0f;
				stream.writeBuf[i].im = read_buffer[i].q / 4096.0f;
			}
			if (!stream.swap(samples_read)) { continue; }
		}
    }

    void updateGainTxt() {
        sprintf(dbTxt, "%.1f dB", (float)gainList[gainId] / 10.0f);
    }

private:
	cariboulite_sample_complex_int16 *read_buffer = NULL;
	size_t read_buffer_length = 0;
    std::string name;
    cariboulite_radio_state_st* openDev;
	bool running = false;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    std::string selectedDevName = "";
    int devId = 0;
    
    int devCount = 0;
    std::thread workerThread;

    bool serverMode = false;
	double freq;
	int bwId = 0;
    double bandWidth;
	int srId = 0;
    double sampleRate;
    int gainId = 0;
    std::vector<int> gainList;
    bool agcActive = false;
    
    // Handler stuff
    int asyncCount = 0;

    char dbTxt[128];

    std::vector<std::string> devNames;
    std::string devListTxt;
    std::string sampleRateListTxt;
	std::string bandWidthListTxt;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = 0;
    config.setPath(core::args["root"].s() + "/cariboulite_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new CaribouLiteSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (CaribouLiteSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}