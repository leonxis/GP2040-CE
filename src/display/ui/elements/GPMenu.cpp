#include "GPMenu.h"

#include <string>

void GPMenu::draw() {
    if (this->getVisibility()) {
        uint16_t dataSize = this->getDataSize();
        const bool multiColumn = (this->menuSizeX >= 2 && this->menuSizeX <= 4);
        const uint16_t numCols = multiColumn ? this->menuSizeX : 1;
        const uint16_t itemsPerPage = this->menuSizeY * numCols;
        const uint16_t totalPages = (dataSize + itemsPerPage - 1) / itemsPerPage;
        const uint16_t itemPage = this->menuIndex / itemsPerPage;

        int16_t currPageItems = dataSize - (itemPage * itemsPerPage);
        if (currPageItems > static_cast<int16_t>(itemsPerPage)) {
            currPageItems = itemsPerPage;
        } else if (currPageItems <= 0) {
            currPageItems = 0;
        }

        getRenderer()->drawText((21 - this->menuTitle.length()) / 2, 0, this->menuTitle.c_str());

        std::string pageDisplay = "Page: " + std::to_string(itemPage + 1) + "/" + std::to_string(totalPages);
        getRenderer()->drawText(11, 7, pageDisplay.c_str());

        if (this->menuEntryData->size() > 0) {
            for (uint16_t i = 0; i < static_cast<uint16_t>(currPageItems); i++) {
                uint16_t pageLine = itemPage * itemsPerPage + i;
                int32_t lineValue = this->menuEntryData->at(pageLine).optionValue;
                bool showCurrentOption = false;
                if (lineValue != -1) {
                    showCurrentOption = (this->menuEntryData->at(pageLine).currentValue() == this->menuEntryData->at(pageLine).optionValue);
                }
                uint16_t col = i % numCols;
                uint16_t row = i / numCols;
                uint16_t x = 2 + col * (21 / numCols);
                uint16_t y = 2 + row;
                getRenderer()->drawText(x, y, this->menuEntryData->at(pageLine).label + (showCurrentOption ? " *" : ""));
            }
        }

        // draw cursor
        uint16_t cursorIdx = this->menuIndex % itemsPerPage;
        uint16_t cursorCol = cursorIdx % numCols;
        uint16_t cursorRow = cursorIdx / numCols;
        getRenderer()->drawText(1 + cursorCol * (21 / numCols), 2 + cursorRow, CHAR_RIGHT);
    }
}
