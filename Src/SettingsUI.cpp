/// @file       SettingsUI.cpp
/// @brief      Implements the Settings window
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
// MARK: LTSettingsUI
//

// Defined in LTImgWindow.cpp:
void cfgSet (dataRefsLT idx, int v);

// Global static pointer to the one settings object
static LTSettingsUI* gpSettings = nullptr;

/// Window's title
constexpr const char* SUI_WND_TITLE = LIVE_TRAFFIC " Settings";
/// Resizing limits (minimum and maximum sizes)
const WndRect SUI_RESIZE_LIMITS = WndRect(300, 300, 99999, 99999);

static float SUI_LABEL_SIZE = NAN;              ///< Width of 1st column, which displays static labels
static float SUI_VALUE_SIZE = NAN;              ///< Ideal Width of 2nd column for text entry

// Constructor creates and displays the window
LTSettingsUI::LTSettingsUI () :
LTImgWindow(WND_MODE_FLOAT_CNT_VR,
            dataRefs.SUItransp ? WND_STYLE_HUD : WND_STYLE_SOLID,
            dataRefs.SUIrect),
// If there is no ADSBEx key yet then display any new entry in clear text,
// If a key is already defined, then by default obscure it
sADSBExKeyEntry     (dataRefs.GetADSBExAPIKey()),
bADSBExKeyClearText (sADSBExKeyEntry.empty()),
// Fill CSL type entry with current values
acTypeEntry     (dataRefs.GetDefaultAcIcaoType()),
gndVehicleEntry (dataRefs.GetDefaultCarIcaoType()),
// Fill debug entry texts with current values
txtDebugFilter  (dataRefs.GetDebugAcFilter()),
txtFixAcType    (dataRefs.cslFixAcIcaoType),
txtFixOp        (dataRefs.cslFixOpIcao),
txtFixLivery    (dataRefs.cslFixLivery)
{
    /// GNF_COUNT is not available in SettingsUI.h (due to order of include files), make _now_ sure that aFlarmAcTys has the correct size
    assert (aFlarmAcTys.size() == size_t(FAT_UAV)+1);
    
    // Fill Flarm aircraft types with current values
    for (size_t i = 0; i < aFlarmAcTys.size(); i++)
        aFlarmAcTys[i] = str_concat(dataRefs.aFlarmToIcaoAcTy[i], " ");
    
    // Fetch OpenSky credentials
    dataRefs.GetOpenSkyCredentials(sOpenSkyUser, sOpenSkyPwd);
    
    // Fetch RealTraffic port number
    sRTPort = std::to_string(DataRefs::GetCfgInt(DR_CFG_RT_TRAFFIC_PORT));
    
    // Fetch FSC credentials
    dataRefs.GetFSCharterCredentials(sFSCUser, sFSCPwd);

    // Set up window basics
    SetWindowTitle(SUI_WND_TITLE);
    SetWindowResizingLimits(SUI_RESIZE_LIMITS.tl.x, SUI_RESIZE_LIMITS.tl.y,
                            SUI_RESIZE_LIMITS.br.x, SUI_RESIZE_LIMITS.br.y);
    SetVisible(true);
    
    // Define Help URL to open for generic Help (?) button
    szHelpURL = HELP_SETTINGS;
}

// Destructor completely removes the window
LTSettingsUI::~LTSettingsUI()
{
    // Save settings
    dataRefs.SaveConfigFile();
    // I am no longer...
    gpSettings = nullptr;
}


// Some setup before UI building starts, here text size calculations
ImGuiWindowFlags_ LTSettingsUI::beforeBegin()
{
    // If not yet done calculate some common widths
    if (std::isnan(SUI_LABEL_SIZE)) {
        /// Size of longest text plus some room for tree indenttion, rounded up to the next 10
        ImGui::SetWindowFontScale(1.0f);
        SUI_LABEL_SIZE = std::ceil(ImGui::CalcTextSize("_____OpenSky Network Master Data_").x / 10.0f) * 10.0f;
        SUI_VALUE_SIZE =
        ImGui::GetWidthIconBtn() +
        std::ceil(ImGui::CalcTextSize("_01234567890abcdefghijklmnopq").x / 10.0f) * 10.0f;
    }
    
    // Save latest screen size to configuration (if not popped out)
    if (!IsPoppedOut())
        dataRefs.SUIrect = GetCurrentWindowGeometry();
    
    // Set background opacity / color
    ImGuiStyle& style = ImGui::GetStyle();
    if ((wndStyle == WND_STYLE_HUD) && !IsPoppedOut())
        style.Colors[ImGuiCol_WindowBg] = ImColor(0.0f, 0.0f, 0.0f, float(dataRefs.UIopacity)/100.0f);
    else
        style.Colors[ImGuiCol_WindowBg] = ImColor(DEF_WND_BG_COL);
    
    return ImGuiWindowFlags_None;
}

