#include "GPButton.h"
#include "GPGFX_UI_layouts.h"
#include "BoardConfig.h"

void GPButton::draw() {
    // new style button:
    uint16_t baseX = this->x;
    uint16_t baseY = this->y;
    // 按下态：低有效引脚低电平=按下，高有效引脚高电平=按下（BUTTON_ACTIVE_HIGH_MASK）
#ifdef BUTTON_ACTIVE_HIGH_MASK
    const Mask_t activeHighMask = (Mask_t)BUTTON_ACTIVE_HIGH_MASK;
#else
    const Mask_t activeHighMask = 0;
#endif
    const Mask_t gpioState = gpio_get_all64();
    const Mask_t pinValues = (~gpioState & ~activeHighMask)
                           |  (gpioState &  activeHighMask);

    // scale to viewport
    double scaleX = this->getScaleX();
    double scaleY = this->getScaleY();

    // set scale on X & Y to be proportionate if either is 0
    if ((scaleX > 0.0f) & ((scaleY == 0.0f) || (scaleY == 1.0f))) {
        scaleY = scaleX;
    } else if (((scaleX == 0.0f) || (scaleX == 1.0f)) & (scaleY > 0.0f)) {
        scaleX = scaleY;
    }

    uint16_t offsetX = ((getRenderer()->getDriver()->getMetrics()->width - (uint16_t)((double)(this->getViewport().right - this->getViewport().left) * scaleX)) / 2);
    uint16_t offsetY = ((getRenderer()->getDriver()->getMetrics()->height - (uint16_t)((double)(this->getViewport().bottom - this->getViewport().top) * scaleY)) / 2);

    if (scaleX > 0.0f) {
        baseX = ((this->x) * scaleX + this->getViewport().left) + offsetX;
    }

    if (scaleY > 0.0f) {
        baseY = ((this->y) * scaleY + this->getViewport().top) + offsetY;
    }

    bool pinState = false;
    bool buttonState = false;
    uint16_t state = 0;
    int16_t setPin = -1;
    Mask_t maskedPins = 0;
    bool useMask = false;
    GamepadButtonMapping *mapMask = NULL;

    if (_inputType == GP_ELEMENT_BTN_BUTTON) {
        // button mask
        buttonState = getProcessedGamepad()->pressedButton(this->_inputMask);
        useMask = true;

        if ((this->_inputMask & GAMEPAD_MASK_B1) == GAMEPAD_MASK_B1) {
            mapMask = getGamepad()->mapButtonB1;
        } else if ((this->_inputMask & GAMEPAD_MASK_B2) == GAMEPAD_MASK_B2) {
            mapMask = getGamepad()->mapButtonB2;
        } else if ((this->_inputMask & GAMEPAD_MASK_B3) == GAMEPAD_MASK_B3) {
            mapMask = getGamepad()->mapButtonB3;
        } else if ((this->_inputMask & GAMEPAD_MASK_B4) == GAMEPAD_MASK_B4) {
            mapMask = getGamepad()->mapButtonB4;
        } else if ((this->_inputMask & GAMEPAD_MASK_L1) == GAMEPAD_MASK_L1) {
            mapMask = getGamepad()->mapButtonL1;
        } else if ((this->_inputMask & GAMEPAD_MASK_R1) == GAMEPAD_MASK_R1) {
            mapMask = getGamepad()->mapButtonR1;
        } else if ((this->_inputMask & GAMEPAD_MASK_L2) == GAMEPAD_MASK_L2) {
            mapMask = getGamepad()->mapButtonL2;
        } else if ((this->_inputMask & GAMEPAD_MASK_R2) == GAMEPAD_MASK_R2) {
            mapMask = getGamepad()->mapButtonR2;
        } else if ((this->_inputMask & GAMEPAD_MASK_S1) == GAMEPAD_MASK_S1) {
            mapMask = getGamepad()->mapButtonS1;
        } else if ((this->_inputMask & GAMEPAD_MASK_S2) == GAMEPAD_MASK_S2) {
            mapMask = getGamepad()->mapButtonS2;
        } else if ((this->_inputMask & GAMEPAD_MASK_L3) == GAMEPAD_MASK_L3) {
            mapMask = getGamepad()->mapButtonL3;
        } else if ((this->_inputMask & GAMEPAD_MASK_R3) == GAMEPAD_MASK_R3) {
            mapMask = getGamepad()->mapButtonR3;
        } else if ((this->_inputMask & GAMEPAD_MASK_A1) == GAMEPAD_MASK_A1) {
            mapMask = getGamepad()->mapButtonA1;
        } else if ((this->_inputMask & GAMEPAD_MASK_A2) == GAMEPAD_MASK_A2) {
            mapMask = getGamepad()->mapButtonA2;
        }
    } else if (_inputType == GP_ELEMENT_DIR_BUTTON) {
        // direction button mask
        buttonState = getProcessedGamepad()->state.dpadOriginal & this->_inputMask;
        useMask = true;

        if ((this->_inputMask & GAMEPAD_MASK_UP) == GAMEPAD_MASK_UP) {
            mapMask = getGamepad()->mapDpadUp;
        } else if ((this->_inputMask & GAMEPAD_MASK_DOWN) == GAMEPAD_MASK_DOWN) {
            mapMask = getGamepad()->mapDpadDown;
        } else if ((this->_inputMask & GAMEPAD_MASK_LEFT) == GAMEPAD_MASK_LEFT) {
            mapMask = getGamepad()->mapDpadLeft;
        } else if ((this->_inputMask & GAMEPAD_MASK_RIGHT) == GAMEPAD_MASK_RIGHT) {
            mapMask = getGamepad()->mapDpadRight;
        }
    } else if (_inputType == GP_ELEMENT_PIN_BUTTON) {
        // physical pin
        pinState = ((pinValues >> this->_inputMask) & 0x01);
        buttonState = true;
    }

    if (useMask && mapMask != NULL) {
        maskedPins = (pinValues & mapMask->pinMask);
        for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
            if ((maskedPins & (Mask_t{1} << pin)) == (Mask_t{1} << pin)) {
                setPin = pin;
                break;
            }
        }

        if (setPin > -1) {
            pinState = ((pinValues >> setPin) & 0x01);
        }
    }

    state = (buttonState || pinState) ? 1 : 0;

    // base
    if (this->_shape == GP_SHAPE_ELLIPSE) {
        uint16_t scaledSize = (uint16_t)((double)this->_sizeX * scaleX);
        uint16_t baseRadius = (uint16_t)scaledSize;

        getRenderer()->drawEllipse(baseX, baseY, baseRadius, baseRadius, this->strokeColor, state);
    } else if (this->_shape == GP_SHAPE_SQUARE) {
        uint16_t sizeX = (this->_sizeX) * scaleX + this->getViewport().left;
        uint16_t sizeY = (this->_sizeY) * scaleY + this->getViewport().top;

        getRenderer()->drawRectangle(baseX, baseY, sizeX+offsetX, sizeY, this->strokeColor, state, this->_angle);
    } else if (this->_shape == GP_SHAPE_LINE) {
        getRenderer()->drawLine(baseX, baseY, this->_sizeX, this->_sizeY, this->strokeColor, 0);
    } else if (this->_shape == GP_SHAPE_POLYGON) {
        uint16_t scaledSize = (uint16_t)((double)this->_sizeX * scaleX);
        uint16_t baseRadius = (uint16_t)scaledSize;

        getRenderer()->drawPolygon(baseX, baseY, baseRadius, this->_sizeY, this->strokeColor, state, this->_angle);
    } else if (this->_shape == GP_SHAPE_ARC) {
        uint16_t scaledSize = (uint16_t)((double)this->_sizeX * scaleX);
        uint16_t baseRadius = (uint16_t)scaledSize;

        getRenderer()->drawArc(baseX, baseY, baseRadius, baseRadius, this->strokeColor, state, this->_angle, this->_angleEnd, this->_closed);
    } else if (this->_shape == GP_SHAPE_PILL) {
        uint16_t sizeX = (this->_sizeX) * scaleX + this->getViewport().left;
        uint16_t sizeY = (this->_sizeY) * scaleY + this->getViewport().top;

        getRenderer()->drawPill(baseX, baseY, sizeX+offsetX, sizeY, this->strokeColor, state, this->_angle);
    }
}
