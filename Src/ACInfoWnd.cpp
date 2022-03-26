/// @file       ACInfoWnd.cpp
/// @brief      Aircraft information window showing details for a selected aircraft
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
// MARK: ACIWnd Implementation
//

/// Resizing limits (minimum and maximum sizes)
const WndRect ACI_RESIZE_LIMITS = WndRect(200, 200, 640, 9999);

/// Bits representing the named sections in the collapsed sections config
enum SectionsBits {
    ACI_SB_IDENTIFICATION = 0,      ///< section "Identification"
    ACI_SB_FLIGHT_INFO,             ///< section "Flight Info / Tracking data"
    ACI_SB_POSITION,                ///< section "Position"
    ACI_SB_SIMULATION,              ///< section "Simulation"
};

constexpr float ACI_AUTO_CHECK_PERIOD = 1.00f;  ///< How often to check for AUTO a/c change? [s]
constexpr float ACI_TREE_V_SEP        = 5.00f;  ///< Separation between tree sections

static float ACI_LABEL_SIZE = NAN;              ///< Width of first column, which displays static labels
static float ACI_AUTO_CB_SIZE = NAN;            ///< Width of AUTO checkbox

// Constructor shows a window for the given a/c key
ACIWnd::ACIWnd(const std::string& _acKey, WndMode _mode) :
LTImgWindow(_mode, WND_STYLE_HUD, dataRefs.ACIrect),
bAuto(_acKey.empty()),              // if _acKey empty -> AUTO mode
keyEntry(_acKey)                    // the passed-in input is taken as the user's entry
{
    // Set up window basics
    SetWindowTitle(GetWndTitle());
    SetWindowResizingLimits(ACI_RESIZE_LIMITS.tl.x, ACI_RESIZE_LIMITS.tl.y,
                            ACI_RESIZE_LIMITS.br.x, ACI_RESIZE_LIMITS.br.y);
    
    // If this is not the first window then it got created right on top of an existing one
    // -> move it down/right by a few pixel
    if (!listACIWnd.empty() && IsInsideSim()) {
        int delta = int(ImGui::GetWidthIconBtn() + 2 * ImGui::GetStyle().ItemSpacing.x);
        SetCurrentWindowGeometry(dataRefs.ACIrect.shift(delta,-delta));
    }
    
    // Define Help URL to open for Help (?) button
    szHelpURL = HELP_AC_INFO_WND;
    
    // Add myself to the list of ACI windows
    listACIWnd.push_back(this);
    
    // Search for a matching a/c
    if (bAuto)
        UpdateFocusAc();
    else
        SearchAndSetFlightData();
}

// Desctructor cleans up
ACIWnd::~ACIWnd()
{
    // remove myself from the list of windows
    listACIWnd.remove(this);
}

// Set the a/c key - no validation, if invalid window will clear
void ACIWnd::SetAcKey (const LTFlightData::FDKeyTy& _key)
{
    ClearAcKey();                   // clear a lot of data
    keyEntry = (acKey = _key);      // remember the key
    SetWindowTitle(GetWndTitle());  // set the window's title
    ReturnKeyboardFocus();          // give up keyboard focus in case we had it

    // set as 'selected' aircraft for debug output
    dataRefs.LTSetAcKey(reinterpret_cast<void*>(long(DR_AC_KEY)),
                        (int)acKey.num);
}

// Clear the a/c key, ie. display no data
void ACIWnd::ClearAcKey ()
{
    // if this was the 'selected' aircraft then it is no longer
    if (acKey == dataRefs.GetSelectedAcKey())
        dataRefs.LTSetAcKey(reinterpret_cast<void*>(long(DR_AC_KEY)), 0);
    
    acKey.clear();
    keyEntry.clear();
    lastAutoCheck = 0.0f;
    
    SetWindowTitle(GetWndTitle());
}

// Set AUTO mode
void ACIWnd::SetAuto (bool _b)
{
    bAuto = _b;
    if (!_b)
        lastAutoCheck = 0.0f;
    else
        UpdateFocusAc();
}

