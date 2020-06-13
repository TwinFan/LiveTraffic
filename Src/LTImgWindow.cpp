/// @file       LTImgWindow.cpp
/// @brief      LiveTraffic-specific enhancements to ImGui / ImgWindow
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2020 Birger Hoppe
/// @copyright  Permission is hereby granted, free of charge, to any person obtaining a
///             copy of this software and associated documentation files (the "Software"),
///             to deal in the Software without restriction, including without limitation
///             the rights to use, copy, modify, merge, publish, distribute, sublicense,
///             and/or sell copies of the Software, and to permit persons to whom the
///             Software is furnished to do so, subject to the following conditions:\n
///             The above copyright notice and this permission notice shall be included in
///             all copies or substantial portions of the Software.\n
///             THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
///             IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
///             FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
///             AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
///             LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
///             OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
///             THE SOFTWARE.

#include "LiveTraffic.h"

#define FATAL_COULD_NOT_LOAD_FONT "Could not load GUI font %s"
#define WARN_NOT_LOADED_ICON_FONT "Could not load icon font, icons will not be displayed properly"

//
// MARK: ImGui extensions
//

namespace ImGui {

// Helper for creating unique IDs
IMGUI_API void PushID_formatted(const char* format, ...)
{
    // format the variable string
    va_list args;
    char sz[500];
    va_start (args, format);
    vsnprintf(sz, sizeof(sz), format, args);
    va_end (args);
    // Call the actual push function
    PushID(sz);
}

// Button with on-hover popup helper text
IMGUI_API bool ButtonTooltip(const char* label,
                             const char* tip,
                             ImU32 colFg,
                             ImU32 colBg,
                             const ImVec2& size)
{
    // Setup colors
    if (colFg != IM_COL32(1,1,1,0))
        ImGui::PushStyleColor(ImGuiCol_Text, colFg);
    if (colBg != IM_COL32(1,1,1,0))
        ImGui::PushStyleColor(ImGuiCol_Button, colBg);

    // do the button
    bool b = ImGui::Button(label, size);
    
    // restore previous colors
    if (colBg != IM_COL32(1,1,1,0))
        ImGui::PopStyleColor();
    if (colFg != IM_COL32(1,1,1,0))
        ImGui::PopStyleColor();

    // do the tooltip
    if (tip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    
    // return if button pressed
    return b;
}

}

//
// MARK: LTImgWindow implementation
//


// Constructor
LTImgWindow::LTImgWindow (WndMode _mode, WndStyle _style, WndRect _initPos) :
ImgWindow (_initPos.left(), _initPos.top(),
           _initPos.right(), _initPos.bottom(),
           toDeco(_style), toLayer(_style)),
wndStyle(_style),
rectFloat(_initPos)
{
    // Create a flight loop id, but don't schedule it yet
    XPLMCreateFlightLoop_t flDef = {
        sizeof(flDef),                              // structSize
        xplm_FlightLoop_Phase_BeforeFlightModel,    // phase
        cbChangeWndMode,                               // callbackFunc
        (void*)this,                                // refcon
    };
    flChangeWndMode = XPLMCreateFlightLoop(&flDef);
    LOG_ASSERT(flChangeWndMode);

    // For HUD-like windows set a transparent background
    if (wndStyle == WND_STYLE_HUD) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_WindowBg] = ImColor(0, 0, 0, 0x30);
        // Set a drag area (for moving the window by mouse) at the top
        SetWindowDragArea(0, 5, INT_MAX, 5 + 2*WND_FONT_SIZE);
    }
    
    // Set the positioning mode
    SetMode(_mode);
    
}

// Desctrucotr
LTImgWindow::~LTImgWindow()
{
    if (flChangeWndMode)
        XPLMDestroyFlightLoop(flChangeWndMode);
    flChangeWndMode = nullptr;
}



/// Set the window mode, move the window if needed
void LTImgWindow::SetMode (WndMode _mode)
{
    // auto-set VR mode if requested
    if (_mode == WND_MODE_FLOAT_OR_VR)
        _mode = dataRefs.IsVREnabled() ? WND_MODE_VR : WND_MODE_FLOAT;
    else if (_mode == WND_MODE_FLOAT_CNT_VR)
        _mode = dataRefs.IsVREnabled() ? WND_MODE_VR : WND_MODE_FLOAT_CENTERED;

    // Floating: Save current geometry to have a chance to get back there
    if (GetMode() == WND_MODE_FLOAT && _mode != WND_MODE_FLOAT)
        rectFloat = GetCurrentWindowGeometry();
    
    // Do set the XP window positioning mode
    SetWindowPositioningMode(toPosMode(_mode));
    
    // reset a wish to re-position
    nextWinMode = WND_MODE_NONE;

    // If we pop in, then we need to explicitely set a position for the window to appear
    if (_mode == WND_MODE_FLOAT && !rectFloat.empty()) {
        SetWindowGeometry(rectFloat.left(),  rectFloat.top(),
                          rectFloat.right(), rectFloat.bottom());
        rectFloat.clear();
    }
    // if we set any of the "centered" modes
    // we shall set it back to floating a few flight loops later
    else if (_mode == WND_MODE_FLOAT_CENTERED)
    {
        nextWinMode = WND_MODE_FLOAT;           // to floating
        rectFloat.clear();                      // but don't move the window!
        XPLMScheduleFlightLoop(flChangeWndMode, -5.0, 1);  // in 5 flight loops time
    }
}

