/// @file       InfoListWnd.cpp
/// @brief      Window listing aircraft, messages, and status information
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

//
// MARK: InfoListWnd Implementation
//

/// Resizing limits (minimum and maximum sizes)
const WndRect ILW_RESIZE_LIMITS = WndRect(200, 200, 9999, 9999);

// Global static pointer to the one info list window object
static InfoListWnd* gpILW = nullptr;

// Constructor shows a window for the given a/c key
InfoListWnd::InfoListWnd(WndMode _mode) :
LTImgWindow(_mode, WND_STYLE_HUD, WndRect(0, dataRefs.ILWheight, dataRefs.ILWwidth, 0)),
wndTitle(LIVE_TRAFFIC)
{
    // Set up window basics
    SetWindowTitle(GetWndTitle());
    SetWindowResizingLimits(ILW_RESIZE_LIMITS.tl.x, ILW_RESIZE_LIMITS.tl.y,
                            ILW_RESIZE_LIMITS.br.x, ILW_RESIZE_LIMITS.br.y);
    
    // Define Help URL to open for Help (?) button
    szHelpURL = HELP_ILW;
}

// Desctructor cleans up
InfoListWnd::~InfoListWnd()
{
    // I am no longer
    gpILW = nullptr;
}

/// Redefine the window title
void InfoListWnd::TabActive (ILWTabTy _tab)
{
    // Shortcut for no change
    if (activeTab == _tab) return;
    activeTab = _tab;

    // Define window title based on active tab
    switch (_tab) {
        case ILW_TAB_AC_LIST:   wndTitle = LIVE_TRAFFIC " - Aircraft List";     break;
        case ILW_TAB_MSG:       wndTitle = LIVE_TRAFFIC " - Messages";          break;
        case ILW_TAB_STATUS:    wndTitle = LIVE_TRAFFIC " - Status / About";    break;
        case ILW_TAB_NONE:      wndTitle = LIVE_TRAFFIC;                        break;
    }
}

// Some setup before UI building starts, here text size calculations
ImGuiWindowFlags_ InfoListWnd::beforeBegin()
{
    // Save latest screen size to configuration (if not popped out)
    if (!IsPoppedOut()) {
        const WndRect r = GetCurrentWindowGeometry();
        dataRefs.ILWwidth   = r.width();
        dataRefs.ILWheight  = r.height();
    }
    
    // Set background opacity
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] =  ImColor(0.0f, 0.0f, 0.0f, float(dataRefs.UIopacity)/100.0f);
    
    return ImGuiWindowFlags_None;
}

// Main function to render the window's interface
void InfoListWnd::buildInterface()
{
    // Scale the font for this window
    const float fFontScale = float(dataRefs.ACIfontScale)/100.0f;
    ImGui::SetWindowFontScale(fFontScale);
    
    // --- Title Bar ---
    buildTitleBar(GetWndTitle());

    // --- Tab Bar ---
    if (ImGui::BeginTabBar("InfoListWnd", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyResizeDown))
    {
        // MARK: Aircraft List
        if (ImGui::BeginTabItem(ICON_FA_PLANE " Aircraft List")) {
            TabActive(ILW_TAB_AC_LIST);
            
            // Limit to visible planes only
            ImGui::Checkbox("Only displayed a/c", &acList.bFilterAcOnly);
            
            // Search a setting
            // If there is a search text then the tree nodes will be suppressed,
            // and only matching config items are shown
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##SearchText", ICON_FA_SEARCH " Search Aircraft", sFilter, IM_ARRAYSIZE(sFilter),
                                     ImGuiInputTextFlags_CharsUppercase);
            if (*sFilter) {
                ImGui::SameLine();
                if (ImGui::ButtonTooltip(ICON_FA_TIMES, "Remove filter"))
                    sFilter[0]=0;
            }
            
            // Show the list of aircraft
            acList.build(sFilter);
            
            ImGui::EndTabItem();
        }
        
        // MARK: Message List
        if (ImGui::BeginTabItem(ICON_FA_CLIPBOARD_LIST " Messages")) {
            TabActive(ILW_TAB_MSG);

            ImGui::EndTabItem();
        }
        
        // MARK: Status / About
        if (ImGui::BeginTabItem(ICON_FA_INFO_CIRCLE " Status / About")) {
            TabActive(ILW_TAB_STATUS);

            ImGui::Text("Showing %d aircraft", dataRefs.GetNumAc());

            // Version information
            if constexpr (VERSION_BETA)
                ImGui::Text(LIVE_TRAFFIC " %s, BETA version limited to %s",
                            LT_VERSION_FULL, LT_BETA_VER_LIMIT_TXT);
            else
                ImGui::Text(LIVE_TRAFFIC " %s", LT_VERSION_FULL);
            
            ImGui::TextUnformatted("(c) 2018-2020 B. Hoppe");
            ImGui::Spacing();

            ImGui::EndTabItem();
        }
        
        // End of tab bar
        ImGui::EndTabBar();
    }

    // Reset font scaling
    ImGui::SetWindowFontScale(1.0f);
}

//
// MARK: Static InfoListWnd functions
//

// Creates/opens/displays/hides/closes the settings window
bool InfoListWnd::ToggleDisplay (int _force)
{
    // If we toggle then do what current is not the state
    if (!_force)
        _force = InfoListWnd::IsDisplayed() ? -1 : 1;
    
    // Open the window?
    if (_force > 0)
    {
        // Create the object and window if needed
        if (!gpILW)
            gpILW = new InfoListWnd();
        // Ensure it is visible and centered
        gpILW->SetMode(WND_MODE_FLOAT_CNT_VR);
        gpILW->SetVisible(true);
        gpILW->BringWindowToFront();
        return true;                    // visible now
    }
    // Close the window
    else
    {
        if (gpILW)                      // just remove the object
            delete gpILW;               // (destructor clears gpILW)
        return false;                   // not visible
    }
}

// Is the settings window currently displayed?
bool InfoListWnd::IsDisplayed ()
{
    return gpILW && gpILW->GetVisible();
}
