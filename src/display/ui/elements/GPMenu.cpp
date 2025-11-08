#include "GPMenu.h"
#include "GPGFX_core.h"

#include <string>

void GPMenu::draw() {
    if (this->getVisibility()) {
        uint16_t baseX = this->x;
        uint16_t baseY = this->y;

        uint16_t menuWidth = this->menuSizeX * 6;
        uint16_t menuHeight = this->menuSizeY * 8;

        uint16_t dataSize = this->getDataSize();
        
        // Check layout mode: 2 = two columns, 3 = three columns, 4 = four columns
        bool twoColumnMode = (this->menuSizeX == 2);
        bool threeColumnMode = (this->menuSizeX == 3);
        bool fourColumnMode = (this->menuSizeX == 4);
        uint16_t itemsPerPage = twoColumnMode ? (this->menuSizeY * 2) : (threeColumnMode ? (this->menuSizeY * 3) : (fourColumnMode ? (this->menuSizeY * 4) : this->menuSizeY));
        uint16_t totalPages = (dataSize + itemsPerPage - 1) / itemsPerPage;
        uint16_t itemPage = (this->menuIndex / itemsPerPage);

        int16_t currPageItems = (dataSize - (itemPage * itemsPerPage));
        if (currPageItems > itemsPerPage) {
            currPageItems = itemsPerPage;
        } else if (currPageItems <= 0) {
            currPageItems = 0;
        }

        getRenderer()->drawText((21-this->menuTitle.length()) / 2, 0, this->menuTitle.c_str());

        std::string pageDisplay = "";
        // For four-column mode with NONE, adjust page display
        uint16_t displayPage = itemPage + 1;
        uint16_t displayTotalPages = totalPages;
        if (fourColumnMode && dataSize > 0 && this->menuEntryData->size() > 0 && this->menuEntryData->at(0).label == "NONE") {
            // Adjust page calculation: if menuIndex == 0, it's page 1; otherwise (menuIndex - 1) / itemsPerPage + 1
            uint16_t adjustedPage = (this->menuIndex == 0) ? 1 : ((this->menuIndex - 1) / itemsPerPage + 1);
            displayPage = adjustedPage;
            // Total pages: (dataSize - 1) / itemsPerPage + 1 (excluding NONE from count)
            displayTotalPages = ((dataSize - 1) + itemsPerPage - 1) / itemsPerPage;
        }
        pageDisplay += "Page: " + std::to_string(displayPage) + "/" + std::to_string(displayTotalPages);
        getRenderer()->drawText(11, 7, pageDisplay.c_str());

        if (this->menuEntryData->size() > 0) {
            // Special handling for four-column mode with NONE on first line
            if (fourColumnMode && dataSize > 0 && this->menuEntryData->at(0).label == "NONE") {
                // Four-column layout with NONE on first line
                // For first page, we need special handling: NONE is at index 0, but it's displayed separately
                // So items 1-16 should all be on first page
                // itemPage calculation: if menuIndex == 0, it's first page; if menuIndex >= 1, page = (menuIndex - 1) / itemsPerPage
                uint16_t adjustedItemPage = (this->menuIndex == 0) ? 0 : ((this->menuIndex - 1) / itemsPerPage);
                bool isFirstPage = (adjustedItemPage == 0);
                
                // Draw NONE on first line if on first page
                if (isFirstPage) {
                    int32_t lineValue = this->menuEntryData->at(0).optionValue;
                    bool showCurrentOption = false;
                    if (lineValue != -1) {
                        showCurrentOption = (this->menuEntryData->at(0).currentValue() == this->menuEntryData->at(0).optionValue);
                    }
                    // Draw NONE label without asterisk
                    std::string label = this->menuEntryData->at(0).label;
                    // Center NONE on first line (screen width is 21 chars, NONE is 4 chars, so center at (21-4)/2 = 8.5 -> 8)
                    uint16_t noneX = 8;
                    getRenderer()->drawText(noneX, 1, label.c_str());
                    // Draw asterisk immediately after NONE text
                    if (showCurrentOption) {
                        getRenderer()->drawText(noneX + label.length(), 1, "*");
                    }
                    
                    // Draw cursor for NONE if selected
                    if (this->menuIndex == 0) {
                        getRenderer()->drawText(7, 1, CHAR_RIGHT);
                    }
                }
                
                // Draw other items in four columns starting from row 2 (or row 1 if not first page)
                // First page: NONE on row 1, other items on rows 2-5 (4 rows available, indices 1-16)
                // Other pages: items on rows 1-4 (4 rows available)
                uint16_t startRow = isFirstPage ? 2 : 1;
                uint16_t availableRows = isFirstPage ? 4 : 4; // 4 rows for both pages
                uint16_t startIndex = isFirstPage ? 1 : 0;
                // For first page, show all items from index 1 to 16 (16 items)
                // For other pages, calculate normally
                uint16_t itemsToShow;
                uint16_t pageStartIndex;
                if (isFirstPage) {
                    // First page: show indices 1-16 (16 items, excluding NONE at index 0)
                    itemsToShow = (dataSize > 16) ? 16 : (dataSize - 1);
                    pageStartIndex = 1;
                } else {
                    // Other pages: calculate normally, but adjust for NONE
                    // adjustedItemPage is already calculated above
                    pageStartIndex = 1 + (adjustedItemPage * itemsPerPage);
                    uint16_t remainingItems = dataSize - pageStartIndex;
                    itemsToShow = (remainingItems > itemsPerPage) ? itemsPerPage : remainingItems;
                }
                
                // Column positions: screen width is 21 chars, need to leave 1 char (6 pixels) for asterisk on the right
                // So available width is 20 chars, 4 columns = 5 chars per column
                // Column positions: 1 (cursor at 0), 6, 11, 16 (asterisk at 20)
                uint16_t colX[4] = {1, 6, 11, 16};
                
                for (uint8_t menuLine = 0; menuLine < availableRows && (menuLine * 4) < itemsToShow; menuLine++) {
                    for (uint8_t col = 0; col < 4; col++) {
                        uint8_t colIndex = pageStartIndex + (menuLine * 4) + col;
                        if (colIndex < dataSize && colIndex >= startIndex && (menuLine * 4 + col) < itemsToShow) {
                            int32_t lineValue = this->menuEntryData->at(colIndex).optionValue;
                            bool showCurrentOption = false;
                            if (lineValue != -1) {
                                showCurrentOption = (this->menuEntryData->at(colIndex).currentValue() == this->menuEntryData->at(colIndex).optionValue);
                            }
                            // Draw label without asterisk first, then draw asterisk directly after the label text
                            std::string label = this->menuEntryData->at(colIndex).label;
                            getRenderer()->drawText(colX[col], startRow + menuLine, label.c_str());
                            // Draw asterisk immediately after the label text (each character is 6 pixels wide, so 1 char position)
                            if (showCurrentOption) {
                                uint16_t asteriskX = colX[col] + label.length();
                                getRenderer()->drawText(asteriskX, startRow + menuLine, "*");
                            }
                        }
                    }
                }
                
                // Draw cursor for four-column mode
                // Use adjusted page calculation for cursor
                uint16_t cursorAdjustedPage = (this->menuIndex == 0) ? 0 : ((this->menuIndex - 1) / itemsPerPage);
                if (cursorAdjustedPage == adjustedItemPage) {
                    if (this->menuIndex == 0 && isFirstPage) {
                        // Cursor on NONE (NONE is centered at column 8, cursor at 7)
                        getRenderer()->drawText(7, 1, CHAR_RIGHT);
                    } else {
                        // Calculate cursor position based on adjusted page
                        if (isFirstPage) {
                            // First page: menuIndex 1-16 map to positions 0-15
                            uint16_t cursorPageIndex = this->menuIndex - 1; // Adjust for NONE (index 0)
                            uint16_t cursorColumn = cursorPageIndex % 4;
                            uint16_t cursorRow = cursorPageIndex / 4;
                            // Cursor position: left of column start to avoid overlap
                            uint16_t cursorX = (cursorColumn == 0 ? 0 : (cursorColumn == 1 ? 5 : (cursorColumn == 2 ? 10 : 15)));
                            if (cursorRow < availableRows) {
                                getRenderer()->drawText(cursorX, startRow + cursorRow, CHAR_RIGHT);
                            }
                        } else {
                            // Other pages: calculate relative to pageStartIndex
                            uint16_t cursorPageIndex = this->menuIndex - pageStartIndex;
                            uint16_t cursorColumn = cursorPageIndex % 4;
                            uint16_t cursorRow = cursorPageIndex / 4;
                            // Cursor position: left of column start to avoid overlap
                            uint16_t cursorX = (cursorColumn == 0 ? 0 : (cursorColumn == 1 ? 5 : (cursorColumn == 2 ? 10 : 15)));
                            if (cursorRow < availableRows) {
                                getRenderer()->drawText(cursorX, startRow + cursorRow, CHAR_RIGHT);
                            }
                        }
                    }
                }
            } else if (threeColumnMode && dataSize > 0 && this->menuEntryData->at(0).label == "NONE") {
                // Three-column layout with NONE on first line
                uint16_t itemsPerColumn = this->menuSizeY;
                bool isFirstPage = (itemPage == 0);
                
                // Draw NONE on first line if on first page
                if (isFirstPage) {
                    int32_t lineValue = this->menuEntryData->at(0).optionValue;
                    bool showCurrentOption = false;
                    if (lineValue != -1) {
                        showCurrentOption = (this->menuEntryData->at(0).currentValue() == this->menuEntryData->at(0).optionValue);
                    }
                    std::string label = this->menuEntryData->at(0).label + (showCurrentOption ? " *" : "");
                    getRenderer()->drawText(7, 1, label.c_str()); // Center NONE on first line
                    
                    // Draw cursor for NONE if selected
                    if (this->menuIndex == 0) {
                        getRenderer()->drawText(6, 1, CHAR_RIGHT);
                    }
                }
                
                // Draw other items in three columns starting from row 2 (or row 1 if not first page)
                // First page: NONE on row 1, other items on rows 2-6 (5 rows available)
                // Other pages: items on rows 1-6 (6 rows available)
                uint16_t startRow = isFirstPage ? 2 : 1;
                uint16_t availableRows = isFirstPage ? 5 : 6; // 5 rows for first page, 6 rows for other pages
                uint16_t startIndex = isFirstPage ? 1 : 0;
                uint16_t itemsToShow = isFirstPage ? (currPageItems - 1) : currPageItems;
                uint16_t pageStartIndex = isFirstPage ? 1 : (itemsPerPage * itemPage);
                
                for (uint8_t menuLine = 0; menuLine < availableRows && (menuLine * 3) < itemsToShow; menuLine++) {
                    // Column 1
                    uint8_t col1Index = pageStartIndex + (menuLine * 3);
                    if (col1Index < dataSize && col1Index >= startIndex) {
                        int32_t lineValue = this->menuEntryData->at(col1Index).optionValue;
                        bool showCurrentOption = false;
                        if (lineValue != -1) {
                            showCurrentOption = (this->menuEntryData->at(col1Index).currentValue() == this->menuEntryData->at(col1Index).optionValue);
                        }
                        std::string label = this->menuEntryData->at(col1Index).label + (showCurrentOption ? " *" : "");
                        getRenderer()->drawText(0, startRow + menuLine, label.c_str());
                    }
                    
                    // Column 2
                    uint8_t col2Index = pageStartIndex + (menuLine * 3) + 1;
                    if (col2Index < dataSize && col2Index >= startIndex && (menuLine * 3 + 1) < itemsToShow) {
                        int32_t lineValue = this->menuEntryData->at(col2Index).optionValue;
                        bool showCurrentOption = false;
                        if (lineValue != -1) {
                            showCurrentOption = (this->menuEntryData->at(col2Index).currentValue() == this->menuEntryData->at(col2Index).optionValue);
                        }
                        std::string label = this->menuEntryData->at(col2Index).label + (showCurrentOption ? " *" : "");
                        getRenderer()->drawText(7, startRow + menuLine, label.c_str());
                    }
                    
                    // Column 3
                    uint8_t col3Index = pageStartIndex + (menuLine * 3) + 2;
                    if (col3Index < dataSize && col3Index >= startIndex && (menuLine * 3 + 2) < itemsToShow) {
                        int32_t lineValue = this->menuEntryData->at(col3Index).optionValue;
                        bool showCurrentOption = false;
                        if (lineValue != -1) {
                            showCurrentOption = (this->menuEntryData->at(col3Index).currentValue() == this->menuEntryData->at(col3Index).optionValue);
                        }
                        std::string label = this->menuEntryData->at(col3Index).label + (showCurrentOption ? " *" : "");
                        getRenderer()->drawText(14, startRow + menuLine, label.c_str());
                    }
                }
                
                // Draw cursor for three-column mode
                uint16_t cursorPage = this->menuIndex / itemsPerPage;
                if (cursorPage == itemPage) {
                    if (this->menuIndex == 0 && isFirstPage) {
                        // Cursor on NONE
                        getRenderer()->drawText(6, 1, CHAR_RIGHT);
                    } else {
                        uint16_t cursorPageIndex = this->menuIndex % itemsPerPage;
                        if (isFirstPage && cursorPageIndex > 0) {
                            cursorPageIndex--; // Adjust for NONE
                            uint16_t cursorColumn = cursorPageIndex % 3;
                            uint16_t cursorRow = cursorPageIndex / 3;
                            uint16_t cursorX = cursorColumn == 0 ? 0 : (cursorColumn == 1 ? 6 : 13);
                            if (cursorRow < availableRows) {
                                getRenderer()->drawText(cursorX, startRow + cursorRow, CHAR_RIGHT);
                            }
                        } else if (!isFirstPage) {
                            uint16_t cursorPageIndex = this->menuIndex % itemsPerPage;
                            uint16_t cursorColumn = cursorPageIndex % 3;
                            uint16_t cursorRow = cursorPageIndex / 3;
                            uint16_t cursorX = cursorColumn == 0 ? 0 : (cursorColumn == 1 ? 6 : 13);
                            if (cursorRow < availableRows) {
                                getRenderer()->drawText(cursorX, startRow + cursorRow, CHAR_RIGHT);
                            }
                        }
                    }
                }
            } else if (twoColumnMode) {
                // Two-column layout
                uint16_t itemsPerColumn = this->menuSizeY;
                for (uint8_t menuLine = 0; menuLine < itemsPerColumn && (menuLine * 2) < currPageItems; menuLine++) {
                    // Left column
                    uint8_t leftPageLine = (itemsPerPage * itemPage) + (menuLine * 2);
                    if (leftPageLine < dataSize) {
                        int32_t lineValue = this->menuEntryData->at(leftPageLine).optionValue;
                        bool showCurrentOption = false;
                        if (lineValue != -1) {
                            showCurrentOption = (this->menuEntryData->at(leftPageLine).currentValue() == this->menuEntryData->at(leftPageLine).optionValue);
                        }
                        std::string label = this->menuEntryData->at(leftPageLine).label + (showCurrentOption ? " *" : "");
                        getRenderer()->drawText(1, 2+menuLine, label.c_str());
                    }
                    
                    // Right column
                    uint8_t rightPageLine = (itemsPerPage * itemPage) + (menuLine * 2) + 1;
                    if (rightPageLine < dataSize && (menuLine * 2 + 1) < currPageItems) {
                        int32_t lineValue = this->menuEntryData->at(rightPageLine).optionValue;
                        bool showCurrentOption = false;
                        if (lineValue != -1) {
                            showCurrentOption = (this->menuEntryData->at(rightPageLine).currentValue() == this->menuEntryData->at(rightPageLine).optionValue);
                        }
                        std::string label = this->menuEntryData->at(rightPageLine).label + (showCurrentOption ? " *" : "");
                        getRenderer()->drawText(11, 2+menuLine, label.c_str());
                    }
                }
                
                // Draw cursor for two-column mode
                // Calculate which page the cursor is on
                uint16_t cursorPage = this->menuIndex / itemsPerPage;
                if (cursorPage == itemPage) {
                    // Cursor is on current page, draw it
                    uint16_t cursorPageIndex = this->menuIndex % itemsPerPage;
                    uint16_t cursorColumn = cursorPageIndex % 2;
                    uint16_t cursorRow = cursorPageIndex / 2;
                    uint16_t cursorX = cursorColumn == 0 ? 0 : 10;
                    if (cursorRow < itemsPerColumn) {
                        getRenderer()->drawText(cursorX, 2+cursorRow, CHAR_RIGHT);
                    }
                }
            } else {
                // Single-column layout (original)
                for (uint8_t menuLine = 0; menuLine < currPageItems; menuLine++) {
                    uint8_t pageLine = (this->menuSizeY * itemPage) + menuLine;
                    int32_t lineValue = this->menuEntryData->at(pageLine).optionValue;
                    bool showCurrentOption = false;
                    if (lineValue != -1) {
                        showCurrentOption = (this->menuEntryData->at(pageLine).currentValue() == this->menuEntryData->at(pageLine).optionValue);
                    }
                    getRenderer()->drawText(2, 2+menuLine, this->menuEntryData->at(pageLine).label + (showCurrentOption ? " *" : ""));
                }
                
                // draw cursor
                getRenderer()->drawText(1, 2+(this->menuIndex % this->menuSizeY), CHAR_RIGHT);
            }
        }
    }
}