/// Get current window mode
WndMode LTImgWindow::GetMode () const
{
    if (IsInVR())       return WND_MODE_VR;
    if (IsPoppedOut())  return WND_MODE_POPOUT;
    return WND_MODE_FLOAT;
}


// Get current window geometry as an WndRect structure
WndRect LTImgWindow::GetCurrentWindowGeometry () const
{
    WndRect r;
    ImgWindow::GetCurrentWindowGeometry(r.left(), r.top(), r.right(), r.bottom());
    return r;
}

// Loose keyboard foucs, ie. return focus to X-Plane proper, if I have it now
bool LTImgWindow::ReturnKeyboardFocus ()
{
    if (XPLMHasKeyboardFocus(GetWindowId())) {
        XPLMTakeKeyboardFocus(0);
        return true;
    }
    return false;
}

//
// MARK: ImGui painting
//

/// Paints title, decorative lines, and window buttons
void LTImgWindow::buildTitleBar (const std::string& _title,
                                 bool bCloseBtn,
                                 bool bWndBtns)
{
    const float btnWidth = GetWidthIconBtn();
    const ImGuiStyle& style = ImGui::GetStyle();
    
    // Close button
    if (bCloseBtn) {
        buildCloseButton();

        // Continue on the same line by painting 3 lines
        ImGui::SameLine();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos_start = ImGui::GetCursorPos();
        pos_start.y += 3;
        float x_end = pos_start.x + btnWidth;   // length of lines
        for (int i = 0; i < 3; i++) {
            draw_list->AddLine(pos_start, {x_end, pos_start.y},
                               ImGui::GetColorU32(ImGuiCol_ScrollbarGrabActive),
                               1.0f);
            pos_start.y += 5;
        }
        
        // title goes on the same line
        ImGui::SameLine();
        ImGui::SetCursorPosX(x_end + style.ItemSpacing.x);
    }
    
    // Title text
    ImGui::TextUnformatted(_title.c_str());
    
    // Continue on the same line by painting 3 lines
    ImGui::SameLine();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos_start = ImGui::GetCursorPos();
    pos_start.y += 3;
    float x_end = ImGui::GetWindowContentRegionWidth();
    if (bWndBtns)
        x_end -= 2 * GetWidthIconBtn();     // space for window buttons
    for (int i = 0; i < 3; i++) {
        draw_list->AddLine(pos_start, {x_end, pos_start.y},
                           ImGui::GetColorU32(ImGuiCol_ScrollbarGrabActive),
                           1.0f);
        pos_start.y += 5;
    }
    
    // Window buttons
    if (bWndBtns)
        buildWndButtons();
}

// Paints close button
void LTImgWindow::buildCloseButton ()
{
    // Setup colors for window sizing buttons
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_ScrollbarGrabActive)); // dark gray
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);                           // transparent
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_ScrollbarGrab)); // lighter gray

    if (ImGui::ButtonTooltip(ICON_FA_WINDOW_CLOSE, "Close the window"))
        nextWinMode = WND_MODE_CLOSE;

    // Restore colors
    ImGui::PopStyleColor(3);

    // Window mode should be set outside drawing calls to avoid crashes
    if (nextWinMode > WND_MODE_NONE)
        ScheduleWndModeChange();
}

