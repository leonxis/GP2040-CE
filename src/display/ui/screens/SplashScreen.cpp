#include "SplashScreen.h"

#include "pico/stdlib.h"
#include "drivermanager.h"

void SplashScreen::init() {
    getRenderer()->clearScreen();
    splashStartTime = getMillis();
    imageStartTime = getMillis();
    currentImageIndex = 0;
    loopCount = 0;
    configMode = DriverManager::getInstance().isConfigMode();
    
    // Count valid images using has_ flags
    // has_splashImage2 = true means user has uploaded a valid (non-deleted) image
    totalValidImages = 1; // splashImage is always valid (has default if not set)
    if (getDisplayOptions().has_splashImage2) {
        totalValidImages++;
    }
}

void SplashScreen::shutdown() {
    clearElements();
}

void SplashScreen::drawScreen() {
	if (getDisplayOptions().splashMode == static_cast<SplashMode>(SPLASH_MODE_NONE)) {
		getRenderer()->drawText(0, 4, " Splash NOT enabled.");
    } else {
        // Display image based on current index
        const uint8_t* imageData = nullptr;
        
        // Find the Nth valid image using has_ flags
        uint8_t validCount = 0;
        
        // splashImage is always available (image 1)
        if (validCount == currentImageIndex) {
            imageData = getDisplayOptions().splashImage.bytes;
        }
        validCount++;
        
        // splashImage2 is only available if has_splashImage2 is true
        if (imageData == nullptr && getDisplayOptions().has_splashImage2) {
            if (validCount == currentImageIndex) {
                imageData = getDisplayOptions().splashImage2.bytes;
            }
            validCount++;
        }
        
        // Fallback to first image if index is out of range
        if (imageData == nullptr) {
            imageData = getDisplayOptions().splashImage.bytes;
        }
        
        getRenderer()->drawSprite((uint8_t*) imageData, 128, 64, 16, 0, 0, 1);
	}
}

int8_t SplashScreen::update() {
    uint32_t currentTime = getMillis();
    uint32_t splashDuration = getDisplayOptions().splashDuration;
    uint32_t animationDuration = getDisplayOptions().splashAnimationDuration;
    
    if (!configMode) {
        // Priority 1: Check if splash mode is disabled
        if (getDisplayOptions().splashMode == static_cast<SplashMode>(SPLASH_MODE_NONE)) {
            return DisplayMode::BUTTONS;
        }
        
        // Priority 2: Animation duration == 0 also means skip splash (same effect as disabled)
        if (animationDuration == 0) {
            return DisplayMode::BUTTONS;
        }
        
        // Splash is enabled and animation duration > 0, proceed with animation logic
        if (totalValidImages > 1) {
            // Multi-image animation: cycle through images
            uint32_t imageElapsed = currentTime - imageStartTime;
            
            // Check if it's time to switch to next image
            if (imageElapsed >= animationDuration) {
                currentImageIndex++;
                
                // Check if we completed a full cycle
                if (currentImageIndex >= totalValidImages) {
                    currentImageIndex = 0;
                    loopCount++;
                    
                    // Check if we should stop (splashDuration != 0 means finite loops)
                    if (splashDuration != 0 && loopCount >= splashDuration) {
                        return DisplayMode::BUTTONS;
                    }
                }
                
                imageStartTime = currentTime;
                // Trigger redraw by returning current mode (forces refresh)
                return DisplayMode::SPLASH;
            }
        } else {
            // Single image: display for splashDuration loops
            if (splashDuration != 0) {
                uint32_t totalElapsed = currentTime - splashStartTime;
                uint32_t totalDuration = splashDuration * animationDuration;
                if (totalElapsed >= totalDuration) {
                    return DisplayMode::BUTTONS;
                }
            }
            // If splashDuration == 0, stay on splash forever (always on mode)
        }
    } else {
        uint16_t buttonState = getGamepad()->state.buttons;
        if (prevButtonState && !buttonState) {
            if (prevButtonState == GAMEPAD_MASK_B2) {
                prevButtonState = 0;
                return DisplayMode::CONFIG_INSTRUCTION;
            }
        }
        prevButtonState = buttonState;
    }
    return -1; // -1 means no change in screen state
}
