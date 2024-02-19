/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ScreenCapture.h"

#include <binder/ProcessState.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/SyncScreenCaptureListener.h>
#include <ui/DisplayState.h>
#include <ui/PixelFormat.h>

#include <cstdio>
#include <signal.h>
#include <time.h>
#include <unistd.h>

using android::gui::ScreenCaptureResults;
using android::ui::PixelFormat;
using android::DisplayCaptureArgs;
using android::GraphicBuffer;
using android::IBinder;
using android::Rect;
using android::ScreenshotClient;
using android::sp;
using android::SurfaceComposerClient;
using aidl::vendor::lineage::screencapture::ScreenCapture;

constexpr int ALS_RADIUS = 64;
constexpr int ALS_X = 975;
constexpr int ALS_Y = 260;
constexpr int SCREENSHOT_INTERVAL = 1;

// See frameworks/base/services/core/jni/com_android_server_display_DisplayControl.cpp and
// frameworks/base/core/java/android/view/SurfaceControl.java
sp<IBinder> ScreenCapture::getInternalDisplayToken() {
    const auto displayIds = SurfaceComposerClient::getPhysicalDisplayIds();
    sp<IBinder> token = SurfaceComposerClient::getPhysicalDisplayToken(displayIds[0]);
    return token;
}

ndk::ScopedAStatus ScreenCapture::getBrightnessValues(RgbCaptureResult* _aidl_return) {
    _aidl_return->r=128;
    _aidl_return->g=128;
    _aidl_return->b=128;
    
    return ndk::ScopedAStatus::ok();

    /*static time_t lastScreenUpdate = 0;
    static sp<GraphicBuffer> outBuffer = new GraphicBuffer(
        ALS_RADIUS * 2 , ALS_RADIUS * 2, ::android::PIXEL_FORMAT_RGB_888,
        GraphicBuffer::USAGE_SW_READ_OFTEN | GraphicBuffer::USAGE_SW_WRITE_OFTEN);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (now.tv_sec - lastScreenUpdate < SCREENSHOT_INTERVAL) {
        ALOGV("Update skipped because interval not expired at %ld", now.tv_sec);
        return ndk::ScopedAStatus::ok();
    }

    sp<IBinder> display = getInternalDisplayToken();

    DisplayCaptureArgs captureArgs;
    captureArgs.displayToken = getInternalDisplayToken();
    captureArgs.pixelFormat = ::android::ui::PixelFormat::RGB_888;
    captureArgs.sourceCrop = Rect(ALS_X - ALS_RADIUS, ALS_Y - ALS_RADIUS,
        ALS_X + ALS_RADIUS, ALS_Y + ALS_RADIUS);
    captureArgs.width = ALS_RADIUS * 2;
    captureArgs.height = ALS_RADIUS * 2;
    captureArgs.useIdentityTransform = true;
    captureArgs.captureSecureLayers = true;

    ALOGV("Capture started at %ld", now.tv_sec);
    sp<::android::SyncScreenCaptureListener> captureListener = new ::android::SyncScreenCaptureListener();

    if (ScreenshotClient::captureDisplay(captureArgs, captureListener) != ::android::NO_ERROR) {
        ALOGE("Capture failed");
        return ndk::ScopedAStatus::ok();
    }

ALOGE("Capture get results");

    ScreenCaptureResults captureResults = captureListener->waitForResults();
    if (!captureResults.fenceResult.ok()) {
        ALOGE("Fence result error");
        return ndk::ScopedAStatus::ok();
    }

ALOGE("Capture got results");

    outBuffer = captureResults.buffer;
    lastScreenUpdate = now.tv_sec;

    uint8_t* out;
    auto resultWidth = outBuffer->getWidth();
    auto resultHeight = outBuffer->getHeight();
    auto stride = outBuffer->getStride();

    captureResults.buffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, reinterpret_cast<void**>(&out));
    // we can sum this directly on linear light
    uint32_t rsum = 0, gsum = 0, bsum = 0;
    for (int y = 0; y < resultHeight; y++) {
        for (int x = 0; x < resultWidth; x++) {
            rsum += out[y * (stride * 4) + x * 4];
            gsum += out[y * (stride * 4) + x * 4 + 1];
            bsum += out[y * (stride * 4) + x * 4 + 2];
        }
    }
    float max = 255 * resultWidth * resultHeight;

    captureResults.buffer->unlock();

    _aidl_return->r = rsum / max;
    _aidl_return->g = gsum / max;
    _aidl_return->b = bsum / max;

    return ndk::ScopedAStatus::ok();*/
}