// Return the text to be used as window title
std::string ACIWnd::GetWndTitle () const
{
    return
    (acKey.empty() ? std::string(ACI_WND_TITLE) : stat.acId(std::string(acKey))) +
    (bAuto ? " (AUTO)" : "");
}

// Taking user's temporary input `keyEntry` searches for a valid a/c, sets acKey on success
bool ACIWnd::SearchAndSetFlightData ()
{
    mapLTFlightDataTy::iterator fdIter = mapFd.end();
    
    trim(keyEntry);
    if (!keyEntry.empty())
        fdIter = mapFdSearchAc(keyEntry);
    
    // found?
    if (fdIter != mapFd.cend()) {
        SetAcKey(fdIter->second.key());         // save the a/c key so we can start rendering its info
        return true;
    }
    
    // not found
    acKey.clear();
    return false;
}

// Get my defined aircraft
/// @details As aircraft can be removed any frame this needs to be called
///          over and over again and can return `nullptr`.
/// @note Deleting aircraft happens in a flight loop callback,
///       which is the same thread as this here is running in.
///       So we can safely assume the returned pointer is valid while
///       rendering the window. But not any longer.
LTFlightData* ACIWnd::GetFlightData () const
{
    // short-cut if there's no key
    if (acKey.empty())
        return nullptr;
    
    // find the flight data by key
    mapLTFlightDataTy::iterator fdIter = mapFd.find(acKey);
    // return flight data if found
    return fdIter != mapFd.end() ? &fdIter->second : nullptr;
}

// switch to another focus a/c?
bool ACIWnd::UpdateFocusAc ()
{
    // just return if not in AUTO mode
    if (!bAuto) return false;
    
    // do that only every so often
    if (!CheckEverySoOften(lastAutoCheck, ACI_AUTO_CHECK_PERIOD))
        return false;
    
    // find the current focus a/c and if different from current one then switch
    const LTFlightData* pFocusAc = LTFlightData::FindFocusAc(DataRefs::GetViewHeading());
    if (pFocusAc && pFocusAc->key() != acKey) {
        SetAcKey(pFocusAc->key());      // set the new focus a/c
        return true;
    }
    
    // nothing found?
    if (!pFocusAc) {
        // Clear the a/c key
        ClearAcKey();
    }
    
    // no new a/c
    return false;
}

// Some setup before UI building starts, here text size calculations
ImGuiWindowFlags_ ACIWnd::beforeBegin()
{
    // If not yet done calculate some common widths
    if (std::isnan(ACI_LABEL_SIZE)) {
        /// Size of longest text plus some room for tree indenttion, rounded up to the next 10
        ImGui::SetWindowFontScale(1.0f);
        ACI_LABEL_SIZE   = std::ceil(ImGui::CalcTextSize("___Heading | Pitch | Roll_").x / 10.0f) * 10.0f;
        ACI_AUTO_CB_SIZE = std::ceil(ImGui::CalcTextSize("_____AUTO").x / 10.0f) * 10.0f;
    }
    
    // Save latest screen size to configuration (if not popped out)
    if (!IsPoppedOut())
        dataRefs.ACIrect = GetCurrentWindowGeometry();
    
    // Set background opacity
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] =  ImColor(0.0f, 0.0f, 0.0f, float(dataRefs.UIopacity)/100.0f);
    
    // For A/C Info Window we don't store any settings in imgui.ini
    // because we can open multiple windows which all get different random ids, which collect over time
    return ImGuiWindowFlags_NoSavedSettings;
}

