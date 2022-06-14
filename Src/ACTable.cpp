/// @file       ACTable.cpp
/// @brief      Aircraft listing in form of an ImGui::Table
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
// MARK: Constants
//

constexpr float ACT_AC_UPDATE_PERIOD = 1.0;     ///< update list of aircraft every second

//
// MARK: Column definitions
//

/// All attributes that define a table column
struct ACTColDefTy {
public:
    std::string             colName;        ///< column name
    float                   colWidth;       ///< column with (positive) or weight (negative)
    ImGui::AlignTy          colAlign;       ///< alignment within the column
    ImGuiTableColumnFlags   colFlags;       ///< flags
public:
    /// Constructor just initializes all member attributes
    ACTColDefTy (const std::string& _name,
                 float _width,
                 ImGui::AlignTy _align = ImGui::IM_ALIGN_LEFT,
                 ImGuiTableColumnFlags _flags = ImGuiTableColumnFlags_None) :
    colName(_name), colWidth(_width), colAlign(_align), colFlags(_flags) {}
};

/// Names all the available columns
std::array<ACTColDefTy,ACT_COL_COUNT> gCols {{
    {"Key",              60,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"Key Type",         60,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"ID",               60,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultSort},
    {"Registration",     60},
    
    {"Type",             45},
    {"Class",            35,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"Manufacturer",    100,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"Model",           100},
    {"Category Descr.", 100,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"Operator",        100,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    
    {"Call Sign",        60},
    {"Squawk",           45,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"Flight",           65},
    {"From",             45},
    {"To",               45},

    {"Position",        150,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"Lat",              75,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"Lon",              75,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"Altitude",         50,    ImGui::IM_ALIGN_RIGHT},
    {"AGL",              50,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {ICON_FA_SORT,       15,    ImGui::IM_ALIGN_CENTER, ImGuiTableColumnFlags_NoSort},
    {"VSI",              45,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"kn",               35,    ImGui::IM_ALIGN_RIGHT},
    {"Track",            35,    ImGui::IM_ALIGN_RIGHT},
    {"Heading",          35,    ImGui::IM_ALIGN_RIGHT},
    {"Pitch",            45,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"Roll",             45,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"Bearing",          35,    ImGui::IM_ALIGN_RIGHT},
    {"Distance",         40,    ImGui::IM_ALIGN_RIGHT},
    
    {"CSL Model",       150,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"Last Data",        35,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"Channel",          60,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"Phase",            85},
    {"Gear",             40,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"Flaps",            40,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"Lights",          140,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},
    {"TCAS Idx",         25,    ImGui::IM_ALIGN_RIGHT,  ImGuiTableColumnFlags_DefaultHide},
    {"Flight Model",    150,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_DefaultHide},

    // This stays last
    {"Actions",         100,    ImGui::IM_ALIGN_LEFT,   ImGuiTableColumnFlags_NoSort},
}};

//
// MARK: FDInfo Implementation
//

FDInfo::FDInfo (const LTFlightData& fd)
{
    std::fill(vf.begin(), vf.end(), NAN);
    UpdateFrom(fd);
}

// Reads relevant data from the fd object
bool FDInfo::UpdateFrom (const LTFlightData& fd)
{
    // buffer and macro for converting numeric data
    char s[50];
#define v_f(idx,fmt,val) vf[idx] = float(val); snprintf(s, sizeof(s), fmt, vf[idx]); v[idx] = s;

    bUpToDate = true;
    v[ACT_COL_KEY] = key = fd.key();    // possible without lock
    v[ACT_COL_KEY_TYPE]  = key.GetKeyTypeText();

    // try fetching some static data
    LTFlightData::FDStaticData stat;
    if (fd.TryGetSafeCopy(stat)) {
        v[ACT_COL_ID]           = stat.acId(v[ACT_COL_KEY]);
        v[ACT_COL_REG]          = stat.reg;
        v[ACT_COL_TYPE]         = stat.acTypeIcao;
        v[ACT_COL_CLASS]        = Doc8643::get(stat.acTypeIcao).classification;
        v[ACT_COL_MAN]          = stat.man;
        v[ACT_COL_MDL]          = stat.mdl;
        v[ACT_COL_CAT_DESCR]    = stat.catDescr;
        v[ACT_COL_OP]           = stat.op;
        v[ACT_COL_CALLSIGN]     = stat.call;
        v[ACT_COL_FLIGHT]       = stat.flight;
        v[ACT_COL_FROM]         = stat.originAp;
        v[ACT_COL_TO]           = stat.destAp;
    } else
        bUpToDate = false;
    
    // try fetching some dynamic data
    LTFlightData::FDDynamicData dyn;
    if (fd.TryGetSafeCopy(dyn)) {
        v[ACT_COL_SQUAWK]       = std::to_string(dyn.radar.code);
        if (dyn.pChannel) {
            v[ACT_COL_CHANNEL]  = dyn.pChannel->ChName();
            // last flight data / channel
            v_f(ACT_COL_LASTDATA, "%+.f", fd.GetYoungestTS() - dataRefs.GetSimTime());
        } else {
            v[ACT_COL_CHANNEL].clear();
            v[ACT_COL_LASTDATA].clear();
            vf[ACT_COL_LASTDATA] = NAN;
        }
    } else
        bUpToDate = false;
    
    
    // try fetching some actual flight data
    LTAircraft* pAc = fd.GetAircraft();
    if (pAc) {
        v[ACT_COL_POS]          = pAc->RelativePositionText();
        v_f(ACT_COL_LAT,    "%.4f",     pAc->GetPPos().lat());
        v_f(ACT_COL_LON,    "%.4f",     pAc->GetPPos().lon());
        v_f(ACT_COL_ALT,    "%.f",      pAc->GetAlt_ft());
        v_f(ACT_COL_AGL,    "%.f",      pAc->GetPHeight_ft());
        v_f(ACT_COL_VSI,    "%.f",      pAc->GetVSI_ft());
        if (vf[ACT_COL_VSI] < -pAc->pMdl->VSI_STABLE)     // up/down arrow depending on value of VSI
            v[ACT_COL_UPDOWN] = ICON_FA_CHEVRON_DOWN;
        else if (vf[ACT_COL_VSI] > pAc->pMdl->VSI_STABLE)
            v[ACT_COL_UPDOWN] = ICON_FA_CHEVRON_UP;
        else
            v[ACT_COL_UPDOWN].clear();
        v_f(ACT_COL_SPEED,  "%.f",      pAc->GetSpeed_kt());
        v_f(ACT_COL_TRACK,  "%.f",      pAc->GetTrack());
        v_f(ACT_COL_HEADING,"%.f",      pAc->GetHeading());
        v_f(ACT_COL_PITCH,  "%.1f",     pAc->GetPitch());
        v_f(ACT_COL_ROLL,   "%.1f",     pAc->GetRoll());
        v_f(ACT_COL_BEARING,"%.f",      pAc->GetCameraBearing());
        v_f(ACT_COL_DIST,   "%.1f",     pAc->GetCameraDist() / M_per_NM);
        
        v[ACT_COL_CSLMDL]       = pAc->GetModelName();
        v[ACT_COL_PHASE]        = pAc->GetFlightPhaseRwyString();
        vf[ACT_COL_PHASE]       = float(pAc->GetFlightPhase()); // sort flight phase by its index
        v_f(ACT_COL_GEAR,   "%.f%%",    pAc->GetGearPos() * 100.0);
        v_f(ACT_COL_FLAPS,  "%.f%%",    pAc->GetFlapsPos() * 100.0);
        v[ACT_COL_LIGHTS]       = pAc->GetLightsStr();
        if (pAc->IsCurrentlyShownAsTcasTarget()) {
            v_f(ACT_COL_TCAS_IDX, "%.f", pAc->GetTcasTargetIdx());
        } else {
            vf[ACT_COL_TCAS_IDX] = NAN;
            v[ACT_COL_TCAS_IDX].clear();
        }
        v[ACT_COL_FLIGHTMDL]    = pAc->pMdl->modelName;
            
    }
    else {
        // if there is no a/c, but there previously was, then we need to clear a lot of fields
        if (!v[ACT_COL_POS].empty()) {
            for (size_t idx: {
                ACT_COL_POS, ACT_COL_LAT, ACT_COL_LON, ACT_COL_ALT, ACT_COL_AGL,
                ACT_COL_VSI, ACT_COL_UPDOWN, ACT_COL_SPEED, ACT_COL_TRACK, ACT_COL_HEADING,
                ACT_COL_PITCH, ACT_COL_ROLL, ACT_COL_BEARING, ACT_COL_DIST,
                ACT_COL_CSLMDL, ACT_COL_PHASE, ACT_COL_GEAR, ACT_COL_FLAPS,
                ACT_COL_LIGHTS, ACT_COL_TCAS_IDX, ACT_COL_FLIGHTMDL
            })
            { vf[idx] = NAN; v[idx].clear(); }
        }
        
        // We can try finding some positional information in the pos deque
        const dequePositionTy& posDeque = fd.GetPosDeque();
        if (!posDeque.empty()) {
            v_f(ACT_COL_LAT,    "%.4f",     posDeque.front().lat());
            v_f(ACT_COL_LON,    "%.4f",     posDeque.front().lon());
            v_f(ACT_COL_ALT,    "%.f",      posDeque.front().alt_ft());
        }
    }
    
    // Have we successfully updated?
    return bUpToDate;
}

// Reads all data again from the matching fd object
bool FDInfo::Update ()
{
    const LTFlightData* pFD = GetFD();
    if (pFD)
        return UpdateFrom(*pFD);
    else
        return false;
}


// Return the related LTFlightData object
LTFlightData* FDInfo::GetFD () const
{
    // find the flight data by key
    mapLTFlightDataTy::iterator fdIter = mapFd.find(key);
    // return flight data if found
    return fdIter != mapFd.end() ? &fdIter->second : nullptr;
}

// Does _any_ value match this filter string?
bool FDInfo::matches (const std::string& _s) const
{
    // short cut if obvious
    if (_s.empty()) return true;
    
    // Otherwise search all values
    for (const std::string& val: v) {
        std::string cpy(val);
        str_toupper(cpy);
        if (cpy.find(_s) != std::string::npos)
            return true;
    }
    return false;
}


//
// MARK: ACTable Implementation
//

// Constructor does not currently do much
ACTable::ACTable (bool _bACOnly) :
bFilterAcOnly(_bACOnly)
{}

// Show the table, to be called from `buildInterface()`
LTFlightData* ACTable::build (const std::string& _filter, int _x, int _y)
{
    // return value: the just selected aircraft, if any
    LTFlightData* pSelFD = nullptr;
    
    // Determine size, basis is the available content region
    ImVec2 tblSize = ImGui::GetContentRegionAvail();
    if (_x <= 0) tblSize.x += float(_x);    // Determine width
    else tblSize.x = float(_x);
    if (_y <= 0) tblSize.y += float(_y);    // Determine height
    else tblSize.y = float(_y);
    
    // Start drawing the table
    if (ImGui::BeginTable("ACList", ACT_COL_COUNT,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                          ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingPolicyFixedX | ImGuiTableFlags_Scroll |
                          ImGuiTableFlags_ScrollFreezeTopRow |
                          ImGuiTableFlags_ScrollFreezeLeftColumn,
                          tblSize))
    {
        // Set up the columns
        for (ImGuiID col = 0; col < ACT_COL_COUNT; ++col) {
            const ACTColDefTy& colDef = gCols[col];
            ImGui::TableSetupColumn(colDef.colName.c_str(),
                                    colDef.colFlags | ImGuiTableColumnFlags_NoHeaderWidth,
                                    colDef.colWidth,
                                    col);       // use ACTColumnsTy as ColumnUserID
        }
        ImGui::TableAutoHeaders();

        // Set up a/c list
        bool bUpdated = UpdateFDIs(_filter);

        // Sort the data if and as needed
        const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
        if (bUpdated ||
            (sortSpecs && sortSpecs->SpecsChanged &&
             sortSpecs->Specs && sortSpecs->SpecsCount >= 1 &&
             vecFDI.size() > 1))
        {
            // We sort only by one column, no multi-column sort yet
            const ImGuiTableSortSpecsColumn& colSpec = *(sortSpecs->Specs);
            Sort((ACTColumnsTy)colSpec.ColumnUserID,
                 colSpec.SortDirection == ImGuiSortDirection_Ascending);
        }
        
        // Fill the data into the list
        for (const FDInfo& fdi: vecFDI) {
            ImGui::TableNextRow();
            for (size_t col = 0; col < ACT_COL_ACTIONS; ++col) {
                if (ImGui::TableSetColumnIndex(int(col))) {
                    ImGui::TextAligned(gCols[col].colAlign, fdi.v[col]);
                }
            }

            // Action column
            if (ImGui::TableSetColumnIndex(ACT_COL_ACTIONS)) {
                // Make sure all the buttons have a unique id
                ImGui::PushID(fdi.key.c_str());
                
                // Make selected buttons more visible: Exchange Hovered (lighter) and std color (darker)
                const ImU32 colHeader = ImGui::GetColorU32(ImGuiCol_Header);
                ImGui::PushStyleColor(ImGuiCol_Header,          ImGui::GetColorU32(ImGuiCol_HeaderHovered));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,   colHeader);
                
                // Limit the width of the selectables
                ImVec2 selSize (ImGui::GetWidthIconBtn(), 0.0f);
                
                // (Potential) access to the aircraft, can be `nullptr`!
                LTFlightData* pFD = fdi.GetFD();
                LTAircraft* pAc = pFD ? pFD->GetAircraft() : nullptr;
                
                // Is a/c visible/auto-visible?
                bool bVisible       = pAc ? pAc->IsVisible()        : false;
                bool bAutoVisible   = pAc ? pAc->IsAutoVisible()    : false;

                // Open a/c info window
                ACIWnd* pACIWnd = ACIWnd::GetWnd(fdi.key);
                if (ImGui::SelectableTooltip(ICON_FA_INFO_CIRCLE "##ACIWnd",
                                             pACIWnd != nullptr, true,              // selected?, enabled!
                                             "Open Aircraft Info Window",
                                             ImGuiSelectableFlags_None, selSize))
                {
                    // Toggle a/c info wnd (safe/restore our context as we are now dealing with another ImGui window,
                    // which can mess up context pointers)
                    ImGuiContext* pCtxt = ImGui::GetCurrentContext();
                    if (pACIWnd) {
                        delete pACIWnd;
                        pACIWnd = nullptr;
                    } else {
                        ACIWnd::OpenNewWnd(fdi.key);
                    }
                    ImGui::SetCurrentContext(pCtxt);

                    // Open an ACIWnd, which is another ImGui window, so safe/restore our context
                }

                // Camera view
                ImGui::SameLine();
                if (ImGui::SelectableTooltip(ICON_FA_CAMERA "##CameraView",
                                             pAc ? pAc->IsInCameraView() : false,   // selected?
                                             pAc != nullptr,                        // enabled?
                                             "Toggle camera view",
                                             ImGuiSelectableFlags_None, selSize))
                    pAc->ToggleCameraView();

                // Link to browser for following the flight
                ImGui::SameLine();
                const std::string url (pFD ? pFD->GetUnsafeStat().slug : "");
                if (ImGui::SelectableTooltip(ICON_FA_EXTERNAL_LINK_SQUARE_ALT "##FlightURL",
                                             false,                                 // selected?
                                             !url.empty(),                          // enabled?
                                             "Open flight in browser",
                                             ImGuiSelectableFlags_None, selSize))
                    LTOpenURL(url);
                
                // Visible
                ImGui::SameLine();
                if (ImGui::SelectableTooltip(ICON_FA_EYE "##Visible", &bVisible,
                                             pAc != nullptr,      // enabled/disabled?
                                             "Toggle aircraft's visibility",
                                             ImGuiSelectableFlags_None, selSize))
                    pAc->SetVisible(bVisible);

                // "Auto Visible" only if some auto-hiding option is on
                if (dataRefs.IsAutoHidingActive()) {
                    ImGui::SameLine();
                    if (ImGui::SelectableTooltip(ICON_FA_EYE "##AutoVisible", &bAutoVisible,
                                                 pAc != nullptr,  // enabled/disabled
                                                 "Toggle aircraft's auto visibility",
                                                 ImGuiSelectableFlags_None, selSize))
                        pAc->SetAutoVisible(bAutoVisible);
                }
                
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            } // action column
        }   // for all a/c rows
        
        // End of aircraft list
        ImGui::EndTable();
    }
    
    // return value: the just selected aircraft, if any
    return pSelFD;
}

/// Update the list of FDIs (periodically or if filter changed)
bool ACTable::UpdateFDIs (const std::string& _filter)
{
    // short-cut in case of no change
    if (!CheckEverySoOften(lastUpdate, ACT_AC_UPDATE_PERIOD) &&
        _filter == filterInUse &&
        bFilterAcOnly == bAcOnlyInUse)
        return false;
    filterInUse = _filter;
    bAcOnlyInUse = bFilterAcOnly;
    
    // Walk all aircraft, test for match, then add
    vecFDI.clear();
    if (filterInUse.empty())        // reserve storage (if no filter defined)
        vecFDI.reserve(mapFd.size());
    
    // First pass: Add all matching and remember those we couldn't get
    std::vector<FDInfo> vecAgain;
    for (const mapLTFlightDataTy::value_type& p: mapFd) {
        // First filter: Visible a/c only?
        if (bFilterAcOnly && !p.second.hasAc())
            continue;
        // others: test if filter matches
        FDInfo fdi(p.second);
        if (fdi.upToDate()) {
            if (fdi.matches(filterInUse))
                vecFDI.emplace_back(std::move(fdi));
        } else {
            vecAgain.emplace_back(std::move(fdi));
        }
    }
    
    // Second pass: Try those again, which couldn't yet fully update
    for (FDInfo& fdi: vecAgain) {
        fdi.Update();
        if (fdi.matches(filterInUse))
            vecFDI.emplace_back(std::move(fdi));
    }
    
    // Did do an update
    return true;
}

// Sort the aircraft list by given column
void ACTable::Sort (ACTColumnsTy _col, bool _asc)
{
    // All right-aligned data is treated as numeric, using the numeric `vf` array,
    // so is the "flight phase", which gets sorted by its numeric value, too:
    if (gCols.at(_col).colAlign == ImGui::IM_ALIGN_RIGHT ||
        _col == ACT_COL_PHASE) {
        std::sort(vecFDI.begin(), vecFDI.end(),
                  [_col,_asc](const FDInfo& a, const FDInfo& b)
        {
            const float af = a.vf[_col]; const bool a_nan = std::isnan(af);
            const float bf = b.vf[_col]; const bool b_nan = std::isnan(bf);
            if (!a_nan && !b_nan)       // standard case: both value defined
                return af < bf;
            if (a_nan && b_nan)         // both NAN -> considered equal, for a stable sort let the key decide
                return a.key < b.key;
            return a_nan != _asc;       // one of them is NAN -> make NAN appear always at the end
        });
    } else {
        // otherwise we sort by text
        std::sort(vecFDI.begin(), vecFDI.end(),
                  [_col](const FDInfo& a, const FDInfo& b)
        {
            const int cmp = a.v[_col].compare(b.v[_col]);
            return
            cmp == 0 ? a.key < b.key :  // in case of equality let the key decide for a stable sorting order
            cmp < 0;
        });
    }
    
    // Sort descending?
    if (!_asc)
        std::reverse(vecFDI.begin(), vecFDI.end());
}
