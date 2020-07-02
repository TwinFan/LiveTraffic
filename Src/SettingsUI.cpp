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
            WndRect(0, dataRefs.SUIheight, dataRefs.SUIwidth, 0))
{
    // Set up window basics
    SetWindowTitle(SUI_WND_TITLE);
    SetWindowResizingLimits(SUI_RESIZE_LIMITS.tl.x, SUI_RESIZE_LIMITS.tl.y,
                            SUI_RESIZE_LIMITS.br.x, SUI_RESIZE_LIMITS.br.y);
    SetVisible(true);
    
    // Define Help URL to open for generic Help (?) button
    szHelpURL = HELP_SETTINGS;
    
    // If there is no ADSBEx key yet then display any new entry in clear text,
    // If a key is already defined, then by default obscure it
    sADSBExKeyEntry = dataRefs.GetADSBExAPIKey();
    bADSBExKeyClearText = sADSBExKeyEntry.empty();
    
    // Fill debug entry texts with current values
    txtDebugFilter  = dataRefs.GetDebugAcFilter();
    txtFixAcType    = dataRefs.cslFixAcIcaoType;
    txtFixOp        = dataRefs.cslFixOpIcao;
    txtFixLivery    = dataRefs.cslFixLivery;
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
    if (!IsPoppedOut()) {
        const WndRect r = GetCurrentWindowGeometry();
        dataRefs.SUIwidth   = r.width();
        dataRefs.SUIheight  = r.height();
    }
    
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
                          ImGuiTableFlags_BordersHInner))
    {
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
            ImGui::FilteredCfgCheckbox("Landing lights during taxi", sFilter, DR_CFG_LND_LIGHTS_TAXI,   "Some models do not feature taxi lights,\nthis makes them visible in the dark");
            
            // auto-open and warning if any of these values are set as they limit what's shown
            const bool bSomeRestrict = dataRefs.IsAIonRequest() || dataRefs.IsAutoHidingActive();
            if (ImGui::TreeNodeLinkHelp("Cooperation", nCol,
                                        bSomeRestrict ? ICON_FA_EXCLAMATION_TRIANGLE : nullptr, nullptr,
                                        "Some options are active, restricting displayed traffic or TCAS!",
                                        HELP_SET_BASICS, "Open Help on Basics in Browser",
                                        sFilter, nOpCl,
                                        (bSomeRestrict ? ImGuiTreeNodeFlags_DefaultOpen : 0) | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                ImGui::FilteredCfgCheckbox("TCAS on request only",   sFilter, DR_CFG_AI_ON_REQUEST,     "Do not take over control of TCAS automatically, but only via menu 'TCAS controlled'");
                ImGui::FilteredCfgCheckbox("Hide a/c while taxiing", sFilter, DR_CFG_HIDE_TAXIING,      "Hide aircraft in phase 'Taxi'");
                ImGui::FilteredCfgNumber("No aircraft below", sFilter, DR_CFG_HIDE_BELOW_AGL, 0, 10000, 100, "%d ft AGL");

                if (!*sFilter) ImGui::TreePop();
            }

            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        }

        // MARK: --- Input Channels ---
        if (ImGui::TreeNodeHelp("Input Channels", nCol,
                                HELP_SET_INPUT_CH, "Open Help on Channels in Browser",
                                sFilter, nOpCl,
                                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
        {
            // --- OpenSky ---
            if (ImGui::TreeNodeCbxLinkHelp("OpenSky Network", nCol,
                                           DR_CHANNEL_OPEN_SKY_ONLINE, "Enable OpenSky tracking data",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " OpenSky Explorer",
                                           "https://opensky-network.org/network/explorer",
                                           "Check OpenSky's coverage",
                                           HELP_SET_CH_OPENSKY, "Open Help on OpenSky in Browser",
                                           sFilter, nOpCl))
            {
                ImGui::FilteredCfgCheckbox("OpenSky Network Master Data", sFilter, DR_CHANNEL_OPEN_SKY_AC_MASTERDATA, "Query OpenSky for aicraft master data like type, registration...");

                if (!*sFilter) ImGui::TreePop();
            }
            
            // --- ADS-B Exchange ---
            if (ImGui::TreeNodeCbxLinkHelp("ADS-B Exchange", nCol,
                                           // we offer the enable checkbox only when an API key is defined
                                           dataRefs.GetADSBExAPIKey().empty() ? CNT_DATAREFS_LT : DR_CHANNEL_ADSB_EXCHANGE_ONLINE,
                                           "Enable ADS-B Exchange tracking data",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " ADSBX Radar View",
                                           "https://tar1090.adsbexchange.com/",
                                           "Check ADS-B Exchange's coverage",
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

            // --- RealTraffic ---
            const bool bWasRTEnabled = dataRefs.IsChannelEnabled(DR_CHANNEL_REAL_TRAFFIC_ONLINE);
            if (ImGui::TreeNodeCbxLinkHelp("RealTraffic", nCol,
                                           DR_CHANNEL_REAL_TRAFFIC_ONLINE,
                                           "Enable RealTraffic tracking data",
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " RealTraffic's web site",
                                           "https://rtweb.flyrealtraffic.com/",
                                           "Open RealTraffic's web site, which has a traffic status overview",
                                           HELP_SET_CH_REALTRAFFIC, "Open Help on RealTraffic in Browser",
                                           sFilter, nOpCl))
            {
                // RealTraffic's connection status details
                if (dataRefs.IsChannelEnabled(DR_CHANNEL_REAL_TRAFFIC_ONLINE) ||
                    (dataRefs.pRTConn && dataRefs.pRTConn->IsConnecting()))
                {
                    if (ImGui::FilteredLabel("Connection Status", sFilter)) {
                        if (dataRefs.pRTConn && dataRefs.pRTConn->IsConnecting()) {
                            // There is a RealTraffic connection object
                            ImGui::TextUnformatted(dataRefs.pRTConn->GetStatusWithTimeStr().c_str());
                            if (dataRefs.pRTConn->IsConnected())
                                ImGui::Text("%ld hPa at %s",
                                            std::lround(dataRefs.pRTConn->GetHPA()),
                                            dataRefs.pRTConn->GetMetarIcao().c_str());
                        }
                        else
                            // Channel is activated, but not yet started
                            ImGui::TextUnformatted("Starting...");
                        ImGui::TableNextCell();
                    }
                }
                
                if (!*sFilter) ImGui::TreePop();
            }
            
            // If RealTraffic has just been enabled then, as a courtesy,
            // we also make sure that OpenSky Master data is enabled
            if (!bWasRTEnabled && dataRefs.IsChannelEnabled(DR_CHANNEL_REAL_TRAFFIC_ONLINE))
                dataRefs.SetChannelEnabled(DR_CHANNEL_OPEN_SKY_AC_MASTERDATA, true);
            
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
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " ForeFlight Mobile EFB",
                                           "https://foreflight.com/products/foreflight-mobile/",
                                           "Open ForeFlight's web site about the Mobile EFB",
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
            }
            if (c != dataRefs.GetLabelShowCfg().GetUInt()) {
                cfgSet(DR_CFG_LABEL_SHOWN, int(c));
                XPMPEnableMap(true, dataRefs.ShallDrawMapLabels());
            }

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

            if (ImGui::TreeNodeHelp("Label Color", nCol, nullptr, nullptr, sFilter, nOpCl))
            {
                // Fixed or dynamic label color?
                int bLabelDyn = dataRefs.IsLabelColorDynamic();
                ImGui::FilteredRadioButton("Dynamic by Flight Model", sFilter, &bLabelDyn, 1);
                ImGui::FilteredRadioButton("Fixed color", sFilter, &bLabelDyn, 0);
                if (bLabelDyn != dataRefs.IsLabelColorDynamic())
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

                if (!*sFilter) ImGui::TreePop();
            }

            if (ImGui::TreeNodeHelp("Aircraft Selection", nCol, nullptr, nullptr, sFilter, nOpCl,
                                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                ImGui::FilteredCfgNumber("Max number of aircraft", sFilter, DR_CFG_MAX_NUM_AC,        5, 100, 5);
                ImGui::FilteredCfgNumber("Search distance",        sFilter, DR_CFG_FD_STD_DISTANCE,   5, 100, 5, "%d nm");
                ImGui::FilteredCfgNumber("Snap to taxiway",        sFilter, DR_CFG_FD_SNAP_TAXI_DIST, 0,  50, 1, "%d m");
                
                ImGui::FilteredCfgNumber("Live data refresh",      sFilter, DR_CFG_FD_REFRESH_INTVL, 10, 180, 5, "%d s");
                ImGui::FilteredCfgNumber("Buffering period",       sFilter, DR_CFG_FD_BUF_PERIOD,    10, 180, 5, "%d s");
                ImGui::FilteredCfgNumber("A/c outdated timeout",   sFilter, DR_CFG_AC_OUTDATED_INTVL,10, 180, 5, "%d s");
                ImGui::FilteredCfgNumber("Network timeout",        sFilter, DR_CFG_NETW_TIMEOUT,     10, 180, 5, "%d s");
            
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
                
                if (ImGui::FilteredLabel("Opacity", sFilter)) {
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
            
            constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_EnterReturnsTrue;
            const float fWidth = ImGui::CalcTextSize("ABCDEF__").x;
            if (ImGui::TreeNodeHelp("Forced Model Matching", nCol, nullptr, nullptr,
                                    sFilter, nOpCl,
                                    (bLimitations ? ImGuiTreeNodeFlags_DefaultOpen : 0) | ImGuiTreeNodeFlags_SpanFullWidth))
            {
                bool bChanged = ImGui::FilteredInputText("ICAO a/c type", sFilter, txtFixAcType, fWidth, nullptr, flags);
                bChanged = ImGui::FilteredInputText("ICAO operator/airline", sFilter, txtFixOp, fWidth, nullptr, flags) || bChanged;
                bChanged = ImGui::FilteredInputText("Livery / registration", sFilter, txtFixLivery, fWidth, nullptr, flags) || bChanged;
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
            
            if (ImGui::FilteredInputText("Filter single a/c", sFilter, txtDebugFilter, fWidth, nullptr, flags))
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

            
            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        } // --- Debug ---
        
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
        gpSettings->SetMode(WND_MODE_FLOAT_CNT_VR);
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
