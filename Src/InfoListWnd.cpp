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

// Controls access to the log list (defined in TextIO)
extern std::recursive_mutex gLogMutex;

// Defined in LTVersion.cpp, contain the build date (like 20200811)
extern int verBuildDate;

// Credits
struct CreditTy {
    const char* txtLink;
    const char* txtAdd;
    const char* url;
};

static const CreditTy CREDITS[] = {
    { "X-Plane APIs", "to integrate with X-Plane",      "https://developer.x-plane.com/sdk/plugin-sdk-documents/" },
    { "XPMP2", "for CSL model processing",              "https://github.com/TwinFan/XPMP2" },
    { "CURL", "for network protocol support",           "https://curl.haxx.se/libcurl/" },
    { "parson", "as JSON parser",                       "https://github.com/kgabis/parson" },
    { "libz/zlib", "as compression library (used by CURL)", "https://zlib.net/" },
    { "ImGui", "for user interfaces",                   "https://github.com/ocornut/imgui" },
    { "ImgWindow", "for integrating ImGui into X-Plane windows", "https://github.com/xsquawkbox/xsb_public" },
    { "IconFontCppHeaders", "for header files for the included icon font", "https://github.com/juliettef/IconFontCppHeaders" },
};

//
// MARK: InfoListWnd Implementation
//

/// Resizing limits (minimum and maximum sizes)
const WndRect ILW_RESIZE_LIMITS = WndRect(200, 200, 9999, 9999);

// Global static pointer to the one info list window object
static InfoListWnd* gpILW = nullptr;

// Constructor shows a window for the given a/c key
InfoListWnd::InfoListWnd(WndMode _mode) :
LTImgWindow(_mode, WND_STYLE_HUD, dataRefs.ILWrect),
wndTitle(LIVE_TRAFFIC)
{
    // Set up window basics
    SetWindowTitle(GetWndTitle());
    SetWindowResizingLimits(ILW_RESIZE_LIMITS.tl.x, ILW_RESIZE_LIMITS.tl.y,
                            ILW_RESIZE_LIMITS.br.x, ILW_RESIZE_LIMITS.br.y);
    
    // Define Help URL to open for Help (?) button
    szHelpURL = HELP_ILW;
    
    // Compute version info text
    char buf[100];
    if constexpr (LIVETRAFFIC_VERSION_BETA)
        snprintf(buf, sizeof(buf), LIVE_TRAFFIC " %s, BETA version limited to %s",
                LT_VERSION_FULL, LT_BETA_VER_LIMIT_TXT);
    else
        snprintf(buf, sizeof(buf), LIVE_TRAFFIC " %s", LT_VERSION_FULL);
    verText = buf;
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

    // Define window title and help URL based on active tab
    switch (_tab) {
        case ILW_TAB_AC_LIST:   wndTitle = LIVE_TRAFFIC " - Aircraft List";     szHelpURL = HELP_ILW_AC_LIST;   break;
        case ILW_TAB_MSG:       wndTitle = LIVE_TRAFFIC " - Messages";          szHelpURL = HELP_ILW_MESSAGES;  break;
        case ILW_TAB_STATUS:    wndTitle = LIVE_TRAFFIC " - Status / About";    szHelpURL = HELP_ILW_STATUS;    break;
        case ILW_TAB_SETTINGS:  wndTitle = LIVE_TRAFFIC " - Status Settings";   szHelpURL = HELP_ILW_SETTINGS;  break;
        case ILW_TAB_NONE:      wndTitle = LIVE_TRAFFIC;                        szHelpURL = HELP_ILW;           break;
    }
}

// Some setup before UI building starts, here text size calculations
ImGuiWindowFlags_ InfoListWnd::beforeBegin()
{
    // Save latest screen size to configuration (if not popped out)
    if (!IsPoppedOut())
        dataRefs.ILWrect = GetCurrentWindowGeometry();
    
    // Set background opacity
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] =  ImColor(0.0f, 0.0f, 0.0f, float(dataRefs.UIopacity)/100.0f);
    
    return ImGuiWindowFlags_None;
}

