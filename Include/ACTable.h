/// @file       ACTable.h
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

#ifndef ACTable_h
#define ACTable_h

/// Indexes for the columns we offer
enum ACTColumnsTy {
    ACT_COL_KEY = 0,        ///< unique key, typically hex transponder code or FLARM id
    ACT_COL_KEY_TYPE,       ///< Transponder Code, Flarm Id etc...
    ACT_COL_ID,             ///< some id, same as "any id" in label config
    ACT_COL_REG,            ///< registration / tail number
    
    ACT_COL_TYPE,           ///< ICAO aircraft type (like "A388")
    ACT_COL_CLASS,          ///< ICAO aircraft class (like "L4J")
    ACT_COL_MAN,            ///< manufacturer (human-readable)
    ACT_COL_MDL,            ///< model (human-readable)
    ACT_COL_CAT_DESCR,      ///< category description
    ACT_COL_OP,             ///< operator
    
    ACT_COL_CALLSIGN,       ///< call sign
    ACT_COL_SQUAWK,         ///< Squawk code
    ACT_COL_FLIGHT,         ///< Flight number
    ACT_COL_FROM,           ///< departure airport
    ACT_COL_TO,             ///< destination airport
    
    ACT_COL_POS,            ///< position relative to nearest airport
    ACT_COL_LAT,            ///< latitude
    ACT_COL_LON,            ///< longitude
    ACT_COL_ALT,            ///< altitude
    ACT_COL_AGL,            ///< height above ground level
    ACT_COL_UPDOWN,         ///< up/down arrows
    ACT_COL_VSI,            ///< vertical speed
    ACT_COL_SPEED,          ///< speed in knots
    ACT_COL_TRACK,          ///< track
    ACT_COL_HEADING,        ///< heading
    ACT_COL_PITCH,          ///< pitch
    ACT_COL_ROLL,           ///< roll
    ACT_COL_BEARING,        ///< bearing relative to camera
    ACT_COL_DIST,           ///< distance to camera
    
    ACT_COL_CSLMDL,         ///< CSL model path/name
    ACT_COL_LASTDATA,       ///< last tracking data received when
    ACT_COL_CHANNEL,        ///< channel feeding this aircraft
    ACT_COL_PHASE,          ///< flight phase
    ACT_COL_GEAR,           ///< gear deployment ratio
    ACT_COL_FLAPS,          ///< flap deployment ratio
    ACT_COL_LIGHTS,         ///< which lights are on?
    ACT_COL_TCAS_IDX,       ///< TCAS Idx (1-63) of plane - if any
    ACT_COL_FLIGHTMDL,      ///< Flight model name
    
    // these must stay last
    ACT_COL_ACTIONS,        ///< actions like a/c info wnd, camera, visibility
    ACT_COL_COUNT           ///< number of columns (always last)
};

/// @brief Selected cached infos from the LTFlightData/LTAircraft classes
/// @details The LTFlightData object can be removed at any time, so we cannot
///          keep pointers to them. And we don't want to build up all lists
///          every frame for performance. So we chache what we need
class FDInfo {
public:
    /// Key into the flight data map
    LTFlightData::FDKeyTy key;
    /// The values (converted to string) to show for each column
    std::array<std::string,ACT_COL_COUNT> v;
    /// The original numeric value (if any) for faster comparison
    std::array<float,ACT_COL_COUNT> vf;
protected:
    /// Did the last update succeed?
    bool bUpToDate = false;
public:
    /// Constructor copies relevant data from the fd object
    FDInfo (const LTFlightData& fd);
    
    /// @brief Reads relevant data from the fd object
    /// @return Successful? Same as upToDate(). Can fail if we didn't get any locks immediately)
    bool UpdateFrom (const LTFlightData& fd);
    /// @brief Reads all data again from the matching fd object
    /// @return Successful? Same as upToDate(). Can fail if we didn't get any locks immediately)
    bool Update ();

    /// Did the last update succeed?
    bool upToDate () const { return bUpToDate; }
    
    /// @brief Return the related LTFlightData object
    /// @warning Can return `nullptr`!
    LTFlightData* GetFD () const;
    
    /// @brief Does any value match this filter string?
    /// @param _s Substring to be searched for, expected in upper case
    bool matches (const std::string& _s) const;
};

//
// Aircraft Table
//
class ACTable
{
public:
    /// Filter: actual aircraft only (otherwise also pure flight data entries)
    bool        bFilterAcOnly = false;
    
protected:
    /// The data to be listed: info on aircraft
    std::vector<FDInfo> vecFDI;
    /// Time when list was updated last
    float lastUpdate = 0.0f;
    /// The filter applied to the above list
    std::string filterInUse;
    /// The filter applied
    bool        bAcOnlyInUse = false;
    
public:
    /// Constructor
    ACTable(bool _bACOnly = false);
    
    /// Show the table, to be called from `buildInterface()`
    /// @param _filter Filter a/c by this substring
    /// @param _x Width: <=0 available content region (reduced by x), >0 absolute width
    /// @param _y Height: <=0 available content region (reduced by y), >0 absolute height
    /// @return Pointer to selected (clicked) flight data object, or `nullptr`
    LTFlightData* build (const std::string& _filter, int _x = 0, int _y = 0);
    
    /// @brief Update the list of FDIs (periodically or if filter changed)
    /// @return Did an update take place?
    bool UpdateFDIs (const std::string& _filter);
    
    /// @brief Sort the aircraft list by given column
    void Sort (ACTColumnsTy _col, bool _asc);
};

#endif /* ACTable_h */
