/*
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

package vendor.lineage.screencapture;

import vendor.lineage.screencapture.RgbCaptureResult;

@VintfStability
interface IScreenCapture {
    RgbCaptureResult getBrightnessValues();
}
