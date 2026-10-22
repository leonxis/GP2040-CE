/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#include "gp2040aux.h"
#include "gamepad.h"
#include "addons/wireless_receiver.h"

#include "drivermanager.h"
#include "storagemanager.h"
#include "usbhostmanager.h"

GP2040Aux::GP2040Aux() : inputDriver(nullptr), isReady(false) {
}

GP2040Aux::~GP2040Aux() {
}

// GP2040Aux will always come after GP2040 setup(), so we can rely on the
// GP2040 setup function for certain setup functions.
void GP2040Aux::setup() {
        // Initialize our input driver's auxilliary functions
        inputDriver = DriverManager::getInstance().getDriver();
        if ( inputDriver != nullptr ) {
                inputDriver->initializeAux();

                // Check if we have a USB listener (PS4Auth / XInputAuth 认证狗)
                USBListener * listener = inputDriver->get_usb_auth_listener();
                if (listener != nullptr) {
                        USBHostManager::getInstance().pushListener(listener);
                }
        }

        // 接收端：关闭 AddonManager 全部插件加载入口
        // （原先 Display/NeoPicoLED/PlayerLED/BuzzerSpeaker/DRV8833Rumble/ReactiveLED 等）
        // 状态灯已由主核 WirelessReceiver 直接驱动 NeoPico

        // Ready to sync Core0 and Core1
        isReady = true;
}

void GP2040Aux::run() {
        while (1) {
                // 状态灯（WS2812/GPIO LED）：读取 core0 写入的 paired/lastPacketTime
                wirelessReceiver.process();

                // Run auxiliary functions for input driver on Core1
                if ( inputDriver != nullptr ) {
                        inputDriver->processAux();
                }
        }
}