/// Paints resizing buttons as needed as per current window status
void LTImgWindow::buildWndButtons ()
{
    // Button with fixed width 30 and standard height
    // to pop out the window in an OS window
    const float btnWidth = GetWidthIconBtn();
    const bool bBtnPopOut = !IsPoppedOut();
#ifdef APL
    // WORKAROUND: With metal, popping back in often crashes, so disable (does work in OpenGL mode, though)
    const bool bBtnPopIn  = !dataRefs.UsingModernDriver() && (IsPoppedOut() || IsInVR());
#else
    const bool bBtnPopIn  = IsPoppedOut() || IsInVR();
#endif
    const bool bBtnVR     = dataRefs.IsVREnabled() && !IsInVR();
    int numBtn = bBtnPopOut + bBtnPopIn + bBtnVR;
    if (numBtn > 0) {
        // Setup colors for window sizing buttons
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_ScrollbarGrabActive)); // dark gray
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);                           // transparent
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_ScrollbarGrab)); // lighter gray

        if (bBtnVR) {
            // Same line, but right-alinged
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - (numBtn * btnWidth));
            if (ImGui::ButtonTooltip(ICON_FA_EXTERNAL_LINK_SQUARE_ALT, "Move into VR"))
                nextWinMode = WND_MODE_VR;
            --numBtn;
        }
        if (bBtnPopIn) {
            // Same line, but right-alinged
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - (numBtn * btnWidth));
            if (ImGui::ButtonTooltip(ICON_FA_WINDOW_RESTORE, "Move back into X-Plane"))
                nextWinMode = WND_MODE_FLOAT;
            --numBtn;
        }
        if (bBtnPopOut) {
            // Same line, but right-alinged
            ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - (numBtn * btnWidth));
            if (ImGui::ButtonTooltip(ICON_FA_WINDOW_MAXIMIZE, "Pop out into separate window"))
                nextWinMode = WND_MODE_POPOUT;
            --numBtn;
        }

        // Restore colors
        ImGui::PopStyleColor(3);

        // Window mode should be set outside drawing calls to avoid crashes
        if (nextWinMode > WND_MODE_NONE)
            ScheduleWndModeChange();
    }

}


//
// MARK: Static and Global functions
//

// Include font 'fa-solid-900' into the binary
#include "fa-solid-900.inc"

// width of an icon button
float LTImgWindow::widthIconBtn = NAN;

// Get width of an icon button (calculate on first use)
float LTImgWindow::GetWidthIconBtn ()
{
    if (std::isnan(widthIconBtn))
        widthIconBtn = ImGui::CalcTextSize(ICON_FA_WINDOW_MAXIMIZE).x + 5;
    return widthIconBtn;
}

// flight loop callback for stuff we cannot do during drawing callback
// Outside all rendering we can change things like window mode
float LTImgWindow::cbChangeWndMode(float, float, int, void* inRefcon)
{
    // refcon is pointer to ImguiWidget
    LTImgWindow& wnd = *reinterpret_cast<LTImgWindow*>(inRefcon);
    
    // Has user requested to close the window?
    if (wnd.nextWinMode == WND_MODE_CLOSE)
        delete &wnd;

    // Has user requested a change in window mode?
    else if (wnd.nextWinMode > WND_MODE_NONE)
        wnd.SetMode(wnd.nextWinMode);
    
    // don't call me again
    return 0.0f;
}

// One-time initializations for all ImGui windows
bool LTImgWindowInit ()
{
    // Create one (and exactly one) font atlas, so all fonts are textured just once
    if (!ImgWindow::sFontAtlas)
        ImgWindow::sFontAtlas = std::make_shared<ImgFontAtlas>();
    
    // Load one standard font (comes with XP)
    if (!ImgWindow::sFontAtlas->AddFontFromFileTTF((dataRefs.GetXPSystemPath() + WND_STANDARD_FONT).c_str(),
                                                   WND_FONT_SIZE))
    {
        LOG_MSG(logFATAL, FATAL_COULD_NOT_LOAD_FONT, WND_STANDARD_FONT);
        return false;
    }
    
    // Now we merge some icons from the OpenFontsIcons font into the above font
    // (see `imgui/docs/FONTS.txt`)
    ImFontConfig config;
    config.MergeMode = true;
    
    // We only read very selectively the individual glyphs we are actually using
    // to safe on texture space
    static ImVector<ImWchar> icon_ranges;
    ImFontGlyphRangesBuilder builder;
    // Add all icons that are actually used (they concatenate into one string)
    builder.AddText(ICON_FA_TRASH_ALT ICON_FA_SEARCH
                    ICON_FA_EXTERNAL_LINK_SQUARE_ALT
                    ICON_FA_WINDOW_MAXIMIZE ICON_FA_WINDOW_MINIMIZE
                    ICON_FA_WINDOW_RESTORE ICON_FA_WINDOW_CLOSE);
    builder.BuildRanges(&icon_ranges);

    // Merge the icon font with the text font
    if (!ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(fa_solid_900_compressed_data,
                                                               fa_solid_900_compressed_size,
                                                               WND_FONT_SIZE,
                                                               &config,
                                                               icon_ranges.Data))
    {
        LOG_MSG(logWARN, "%s", WARN_NOT_LOADED_ICON_FONT);
    }
    
    return true;
}

// Cleanup of any resources
void LTImgWindowCleanup ()
{
    // Destroy the font atlas
    ImgWindow::sFontAtlas.reset();
}