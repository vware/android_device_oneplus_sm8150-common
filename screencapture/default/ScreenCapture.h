/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/lineage/screencapture/BnScreenCapture.h>
#include <binder/ProcessState.h>

namespace aidl {
namespace vendor {
namespace lineage {
namespace screencapture {

class ScreenCapture : public BnScreenCapture {
  private:
    static ::android::sp<::android::IBinder> getInternalDisplayToken();
  
  public:
    ndk::ScopedAStatus getBrightnessValues(RgbCaptureResult* _aidl_return) override;
};

}  // namespace screencapture
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
