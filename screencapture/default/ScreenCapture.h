/*
 * Copyright (C) 2021-2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/lineage/screencapture/BnScreenCapture.h>

namespace aidl {
namespace vendor {
namespace lineage {
namespace screencapture {

class ScreenCapture : public BnScreenCapture {
  public:
    ScreenCapture();
    ndk::ScopedAStatus getBrightnessValues(RgbCaptureResult* _aidl_return) override;

  private:
    static ::android::sp<::android::IBinder> getInternalDisplayToken();
};

}  // namespace screencapture
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
