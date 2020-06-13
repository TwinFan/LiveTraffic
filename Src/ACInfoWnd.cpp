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

/// Initial size of an A/c Info Window
const WndRect ACI_INIT_SIZE = WndRect(0, 0, 270, 365);
/// Resizing limits (minimum and maximum sizes)
const WndRect ACI_RESIZE_LIMITS = WndRect(200, 200, 400, 600);
/// How often to check for AUTO a/c change? [s]
constexpr float ACI_AUTO_CHECK_PERIOD = 1.0f;

/// Width of first column, which displays static labels
constexpr float ACI_LABEL_SIZE = 100.0f;
/// Width of AUTO checkbox
constexpr float ACI_AUTO_CB_SIZE = 60.0f;

// Constructor shows a window for the given a/c key
ACIWnd::ACIWnd(const std::string& _acKey, WndMode _mode) :
LTImgWindow(_mode, WND_STYLE_HUD, ACI_INIT_SIZE),
bAuto(_acKey.empty()),              // if _acKey empty -> AUTO mode
keyEntry(_acKey)                    // the passed-in input is taken as the user's entry
{
    // Set up window basics
    SetWindowTitle(GetWndTitle());
    SetWindowResizingLimits(ACI_RESIZE_LIMITS.tl.x, ACI_RESIZE_LIMITS.tl.y,
                            ACI_RESIZE_LIMITS.br.x, ACI_RESIZE_LIMITS.br.y);
    SetVisible(true);
    
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
    keyEntry = (acKey = _key);      // remember the key
    SetWindowTitle(GetWndTitle());  // set the window's title
    ReturnKeyboardFocus();          // give up keyboard focus in case we had it
}

// Clear the a/c key, ie. display no data
void ACIWnd::ClearAcKey ()
{
    acKey.clear();
    keyEntry.clear();
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
    (acKey.empty() ? std::string(ACI_WND_TITLE) : std::string(acKey)) +
    (bAuto ? " (AUTO)" : "");
}

// Taking user's temporary input `keyEntry` searches for a valid a/c, sets acKey on success
bool ACIWnd::SearchAndSetFlightData ()
{
    mapLTFlightDataTy::const_iterator fdIter = mapFd.cend();
    
    trim(keyEntry);
    if (!keyEntry.empty()) {
        // is it a small integer number, i.e. used as index?
        if (keyEntry.length() <= 3 &&
            keyEntry.find_first_not_of("0123456789") == std::string::npos)
        {
            int i = std::stoi(keyEntry);
            // let's find the i-th aircraft by looping over all flight data
            // and count those objects, which have an a/c
            if (i > 0) for (fdIter = mapFd.cbegin();
                 fdIter != mapFd.cend();
                 ++fdIter)
            {
                if (fdIter->second.hasAc())         // has an a/c
                    if ( --i == 0 )                 // and it's the i-th!
                        break;
            }
        }
        else
        {
            // search the map of flight data by text key
            fdIter =
            std::find_if(mapFd.cbegin(), mapFd.cend(),
                         [&](const mapLTFlightDataTy::value_type& mfd)
                         { return mfd.second.IsMatch(keyEntry); }
                         );
        }
    }
    
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
    if (dataRefs.GetMiscNetwTime() < lastAutoCheck + ACI_AUTO_CHECK_PERIOD)
        return false;
    lastAutoCheck = dataRefs.GetMiscNetwTime();
    
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

// Main function to render the window's interface
void ACIWnd::buildInterface()
{
    // (maybe) update the focus a/c
    UpdateFocusAc();
    
    // --- Title Bar ---
    buildTitleBar(GetWndTitle());
    
    // --- Start the table, which will hold our values
    if (ImGui::BeginTable("ACInfo", 2,
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_ScrollFreezeLeftColumn))
    {
        // The data we will deal with, can be NULL!
        const LTFlightData* pFD = GetFlightData();
        const LTAircraft* pAc = pFD ? pFD->GetAircraft() : nullptr;
        
        // Try fetching fresh static / dynamic data
        if (pFD) {
            pFD->TryGetSafeCopy(stat);
            pFD->TryGetSafeCopy(dyn);
        }
        
        // Set up the columns of the table
        ImGui::TableSetupColumn("Item",  ImGuiTableColumnFlags_WidthFixed   | ImGuiTableColumnFlags_NoSort, ACI_LABEL_SIZE);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
    
        // --- Identification ---
        ImGui::TableNextRow();
        const bool bIdOpen = ImGui::TreeNodeEx("A/C key", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
        ImGui::TableNextCell();
        if (ImGui::BeginTable("KeyOrAUTO", 2))
        {
            ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
            ImGui::TableSetupColumn("Auto", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, ACI_AUTO_CB_SIZE);
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
        
        if (bIdOpen) {
            // registration / tail number
            ImGui::TableNextRow();
            ImGui::TextUnformatted("Registration");
            ImGui::TableNextCell();
            if (pFD) ImGui::TextUnformatted(stat.reg.c_str());
            
            // end of the tree
            ImGui::TreePop();
        }
        
        // --- End of the table
        ImGui::EndTable();
    }
}


//
// MARK: Static ACIWnd functions
//

// Are the ACI windows displayed or hidden?
bool ACIWnd::bAreShown = true;

// we keep a list of all created windows
std::list<ACIWnd*> ACIWnd::listACIWnd;

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
