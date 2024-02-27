/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "AlsCorrection.h"

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <binder/IBinder.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <algorithm>
#include <cmath>
#include <cutils/properties.h>
#include <fstream>
#include <log/log.h>
#include <string>
#include <sstream>
#include <utils/Timers.h>

using aidl::vendor::lineage::oplus_als::AreaRgbCaptureResult;
using android::base::GetProperty;

#define PERSIST_ENG "/mnt/vendor/persist/engineermode/"
#define PROC_ALS "/proc/sensor/als_cali/"
#define SYSFS_BACKLIGHT "/sys/class/backlight/panel0-backlight/"

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace implementation {

static const std::string rgbw_max_lux_paths[4] = {
    PROC_ALS "red_max_lux",
    PROC_ALS "green_max_lux",
    PROC_ALS "blue_max_lux",
    PROC_ALS "white_max_lux",
};

struct als_config {
    bool hbr;
    float rgbw_max_lux[4];
    float rgbw_max_lux_div[4];
    float rgbw_lux_postmul[4];
    float rgbw_poly[4][4];
    float grayscale_weights[3];
    float sensor_inverse_gain[4];
    float agc_threshold;
    float calib_gain;
    float bias;
    float max_brightness;
};

static struct {
    float middle;
    float min, max;
} hysteresis_ranges[] = {
    { 0, 0, 4 },
    { 7, 1, 12 },
    { 15, 5, 30 },
    { 30, 10, 50 },
    { 360, 25, 700 },
    { 1200, 300, 1600 },
    { 2250, 1000, 2940 },
    { 4600, 2000, 5900 },
    { 10000, 4000, 80000 },
    { HUGE_VALF, 8000, HUGE_VALF },
};

static struct {
    nsecs_t last_update, last_forced_update;
    bool force_update;
    float hyst_min, hyst_max;
    float last_corrected_value;
    float last_agc_gain;
} state = {
    .last_update = 0,
    .force_update = true,
    .hyst_min = -1.0, .hyst_max = -1.0,
    .last_agc_gain = 0.0,
};

static als_config conf;
static std::shared_ptr<IAreaCapture> service;

template <typename T>
static T get(const std::string& path, const T& def) {
    std::ifstream file(path);
    T result;

    file >> result;
    return file.fail() ? def : result;
}

void AlsCorrection::init() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    std::istringstream is;
    conf.hbr = android::base::GetBoolProperty("vendor.sensors.als_correction.hbr", false);
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.bias", ""));
    is >> conf.bias;
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.rgbw_max_lux", ""));
    is >> conf.rgbw_max_lux[0] >> conf.rgbw_max_lux[1]
        >> conf.rgbw_max_lux[2] >> conf.rgbw_max_lux[3];
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.rgbw_max_lux_div", ""));
    is >> conf.rgbw_max_lux_div[0] >> conf.rgbw_max_lux_div[1]
        >> conf.rgbw_max_lux_div[2] >> conf.rgbw_max_lux_div[3];
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.rgbw_poly1", ""));
    is >> conf.rgbw_poly[0][0] >> conf.rgbw_poly[0][1]
        >> conf.rgbw_poly[0][2] >> conf.rgbw_poly[0][3];
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.rgbw_poly2", ""));
    is >> conf.rgbw_poly[1][0] >> conf.rgbw_poly[1][1]
        >> conf.rgbw_poly[1][2] >> conf.rgbw_poly[1][3];
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.rgbw_poly3", ""));
    is >> conf.rgbw_poly[2][0] >> conf.rgbw_poly[2][1]
        >> conf.rgbw_poly[2][2] >> conf.rgbw_poly[2][3];
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.rgbw_poly4", ""));
    is >> conf.rgbw_poly[3][0] >> conf.rgbw_poly[3][1]
        >> conf.rgbw_poly[3][2] >> conf.rgbw_poly[3][3];
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.grayscale_weights", ""));
    is >> conf.grayscale_weights[0] >> conf.grayscale_weights[1] >> conf.grayscale_weights[2];
    is = std::istringstream(GetProperty("vendor.sensors.als_correction.sensor_inverse_gain", ""));
    is >> conf.sensor_inverse_gain[0] >> conf.sensor_inverse_gain[1]
        >> conf.sensor_inverse_gain[2] >> conf.sensor_inverse_gain[3];

    float rgbw_acc = 0.0;
    for (int i = 0; i < 4; i++) {
        float max_lux = get(rgbw_max_lux_paths[i], 0.0);
        if (max_lux != 0.0) {
            conf.rgbw_max_lux[i] = max_lux;
        }
        if (i < 3) {
            rgbw_acc += conf.rgbw_max_lux[i];
            conf.rgbw_lux_postmul[i] = conf.rgbw_max_lux[i] / conf.rgbw_max_lux_div[i];
        } else {
            rgbw_acc -= conf.rgbw_max_lux[i];
            conf.rgbw_lux_postmul[i] = rgbw_acc / conf.rgbw_max_lux_div[i];
        }
    }
    ALOGI("Display maximums: R=%.0f G=%.0f B=%.0f W=%.0f",
        conf.rgbw_max_lux[0], conf.rgbw_max_lux[1],
        conf.rgbw_max_lux[2], conf.rgbw_max_lux[3]);

    float row_coe = get(PROC_ALS "row_coe", 0.0);
    if (row_coe != 0.0) {
        conf.sensor_inverse_gain[0] = row_coe / 1000.0;
    }
    conf.agc_threshold = 800.0 / conf.sensor_inverse_gain[0];

    float cali_coe = get(PROC_ALS "cali_coe", 0.0);
    conf.calib_gain = cali_coe > 0.0 ? cali_coe / 1000.0 : 1.0;
    ALOGI("Calibrated sensor gain: %.2fx", 1.0 / (conf.calib_gain * conf.sensor_inverse_gain[0]));

    float max_brightness = get(SYSFS_BACKLIGHT "max_brightness", 0.0);
    conf.max_brightness = max_brightness > 0.0 ? max_brightness : 1023.0;

    for (auto& range : hysteresis_ranges) {
        range.min /= conf.calib_gain * conf.sensor_inverse_gain[0];
        range.max /= conf.calib_gain * conf.sensor_inverse_gain[0];
    }
    hysteresis_ranges[0].min = -1.0;

    android::ProcessState::initWithDriver("/dev/vndbinder");
    service = getCaptureService();
    if (service == nullptr) {
        ALOGE("Service not found");
        return;
    }
}