// Main function to render the window's interface
void ACIWnd::buildInterface()
{
    char buf[50];
    
    // (maybe) update the focus a/c
    UpdateFocusAc();
    
    // Scale the font for this window
    const float fFontScale = float(dataRefs.UIFontScale)/100.0f;
    ImGui::SetWindowFontScale(fFontScale);
    
    // The data we will deal with, can be NULL!
    const double ts = dataRefs.GetSimTime();
    const LTFlightData* pFD = GetFlightData();
    LTAircraft* pAc   = pFD ? pFD->GetAircraft() : nullptr;
    // Try fetching fresh static / dynamic data
    if (pFD) {
        pFD->TryGetSafeCopy(stat);
        pFD->TryGetSafeCopy(dyn);
    }
    const Doc8643* pDoc8643 = pFD ? stat.pDoc8643 : nullptr;
    const LTChannel* pChannel = pFD ? dyn.pChannel : nullptr;
    
    // --- Title Bar ---
    buildTitleBar(GetWndTitle(),
                  // no close button if in VR with camera view on this A/C
                  !(dataRefs.IsVREnabled() && pAc && pAc->IsInCameraView()));
    
    // --- Start the table, which will hold our values
    
    // We need some room under the table for the action buttons
    ImVec2 tblSize = ImGui::GetContentRegionAvail();
    tblSize.x = 0;                      // let table use default width
    tblSize.y -= ImGui::GetTextLineHeightWithSpacing() + 5;
    
    ImGui::PushStyleColor(ImGuiCol_TableRowBg,    IM_COL32(0,0,0,0x00));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32(0,0,0,0x08));
    if (tblSize.y >= ImGui::GetTextLineHeight() &&
        ImGui::BeginTable("ACInfo", 2,
                          ImGuiTableFlags_Scroll |
                          ImGuiTableFlags_ScrollFreezeLeftColumn |
                          ImGuiTableFlags_RowBg,
                          tblSize))
    {
        // Set up the columns of the table
        ImGui::TableSetupColumn("Item",  ImGuiTableColumnFlags_WidthFixed   | ImGuiTableColumnFlags_NoSort, ACI_LABEL_SIZE * fFontScale);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
    
        // MARK: --- Identification ---
        ImGui::TableNextRow();
        snprintf(buf, sizeof(buf), pFD ? "A/C key (%s)" : "A/C key",    // add the key's type to the label
                 pFD ? pFD->key().GetKeyTypeText() : "");
        bool bOpen = ImGui::TreeNodeEx(buf,
                                       CollSecGetSet(ACI_SB_IDENTIFICATION) | ImGuiTreeNodeFlags_SpanFullWidth);
        ImGui::TableNextCell();
        if (ImGui::BeginTable("KeyOrAUTO", 2))
        {
            ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
            ImGui::TableSetupColumn("Auto", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, ACI_AUTO_CB_SIZE * fFontScale);
            ImGui::TableNextRow();
            if (ImGui::InputText("##NewKey", &keyEntry,
                                 ImGuiInputTextFlags_CharsUppercase |
                                 ImGuiInputTextFlags_CharsNoBlank |
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                // Enter pressed in key entry field
                bAuto = false;
                SearchAndSetFlightData();
            }
            ImGui::TableNextCell();
            if (ImGui::Checkbox("AUTO", &bAuto))
                lastAutoCheck = 0.0f;       // enforce search for a/c next frame
            
            ImGui::EndTable();
        }
        
        if (bOpen) {
            CollSecClear(ACI_SB_IDENTIFICATION);
            buildRow("Registration",        stat.reg,           pFD);
            if (pDoc8643 && !pDoc8643->classification.empty())
                buildRow("ICAO Type (Class)", pFD,
                         "%s (%s)",
                         stat.acTypeIcao.c_str(),
                         pDoc8643->classification.c_str());
            else
                buildRow("ICAO Type (Class)", pFD,
                         "%s",
                         stat.acTypeIcao.c_str());
            buildRow("Manufacturer",    stat.man,           pFD);
            buildRow("Model",           stat.mdl,           pFD);
            buildRow("Operator",
                     stat.opIcao.empty() ? stat.op : stat.opIcao + ": " + stat.op,
                     pFD);

            // end of the tree
            ImGui::TreePop();
        }
        
        // MARK: --- Flight Info / Tracking data ---
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ACI_TREE_V_SEP*fFontScale);
        ImGui::TableNextRow();
        if (ImGui::TreeNodeEx("Call Sign | Squawk",
                              CollSecGetSet(ACI_SB_FLIGHT_INFO) | ImGuiTreeNodeFlags_SpanFullWidth))
        {
            // Node is open, add individual lines per value
            CollSecClear(ACI_SB_FLIGHT_INFO);
            ImGui::TableNextCell();
            if (pFD)
                ImGui::Text("%s | %s", stat.call.c_str(), dyn.GetSquawk().c_str());
            
            buildRow("Flight: Route",  stat.flightRoute(), pFD);
                        
            // end of the tree
            ImGui::TreePop();
        } else {
            // Node is closed: Combine call sign, squawk, flight no into one cell
            ImGui::TableNextCell();
            if (pFD) {
                std::string s (stat.call);
                s += " | ";
                s += dyn.GetSquawk();
                if (!stat.flight.empty()) {
                    s += " | ";
                    s += stat.flight;
                }
                ImGui::TextUnformatted(s.c_str());
            }
        }
        
        // MARK: --- Position ---
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ACI_TREE_V_SEP*fFontScale);
        ImGui::TableNextRow();
        bOpen =ImGui::TreeNodeEx("Position",
                                 CollSecGetSet(ACI_SB_POSITION) | ImGuiTreeNodeFlags_SpanFullWidth);
        ImGui::TableNextCell();
        if (pAc)
            ImGui::TextUnformatted(pAc->RelativePositionText().c_str());
        if (bOpen)
        {
            // Node is open
            CollSecClear(ACI_SB_POSITION);
            buildRow("Coordinates", pAc ? std::string(pAc->GetPPos()).c_str() : "", pAc);
            buildRowLabel("Altitude | AGL");
            if (pAc && ImGui::BeginTable("##AltAGL", 2)) {
                ImGui::TableNextRow();
                ImGui::Text("%.f ft", pAc->GetAlt_ft());
                ImGui::TableNextCell();
                if (pAc->IsOnGrnd())
                    ImGui::TextUnformatted("On Grnd");
                else {
                    ImGui::Text("%.f ft", pAc->GetPHeight_ft());
                    if (std::abs(pAc->GetVSI_ft()) > pAc->pMdl->VSI_STABLE) {
                        ImGui::SameLine();
                        ImGui::TextUnformatted(pAc->GetVSI_ft() > 0.0 ? ICON_FA_CHEVRON_UP : ICON_FA_CHEVRON_DOWN);
                    }
                }
                ImGui::EndTable();
            }
            buildRowLabel("Speed | VSI");
            if (pAc && ImGui::BeginTable("##SpeedVSI", 2)) {
                ImGui::TableNextRow();
                ImGui::Text("%.f kn", pAc->GetSpeed_kt());
                ImGui::TableNextCell();
                ImGui::Text("%+.f ft/min", pAc->GetVSI_ft());
                ImGui::EndTable();
            }
            buildRowLabel("Track/Head. | Pitch/Roll");
            if (pAc && ImGui::BeginTable("##TrckPitchRll", 4)) {
                ImGui::TableNextRow();
                ImGui::Text("%03.0f°", pAc->GetTrack());
                ImGui::TableNextCell();
                ImGui::Text("%03.0f°", pAc->GetHeading());
                ImGui::TableNextCell();
                ImGui::Text("%.1f°", pAc->GetPitch());
                ImGui::TableNextCell();
                ImGui::Text("%.1f°", pAc->GetRoll());
                ImGui::EndTable();
            }
            buildRowLabel("Bearing | Dist.");
            if (pAc && ImGui::BeginTable("##Bearing", 2)) {
                ImGui::TableNextRow();
                ImGui::Text("%03.0f°", pAc->GetCameraBearing());
                ImGui::TableNextCell();
                ImGui::Text("%.1fnm", pAc->GetCameraDist() / M_per_NM);
                ImGui::EndTable();
            }
            else
                ImGui::NewLine();
            
            // end of the tree
            ImGui::TreePop();
        }
        
        // MARK: --- Simulation ---
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ACI_TREE_V_SEP*fFontScale);
        ImGui::TableNextRow();
        bOpen =ImGui::TreeNodeEx("CSL Model",
                                 CollSecGetSet(ACI_SB_SIMULATION) | ImGuiTreeNodeFlags_SpanFullWidth);
        ImGui::TableNextCell();
        if (pAc)
            ImGui::TextUnformatted(pAc->GetModelName().c_str());
        if (bOpen)
        {
            CollSecClear(ACI_SB_SIMULATION);
            buildRow("Simulated Time", dataRefs.GetSimTimeString().c_str(), true);

            // last received tracking data
            const double lstDat = pFD ? (pFD->GetYoungestTS() - ts) : -99999.9;
            if (-10000 <= lstDat && lstDat <= 10000) {
                buildRowLabel("Tracking Data");
                if (!stat.slug.empty()) {
                    if (ImGui::SelectableTooltip(ICON_FA_EXTERNAL_LINK_SQUARE_ALT "##FlightURL",
                                                 false,                         // selected?
                                                 true,                          // enabled?
                                                 "Open flight in browser",
                                                 ImGuiSelectableFlags_None,
                                                 ImVec2 (ImGui::GetWidthIconBtn(), 0.0f)))
                        LTOpenURL(stat.slug);
                    ImGui::SameLine();
                }
                ImGui::Text("%+.1fs, %s", lstDat,
                            pChannel ? pChannel->ChName() : "?");
            }
            else
                buildRow("Tracking Data", pChannel ? pChannel->ChName() : "?", pFD);

            buildRow("Flight Phase", pAc ? pAc->GetFlightPhaseRwyString() : "", pAc);
            buildRowLabel("Gear | Flaps");
            if (pAc && ImGui::BeginTable("##GearFlpas", 2)) {
                ImGui::TableNextRow();
                ImGui::Text("%.f%%", pAc->GetGearPos() * 100.0);
                ImGui::TableNextCell();
                ImGui::Text("%.f%%", pAc->GetFlapsPos() * 100.0);
                ImGui::EndTable();
            }
            buildRow("Lights", pAc ? pAc->GetLightsStr() : "", pAc);

            // end of the tree
            ImGui::TreePop();
        }

        
        // MARK: --- Global Window configuration ---
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ACI_TREE_V_SEP*fFontScale);
        ImGui::TableNextRow();
        if (ImGui::TreeNodeEx("Settings", ImGuiTreeNodeFlags_SpanFullWidth))
        {
            ImGui::TableNextCell();
            ImGui::TextUnformatted("Drag controls with mouse:");
            
            buildRowLabel("Font Scaling");
            ImGui::DragInt("##FontScaling", &dataRefs.UIFontScale, 1.0f, 10, 200, "%d%%");
            
            buildRowLabel("Opacity");
            ImGui::DragInt("##Opacity", &dataRefs.UIopacity, 1.0f, 0, 100, "%d%%");

            ImGui::TableNextRow();
            ImGui::TableNextCell();
            if (ImGui::Button(ICON_FA_UNDO " Reset to Defaults")) {
                dataRefs.UIFontScale   = DEF_UI_FONT_SCALE;
                dataRefs.UIopacity      = DEF_UI_OPACITY;
            }

            ImGui::TreePop();
        }

        // --- End of the table
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);
    
    // --- Actions ---
    ImGui::Separator();
    const ImVec2 selSize = ImVec2(ImGui::CalcTextSize("__Auto Visible__").x, 0.0f);
    
    bool bVisible       = pAc ? pAc->IsVisible()        : false;
    bool bAutoVisible   = pAc ? pAc->IsAutoVisible()    : false;

    if (ImGui::Selectable(ICON_FA_CAMERA " Camera",
                          pAc ? pAc->IsInCameraView() : false,
                          pAc ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled,
                          selSize))
        pAc->ToggleCameraView();

    ImGui::SameLine();
    if (ImGui::Selectable(ICON_FA_EYE " Visible", &bVisible,
                          pAc ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled,
                          selSize))
        pAc->SetVisible(bVisible);

    // "Auto Visible" only if some auto-hiding option is on
    if (dataRefs.IsAutoHidingActive()) {
        ImGui::SameLine();
        if (ImGui::Selectable("Auto Visible", &bAutoVisible,
                              pAc ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled,
                              selSize))
            pAc->SetAutoVisible(bAutoVisible);
    }
    
    // Reset font scaling
    ImGui::SetWindowFontScale(1.0f);
}