// Main function to render the window's interface
void LTSettingsUI::buildInterface()
{
    // --- Title Bar (only if created HUD-style) ---
    const bool bSelfDeco = (wndStyle == WND_STYLE_HUD) && !IsPoppedOut();
    if (bSelfDeco)
        buildTitleBar(SUI_WND_TITLE);
    
    // "Open all" and "Close all" buttons
    int nOpCl = 0;          // 3 states: -1 close, 0 do nothing, 1 open
    if (ImGui::ButtonTooltip(ICON_FA_ANGLE_DOUBLE_DOWN, "Open all sections"))
        nOpCl = 1;
    ImGui::SameLine();
    if (ImGui::ButtonTooltip(ICON_FA_ANGLE_DOUBLE_UP, "Close all sections"))
        nOpCl = -1;
    ImGui::SameLine();
    
    // Search a setting
    // If there is a search text then the tree nodes will be suppressed,
    // and only matching config items are shown
    ImGui::InputTextWithHint("##SearchText", ICON_FA_SEARCH " Search Settings", sFilter, IM_ARRAYSIZE(sFilter),
                             ImGuiInputTextFlags_CharsUppercase);
    if (*sFilter) {
        ImGui::SameLine();
        if (ImGui::ButtonTooltip(ICON_FA_TIMES, "Remove filter"))
            sFilter[0]=0;
    }

    // Help and some occasional window buttons, if they aren't already in the self-decorated title bar
    if (!bSelfDeco) {
        ImGui::SameLine();
        buildWndButtons();
    }
    
    // --- Start the table, which will hold our values
    const unsigned COL_TBL_BG = IM_COL32(0x1E,0x2A,0x3A,0xFF);
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, COL_TBL_BG);
    ImGui::PushStyleColor(ImGuiCol_TableRowBg,    IM_COL32_BLACK_TRANS);
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32_BLACK_TRANS);

    // minimum 2 columns, in wider windows we allow for more columns dynamically
    const int nCol = std::max (2, int(ImGui::GetWindowContentRegionWidth() / (SUI_LABEL_SIZE+SUI_VALUE_SIZE)) * 2);
    if (ImGui::BeginTable("Settings", nCol,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerH))
    {
        const float fSmallWidth = ImGui::CalcTextSize("ABCDEF__").x;

        // Set up the columns of the table
        for (int i = 0; i < nCol; i += 2) {
            ImGui::TableSetupColumn("Item",  ImGuiTableColumnFlags_WidthFixed   | ImGuiTableColumnFlags_NoSort, SUI_LABEL_SIZE);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
        }
        ImGui::TableNextRow();
        
        // MARK: --- Basics ---
        if (ImGui::TreeNodeHelp("Basics", nCol,
                                HELP_SET_BASICS, "Open Help on Basics in Browser",
                                sFilter, nOpCl,
                                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
        {
            ImGui::FilteredCfgCheckbox("Show Live Aircraft",    sFilter, DR_CFG_AIRCRAFT_DISPLAYED,     "Main switch to enable display of live traffic");
            ImGui::FilteredCfgCheckbox("Auto Start",            sFilter, DR_CFG_AUTO_START,             "Show Live Aircraft automatically after start of X-Plane?");
            
            // auto-open and warning if any of these values are set as they limit what's shown
            const bool bSomeRestrict = dataRefs.IsAIonRequest() || dataRefs.IsAINotOnGnd() || dataRefs.IsAutoHidingActive() ||
                                       dataRefs.ShallUseExternalCamera() || dataRefs.GetRemoteSupport() < 0;
            if (bSomeRestrict)
                ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
            if (ImGui::TreeNodeLinkHelp("Cooperation", nCol,
                                        bSomeRestrict ? ICON_FA_EXCLAMATION_TRIANGLE : nullptr, nullptr,
                                        "Some options are active, restricting displayed traffic, TCAS, or functionality!",
                                        HELP_SET_BASICS, "Open Help on Basics in Browser",
                                        sFilter, nOpCl, ImGuiTreeNodeFlags_SpanFullWidth))
            {
                ImGui::FilteredCfgCheckbox("TCAS on request only",   sFilter, DR_CFG_AI_ON_REQUEST,     "Do not take over control of TCAS automatically, but only via menu 'TCAS controlled'");
                ImGui::FilteredCfgCheckbox("No TCAS/AI for ground a/c", sFilter, DR_CFG_AI_NOT_ON_GND,  "Aircraft on the ground will not be reported to TCAS or AI/multiplayer interfaces");
                ImGui::FilteredCfgCheckbox("Hide a/c while taxiing", sFilter, DR_CFG_HIDE_TAXIING,      "Hide aircraft in phase 'Taxi'");
                ImGui::FilteredCfgCheckbox("Hide a/c while parking", sFilter, DR_CFG_HIDE_PARKING,      "Hide aircraft parking at a gate or ramp position");
                ImGui::FilteredCfgCheckbox("Hide all a/c in Reply", sFilter, DR_CFG_HIDE_IN_REPLAY,     "Hide all aircraft while in Replay mode");
                ImGui::FilteredCfgNumber("No aircraft below", sFilter, DR_CFG_HIDE_BELOW_AGL, 0, 10000, 100, "%d ft AGL");
                ImGui::FilteredCfgNumber("Hide ground a/c closer than", sFilter, DR_CFG_HIDE_NEARBY_GND, 0, 500, 10, "%d m");
                ImGui::FilteredCfgNumber("Hide airborne a/c closer than", sFilter, DR_CFG_HIDE_NEARBY_AIR, 0, 5000, 100, "%d m");
                ImGui::FilteredCfgCheckbox("Hide static objects", sFilter, DR_CFG_HIDE_STATIC_TWR,      "Do not display static objects like towers");
                ImGui::FilteredCfgCheckbox("Use 3rd party camera", sFilter, DR_CFG_EXTERNAL_CAMERA, "Don't activate LiveTraffic's camera view when clicking the camera button\nbut expect a 3rd party camera plugin to spring on instead");
                if (ImGui::FilteredLabel("XPMP2 Remote Client support", sFilter)) {
                    const float cbWidth = ImGui::CalcTextSize("Auto Detect (default)_____").x;
                    ImGui::SetNextItemWidth(cbWidth);
                    int n = 1 - std::clamp<int>(dataRefs.GetRemoteSupport(),-1,1);  // this turns the order around: 0 - on, 1 - Auto, 2 - Off
                    if (ImGui::Combo("##RemoteSupport", &n, "Always On\0Auto Detect (default)\0Off\0", 5))
                        DATA_REFS_LT[DR_CFG_REMOTE_SUPPORT].setData(1-n);
                    ImGui::TableNextCell();
                }

                if (!*sFilter) ImGui::TreePop();
            }

            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        }

        // MARK: --- Input Channels ---
        if (ImGui::TreeNodeLinkHelp("Input Channels", nCol,
                                    (!LTFlightDataAnyTrackingChEnabled() || LTFlightDataAnyChInvalid()) ? ICON_FA_EXCLAMATION_TRIANGLE : nullptr, nullptr,
                                    LTFlightDataAnyChInvalid() ? ERR_CH_INACTIVE1 : ERR_CH_NONE_ACTIVE1,
                                    HELP_SET_INPUT_CH, "Open Help on Channels in Browser",
                                    sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
        {
            // --- Restart inactive channels ---
            if (LTFlightDataAnyChInvalid())
            {
                if (ImGui::FilteredLabel(ICON_FA_EXCLAMATION_TRIANGLE " There are stopped channels:", sFilter)) {
                    if (ImGui::ButtonTooltip(ICON_FA_UNDO " Restart Stopped Channels", "Restarts all channels that got temporarily inactivated"))
                        LTFlightDataRestartInvalidChs();
                    ImGui::TableNextCell();
                }
            }
            
            // --- OpenSky ---
            if (ImGui::TreeNodeCbxLinkHelp("OpenSky Network", nCol,
                                           DR_CHANNEL_OPEN_SKY_ONLINE, "Enable OpenSky tracking data",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " " OPSKY_CHECK_NAME,
                                           OPSKY_CHECK_URL,
                                           OPSKY_CHECK_POPUP,
                                           HELP_SET_CH_OPENSKY, "Open Help on OpenSky in Browser",
                                           sFilter, nOpCl))
            {
                LTChannel* pOpenSkyCh = LTFlightDataGetCh(DR_CHANNEL_OPEN_SKY_ONLINE);
                const bool bOpenSkyOn = dataRefs.IsChannelEnabled(DR_CHANNEL_OPEN_SKY_ONLINE);

                ImGui::FilteredCfgCheckbox("OpenSky Network Master Data", sFilter, DR_CHANNEL_OPEN_SKY_AC_MASTERDATA, "Query OpenSky for aicraft master data like type, registration...");
                
                // Hint that user/password increases number of allowed requests
                if (!*sFilter && (sOpenSkyUser.empty() || sOpenSkyPwd.empty())) {
                    ImGui::ButtonURL(ICON_FA_EXTERNAL_LINK_SQUARE_ALT " Registration",
                                     "https://opensky-network.org/login?view=registration",
                                     "Opens OpenSky Network's user registration page");
                    ImGui::TableNextCell();
                    ImGui::TextUnformatted("Using a registered user allows for more requests to OpenSky per day ");
                    ImGui::TableNextCell();
                }

                // User
                if (ImGui::FilteredLabel("Username", sFilter)) {
                    ImGui::Indent(ImGui::GetWidthIconBtn(true));
                    ImGui::InputTextWithHint("##OpenSkyUser",
                                             "OpenSky Network username",
                                             &sOpenSkyUser,
                                             // prohibit changes to the user while channel on
                                             (bOpenSkyOn ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None));
                    ImGui::Unindent(ImGui::GetWidthIconBtn(true));

                    ImGui::TableNextCell();
                }
                
                // Password
                if (ImGui::FilteredLabel("Password", sFilter)) {
                    // "Eye" button changes password flag
                    ImGui::Selectable(ICON_FA_EYE "##OpenSkyPwdVisible", &bOpenSkyPwdClearText,
                                      ImGuiSelectableFlags_None, ImVec2(ImGui::GetWidthIconBtn(),0));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", "Show/Hide password");
                    ImGui::SameLine();  // make text entry the size of the remaining space in cell, but not larger
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::InputTextWithHint("##OpenSkyPwd",
                                             "Enter or paste OpenSky Network password",
                                             &sOpenSkyPwd,
                                             // clear text or password mode?
                                             (bOpenSkyPwdClearText ? ImGuiInputTextFlags_None     : ImGuiInputTextFlags_Password) |
                                             // prohibit changes to the pwd while channel on
                                             (bOpenSkyOn ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None));
                    
                    ImGui::TableNextCell();
                }
                
                // Save button or hint how to change
                if (!*sFilter) {
                    ImGui::TableNextCell();
                    if (bOpenSkyOn) {
                        ImGui::TextUnformatted("Disable the channel first if you want to change user/password.");
                    } else {
                        if (ImGui::ButtonTooltip(ICON_FA_SAVE " Save and Try", "Saves the credentials and activates the channel")) {
                            dataRefs.SetOpenSkyUser(sOpenSkyUser);
                            dataRefs.SetOpenSkyPwd(sOpenSkyPwd);
                            if (pOpenSkyCh) pOpenSkyCh->SetValid(true,false);
                            dataRefs.SetChannelEnabled(DR_CHANNEL_OPEN_SKY_ONLINE, true);
                            bOpenSkyPwdClearText = false;           // and hide the pwd now
                        }
                    }
                    ImGui::TableNextCell();
                }

                // OpenSky's connection status details
                if (ImGui::FilteredLabel("Connection Status", sFilter)) {
                    if (pOpenSkyCh) {
                        ImGui::TextUnformatted(pOpenSkyCh->GetStatusText().c_str());
                    } else {
                        ImGui::TextUnformatted("Off");
                    }
                    ImGui::TableNextCell();
                }
                
                if (!*sFilter) ImGui::TreePop();
            }
            
            // --- ADS-B Exchange ---
            if (ImGui::TreeNodeCbxLinkHelp("ADS-B Exchange", nCol,
                                           // we offer the enable checkbox only when an API key is defined
                                           dataRefs.GetADSBExAPIKey().empty() ? dataRefsLT(-1) : DR_CHANNEL_ADSB_EXCHANGE_ONLINE,
                                           dataRefs.GetADSBExAPIKey().empty() ? "ADS-B Exchange requires an API key" : "Enable ADS-B Exchange tracking data",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " " ADSBEX_CHECK_NAME,
                                           ADSBEX_CHECK_URL,
                                           ADSBEX_CHECK_POPUP,
                                           HELP_SET_CH_ADSBEX, "Open Help on ADS-B Exchange in Browser",
                                           sFilter, nOpCl))
            {
                // Have no ADSBEx key?
                if (dataRefs.GetADSBExAPIKey().empty()) {
                    if (ImGui::FilteredLabel("ADS-B Exchange", sFilter, false)) {
                        ImGui::TextDisabled("%s", "requires an API key:");
                        ImGui::TableNextCell();
                    }
                }
                
                // ADS-B Exchange's API key
                if (ImGui::FilteredLabel("API Key", sFilter)) {
                    // "Eye" button changes password flag
                    ImGui::Selectable(ICON_FA_EYE "##ADSBExKeyVisible", &bADSBExKeyClearText,
                                      ImGuiSelectableFlags_None, ImVec2(ImGui::GetWidthIconBtn(),0));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", "Show/Hide key");
                    ImGui::SameLine();  // make text entry the size of the remaining space in cell, but not larger
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputTextWithHint("##ADSBExKey",
                                                 "Enter or paste API key",
                                                 &sADSBExKeyEntry,
                                                 // clear text or password mode?
                                                 (bADSBExKeyClearText ? ImGuiInputTextFlags_None     : ImGuiInputTextFlags_Password) |
                                                 // prohibit changes to the key while test is underway
                                                 (eADSBExKeyTest == ADSBX_KEY_TESTING ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None)))
                        // when key is changing reset a potential previous result
                        eADSBExKeyTest = ADSBX_KEY_NO_ACTION;
                    // key is changed and different -> offer to test it
                    if (eADSBExKeyTest == ADSBX_KEY_NO_ACTION &&
                        !sADSBExKeyEntry.empty() &&
                        sADSBExKeyEntry != dataRefs.GetADSBExAPIKey())
                    {
                        if (ImGui::ButtonTooltip(ICON_FA_UNDO " Reset to saved", "Resets the key to the previously saved key"))
                        {
                            sADSBExKeyEntry = dataRefs.GetADSBExAPIKey();
                        }
                        ImGui::SameLine();
                        if (ImGui::ButtonTooltip(ICON_FA_CHECK " Test and Save Key", "Sends a request to ADS-B Exchange using your entered key to test its validity.\nKey is saved only after a successful test."))
                        {
                            ADSBExchangeConnection::TestADSBExAPIKey(sADSBExKeyEntry);
                            eADSBExKeyTest = ADSBX_KEY_TESTING;
                        }
                    }
                    // Test of key underway? -> check for result
                    if (eADSBExKeyTest == ADSBX_KEY_TESTING) {
                        bool bSuccess = false;
                        if (ADSBExchangeConnection::TestADSBExAPIKeyResult(bSuccess)) {
                            eADSBExKeyTest = bSuccess ? ADSBX_KEY_SUCCESS : ADSBX_KEY_FAILED;
                            if (bSuccess) {
                                dataRefs.SetADSBExAPIKey(sADSBExKeyEntry);
                                
                            }
                        } else {
                            ImGui::TextUnformatted(ICON_FA_SPINNER " Key is being tested...");
                        }
                    }
                    // Key tested successfully
                    if (eADSBExKeyTest == ADSBX_KEY_SUCCESS)
                        ImGui::TextUnformatted(ICON_FA_CHECK_CIRCLE " Key tested successfully");
                    else if (eADSBExKeyTest == ADSBX_KEY_FAILED)
                        ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " Key test failed!");
                    
                    // If available, show information on remaining RAPID API data usage
                    if (dataRefs.ADSBExRLimit || dataRefs.ADSBExRRemain)
                    {
                        ImGui::Text("%ld / %ld RAPID API requests left",
                                    dataRefs.ADSBExRRemain, dataRefs.ADSBExRLimit);
                    }

                    ImGui::TableNextCell();
                }

                if (!*sFilter) ImGui::TreePop();
            }

            // --- Open Glider Network ---
            if (ImGui::TreeNodeCbxLinkHelp("Open Glider Network", nCol,
                                           DR_CHANNEL_OPEN_GLIDER_NET, "Enable OGN tracking data",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " "  OPGLIDER_CHECK_NAME,
                                           OPGLIDER_CHECK_URL,
                                           OPGLIDER_CHECK_POPUP,
                                           HELP_SET_CH_OPENGLIDER, "Open Help on Open Glider Network in Browser",
                                           sFilter, nOpCl))
            {
                ImGui::TextUnformatted("Flarm A/c Types");
                ImGui::TableNextCell();
                ImGui::TextUnformatted("Map FLARM's aircraft types to one or more ICAO types for model matching:");
                ImGui::TableNextCell();
                    
                // One edit field for each Flarm aircraft type
                for (size_t i = 0; i < aFlarmAcTys.size(); i++) {
                    // Flarm Aircraft Type in human readable text
                    if (ImGui::FilteredLabel(OGNGetAcTypeName(FlarmAircraftTy(i)), sFilter)) {
                        ImGui::PushID(int(i));
                        
                        // Warning if text entry too short
                        if (aFlarmAcTys[i].length() < 2) {
                            ImGui::TablePrevCell();
                            ImGui::Indicator(false, "", "Too short a text to serve as ICAO aircraft type");
                            ImGui::TableNextCell();
                        }

                        // Edit field for entering ICAO aircraft type(s)
                        ImGui::SetNextItemWidth(2 * fSmallWidth);
                        ImGui::InputText("", &aFlarmAcTys[i], ImGuiInputTextFlags_CharsUppercase);
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            // definition changed, process it if it contains 2 chars or more
                            if (aFlarmAcTys[i].length() >= 2)
                                // set the array of values in the dataRefs, then read it back into our edit variable, formatted with a standard space separator
                                dataRefs.aFlarmToIcaoAcTy[i] = str_tokenize(aFlarmAcTys[i], " ,;/");
                        }
                        
                        // Undo change if not confirmed with Enter, also refreshes after Enter
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            aFlarmAcTys[i] = str_concat(dataRefs.aFlarmToIcaoAcTy[i], " ");
                        else if (ImGui::IsItemActive()) {
                            ImGui::SameLine();
                            ImGui::TextUnformatted("Press [Enter] to save");
                        }
                        
                        ImGui::TableNextCell();
                        ImGui::PopID();
                    } // if Flarm type visible
                } // for all Flarm types
                
                // User alternate Request/Reply way of requesting tracking data
                ImGui::FilteredCfgCheckbox("Use alternate connection", sFilter, DR_CFG_OGN_USE_REQUREPL, "Switches to requesting OGN tracking data via HTTP requests instead of receiving push information from an APRS connection.");
                
                if (!*sFilter) ImGui::TreePop();
            }
            
            // --- RealTraffic ---
            const bool bWasRTEnabled = dataRefs.IsChannelEnabled(DR_CHANNEL_REAL_TRAFFIC_ONLINE);
            if (ImGui::TreeNodeCbxLinkHelp("RealTraffic", nCol,
                                           DR_CHANNEL_REAL_TRAFFIC_ONLINE,
                                           "Enable RealTraffic tracking data",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " " RT_CHECK_NAME,
                                           RT_CHECK_URL,
                                           RT_CHECK_POPUP,
                                           HELP_SET_CH_REALTRAFFIC, "Open Help on RealTraffic in Browser",
                                           sFilter, nOpCl))
            {
                // RealTraffic traffic port number
                if (ImGui::FilteredLabel("Traffic Port", sFilter)) {
                    ImGui::SetNextItemWidth(fSmallWidth);
                    ImGui::InputText("", &sRTPort, ImGuiInputTextFlags_CharsDecimal);
                    // if changed then set (then re-read) the value
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        dataRefs.SetRTTrafficPort(std::stoi(sRTPort));
                        sRTPort = std::to_string(DataRefs::GetCfgInt(DR_CFG_RT_TRAFFIC_PORT));
                    }
                    else if (ImGui::IsItemActive()) {
                        ImGui::SameLine();
                        ImGui::TextUnformatted("[Enter] to save. Default: 49005, alternate: 49003");
                    }
                    ImGui::TableNextCell();
                }
                
                // RealTraffic's connection status details
                if (ImGui::FilteredLabel("Connection Status", sFilter)) {
                    const LTChannel* pRTCh = LTFlightDataGetCh(DR_CHANNEL_REAL_TRAFFIC_ONLINE);
                    if (pRTCh) {
                        ImGui::TextUnformatted(pRTCh->GetStatusText().c_str());
                        const std::string extStatus = pRTCh->GetStatusTextExt();
                        if (!extStatus.empty())
                            ImGui::TextUnformatted(extStatus.c_str());
                    } else {
                        ImGui::TextUnformatted("Off");
                    }
                    ImGui::TableNextCell();
                }
                
                if (!*sFilter) ImGui::TreePop();
            }
            
            // If RealTraffic has just been enabled then, as a courtesy,
            // we also make sure that OpenSky Master data is enabled
            if (!bWasRTEnabled && dataRefs.IsChannelEnabled(DR_CHANNEL_REAL_TRAFFIC_ONLINE))
                dataRefs.SetChannelEnabled(DR_CHANNEL_OPEN_SKY_AC_MASTERDATA, true);
            
            // --- FSCharter ---
            if (ImGui::TreeNodeCbxLinkHelp(FSC_NAME, nCol,
                                           DR_CHANNEL_FSCHARTER,
                                           "Enable tracking " FSC_NAME " online flights",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " " FSC_CHECK_NAME,
                                           FSC_CHECK_URL,
                                           FSC_CHECK_POPUP,
                                           HELP_SET_CH_FSCHARTER, "Open Help on " FSC_NAME " in Browser",
                                           sFilter, nOpCl))
            {
                LTChannel* pFSCCh = LTFlightDataGetCh(DR_CHANNEL_FSCHARTER);
                const bool bFSCon = dataRefs.IsChannelEnabled(DR_CHANNEL_FSCHARTER);
                
                // User
                if (ImGui::FilteredLabel("Log In", sFilter)) {
                    ImGui::Indent(ImGui::GetWidthIconBtn(true));
                    ImGui::InputTextWithHint("##FSCUser",
                                             "Email Address",
                                             &sFSCUser,
                                             // prohibit changes to the user while channel on
                                             (bFSCon ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None));
                    ImGui::Unindent(ImGui::GetWidthIconBtn(true));

                    ImGui::TableNextCell();
                }
                
                // Password
                if (ImGui::FilteredLabel("Password", sFilter)) {
                    // "Eye" button changes password flag
                    ImGui::Selectable(ICON_FA_EYE "##FSCPwdVisible", &bFSCPwdClearText,
                                      ImGuiSelectableFlags_None, ImVec2(ImGui::GetWidthIconBtn(),0));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", "Show/Hide password");
                    ImGui::SameLine();  // make text entry the size of the remaining space in cell, but not larger
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::InputTextWithHint("##FSCPwd",
                                             "Enter or paste FSC password",
                                             &sFSCPwd,
                                             // clear text or password mode?
                                             (bFSCPwdClearText ? ImGuiInputTextFlags_None     : ImGuiInputTextFlags_Password) |
                                             // prohibit changes to the pwd while channel on
                                             (bFSCon ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None));
                    
                    ImGui::TableNextCell();
                }
                
                // Save button or hint how to change
                if (!*sFilter) {
                    ImGui::TableNextCell();
                    if (bFSCon) {
                        ImGui::TextUnformatted("Disable the channel first if you want to change user/password.");
                    } else {
                        if (ImGui::ButtonTooltip(ICON_FA_SAVE " Save and Try", "Saves the credentials and activates the channel")) {
                            dataRefs.SetFSCharterUser(sFSCUser);
                            dataRefs.SetFSCharterPwd(sFSCPwd);
                            if (pFSCCh) pFSCCh->SetValid(true,false);
                            dataRefs.SetChannelEnabled(DR_CHANNEL_FSCHARTER, true);
                            bFSCPwdClearText = false;           // and hide the pwd now
                        }
                    }
                    ImGui::TableNextCell();
                }

                // FSCharter's connection status details
                if (ImGui::FilteredLabel("Connection Status", sFilter)) {
                    if (pFSCCh) {
                        ImGui::TextUnformatted(pFSCCh->GetStatusText().c_str());
                    } else {
                        ImGui::TextUnformatted("Off");
                    }
                    ImGui::TableNextCell();
                }

                if (!*sFilter) ImGui::TreePop();
            }

            
            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        } // --- Input Channels ---
        
        // MARK: --- Output Channels ---
        if (ImGui::TreeNodeHelp("Output Channels", nCol,
                                HELP_SET_OUTPUT_CH, "Open Help on Output Channels in Browser",
                                sFilter, nOpCl))
        {
            // --- ForeFlight ---
            if (ImGui::TreeNodeCbxLinkHelp("ForeFlight", nCol,
                                           DR_CHANNEL_FORE_FLIGHT_SENDER,
                                           "Enable sending data to ForeFlight",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " " FF_CHECK_NAME,
                                           FF_CHECK_URL,
                                           FF_CHECK_POPUP,
                                           HELP_SET_CH_FOREFLIGHT, "Open Help on ForeFlight in Browser",
                                           sFilter, nOpCl))
            {
                ImGui::FilteredCfgCheckbox("Send user's position", sFilter, DR_CFG_FF_SEND_USER_PLANE,  "Include your own plane's position in ForeFlight stream");
                ImGui::FilteredCfgCheckbox("Send traffic", sFilter, DR_CFG_FF_SEND_TRAFFIC,             "Include live traffic in ForeFlight stream");
                ImGui::FilteredCfgNumber("Send traffic every", sFilter, DR_CFG_FF_SEND_TRAFFIC_INTVL, 1, 30, 1, "%d seconds");

                if (!*sFilter) ImGui::TreePop();
            }
            
            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        } // --- Input Channels ---
        
        // MARK: --- Aircraft Labels ---
        if (ImGui::TreeNodeHelp("Aircraft Labels", nCol,
                                HELP_SET_ACLABELS, "Open Help on Aircraft Label options in Browser",
                                sFilter, nOpCl))
        {
            // When to show?
            unsigned c = dataRefs.GetLabelShowCfg().GetUInt();
            if (ImGui::FilteredLabel("Show in which views", sFilter)) {
                ImGui::CheckboxFlags("External", &c, (1 << 0)); ImGui::SameLine();
                ImGui::CheckboxFlags("Internal", &c, (1 << 1)); ImGui::SameLine();
                ImGui::CheckboxFlags("VR",       &c, (1 << 2)); ImGui::SameLine();
                ImGui::CheckboxFlags("Map",      &c, (1 << 3));
                ImGui::TableNextCell();
            }
            if (c != dataRefs.GetLabelShowCfg().GetUInt()) {
                cfgSet(DR_CFG_LABEL_SHOWN, int(c));
                XPMPEnableMap(true, dataRefs.ShallDrawMapLabels());
            }
            
            // Label cut off: distance / visibility
            ImGui::FilteredCfgNumber  ("Max Distance",          sFilter, DR_CFG_LABEL_MAX_DIST, 1, 50, 1, "%d nm");
            ImGui::FilteredCfgCheckbox("Cut off at Visibility", sFilter, DR_CFG_LABEL_VISIBILITY_CUT_OFF);

            // Static / dynamic info
            c = dataRefs.GetLabelCfg().GetUInt();
            if (ImGui::TreeNodeHelp("Static Information", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                ImGui::FilteredCheckboxFlags("ICAO A/C Type Code",  sFilter, &c, (1 << 0));
                ImGui::FilteredCheckboxFlags("Any A/C ID",          sFilter, &c, (1 << 1));
                ImGui::FilteredCheckboxFlags("Transponder Hex Code",sFilter, &c, (1 << 2));
                ImGui::FilteredCheckboxFlags("Registration",        sFilter, &c, (1 << 3));
                ImGui::FilteredCheckboxFlags("ICAO Operator Code",  sFilter, &c, (1 << 4));
                ImGui::FilteredCheckboxFlags("Call Sign",           sFilter, &c, (1 << 5));
                ImGui::FilteredCheckboxFlags("Flight Number",       sFilter, &c, (1 << 6));
                ImGui::FilteredCheckboxFlags("Route",               sFilter, &c, (1 << 7));
                if (!*sFilter) ImGui::TreePop();
            }

            if (ImGui::TreeNodeHelp("Dynamic Information", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                ImGui::FilteredCheckboxFlags("Flight Phase",        sFilter, &c, (1 << 8));
                ImGui::FilteredCheckboxFlags("Heading",             sFilter, &c, (1 << 9));
                ImGui::FilteredCheckboxFlags("Altitude [ft]",       sFilter, &c, (1 << 10));
                ImGui::FilteredCheckboxFlags("Height AGL [ft]",     sFilter, &c, (1 << 11));
                ImGui::FilteredCheckboxFlags("Speed [kn]",          sFilter, &c, (1 << 12));
                ImGui::FilteredCheckboxFlags("VSI [ft/min]",        sFilter, &c, (1 << 13));
                if (!*sFilter) ImGui::TreePop();
            }
            
            // Did the config change?
            if (c != dataRefs.GetLabelCfg().GetUInt())
                cfgSet(DR_CFG_LABELS, int(c));

            if (ImGui::TreeNodeHelp("Label Color", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                // Fixed or dynamic label color?
                int bLabelDyn = dataRefs.IsLabelColorDynamic();
                ImGui::FilteredRadioButton("Dynamic by Flight Model", sFilter, &bLabelDyn, 1);
                ImGui::FilteredRadioButton("Fixed color", sFilter, &bLabelDyn, 0);
                if (bool(bLabelDyn) != dataRefs.IsLabelColorDynamic())
                    cfgSet(DR_CFG_LABEL_COL_DYN, bLabelDyn);
                
                // If fixed then offer color selection
                if (!bLabelDyn && ImGui::FilteredLabel("Pick any color", sFilter)) {
                    constexpr const char* SUI_LBL_COL = "Label Color Picker";
                    float lblCol[4];
                    dataRefs.GetLabelColor(lblCol);
                    if (ImGui::ColorButton("Click to pick Label Color",
                                           ImVec4(lblCol[0], lblCol[1], lblCol[2], lblCol[3]),
                                           ImGuiColorEditFlags_NoAlpha))
                        ImGui::OpenPopup(SUI_LBL_COL);
                    
                    if (ImGui::BeginPopup(SUI_LBL_COL)) {
                        if (ImGui::ColorPicker3 (SUI_LBL_COL, lblCol,
                                                 ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoLabel |
                                                 ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoSidePreview))
                        {
                            const int col = (int)(
                            (std::lround(lblCol[0] * 255.0f) << 16) +   // red
                            (std::lround(lblCol[1] * 255.0f) <<  8) +   // green
                            (std::lround(lblCol[2] * 255.0f) <<  0));   // blue
                            cfgSet(DR_CFG_LABEL_COLOR, col);
                        }
                        ImGui::EndPopup();
                    }
                    
                    ImGui::SameLine();
                    ImGui::TextUnformatted("or a default:");
                    ImGui::SameLine();
                    if (ImGui::ColorButton("Yellow",ImVec4(1.0f, 1.0f, 0.0f, 1.0f)))
                        cfgSet(DR_CFG_LABEL_COLOR, COLOR_YELLOW);
                    ImGui::SameLine();
                    if (ImGui::ColorButton("Red",   ImVec4(1.0f, 0.0f, 0.0f, 1.0f)))
                        cfgSet(DR_CFG_LABEL_COLOR, COLOR_RED);
                    ImGui::SameLine();
                    if (ImGui::ColorButton("Green", ImVec4(0.0f, 1.0f, 0.0f, 1.0f)))
                        cfgSet(DR_CFG_LABEL_COLOR, COLOR_GREEN);
                    ImGui::SameLine();
                    if (ImGui::ColorButton("Blue",  ImVec4(0.0f, 0.94f, 0.94f, 1.0f)))
                        cfgSet(DR_CFG_LABEL_COLOR, COLOR_BLUE);
                    ImGui::TableNextCell();
                }

                if (!*sFilter) ImGui::TreePop();
            }
            
            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        } // --- Aircraft Labels ---


        // MARK: --- Advanced ---
        if (ImGui::TreeNodeHelp("Advanced", nCol,
                                HELP_SET_ADVANCED, "Open Help on Advanced options in Browser",
                                sFilter, nOpCl))
        {
            if (ImGui::TreeNodeHelp("Logging", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                const float cbWidth = ImGui::CalcTextSize("Warning (default)_____").x;
                if (ImGui::FilteredLabel("Log.txt logging level", sFilter)) {
                    ImGui::SetNextItemWidth(cbWidth);
                    int n = dataRefs.GetLogLevel();
                    if (ImGui::Combo("##LogLevel", &n, "Debug\0Info\0Warning (default)\0Error\0Fatal\0", 5))
                        dataRefs.SetLogLevel(n);
                    ImGui::TableNextCell();
                }
                if (ImGui::FilteredLabel("Message area", sFilter)) {
                    ImGui::SetNextItemWidth(cbWidth);
                    int n = dataRefs.GetMsgAreaLevel()-1;   // 0=Debug is not used
                    if (ImGui::Combo("##MsgLevel", &n, "Info (default)\0Warning\0Error\0Fatal\0", 4))
                        dataRefs.SetMsgAreaLevel(n+1);
                    ImGui::TableNextCell();
                }
                ImGui::FilteredCfgNumber("Max Message List Len", sFilter, DR_CFG_LOG_LIST_LEN, 25, 500, 25);

                if (!*sFilter) ImGui::TreePop();
            }

            if (ImGui::TreeNodeHelp("Aircraft Selection", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                ImGui::FilteredCfgNumber("Max number of aircraft", sFilter, DR_CFG_MAX_NUM_AC,        5, MAX_NUM_AIRCRAFT, 5);
                ImGui::FilteredCfgNumber("Search distance",        sFilter, DR_CFG_FD_STD_DISTANCE,   5, 100, 5, "%d nm");
                ImGui::FilteredCfgNumber("Snap to taxiway",        sFilter, DR_CFG_FD_SNAP_TAXI_DIST, 0,  50, 1, "%d m");
                
                ImGui::FilteredCfgNumber("Live data refresh",      sFilter, DR_CFG_FD_REFRESH_INTVL, 10, 180, 5, "%d s");
                ImGui::FilteredCfgNumber("Buffering period",       sFilter, DR_CFG_FD_BUF_PERIOD,    10, 180, 5, "%d s");
                ImGui::FilteredCfgNumber("A/c outdated timeout",   sFilter, DR_CFG_AC_OUTDATED_INTVL,10, 180, 5, "%d s");
                ImGui::FilteredCfgNumber("Max. Network timeout",   sFilter, DR_CFG_NETW_TIMEOUT,     10, 180, 5, "%d s");
            
                if (!*sFilter) ImGui::TreePop();
            }

            if (ImGui::TreeNodeHelp("Export", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                ImGui::FilteredCfgCheckbox("Export Tracking Data", sFilter, DR_DBG_EXPORT_FD,
                                           "Exports all received tracking data to 'LTExportFD.csv'\nfor analysis or use by feeding scripts.");
                ImGui::FilteredCfgCheckbox("Export User Aircraft", sFilter, DR_DBG_EXPORT_USER_AC,
                                           "Exports user's aircraft positions as tracking data to 'LTExportFD.csv'\nfor analysis or use by feeding scripts.");
                ImGui::FilteredCfgCheckbox("Export: Normalize Time", sFilter, DR_DBG_EXPORT_NORMALIZE_TS,
                                           "In each export file, have all timestamps start at zero.\nMakes it easier to combine data from different files later.");
                if (ImGui::FilteredLabel("Export file format", sFilter)) {
                    if (ImGui::RadioButton("AITFC", dataRefs.GetDebugExportFormat() == EXP_FD_AITFC))
                        dataRefs.SetDebugExportFormat(EXP_FD_AITFC);
                    ImGui::SameLine();
                    if (ImGui::RadioButton("RTTFC", dataRefs.GetDebugExportFormat() == EXP_FD_RTTFC))
                        dataRefs.SetDebugExportFormat(EXP_FD_RTTFC);
                }
                if (!*sFilter) ImGui::TreePop();
            }

            if (ImGui::TreeNodeHelp("User Interface", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                if (ImGui::FilteredLabel("Transparent Settings", sFilter)) {
                    ImGui::CheckboxFlags(// If setting mismatches reality then show re-open hint
                                         bool(dataRefs.SUItransp) != (wndStyle == WND_STYLE_HUD) ?
                                         "Reopen Settings window to take effect##TranspSettings" :
                                         "##TranspSettings",
                                         (unsigned*)&dataRefs.SUItransp, 1);
                    ImGui::TableNextCell();
                }
                
                if (ImGui::FilteredLabel("Font Scaling", sFilter)) {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);            // Slider is otherwise calculated too large, so we help here a bit
                    ImGui::SliderInt("##FontScaling", &dataRefs.UIFontScale, 10, 200, "%d%%");
                    ImGui::TableNextCell();
                }
                if (ImGui::FilteredLabel("Opacity", sFilter)) {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);            // Slider is otherwise calculated too large, so we help here a bit
                    ImGui::SliderInt("##Opacity", &dataRefs.UIopacity, 0, 100, "%d%%");
                    ImGui::TableNextCell();
                }
                if (!*sFilter) ImGui::TreePop();
            }

            // "Reset to Defaults" button: Pop up a confirmation dialog before actually resetting
            if (!*sFilter) {
                ImGui::TableNextCell();
                constexpr const char* SUI_ADVSET_POPUP = "Advanced Settings";
                if (ImGui::Button(ICON_FA_UNDO " Reset to Defaults..."))
                    ImGui::OpenPopup(SUI_ADVSET_POPUP);

                if (ImGui::BeginPopup(SUI_ADVSET_POPUP)) {
                    ImGui::TextUnformatted("Confirm: Resetting Advanced Settings to defaults");
                    if (ImGui::Button(ICON_FA_UNDO " Reset to Defaults")) {
                        dataRefs.SetLogLevel(logWARN);
                        dataRefs.SetMsgAreaLevel(logINFO);
                        cfgSet(DR_CFG_MAX_NUM_AC,           DEF_MAX_NUM_AC);
                        cfgSet(DR_CFG_FD_STD_DISTANCE,      DEF_FD_STD_DISTANCE);
                        cfgSet(DR_CFG_FD_SNAP_TAXI_DIST,    DEF_FD_SNAP_TAXI_DIST);
                        cfgSet(DR_CFG_FD_REFRESH_INTVL,     DEF_FD_REFRESH_INTVL);
                        cfgSet(DR_CFG_FD_BUF_PERIOD,        DEF_FD_BUF_PERIOD);
                        cfgSet(DR_CFG_AC_OUTDATED_INTVL,    DEF_AC_OUTDATED_INTVL);
                        cfgSet(DR_CFG_FD_BUF_PERIOD,        DEF_FD_BUF_PERIOD);     // there are interdependencies between refresh intvl, outdated intl, and buf_period
                        cfgSet(DR_CFG_FD_REFRESH_INTVL,     DEF_FD_REFRESH_INTVL);  // hence try resetting in both forward and backward order...one will work out
                        cfgSet(DR_CFG_NETW_TIMEOUT,         DEF_NETW_TIMEOUT);
                        dataRefs.UIFontScale    = DEF_UI_FONT_SCALE;
                        dataRefs.UIopacity      = DEF_UI_OPACITY;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_TIMES " Cancel"))
                        ImGui::CloseCurrentPopup();
                    
                    ImGui::EndPopup();
                } // Popup
                
                ImGui::TableNextCell();
            }

            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        } // --- Advanced ---
        
        // MARK: --- CSL ---
        if (ImGui::TreeNodeHelp("CSL", nCol,
                                HELP_SET_CSL, "Open Help on CSL Model options in Browser",
                                sFilter, nOpCl))
        {
            // Modelling Options
            if (ImGui::TreeNodeHelp("Modelling Options", nCol, nullptr, nullptr, sFilter, nOpCl,
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                ImGui::FilteredCfgCheckbox("Landing lights during taxi", sFilter, DR_CFG_LND_LIGHTS_TAXI,   "Some models do not feature taxi lights,\nthis makes them visible in the dark");

                if (ImGui::FilteredLabel("Default a/c type", sFilter)) {
                    // Indicator if saved OK
                    if (acTypeOK) {
                        ImGui::TablePrevCell();
                        ImGui::Indicator(acTypeOK > 0,
                                         "New default successfully saved",
                                         "Type doesn't exist, not saved");
                        ImGui::TableNextCell();
                    }
                    ImGui::SetNextItemWidth(fSmallWidth);
                    ImGui::InputText("##DefaultAcType", &acTypeEntry, ImGuiInputTextFlags_CharsUppercase);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        acTypeOK = dataRefs.SetDefaultAcIcaoType(acTypeEntry) ? 1 : -1;
                    }
                    if (ImGui::IsItemEdited()) acTypeOK = 0;
                    ImGui::TableNextCell();
                }

                if (ImGui::FilteredLabel("Ground vehicle type", sFilter)) {
                    // Indicator if saved OK
                    if (gndVehicleOK) {
                        ImGui::TablePrevCell();
                        ImGui::Indicator(gndVehicleOK > 0,
                                         "New vehicle type successfully saved",
                                         "Wrong text length, not saved");
                        ImGui::TableNextCell();
                    }
                    ImGui::SetNextItemWidth(fSmallWidth);
                    ImGui::InputText("##CarType", &gndVehicleEntry, ImGuiInputTextFlags_CharsUppercase);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        gndVehicleOK = dataRefs.SetDefaultCarIcaoType(gndVehicleEntry) ? 1 : -1;
                    }
                    if (ImGui::IsItemEdited()) gndVehicleOK = 0;
                    ImGui::TableNextCell();
                }
                
                ImGui::FilteredCfgCheckbox("Enhance models, copies files", sFilter, DR_CFG_COPY_OBJ_FILES, "Replaces dataRefs and textures (if needed) to support more animation and show correct livery, requires disk spaces for copied .obj files");

                if (!*sFilter) ImGui::TreePop();
            } // Modelling Options

            // Existing CSL paths (aren't included in filter search)
            if (!*sFilter &&
                ImGui::TreeNodeHelp("CSL Package Paths", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                constexpr const char* SUI_OPEN_FOLDER = "OpenFolder";
                int iDelete = -1;               // delete a path?
                DataRefs::vecCSLPaths& vec = dataRefs.GetCSLPaths();
                for (int i = 0; (size_t)i < vec.size(); ++i)
                {
                    DataRefs::CSLPathCfgTy& pathCfg = vec[size_t(i)];
                    ImGui::PushID(pathCfg.getPath().c_str());
                    
                    // Enable Checkbox / Load Button
                    ImGui::Checkbox("Auto Load", &pathCfg.bEnabled);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", "Load CSL package during startup?");
                    
                    // Indicator if path exists, right-aligned
                    ImGui::SameLine();
                    ImGui::Indicator(cslActiveLn == i ? bCslEntryExists : pathCfg.exists(),
                                     "Path exists and contains files",
                                     "Path does not exist or is empty");
                    
                    // Path entry
                    ImGui::TableNextCell();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 5.0f * ImGui::GetWidthIconBtn());
                    if (cslActiveLn == i) {
                        if (ImGui::InputText("##PathEntry", &cslEntry, ImGuiInputTextFlags_EnterReturnsTrue)) {
                            pathCfg = cslEntry;             // store path and stop editing
                            cslActiveLn = -1; cslEntry.clear();
                        }
                        // did the entry line just get changed? then test for valid path
                        if (ImGui::IsItemEdited())
                            bCslEntryExists = LTNumFilesInPath(LTCalcFullPath(cslEntry)) > 0;
                    } else
                        ImGui::InputText("##PathEntry", (std::string*)&pathCfg.getPath());
                    
                    // Now editing _this_ line?
                    if (cslActiveLn != i && ImGui::IsItemActive()) {
                        cslActiveLn = i;
                        cslEntry = pathCfg.getPath();
                        bCslEntryExists = pathCfg.existsSave();
                    }
                    
                    // Open Folder button
                    ImGui::SameLine();
                    if (ImGui::SmallButtonTooltip(ICON_FA_FOLDER_OPEN, "Select a folder") ||
                        // or this is the active line and we shall re-open the popup
                        (cslActiveLn == i && bSubDirsOpen)) {
                        if (cslActiveLn != i) {     // if not yet active:
                            cslActiveLn = i;        // make this the active line
                            cslEntry = pathCfg.getPath();
                            bCslEntryExists = pathCfg.existsSave();
                        }
                        ImGui::OpenPopup(SUI_OPEN_FOLDER);
                        bSubDirsOpen = false;
                    }
                    if (ImGui::SelectPath(SUI_OPEN_FOLDER, cslEntry)) {
                        bSubDirsOpen = true;        // reopen next frame!
                        bCslEntryExists = LTNumFilesInPath(LTCalcFullPath(cslEntry)) > 0;
                    }

                    ImGui::SameLine();
                    if (cslActiveLn == i && cslEntry != pathCfg.getPath()) {
                        // This line being edited and changed: offer Save button
                        if (ImGui::SmallButtonTooltip(ICON_FA_SAVE, "Save the changed path")) {
                            pathCfg = cslEntry;
                            cslActiveLn = -1; cslEntry.clear();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButtonTooltip(ICON_FA_UNDO, "Undo path change")) {
                            // actually, we stop editing without saving
                            cslActiveLn = -1; cslEntry.clear();
                        }
                    } else {
                        // Not being edited: offer Load button
                        if (ImGui::SmallButtonTooltip(ICON_FA_UPLOAD, "Load CSL packages now from this path (again)"))
                            dataRefs.LoadCSLPackage(pathCfg.getPath());
                        ImGui::SameLine();
                        // Delete button, requires confirmation
                        constexpr const char* SUI_CSL_DEL_POPUP = "Delete CSL Path";
                        if (ImGui::SmallButtonTooltip(ICON_FA_TRASH_ALT, "Remove this path from the configuration"))
                            ImGui::OpenPopup(SUI_CSL_DEL_POPUP);
                        if (ImGui::BeginPopup(SUI_CSL_DEL_POPUP)) {
                            ImGui::Text("Confirm deletion of path\n%s", pathCfg.getPath().c_str());
                            if (ImGui::Button(ICON_FA_TRASH_ALT " Delete"))
                                iDelete = i;        // can't delete here as we loop the array, delete later
                            ImGui::SameLine();
                            if (ImGui::Button(ICON_FA_UNDO " Keep"))
                                ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                        }
                    }
                    
                    ImGui::TableNextCell();
                    ImGui::PopID();
                } // for all CSL paths
                
                // One additional line for the possibility to add a new path
                
                bool bDoAdd = false;
                ImGui::TextUnformatted("Add new path:");
                if (!cslNew.empty()) {
                    // Indicator if path exists, right-aligned
                    ImGui::SameLine();
                    ImGui::Indicator(bCslNewExists,
                                     "Path exists and contains files",
                                     "Path does not exist or is empty");
                }
                
                // Text Input
                ImGui::TableNextCell();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 5.0f * ImGui::GetWidthIconBtn());
                if (ImGui::InputText("##NewCSLPath", &cslNew, ImGuiInputTextFlags_EnterReturnsTrue) &&
                    bCslNewExists)        // store new path only if it exists
                    bDoAdd = true;
                // did the entry line just get changed? then test for valid path
                if (ImGui::IsItemEdited())
                    bCslNewExists = LTNumFilesInPath(LTCalcFullPath(cslNew)) > 0;
                ImGui::SameLine();
                
                // Folder selection button
                if (ImGui::SmallButtonTooltip(ICON_FA_FOLDER_OPEN, "Select a folder") ||
                    bNewSubDirsOpen) {
                    ImGui::OpenPopup(SUI_OPEN_FOLDER);
                    bNewSubDirsOpen = false;
                }
                if (ImGui::SelectPath(SUI_OPEN_FOLDER, cslNew)) {
                    bNewSubDirsOpen = true;             // open next frame again
                    bCslNewExists = LTNumFilesInPath(LTCalcFullPath(cslNew)) > 0;
                }
                
                // Save button
                if (bCslNewExists && !cslNew.empty()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButtonTooltip(ICON_FA_SAVE, "Add the new path and load the models"))
                        bDoAdd = true;
                }
                
                // Shall we delete any of the paths?
                if (0 <= iDelete && iDelete < (int)vec.size())
                    vec.erase(std::next(vec.begin(), iDelete));
                
                // Shall we add a new path?
                if (bDoAdd) {
                    // avoid duplicates
                    if (std::find(vec.cbegin(), vec.cend(), cslNew) == vec.cend()) {
                        vec.emplace_back(true, cslNew);
                        dataRefs.LoadCSLPackage(vec.back().getPath());
                    }
                    cslNew.clear();
                    bCslNewExists = false;
                }
                
                if (!*sFilter) ImGui::TreePop();
            } // List of paths

            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        } // --- Advanced ---

        // MARK: --- Debug ---
        const bool bLimitations =
        !dataRefs.GetDebugAcFilter().empty() ||
        !dataRefs.cslFixAcIcaoType.empty() ||
        !dataRefs.cslFixOpIcao.empty() ||
        !dataRefs.cslFixLivery.empty();

        if (ImGui::TreeNodeLinkHelp("Debug", nCol,
                                    bLimitations ? ICON_FA_EXCLAMATION_TRIANGLE : nullptr, nullptr,
                                    "Forced Model Matching or filter options are active, restricting chosen aircraft or CSL models!",
                                    HELP_SET_DEBUG, "Open Help on Debug options in Browser",
                                    sFilter, nOpCl,
                                    (bLimitations ? ImGuiTreeNodeFlags_DefaultOpen : 0) | ImGuiTreeNodeFlags_SpanFullWidth))
        {
            if (ImGui::TreeNodeHelp("Logging", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                unsigned int bLogDebug = dataRefs.GetLogLevel() == logDEBUG;
                if (ImGui::FilteredCheckboxFlags("Set Log Level = Debug", sFilter, &bLogDebug, 1,
                                                 "Sets the logging level for Log.txt to 'Debug',\nsame as above in 'Advanced/Logging'"))
                    dataRefs.SetLogLevel(bLogDebug ? logDEBUG : logWARN);
                ImGui::FilteredCfgCheckbox("Log Model Matching", sFilter, DR_DBG_MODEL_MATCHING,
                                           "Logs how available tracking data was matched with the chosen CSL model (into Log.txt)");
                ImGui::FilteredCfgCheckbox("Log a/c positions", sFilter, DR_DBG_AC_POS,
                                           "Logs detailed position information of currently selected aircraft (into Log.txt)");
                ImGui::FilteredCfgCheckbox("Log Raw Network Data", sFilter, DR_DBG_LOG_RAW_FD,
                                           "Creates additional log file 'LTRawFD.log'\ncontaining all raw network requests and responses.");

                if (!*sFilter) ImGui::TreePop();
            }
            
            constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank;
            if (ImGui::TreeNodeHelp("Forced Model Matching", nCol, nullptr, nullptr,
                                    sFilter, nOpCl,
                                    (bLimitations ? ImGuiTreeNodeFlags_DefaultOpen : 0) | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                bool bChanged = ImGui::FilteredInputText("ICAO a/c type", sFilter, txtFixAcType, fSmallWidth, nullptr, flags);
                bChanged = ImGui::FilteredInputText("ICAO operator/airline", sFilter, txtFixOp, fSmallWidth, nullptr, flags) || bChanged;
                bChanged = ImGui::FilteredInputText("Livery / registration", sFilter, txtFixLivery, fSmallWidth, nullptr, flags) || bChanged;
                if (bChanged) {
                    dataRefs.cslFixAcIcaoType = txtFixAcType;
                    dataRefs.cslFixOpIcao = txtFixOp;
                    dataRefs.cslFixLivery = txtFixLivery;
                    if (dataRefs.cslFixAcIcaoType.empty()   &&
                        dataRefs.cslFixOpIcao.empty()       &&
                        dataRefs.cslFixLivery.empty())
                        SHOW_MSG(logWARN, MSG_MDL_NOT_FORCED)
                    else
                        SHOW_MSG(logWARN, MSG_MDL_FORCED,
                                 dataRefs.cslFixAcIcaoType.c_str(),
                                 dataRefs.cslFixOpIcao.c_str(),
                                 dataRefs.cslFixLivery.c_str());
                }
                
                if (!*sFilter) ImGui::TreePop();
            }
            
            if (ImGui::FilteredInputText("Filter single a/c", sFilter, txtDebugFilter, fSmallWidth, nullptr, flags))
            {
                mapLTFlightDataTy::iterator fdIter = mapFd.end();
                if (!txtDebugFilter.empty())
                    fdIter = mapFdSearchAc(txtDebugFilter);
                // found?
                if (fdIter != mapFd.cend()) {
                    txtDebugFilter = fdIter->second.key();
                    DataRefs::LTSetDebugAcFilter(NULL,int(fdIter->second.key().num));
                }
                else
                    DataRefs::LTSetDebugAcFilter(NULL,0);
            }

            if (ImGui::FilteredInputText("Dump apt layout", sFilter, txtAptDump, fSmallWidth,
                                         "Dump internal airport layout data\nthat can be displayed using GPS Visualizer", flags))
            {
                if (LTAptDump(txtAptDump)) {
                    SHOW_MSG(logMSG, "Dumped airport layout of %s", txtAptDump.c_str());
                } else {
                    SHOW_MSG(logERR, "FAILED dumping airport layout of %s! Does this airport exist?", txtAptDump.c_str());
                }
            }

            
            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        } // --- Debug ---
        
        // Version information
        if constexpr (LIVETRAFFIC_VERSION_BETA) {
            if (ImGui::FilteredLabel("BETA Version", sFilter)) {
                ImGui::Text("%s, limited to %s",
                            LT_VERSION_FULL, LT_BETA_VER_LIMIT_TXT);
            }
        } else {
            if (ImGui::FilteredLabel("Version", sFilter)) {
                ImGui::Text("%s", LT_VERSION_FULL);
            }
        }
        
        // --- End of the table
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(3);
}

/*
void LTSettingsUI::SaveCSLPath(int idx)
{
    // what to save
    DataRefs::CSLPathCfgTy newPath {
        static_cast<bool>(XPGetWidgetProperty(widgetIds[UI_CSL_BTN_ENABLE_1 + idx*SETUI_CSL_ELEMS_PER_PATH],
                                              xpProperty_ButtonState,NULL)),
        txtCSLPaths[idx].GetDescriptor()
    };
    
    // save
    dataRefs.SaveCSLPath(idx, newPath);
}

*/

//
// MARK: Static Functions
//

// Creates/opens/displays/hides/closes the settings window
bool LTSettingsUI::ToggleDisplay (int _force)
{
    // If we toggle then do what current is not the state
    if (!_force)
        _force = LTSettingsUI::IsDisplayed() ? -1 : 1;
    
    // Open the window?
    if (_force > 0)
    {
        // Create the object and window if needed
        if (!gpSettings)
            gpSettings = new LTSettingsUI();
        // Ensure it is visible and centered
        gpSettings->SetVisible(true);
        gpSettings->BringWindowToFront();
        return true;                    // visible now
    }
    // Close the window
    else
    {
        if (gpSettings)                 // just remove the object
            delete gpSettings;          // (constructor clears gpSettings)
        return false;                   // not visible
    }
}

// Is the settings window currently displayed?
bool LTSettingsUI::IsDisplayed ()
{
    return gpSettings && gpSettings->GetVisible();
}