std::shared_ptr<IAreaCapture> AlsCorrection::getCaptureService() {
    auto instancename = std::string(IAreaCapture::descriptor) + "/default";

    if (!AServiceManager_isDeclared(instancename.c_str())) {
        ALOGE("Service is not registered");
        return nullptr;
    }

    ::ndk::SpAIBinder binder(AServiceManager_waitForService(instancename.c_str()));
    return IAreaCapture::fromBinder(binder);
}

void AlsCorrection::process(Event& event) {
    ALOGI("d0:%f d1:%f d2:%f d3:%f d4:%f d5:%f d6:%f d7:%f d8:%f d9:%f d10:%f d11:%f d12:%f d13:%f d14:%f d15:%f",
            event.u.data[0],
            event.u.data[1],
            event.u.data[2],
            event.u.data[3],
            event.u.data[4],
            event.u.data[5],
            event.u.data[6],
            event.u.data[7],
            event.u.data[8],
            event.u.data[9],
            event.u.data[10],
            event.u.data[11],
            event.u.data[12],
            event.u.data[13],
            event.u.data[14],
            event.u.data[15]);

    ALOGI("Raw sensor reading: %.0f", event.u.scalar);

    if (event.u.scalar > conf.bias) {
        event.u.scalar -= conf.bias;
    }

    nsecs_t now = systemTime(SYSTEM_TIME_BOOTTIME);
    float brightness = get(SYSFS_BACKLIGHT "brightness", 0.0);

    if (state.last_update == 0) {
        state.last_update = now;
        state.last_forced_update = now;
    } else {
        if (brightness > 0.0 && (now - state.last_forced_update) > s2ns(3)) {
            ALOGI("Forcing screenshot");
            state.last_forced_update = now;
            state.force_update = true;
        }
        if ((now - state.last_update) < ms2ns(100)) {
            ALOGI("Events coming too fast, dropping");
            // TODO figure out a better way to drop events
            event.sensorHandle = 0;
            return;
        }
        state.last_update = now;
    }

    float sensor_raw_calibrated = event.u.scalar * conf.calib_gain * state.last_agc_gain;
    if (state.force_update
            || ((event.u.scalar < state.hyst_min || event.u.scalar > state.hyst_max)
                && (sensor_raw_calibrated < 10.0 || sensor_raw_calibrated > (5.0 / .07)))) {

        AreaRgbCaptureResult screenshot = {.r = 0.0f, .g = 0.0f, .b = 0.0f};
        if (service == nullptr || !service->getAreaBrightness(&screenshot).isOk()) {
            ALOGE("Could not get area above sensor");
        }
        ALOGI("Screen color above sensor: %f %f %f", screenshot.r, screenshot.g, screenshot.b);

        float rgbw[4] = {
            screenshot.r, screenshot.g, screenshot.b,
            screenshot.r * conf.grayscale_weights[0]
                + screenshot.g * conf.grayscale_weights[1]
                + screenshot.b * conf.grayscale_weights[2]
        };
        float cumulative_correction = 0.0;
        for (int i = 0; i < 4; i++) {
            float corr = 0.0;
            for (float coef : conf.rgbw_poly[i]) {
                corr *= rgbw[i];
                corr += coef;
            }
            corr *= conf.rgbw_lux_postmul[i];
            if (i < 3) {
                cumulative_correction += std::max(corr, 0.0f);
            } else {
                cumulative_correction -= corr;
            }
        }
        cumulative_correction *= brightness / conf.max_brightness;
        float brightness_fullwhite = conf.rgbw_max_lux[3] * brightness / conf.max_brightness;
        float brightness_grayscale_gamma = std::pow(rgbw[3] / 255.0, 2.2) * brightness_fullwhite;
        cumulative_correction = std::min(cumulative_correction, brightness_fullwhite);
        cumulative_correction = std::max(cumulative_correction, brightness_grayscale_gamma);
        ALOGI("Estimated screen brightness: %.0f", cumulative_correction);

        float sensor_raw_corrected = std::max(event.u.scalar - cumulative_correction, 0.0f);

        float agc_gain = conf.sensor_inverse_gain[0];
        if (sensor_raw_corrected > conf.agc_threshold) {
            if (!conf.hbr) {
                float gain_estimate = sensor_raw_corrected / event.u.data[2];
                if (gain_estimate > 85.0) {
                    agc_gain = conf.sensor_inverse_gain[0];
                } else if (gain_estimate >= 39.0) {
                    agc_gain = conf.sensor_inverse_gain[1];
                } else if (gain_estimate >= 29.0) {
                    agc_gain = conf.sensor_inverse_gain[2];
                } else {
                    agc_gain = conf.sensor_inverse_gain[3];
                }
            } else {
                float gain_estimate = event.u.data[2] * 1000.0 / event.u.scalar;
                if (gain_estimate > 1050.0) {
                    agc_gain = conf.sensor_inverse_gain[3];
                } else if (gain_estimate > 800.0) {
                    agc_gain = conf.sensor_inverse_gain[2];
                } else if (gain_estimate > 450.0) {
                    agc_gain = conf.sensor_inverse_gain[1];
                } else {
                    agc_gain = conf.sensor_inverse_gain[0];
                }
            }
        }
        ALOGI("AGC gain: %f", agc_gain);

        if (cumulative_correction <= event.u.scalar * 1.35
                || event.u.scalar * conf.calib_gain * agc_gain < 10000.0
                || state.force_update) {
            float sensor_corrected = sensor_raw_corrected * conf.calib_gain * agc_gain;
            state.last_agc_gain = agc_gain;
            for (auto& range : hysteresis_ranges) {
                if (sensor_corrected <= range.middle) {
                    state.hyst_min = range.min;
                    state.hyst_max = range.max + brightness_fullwhite;
                    break;
                }
            }
            sensor_corrected = std::max(sensor_corrected - 14.0, 0.0);
            event.u.scalar = sensor_corrected;
            state.last_corrected_value = sensor_corrected;
            ALOGI("Fully corrected sensor value: %.0f lux", sensor_corrected);
        } else {
            event.u.scalar = state.last_corrected_value;
            ALOGI("Reusing cached value: %.0f lux", event.u.scalar);
        }

        state.force_update = false;
    } else {
        event.u.scalar = state.last_corrected_value;
        ALOGI("Reusing cached value: %.0f lux", event.u.scalar);
    }
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