// Add a label to the list of a/c info
void ACIWnd::buildRowLabel (const std::string& label)
{
    ImGui::TableNextRow();
    ImGui::TextUnformatted(label.c_str());
    ImGui::TableNextCell();
}

// Add a label and a value to the list of a/c info
void ACIWnd::buildRow (const std::string& label,
                       const std::string& val,
                       bool bShowVal)
{
    buildRowLabel(label);
    if (bShowVal)
        ImGui::TextUnformatted(val.c_str());
    else
        ImGui::NewLine();
}

/// Add a label and a value to the list of a/c info
void ACIWnd::buildRow (const std::string& label,
                       bool bShowVal,
                       const char* szFormat, ...)
{
    buildRowLabel(label);
    if (bShowVal) {
        va_list args;
        va_start (args, szFormat);
        ImGui::TextV(szFormat, args);
        va_end (args);
    }
    else
        ImGui::NewLine();
}

// Add a label and a value to the list of a/c info
void ACIWnd::buildRow (const std::string& label,
                       int iVal, bool bShowVal,
                       const char* szFormat)
{
    buildRowLabel(label);
    if (bShowVal)
        ImGui::Text(szFormat, iVal);
    else
        ImGui::NewLine();
}

// Add a label and a value to the list of a/c info
void ACIWnd::buildRow (const std::string& label,
                       double fVal, bool bShowVal,
                       const char* szFormat)
{
    buildRowLabel(label);
    if (bShowVal)
        ImGui::Text(szFormat, fVal);
    else
        ImGui::NewLine();
}



