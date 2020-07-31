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
                 ImGuiTableColumnFlags _flags = ImGuiTableColumnFlags_NoHeaderWidth) :
    colName(_name), colWidth(_width), colAlign(_align), colFlags(_flags) {}
};

/// Names all the available columns
std::array<ACTColDefTy,ACT_COL_COUNT> gCols {{
    {"Key",              60},
    {"ID",               60,    ImGui::IM_ALIGN_LEFT,  ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoHeaderWidth},
    {"Registration",     60},
    
    {"Manufacturer",    100},
    {"Model",           100},
    
    {"Call Sign",        60},
    {"Squawk",           50},
    
    {"Position",        200},
    {"Speed",            60,    ImGui::IM_ALIGN_RIGHT},
}};

//
// MARK: FDInfo Implementation
//

FDInfo::FDInfo (const LTFlightData& fd)
{
    UpdateFrom(fd);
}

// Reads relevant data from the fd object
bool FDInfo::UpdateFrom (const LTFlightData& fd)
{
    bUpToDate = true;
    v[ACT_COL_KEY] = key = fd.key();    // possible without lock
    
    // try fetching some static data
    LTFlightData::FDStaticData stat;
    if (fd.TryGetSafeCopy(stat)) {
        v[ACT_COL_ID]           = stat.acId(v[ACT_COL_KEY]);
        v[ACT_COL_REG]          = stat.reg;
        v[ACT_COL_MAN]          = stat.man;
        v[ACT_COL_MDL]          = stat.mdl;
        v[ACT_COL_CALLSIGN]     = stat.call;
    } else
        bUpToDate = false;

    // try fetching some dynamic data
    LTFlightData::FDDynamicData dyn;
    if (fd.TryGetSafeCopy(dyn)) {
        v[ACT_COL_SQUAWK]       = std::to_string(dyn.radar.code);
    } else
        bUpToDate = false;
    
    // try fetching some actual flight data
    LTAircraft* pAc = fd.GetAircraft();
    if (pAc) {
        char s[50];
        v[ACT_COL_POS]          = pAc->RelativePositionText();
        snprintf(s, sizeof(s), "%.f", pAc->GetSpeed_kt());
        v[ACT_COL_SPEED]        = s;
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
                                    colDef.colFlags,
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
            for (int col = 0; col < ACT_COL_COUNT; ++col) {
                if (ImGui::TableSetColumnIndex(col)) {
                    ImGui::TextAligned(gCols[col].colAlign, fdi.v[col]);
                }
            }
        }

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
    if (_filter == filterInUse &&
        dataRefs.GetMiscNetwTime() < lastUpdate + ACT_AC_UPDATE_PERIOD)
        return false;
    filterInUse = _filter;
    lastUpdate = dataRefs.GetMiscNetwTime();
    
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
    // All right-aligned data is treated as numeric
    if (gCols.at(_col).colAlign == ImGui::IM_ALIGN_RIGHT) {
        std::sort(vecFDI.begin(), vecFDI.end(),
                  [_col](const FDInfo& a, const FDInfo& b)
        {
            return
            (a.v[_col].empty() ? NAN : std::stof(a.v[_col])) <
            (b.v[_col].empty() ? NAN : std::stof(b.v[_col]));
        });
    } else {
        std::sort(vecFDI.begin(), vecFDI.end(),
                  [_col](const FDInfo& a, const FDInfo& b)
        {
            return a.v[_col] < b.v[_col];
        });
    }
    
    // Sort descending?
    if (!_asc)
        std::reverse(vecFDI.begin(), vecFDI.end());
}
