/*
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ScreenCapture.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using ::aidl::vendor::lineage::screencapture::ScreenCapture;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    ABinderProcess_startThreadPool();
    std::shared_ptr<ScreenCapture> sc = ndk::SharedRefBase::make<ScreenCapture>();

    const std::string instance = std::string(ScreenCapture::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(sc->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
