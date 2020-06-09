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
        cbFlightLoop,                               // callbackFunc
        (void*)this,                                // refcon
    };
    flId = XPLMCreateFlightLoop(&flDef);
    LOG_ASSERT(flId);

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
    if (flId)
        XPLMDestroyFlightLoop(flId);
    flId = nullptr;
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
        XPLMScheduleFlightLoop(flId, -5.0, 1);  // in 5 flight loops time
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
// MARK: Static and Global functions
//

// flight loop callback for stuff we cannot do during drawing callback
// Outside all rendering we can change things like window mode
float LTImgWindow::cbFlightLoop(float, float, int, void* inRefcon)
{
    // refcon is pointer to ImguiWidget
    LTImgWindow& wnd = *reinterpret_cast<LTImgWindow*>(inRefcon);

    // Has user requested a change in window mode?
    if (wnd.nextWinMode > WND_MODE_NONE)
        wnd.SetMode(wnd.nextWinMode);
    
    // don't call me again
    return 0.0f;
}

// One-time initializations for all ImGui windows
bool LTImgWindowInit ()
{
    // Create one (and exactly one) font atlas, so all fonts are textured just once
    ImgWindow::sFontAtlas = std::make_shared<ImgFontAtlas>();
    
    // Load one standard font (comes with XP)
    if (!ImgWindow::sFontAtlas->AddFontFromFileTTF((dataRefs.GetXPSystemPath() + WND_STANDARD_FONT).c_str(),
                                                   WND_FONT_SIZE))
    {
        LOG_MSG(logFATAL, FATAL_COULD_NOT_LOAD_FONT, WND_STANDARD_FONT);
        return false;
    }
    return true;
}

// Cleanup of any resources
void LTImgWindowCleanup ()
{
    // Destroy the font atlas
    ImgWindow::sFontAtlas.reset();
}