// Main function to render the window's interface
void InfoListWnd::buildInterface()
{
    // Scale the font for this window
    const float fFontScale = float(dataRefs.UIFontScale)/100.0f;
    ImGui::SetWindowFontScale(fFontScale);
    
    // --- Title Bar ---
    buildTitleBar(GetWndTitle());

    // --- Tab Bar ---
    if (ImGui::BeginTabBar("InfoListWnd",
                           ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyResizeDown))
    {
        // MARK: Aircraft List
        if (ImGui::BeginTabItem(ICON_FA_PLANE " Aircraft List  ", nullptr, ImGuiTabItemFlags_NoTooltip)) {
            TabActive(ILW_TAB_AC_LIST);
            
            // Limit to visible planes only
            ImGui::Checkbox("Only displayed a/c", &acList.bFilterAcOnly);
            
            // Search a setting
            // If there is a search text then the tree nodes will be suppressed,
            // and only matching config items are shown
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##AcSearchText", ICON_FA_SEARCH " Search Aircraft", sAcFilter, IM_ARRAYSIZE(sAcFilter),
                                     ImGuiInputTextFlags_CharsUppercase);
            if (*sAcFilter) {
                ImGui::SameLine();
                if (ImGui::ButtonTooltip(ICON_FA_TIMES "##acFilter", "Remove filter"))
                    sAcFilter[0]=0;
            }
            
            // Show the list of aircraft
            acList.build(sAcFilter);
            
            ImGui::EndTabItem();
        }
        
        // MARK: Message List
        if (ImGui::BeginTabItem(ICON_FA_CLIPBOARD_LIST " Messages  ", nullptr, ImGuiTabItemFlags_NoTooltip)) {
            TabActive(ILW_TAB_MSG);
            
            // Filter which log levels?
            bool bFilterChanged = false;
            for (logLevelTy lvl: {logDEBUG, logMSG, logINFO, logWARN, logERR, logFATAL}) {
                if (lvl) ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ConvColor(LogLvlColor(lvl)));
                if (ImGui::CheckboxFlags(LogLvlText(lvl), &msgLvlFilter, 1 << lvl))
                    bFilterChanged = true;
                ImGui::PopStyleColor();
            }

            // Text filter
            if (ImGui::InputTextWithHint("##MsgSearchText", ICON_FA_SEARCH " Search Messages", sMsgFilter, IM_ARRAYSIZE(sMsgFilter),
                                         ImGuiInputTextFlags_CharsUppercase))
                bFilterChanged = true;
            if (*sMsgFilter) {
                ImGui::SameLine();
                if (ImGui::ButtonTooltip(ICON_FA_TIMES "##msgFilter", "Remove filter")) {
                    sMsgFilter[0]=0;
                    bFilterChanged = true;
                }
            }

            // List of messages
            if (ImGui::BeginTable("MsgList", 7,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                  ImGuiTableFlags_Hideable |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingPolicyFixedX | ImGuiTableFlags_Scroll |
                                  ImGuiTableFlags_ScrollFreezeTopRow |
                                  ImGuiTableFlags_ScrollFreezeLeftColumn))
            {
                // Set up columns
                ImGui::TableSetupColumn("Time",         ImGuiTableColumnFlags_NoHeaderWidth,  70);
                ImGui::TableSetupColumn("Network Time", ImGuiTableColumnFlags_NoHeaderWidth,  85);
                ImGui::TableSetupColumn("Level",        ImGuiTableColumnFlags_NoHeaderWidth,  70);
                ImGui::TableSetupColumn("File",         ImGuiTableColumnFlags_DefaultHide | ImGuiTableColumnFlags_NoHeaderWidth, 120);
                ImGui::TableSetupColumn("Line",         ImGuiTableColumnFlags_DefaultHide | ImGuiTableColumnFlags_NoHeaderWidth,  50);
                ImGui::TableSetupColumn("Function",     ImGuiTableColumnFlags_DefaultHide | ImGuiTableColumnFlags_NoHeaderWidth, 120);
                ImGui::TableSetupColumn("Message",      ImGuiTableColumnFlags_NoHeaderWidth, 650);
                ImGui::TableAutoHeaders();
                
                // Set up / update list of messages to show
                
                // generally, we only need to check for new messages added to the global list
                unsigned long msgCounterReadTo = msgCounterLastDisp;
                
                // Figure out if messages were purged from the back of the message list
                if (gLog.empty() ||
                    gLog.back().counter != msgCounterEnd)
                    bFilterChanged = true;
                // Redo the entire list?
                if (bFilterChanged) {
                    // start over
                    msgIterList.clear();
                    msgCounterReadTo = 0;
                }
                
                // Messages are added to the beginning of the list
                const LogMsgIterListTy::iterator insBefore = msgIterList.begin();
                    
                // Access to static buffer and list guarded by a lock
                if (!gLog.empty())
                {
                    // We lock once here to avoid re-locking with every match attempt
                    std::lock_guard<std::recursive_mutex> lock(gLogMutex);
                    
                    // Loop all messages and remember those which match
                    for (LogMsgListTy::const_iterator iMsg = gLog.cbegin();
                         iMsg != gLog.cend() && iMsg->counter != msgCounterReadTo;
                         ++iMsg)
                    {
                        // apply filter on message levels first
                        if ((msgLvlFilter & (1 << iMsg->lvl)) > 0 &&
                            // then test for match string
                            (!sMsgFilter[0] || iMsg->matches(sMsgFilter)))
                        {
                            // add iterator to list of matching iterators
                            msgIterList.insert(insBefore, iMsg);
                        }
                    }
                    
                    // remember based on what we made up the list
                    msgCounterLastDisp = gLog.front().counter;
                    msgCounterEnd = gLog.back().counter;
                }
                else {
                    msgCounterLastDisp = 0;
                    msgCounterEnd = 0;
                }
                
                // Add rows from the pre-filtered list of iterators
                for (const LogMsgListTy::const_iterator& iMsg: msgIterList) {
                    // the message to show
                    const LogMsgTy& msg = *iMsg;
                    ImGui::TableNextRow();
                    
                    // Time
                    char buf[50];
                    if (ImGui::TableSetColumnIndex(0)) {
                        std::time_t t_c = std::chrono::system_clock::to_time_t(msg.wallTime);
                        strftime(buf, sizeof(buf), "%H:%M:%S",
                                 std::localtime(&t_c));
                        ImGui::TextUnformatted(buf);
                    }
                    // Network Time
                    if (ImGui::TableSetColumnIndex(1))
                        ImGui::TextUnformatted(NetwTimeString(msg.netwTime).c_str());
                    // Level
                    if (ImGui::TableSetColumnIndex(2)) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ConvColor(LogLvlColor(msg.lvl)));
                        ImGui::TextUnformatted(LogLvlText(msg.lvl));
                        ImGui::PopStyleColor();
                    }
                    // File
                    if (ImGui::TableSetColumnIndex(3))
                        ImGui::TextUnformatted(msg.fileName.c_str());
                    // Line
                    if (ImGui::TableSetColumnIndex(4))
                        ImGui::Text("%d", msg.ln);
                    // Function
                    if (ImGui::TableSetColumnIndex(5))
                        ImGui::TextUnformatted(msg.func.c_str());
                    // Message
                    if (ImGui::TableSetColumnIndex(6)) {
                        ImGui::PushTextWrapPos();
                        ImGui::TextUnformatted(msg.msg.c_str());
                        ImGui::PopTextWrapPos();
                    }
                }
                
                ImGui::EndTable();
            }


            ImGui::EndTabItem();
        }
        
        // MARK: Status / About
        if (ImGui::BeginTabItem(ICON_FA_INFO_CIRCLE " Status / About  ", nullptr, ImGuiTabItemFlags_NoTooltip)) {
            TabActive(ILW_TAB_STATUS);

            // Some info we update only irregularly
            if (CheckEverySoOften(lastStatusUpdate, 5.0f)) {
                // Who's in control of TCAS?
                if (dataRefs.HaveAIUnderControl())
                    aiCtrlPlugin = LIVE_TRAFFIC;
                else
                    aiCtrlPlugin = GetAIControlPluginName();
                
                // What's the weather?
                dataRefs.GetWeather(weatherHPA, weatherStationId, weatherMETAR);
                
                // How many CSL models are installed? (This being 0 or 1 is one of the most often installation errors)
                numCSLModels = XPMPGetNumberOfInstalledModels();
            }
            
            // Child window for scrolling region
            if (ImGui::BeginChild("StatusAndInfo", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar)) {
                // Aircraft / Channel status
                if (ImGui::TreeNodeEx("Aircraft / Channel Status", ImGuiTreeNodeFlags_DefaultOpen)) {
                
                    if (ImGui::BeginTable("StatusInfo", 2, ImGuiTableFlags_SizingPolicyFixedX)) {
                        
                        // Are we active at all?
                        if (dataRefs.AreAircraftDisplayed()) {
                            // Number of aircraft seen/shown
                            ImGui::TableNextRow();
                            if (ImGui::TableSetColumnIndex(0)) ImGui::TextUnformatted("Live aircraft shown");
                            if (ImGui::TableSetColumnIndex(1)) ImGui::Text("%d", dataRefs.GetNumAc());
                            ImGui::TableNextRow();
                            if (ImGui::TableSetColumnIndex(0)) ImGui::TextUnformatted("Aircraft seen in tracking data");
                            if (ImGui::TableSetColumnIndex(1)) ImGui::Text("%lu", (long unsigned)mapFd.size());
                            
                            // Warning of there's one CSL model only
                            if (numCSLModels == 1) {
                                ImGui::TableNextRow();
                                if (ImGui::TableSetColumnIndex(0)) ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " Just 1 CSL Model");
                                if (ImGui::TableSetColumnIndex(1)) ImGui::TextUnformatted("With only one Model installed, all planes will look alike. Check out menu LiveTraffic > Help > " MENU_HELP_INSTALL_CSL );
                                ImGui::TableNextRow();
                                if (ImGui::TableSetColumnIndex(1)) ImGui::TextUnformatted(MSG_CFG_CSL_INSTALL);
                            }
                            else {
                                // Number of CSL Models available
                                ImGui::TableNextRow();
                                if (ImGui::TableSetColumnIndex(0)) ImGui::TextUnformatted("Number of available CSL Models");
                                if (ImGui::TableSetColumnIndex(1)) ImGui::Text("%d", numCSLModels);
                            }

                            // TCAS / Multiplayer control
                            ImGui::TableNextRow();
                            if (ImGui::TableSetColumnIndex(0)) ImGui::TextUnformatted("TCAS / Multiplayer");
                            if (ImGui::TableSetColumnIndex(1)) {
                                // LiveTraffic has requested AI control
                                if (dataRefs.AwaitingAIControl()) {
                                    if (!aiCtrlPlugin.empty())
                                        ImGui::Text("control requested from %s", aiCtrlPlugin.c_str());
                                    else
                                        ImGui::TextUnformatted("control requested");
                                }
                                // Some plugin, maybe LiveTraffic, has control:
                                else {
                                    if (!aiCtrlPlugin.empty())
                                        ImGui::Text("controlled by %s", aiCtrlPlugin.c_str());
                                    else
                                        ImGui::TextUnformatted("not controlled");
                                }
                            }
                            
                            // Weather
                            ImGui::TableNextRow();
                            if (ImGui::TableSetColumnIndex(0)) ImGui::TextUnformatted("Live Weather");
                            if (ImGui::TableSetColumnIndex(1)) {
                                ImGui::Text(weatherStationId.empty() ? "QNH %.f hPa" : "QNH %.f hPa at %s",
                                            weatherHPA, weatherStationId.c_str());
                                if (!weatherMETAR.empty())
                                    ImGui::TextUnformatted(weatherMETAR.c_str());
                            }

                            // Status of channels
                            
                            // Are there invalid channels?
                            if (LTFlightDataAnyChInvalid())
                            {
                                ImGui::TableNextRow();
                                if (ImGui::TableSetColumnIndex(1)) {
                                    ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " " ERR_CH_INACTIVE1 " ");
                                    ImGui::SameLine();
                                    if (ImGui::ButtonTooltip(ICON_FA_UNDO " Restart Stopped Channels", "Restarts all channels that got temporarily inactivated"))
                                        LTFlightDataRestartInvalidChs();
                                }
                            }
                            
                            // No tracking data channel enabled?
                            if (!LTFlightDataAnyTrackingChEnabled())
                            {
                                ImGui::TableNextRow();
                                if (ImGui::TableSetColumnIndex(1))
                                    ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " " ERR_CH_NONE_ACTIVE);
                            }
                            
                            // Individual channels' status
                            for (const ptrLTChannelTy& pCh: listFDC) {
                                ImGui::TableNextRow();
                                ImGui::PushID(pCh->ChName());           // helps making link buttons distinguishable
                                // Channel's link and name
                                if (ImGui::TableSetColumnIndex(0)) {
                                    ImGui::ButtonURL(ICON_FA_EXTERNAL_LINK_SQUARE_ALT, pCh->urlLink.c_str(),
                                                     (pCh->urlName + '\n' + pCh->urlPopup).c_str(), true);
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(pCh->ChName());
                                }
                                // Channel's status
                                if (ImGui::TableSetColumnIndex(1)) {
                                    ImGui::TextUnformatted(pCh->GetStatusText().c_str());
                                    const std::string extStatus = pCh->GetStatusTextExt();
                                    if (!extStatus.empty())
                                        ImGui::TextUnformatted(extStatus.c_str());
                                }
                                ImGui::PopID();
                            }
                        }
                        // INACTIVE!
                        else {
                            ImGui::TableNextRow();
                            if (ImGui::TableSetColumnIndex(0)) ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " " LIVE_TRAFFIC " is");
                            if (ImGui::TableSetColumnIndex(1)) ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "INACTIVE");
                            
                            // Additional warning if there's no CSL model
                            if (numCSLModels == 0) {
                                ImGui::TableNextRow();
                                if (ImGui::TableSetColumnIndex(0)) ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " No CSL Models");
                                if (ImGui::TableSetColumnIndex(1)) ImGui::TextUnformatted(ERR_CFG_CSL_ZERO_MODELS);
                                ImGui::TableNextRow();
                                if (ImGui::TableSetColumnIndex(1)) ImGui::TextUnformatted(MSG_CFG_CSL_INSTALL);
                            }

                        }
                        
                        ImGui::EndTable();
                    }
                    
                    ImGui::TreePop();
                }

                // Version information
                if (ImGui::TreeNode(verText.c_str())) {
                    ImGui::Text("(c) 2018-%d B. Hoppe", verBuildDate / 10000);
                    ImGui::ButtonURL("MIT License", "https://github.com/TwinFan/LiveTraffic/blob/master/LICENSE", nullptr, true);
                    ImGui::TextUnformatted("Open Source"); ImGui::SameLine();
                    ImGui::ButtonURL("available on GitHub", "https://github.com/TwinFan/LiveTraffic", nullptr, true);
                    
                    ImGui::TreePop();
                }
                
                // Credits
                if (ImGui::TreeNode("Credits")) {
                    ImGui::TextUnformatted(LIVE_TRAFFIC " is based on a number of other great libraries and APIs, most notably:");
                    for (const CreditTy& c: CREDITS)
                    {
                        ImGui::ButtonURL(c.txtLink, c.url, nullptr, true); ImGui::SameLine();
                        ImGui::TextUnformatted(c.txtAdd);
                    }
                    ImGui::TreePop();
                }
                
                // Thanks
                if (ImGui::TreeNode("Thanks")) {
                    ImGui::TextUnformatted("172MC, Dozo, and Sir.Anri for continued Beta testing.");
                    ImGui::Spacing();

                    ImGui::TextUnformatted("Sparker for providing"); ImGui::SameLine();
                    ImGui::ButtonURL("imgui4xp", "https://github.com/sparker256/imgui4xp", nullptr, true); ImGui::SameLine();
                    ImGui::TextUnformatted("as a testbed for ImGui integration and for accepting my additions to it;");
                    ImGui::TextUnformatted("as well as for providing the initial Linux build Docker environment.");
                    ImGui::Spacing();
                    
                    ImGui::TextUnformatted("Crbascott for compiling and providing the"); ImGui::SameLine();
                    ImGui::ButtonURL("model_typecode.txt", "https://github.com/TwinFan/LiveTraffic/blob/master/Resources/model_typecode.txt", nullptr, true); ImGui::SameLine();
                    ImGui::TextUnformatted("file.");
                    ImGui::Spacing();

                    ImGui::TextUnformatted("Dimitri van Heesch for"); ImGui::SameLine();
                    ImGui::ButtonURL("Doxygen", "https://www.doxygen.nl/", nullptr, true); ImGui::SameLine();
                    ImGui::TextUnformatted(", with which more and more parts of LiveTraffic's");
                    ImGui::TextUnformatted("(and all of XPMP2's) code documentation have been created.");
                    ImGui::Spacing();

                    ImGui::ButtonURL("FontAwesome", "https://fontawesome.com/icons?d=gallery&s=solid&m=free", nullptr, true); ImGui::SameLine();
                    ImGui::TextUnformatted("for the icon font fa-solid-900.ttf " ICON_FA_PLANE);

                    ImGui::TreePop();
                }

            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        
        // MARK: Settings
        const bool bSettingsActive = ImGui::BeginTabItem(ICON_FA_SLIDERS_H "##UI Settings", nullptr, ImGuiTabItemFlags_NoTooltip);
        // I found not way to provide a _different_ tooltop to the BeginTabItem functions, so we do the tooltip by hand:
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", "UI Settings");
        if (bSettingsActive) {
            TabActive(ILW_TAB_SETTINGS);
            
            if (ImGui::BeginTable("StatusSettings", 2, ImGuiTableFlags_SizingPolicyFixedX)) {
                
                ImGui::TableNextRow();
                ImGui::TableNextCell();
                ImGui::TextUnformatted("Settings also affect A/C Info windows.");
                ImGui::TextUnformatted("Drag controls with mouse:");
                
                ImGui::TableNextRow();
                ImGui::TextUnformatted("Font Scaling");
                ImGui::TableNextCell();
                ImGui::DragInt("##FontScaling", &dataRefs.UIFontScale, 1.0f, 10, 200, "%d%%");
                
                ImGui::TableNextRow();
                ImGui::TextUnformatted("Opacity");
                ImGui::TableNextCell();
                ImGui::DragInt("##Opacity", &dataRefs.UIopacity, 1.0f, 0, 100, "%d%%");

                ImGui::TableNextRow();
                ImGui::TableNextCell();
                if (ImGui::Button(ICON_FA_UNDO " Reset to Defaults")) {
                    dataRefs.UIFontScale   = DEF_UI_FONT_SCALE;
                    dataRefs.UIopacity      = DEF_UI_OPACITY;
                }
                
                ImGui::EndTable();
            }
            
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
        // Ensure it is visible
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