//
// MARK: Static ACIWnd functions
//

bool  ACIWnd::bAreShown     = true;                 // Are the ACI windows currently displayed or hidden?

// we keep a list of all created windows
std::list<ACIWnd*> ACIWnd::listACIWnd;

/// Clear a bit in the configuration of collasped sections
void ACIWnd::CollSecClear (int bit)
{
    dataRefs.ACIcollapsed &= ~(1 << bit);           // clear the bit
}

/// Returns `ImGuiTreeNodeFlags_DefaultOpen` if bit is _not_ set, 0 otherwise
ImGuiTreeNodeFlags_ ACIWnd::CollSecGetSet (int bit)
{
    if (dataRefs.ACIcollapsed & (1 << bit))         // is bit set?
        return ImGuiTreeNodeFlags_None;
    else {
        dataRefs.ACIcollapsed |= (1 << bit);        // set the bit
        return ImGuiTreeNodeFlags_DefaultOpen;
    }
}

// static function: creates a new window
ACIWnd* ACIWnd::OpenNewWnd (const std::string& _acKey, WndMode _mode)
{
    // creation of windows only makes sense if windows are shown
    if (!AreShown())
        ToggleHideShowAll();
    
    // now create the new window
    return new ACIWnd(_acKey, _mode);
}

// move all windows into/out of VR
void ACIWnd::MoveAllVR (bool bIntoVR)
{
    // move into VR
    if (bIntoVR) {
        for (ACIWnd* pWnd: listACIWnd) {
            if (pWnd->GetMode() == WND_MODE_FLOAT)
                pWnd->SetMode(WND_MODE_VR);
        }
    }
    // move out of VR
    else {
        for (ACIWnd* pWnd: listACIWnd) {
            if (pWnd->GetMode() == WND_MODE_VR)
                pWnd->SetMode(WND_MODE_FLOAT);
        }
    }
}

// show/hide all windows
bool ACIWnd::ToggleHideShowAll()
{
    // Toggle
    bAreShown = !bAreShown;
    
    // now apply that new state to all windows
    for (ACIWnd* pWnd: listACIWnd)
        pWnd->SetVisible(bAreShown);
    
    // return new state
    return bAreShown;
}

// return the window (if it exists) for the given key
ACIWnd* ACIWnd::GetWnd (const LTFlightData::FDKeyTy& _key)
{
    for (ACIWnd* pWnd: listACIWnd)
        if (pWnd->GetAcKey() == _key)
            return pWnd;
    return nullptr;
}

// close all windows
void ACIWnd::CloseAll()
{
    // we don't close us when in VR camera view
    if (dataRefs.IsVREnabled() && LTAircraft::IsCameraViewOn())
        return;
    
    // keep closing the first window until map empty
    while (!listACIWnd.empty()) {
        ACIWnd* pWnd = *listACIWnd.begin();
        delete pWnd;                        // destructor removes from list
    }
}
