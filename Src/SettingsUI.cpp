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

// Global static pointer to the one settings object
static LTSettingsUI* gpSettings = nullptr;

/// Initial size of Settings Window (XP coordinates: l,t;r,b with t > b)
const WndRect SUI_INIT_SIZE = WndRect(0, 500, 690, 0);
/// Resizing limits (minimum and maximum sizes)
const WndRect SUI_RESIZE_LIMITS = WndRect(300, 300, 99999, 99999);

static float SUI_LABEL_SIZE = NAN;              ///< Width of 1st column, which displays static labels
static float SUI_VALUE_SIZE = NAN;              ///< Ideal Width of 2nd column for text entry

// Constructor creates and displays the window
LTSettingsUI::LTSettingsUI () :
LTImgWindow(WND_MODE_FLOAT_CNT_VR, WND_STYLE_SOLID, SUI_INIT_SIZE)
{
    // Set up window basics
    SetWindowTitle( LIVE_TRAFFIC " Settings");
    SetWindowResizingLimits(SUI_RESIZE_LIMITS.tl.x, SUI_RESIZE_LIMITS.tl.y,
                            SUI_RESIZE_LIMITS.br.x, SUI_RESIZE_LIMITS.br.y);
    SetVisible(true);
    
    // Define Help URL to open for generic Help (?) button
    szHelpURL = HELP_SETTINGS;
    
    // If there is no ADSBEx key yet then display any new entry in clear text,
    // If a key is already defined, then by default obscure it
    sADSBExKeyEntry = dataRefs.GetADSBExAPIKey();
    bADSBExKeyClearText = sADSBExKeyEntry.empty();
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
        std::ceil(ImGui::CalcTextSize("_01234567890abcdefghijklmnopq"/*rstuvwxyz01234567890ab"*/).x / 10.0f) * 10.0f;
    }
    return ImGuiWindowFlags_None;
}

// Main function to render the window's interface
void LTSettingsUI::buildInterface()
{
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

    // Help and Maximize etc. buttons
    ImGui::SameLine();
    buildWndButtons();
    
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
            if (ImGui::TreeNodeLinkHelp("Parallelism with other plugins", nCol,
                                        bSomeRestrict ? ICON_FA_EXCLAMATION_TRIANGLE : nullptr, nullptr,
                                        "Some options are active restricting displayed traffic or TCAS!",
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
                                           ICON_FA_EXTERNAL_LINK_SQUARE_ALT " OpenSky Exporer",
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
                if (ImGui::FilteredLabel("ADSBEx API Key", sFilter)) {
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

        // MARK: --- Advanced ---
        if (ImGui::TreeNodeHelp("Advanced", nCol,
                                HELP_SET_ADVANCED, "Open Help on Advanced options in Browser",
                                sFilter, nOpCl))
        {
            ImGui::FilteredCfgNumber("Max number of aircraft", sFilter, DR_CFG_MAX_NUM_AC,        5, 100, 5);
            ImGui::FilteredCfgNumber("Search distance",        sFilter, DR_CFG_FD_STD_DISTANCE,   5, 100, 5, "%d nm");
            ImGui::FilteredCfgNumber("Snap to taxiway",        sFilter, DR_CFG_FD_SNAP_TAXI_DIST, 0,  50, 1, "%d m");
            
            ImGui::FilteredCfgNumber("Live data refresh",      sFilter, DR_CFG_FD_REFRESH_INTVL, 10, 180, 5, "%d s");
            ImGui::FilteredCfgNumber("Buffering period",       sFilter, DR_CFG_FD_BUF_PERIOD,    10, 180, 5, "%d s");
            ImGui::FilteredCfgNumber("A/c outdated timeout",   sFilter, DR_CFG_AC_OUTDATED_INTVL,10, 180, 5, "%d s");
            ImGui::FilteredCfgNumber("Network timeout",        sFilter, DR_CFG_NETW_TIMEOUT,     10, 180, 5, "%d s");

            if (!*sFilter) { ImGui::TreePop(); ImGui::Spacing(); }
        } // --- Advanced ---
        
        // --- End of the table
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(3);
}

/*
void LTSettingsUI::LabelBtnSave()
{
    // store the checkboxes states in a zero-inited configuration
    DataRefs::LabelCfgTy cfg = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    cfg.bIcaoType     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_TYPE],xpProperty_ButtonState,NULL);
    cfg.bAnyAcId      = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_AC_ID],xpProperty_ButtonState,NULL);
    cfg.bTranspCode   = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_TRANSP],xpProperty_ButtonState,NULL);
    cfg.bReg          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_REG],xpProperty_ButtonState,NULL);
    cfg.bIcaoOp       = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_OP],xpProperty_ButtonState,NULL);
    cfg.bCallSign     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_CALL_SIGN],xpProperty_ButtonState,NULL);
    cfg.bFlightNo     = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_FLIGHT_NO],xpProperty_ButtonState,NULL);
    cfg.bRoute        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_ROUTE],xpProperty_ButtonState,NULL);
    cfg.bPhase        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_PHASE],xpProperty_ButtonState,NULL);
    cfg.bHeading      = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_HEADING],xpProperty_ButtonState,NULL);
    cfg.bAlt          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_ALT],xpProperty_ButtonState,NULL);
    cfg.bHeightAGL    = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_HEIGHT],xpProperty_ButtonState,NULL);
    cfg.bSpeed        = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_SPEED],xpProperty_ButtonState,NULL);
    cfg.bVSI          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_VSI],xpProperty_ButtonState,NULL);
    // save as current config
    drCfgLabels.Set(cfg.GetInt());
    
    // store the when-to-show information in a similar way
    DataRefs::LabelShowCfgTy show = { 0, 0, 0 };
    show.bExternal    = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_EXTERNAL],xpProperty_ButtonState,NULL);
    show.bInternal    = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_INTERNAL],xpProperty_ButtonState,NULL);
    show.bVR          = (unsigned)XPGetWidgetProperty(widgetIds[UI_LABELS_BTN_VR],xpProperty_ButtonState,NULL);
    drCfgLabelShow.Set(show.GetInt());
}

void LTSettingsUI::UpdateRealTraffic()
{
    if (dataRefs.pRTConn) {
        capRealTrafficStatus.SetDescriptor(dataRefs.pRTConn->GetStatusWithTimeStr());
        capRealTrafficMetar.SetDescriptor(dataRefs.pRTConn->IsConnected() ?
                                          std::to_string(std::lround(dataRefs.pRTConn->GetHPA())) +
                                          " hPa @ " + dataRefs.pRTConn->GetMetarIcao() : "");
    } else {
        capRealTrafficStatus.SetDescriptor("");
        capRealTrafficMetar.SetDescriptor("");
    }
}

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
