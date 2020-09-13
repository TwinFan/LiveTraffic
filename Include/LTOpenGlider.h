/// @file       LTOpenGlider.h
/// @brief      Open Glider Network: Requests and processes live tracking data
/// @see        http://wiki.glidernet.org/
/// @see        https://github.com/glidernet/ogn-live#backend
/// @see        http://live.glidernet.org/
/// @details    Defines OpenGliderConnection:\n
///             - Provides a proper REST-conform URL\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
/// @author     Birger Hoppe
/// @copyright  (c) 2020 Birger Hoppe
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

#ifndef LTOpenGlider_h
#define LTOpenGlider_h

#include "LTChannel.h"

//MARK: OpenGlider Constants
#define OPGLIDER_CHECK_NAME     "Live Glidernet"
#define OPGLIDER_CHECK_URL      "http://live.glidernet.org/"
#define OPGLIDER_CHECK_POPUP    "Check Open Glider Network's coverage"

#define OPGLIDER_NAME           "Open Glider Network"
#define OPGLIDER_URL            "http://live.glidernet.org/lxml.php?a=0&b=%.3f&c=%.3f&d=%.3f&e=%.3f"

//    a="lat      ,lon     ,CN ,reg   ,alt_m,ts      ,age_s,trk,speed_km_h,vert_m_per_s,a/c type,receiver,device id,OGN registration id"
// <m a="49.815819,7.957970,ADA,D-HYAF,188  ,21:20:27,318  ,343,11        ,-2.0        ,3       ,Waldalg3,3E1205   ,24064512"/>

/// Field indexes in live.glidernet.org's response
enum GliderNetFieldsTy {
    GNF_LAT         = 0,        ///< latitude
    GNF_LON,                    ///< longitude
    GNF_CN,                     ///< CN ("Wettbewerbskennung"), either registered, or some short form of the OGN registration id
    GNF_REG,                    ///< either official registration, or the (daily changing) OGN registration id
    GNF_ALT_M,                  ///< altitude in meter
    GNF_TS,                     ///< timestamp (zulu)
    GNF_AGE_S,                  ///< seconds since last received message (beacon)
    GNF_TRK,                    ///< track in degrees
    GNF_SPEED_KM_H,             ///< ground speed in km/h
    GNF_VERT_M_S,               ///< vertical speed in m/s
    GNF_FLARM_ACFT_TYPE,        ///< Flarm aircraft type (see ::FlarmAircraftTy)
    GNF_RECEIVER_ID,            ///< receiver id (of the station providing this received data)
    GNF_FLARM_DEVICE_ID,        ///< unique FLARM device id of the sender, optional, can be 0
    GNF_OGN_REG_ID,             ///< OGN registration id (expect to renew every day, so considered temporary)
    GNF_COUNT                   ///< always last, counts the number of fields
};

/// @brief OGN Aircraft type
/// @see https://github.com/wbuczak/ogn-commons-java/blob/master/src/main/java/org/ogn/commons/beacon/AircraftType.java
/// @see http://forums.skydemon.aero/Topic16427.aspx
enum FlarmAircraftTy {
    FAT_UNKNOWN     = 0,        ///< unknown
    FAT_GLIDER      = 1,        ///< Glider / Sailplane / Motor-Glider
    FAT_TOW_PLANE   = 2,        ///< Tow / Tug Plane (usually a L1P type of plane)
    FAT_HELI_ROTOR  = 3,        ///< Helicopter, Rotorcraft
    FAT_PARACHUTE   = 4,        ///< Parachute
    FAT_DROP_PLANE  = 5,        ///< Drop Plane for parachutes (not rarely a L2T type of plane)
    FAT_HANG_GLIDER = 6,        ///< Hangglider
    FAT_PARA_GLIDER = 7,        ///< Paraglider
    FAT_POWERED_AC  = 8,        ///< Powered Aircraft
    FAT_JET_AC      = 9,        ///< Jet Aircraft
    FAT_UFO         = 10,       ///< Flying Saucer, UFO (well, yea...specification says so...not sure how the aliens can get hold of a FLARM sender before reaching earth, though...and _if_ they are interested in being tracked at all)
    FAT_BALLOON     = 11,       ///< Balloon
    FAT_AIRSHIP     = 12,       ///< Airship
    FAT_UAV         = 13,       ///< unmanned aerial vehicle
    FAT_STATIC_OBJ  = 15,       ///< static object (ignored)
};

//
// MARK: OpenSky
//
class OpenGliderConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    /// Constructor
    OpenGliderConnection ();
    /// Returns URL to fetch current data from live.glidernet.org
    virtual std::string GetURL (const positionTy& pos);
    /// @brief Processes the fetched data
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual const char* ChName() const { return OPGLIDER_NAME; }
    virtual bool FetchAllData(const positionTy& pos) { return LTOnlineChannel::FetchAllData(pos); }
};

//
// MARK: Global Functions
//

/// Return a descriptive text per flam a/c type
const char* OGNGetAcTypeName (FlarmAircraftTy _acTy);

/// @brief Return a matching ICAO type code per flarm a/c type
/// @details Pick one of the types defined by the user
const std::string& OGNGetIcaoAcType (FlarmAircraftTy _acTy);

/// Fill defaults for Flarm aircraft types where not existing
void OGNFillDefaultFlarmAcTypes ();

#endif /* LTOpenGlider_h */
