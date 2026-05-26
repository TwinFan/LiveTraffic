/// @file       LTRealTraffic.h
/// @brief      RealTraffic: Receives and processes live tracking data
/// @see        https://rtweb.flyrealtraffic.com/
/// @details    Defines RealTrafficConnection in two different variants:\n
///             - Direct Connection:
///               - Expects RealTraffic license information
///               - Sends authentication, weather, and tracking data requests to RealTraffic servcers
///             - via RealTraffic app
///               - Sends current position to RealTraffic app\n
///               - Receives tracking data via UDP\n
///               - Interprets the response and passes the tracking data on to LTFlightData.\n
/// @author     Birger Hoppe
/// @copyright  (c) 2019-2024 Birger Hoppe
/// @copyright  Permission is hereby granted, free of charge, to any person obtaining a
///             copy of this software and associated documentation files (the "Software"),
///             to deal in the Software without restriction, including without limitation
///             the rights to use, copy, modify, merge, publish, distribute, sublicense,
///             and/or sell copies of the Software, and to permit persons to whom the
///             Software is furnished to do so, subject to the follow ing conditions:\n
///             The above copyright notice and this permission notice shall be included in
///             all copies or substantial portions of the Software.\n
///             THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
///             IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
///             FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
///             AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
///             LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
///             OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
///             THE SOFTWARE.

#ifndef LTRealTraffic_h
#define LTRealTraffic_h

#include "LTChannel.h"

//
// MARK: RealTraffic Constants
//

#define RT_CHECK_NAME           "RealTraffic Web Site"
#define RT_CHECK_URL            "https://www.flyrealtraffic.com/"
#define RT_CHECK_POPUP          "Open RealTraffic's web site"
#define RT_SLUG                 "https://www.flyrealtraffic.com/livemap/?hex=%lx"

#define REALTRAFFIC_NAME        "RealTraffic"

#define RT_BASE_URL             "https://rtwa.flyrealtraffic.com/v6"
#define RT_METAR_UNKN           "UNKN"

#define RT_AUTH_URL             RT_BASE_URL "/auth"
#define RT_AUTH_LIC_POST        "license=%s&software=%s"
#define RT_AUTH_TOKEN_POST      "token=%s&software=%s"
#define RT_DEAUTH_URL           RT_BASE_URL "/deauth"
#define RT_DEAUTH_POST          "GUID=%s"
#define RT_NEAREST_METAR_URL    RT_BASE_URL "/nearestmetar"
#define RT_NEAREST_METAR_POST   "GUID=%s&lat=%.2f&lon=%.2f&toffset=%ld&maxcount=7"
#define RT_WEATHER_URL          RT_BASE_URL "/weather"
#define RT_WEATHER_POST         "GUID=%s&lat=%.2f&lon=%.2f&alt=%ld&airports=%s&querytype=locwx&toffset=%ld"
#define RT_TRAFFIC_URL          RT_BASE_URL "/traffic"
#define RT_TRAFFIC_POST         "GUID=%s&top=%.2f&bottom=%.2f&left=%.2f&right=%.2f&querytype=locationtraffic&toffset=%ld"
#define RT_TRAFFIC_POST_BUFFER  "GUID=%s&top=%.2f&bottom=%.2f&left=%.2f&right=%.2f&querytype=locationtraffic&toffset=%ld&buffercount=%d&buffertime=%d"
#define RT_TRAFFIC_POST_PARKED  "GUID=%s&top=%.2f&bottom=%.2f&left=%.2f&right=%.2f&querytype=parkedtraffic&toffset=%ld"

#define RT_LOCALHOST            "0.0.0.0"
constexpr size_t RT_NET_BUF_SIZE    = 8192;

// constexpr double RT_SMOOTH_AIRBORNE = 65.0; // smooth 65s of airborne data
// constexpr double RT_SMOOTH_GROUND   = 35.0; // smooth 35s of ground data
constexpr double RT_VSI_AIRBORNE    = 80.0; ///< if VSI is more than this then we assume "airborne"

/// [s] interval at which the parked-traffic request is re-issued so RT's
/// parked snapshot stays fresh while the user sits at an airport. Without
/// this the parked picture is fetched exactly once (on airport-data load)
/// and then frozen for the whole visit: new arrivals never appear and
/// aircraft that have since departed are never reconciled. 300 s = 5 min.
constexpr time_t RT_PARKED_REFRESH_INTVL_S = 300;

#define MSG_RT_STATUS           "RealTraffic network status changed to: %s"
#define MSG_RT_LAST_RCVD        " | last msg %.0fs ago"
#define MSG_RT_ADJUST           " | historic traffic from %s"

#define INFO_RT_REAL_TIME       "RealTraffic: Tracking data is real-time again."
#define INFO_RT_ADJUST_TS       "RealTraffic: Receive and display past tracking data from %s"
#define ERR_RT_CANTLISTEN       "RealTraffic: Cannot listen to network, can't tell RealTraffic our position"
#define ERR_RT_DISCARDED_MSG    "RealTraffic: Discarded invalid message: %s"
#define ERR_SOCK_NOTCONNECTED   "%s: Cannot send position: not connected"
#define ERR_SOCK_INV_POS        "%s: Cannot send position: position not fully valid"

// Traffic data format and fields
#define RT_TRAFFIC_RTTFC        "RTTFC"
#define RT_TRAFFIC_AITFC        "AITFC"
#define RT_TRAFFIC_XTRAFFICPSX  "XTRAFFICPSX"
#define RT_TRAFFIC_XATTPSX      "XATTPSX"
#define RT_TRAFFIC_XGPSPSX      "XGPSPSX"

// Constant for direct connection.
//
// Floor on the wait between traffic requests in milliseconds. The
// per-response `rrl` value supplied by the RealTraffic server is the
// authoritative rate limit (see `ProcessFetchedData`) and is honoured
// directly when it is at or above this floor. The floor exists only
// to defend against the server returning rrl=0 (or no rrl at all),
// in which case we fall back to this conservative interval.
//
// RT supports 2 s polling in regular operation; the floor matches that
// minimum so a 2 s `rrl` from the server is taken at face value rather
// than overridden to a larger value. The previous 8 s floor predates
// RT's documented 2 s capability and was capping the per-aircraft
// position granularity at ~10-20 s, which the ground renderer can
// observably struggle with (sideways-during-turn, backward routing
// through SnapToTaxiways, jumpy liftoff). Tighter polling makes the
// per-aircraft sample gap closer to the EHS heading update interval,
// so the filtering layers added in earlier commits engage less often
// and the visual quality improves overall.
constexpr long RT_DRCT_DEFAULT_WAIT = 2000L;                                ///< [ms] Floor between traffic requests (RT's `rrl` controls the actual cadence)
constexpr std::chrono::seconds RT_DRCT_ERR_WAIT = std::chrono::seconds(5);  ///< standard wait between errors
constexpr std::chrono::seconds RT_DRCT_ERR_RATE = std::chrono::seconds(10); ///< wait in case of rate violations, too many sessions
constexpr std::chrono::minutes RT_DRCT_WX_WAIT = std::chrono::minutes(1);   ///< How often to update weather?
constexpr int RT_DRCT_MAX_WX_ERR = 5;                                       ///< Max number of consecutive errors during initial weather requests we wait for...before not asking for weather any longer
constexpr int RT_CNT_SEND_TIMING = 240;                                     ///< RT App: After how many position messages also to send a timing message? (with 250ms period, 240 means: every minute)
constexpr int RT_BUFFER_PERIOD = 10;                                        ///< [s] When requesting buffered traffic, how much time between two buffers?

/// Fields in a response of a direct connection's request
enum RT_DIRECT_FIELDS_TY {
    RT_DRCT_HexId = 0,              ///< hexid (7c68a1)
    RT_DRCT_Lat,                    ///< latitude (-16.754288)
    RT_DRCT_Lon,                    ///< longitude (145.693311)
    RT_DRCT_Track,                  ///< track in degrees (156.07)
    RT_DRCT_BaroAlt,                ///< barometric altitude in ft (std pressure) (2325)
    RT_DRCT_GndSpeed,               ///< Ground speed in kts (165.2)
    RT_DRCT_Squawk,                 ///< Squawk / transponder code (6042)
    RT_DRCT_DataSrc,                ///< Data source: "X" (the provider code where the data came from)
    RT_DRCT_AcType,                 ///< Type (E190)
    RT_DRCT_Reg,                    ///< Registration (VH-UYB)
    RT_DRCT_TimeStamp,              ///< Epoch timestamp of last position update (1658644401.01)
    RT_DRCT_Origin,                 ///< IATA origin (BNE)
    RT_DRCT_Dest,                   ///< IATA destination (CNS)
    RT_DRCT_CallSign,               ///< ATC Callsign (QFA1926)
    RT_DRCT_Gnd,                    ///< On ground (0)
    RT_DRCT_BaroVertRate,           ///< Barometric vertical rate in fpm (-928)
    RT_DRCT_FlightNum,              ///< Flight number
    RT_DRCT_MsgSrcType,             ///< Message source type (X_adsb_icao)
    RT_DRCT_GeoAlt,                 ///< Geometric altitude in ft (=GPS altitude) (2625)
    RT_DRCT_IAS,                    ///< Indicated air speed / IAS in kts (173)
    RT_DRCT_TAS,                    ///< True air speed / TAS in kts (182)
    RT_DRCT_Mach,                   ///< Mach number (0.272)
    RT_DRCT_TurnRate,               ///< Track rate of turn (-0.09) negative = left
    RT_DRCT_Roll,                   ///< Roll / Bank (-1.41) – negative = left
    RT_DRCT_HeadMag,                ///< Magnetic heading (146.6)
    RT_DRCT_HeadTrue,               ///< True heading (153.18)
    RT_DRCT_GeoVertRate,            ///< Geometric vertical rate in fpm (-928)
    RT_DRCT_Emergency,              ///< Emergency (none)
    RT_DRCT_Category,               ///< Category (A3)
    RT_DRCT_SetQNH,                 ///< QNH set by crew in hPa (1014.4)
    RT_DRCT_MCPSelAlt,              ///< MCP selected altitude in ft (3712)
    RT_DRCT_AutoTgtAlt,             ///< Autopilot target altitude in ft (2896)
    RT_DRCT_SelHead,                ///< Selected heading (empty)
    RT_DRCT_SelAutoMode,            ///< Selected autopilot modes (AP on, approach mode, TCAS active)
    RT_DRCT_NavIntCat,              ///< Navigation integrity category (8)
    RT_DRCT_CntmntRad,              ///< Radius of containment in meters (186)
    RT_DRCT_NavIntCatBaro,          ///< Navigation integrity category for barometric altimeter (1)
    RT_DRCT_NavAccuracyPos,         ///< Navigation accuracy for Position (9)
    RT_DRCT_NavAccuracyVel,         ///< Navigation accuracy for velocity (2)
    RT_DRCT_PosAge,                 ///< Age of position in seconds (0.1)
    RT_DRCT_SigStrengt,             ///< Signal strength reported by receiver (-20.2 dbFS, -49.5 indicates a source that doesn’t provide signal strength, e.g. ADS-C positions)
    RT_DRCT_Alert,                  ///< Flight status alert bit (0)
    RT_DRCT_SpecialPos,             ///< Flight status special position identification bit (0)
    RT_DRCT_WindDir,                ///< Wind direction (123)
    RT_DRCT_WindSpeed,              ///< Wind speed (19)
    RT_DRCT_SAT_OAT,                ///< SAT/OAT in C (none)
    RT_DRCT_TAT,                    ///< TAT (none)
    RT_DRCT_ICAO_ID,                ///< (47) Is this an ICAO valid hex ID (1)
    RT_DRCT_Operator,               ///< (48) ICAO operator/airline flag code (e.g. "QFA", "FDX"); empty when the hex is not in the BaseStation DB (~30% of records). Hex-keyed, so unaffected by wet-lease / codeshare callsign confusion. Used to populate `FDStaticData::opIcao` for livery matching. (v6)
    RT_DRCT_NUM_FIELDS              ///< Number of known fields (= 49 in v6)
};

/// Fields in a response to a parked aircraft request
enum RT_PARKED_FIELDS_TY {
    RT_PARK_Lat = 0,                ///< latitude (-33.936407)
    RT_PARK_Lon,                    ///< longitude (151.169229)
    RT_PARK_ParkPosName,            ///< some text indicating the parking position as per Jepperson
    RT_PARK_AcType,                 ///< Type ("A388")
    RT_PARK_Reg,                    ///< Registration ("VH-OQA")
    RT_PARK_LastTimeStamp,          ///< Last Epoch timestamp when moved into position, can be long ago (1721590016.01)
    RT_PARK_CallSign,               ///< ATC Callsign ("QFA2")
    RT_PARK_NUM_FIELDS              ///< Number of known fields
};

/// Fields in a RealTraffic AITFC message (older format on port 49003)
enum RT_AITFC_FIELDS_TY {
    RT_AITFC_REC_TYPE = 0,      ///< "AITFC" or "XTRAFFICPSX"
    RT_AITFC_HEXID,             ///< transponder hex code, converted to decimal
    RT_AITFC_LAT,               ///< latitude in degrees
    RT_AITFC_LON,               ///< longitude in degrees
    RT_AITFC_ALT,               ///< altitude in feet (not adapted for local pressure)
    RT_AITFC_VS,                ///< vertical speed in ft/min
    RT_AITFC_AIRBORNE,          ///< airborne: 1 or 0
    RT_AITFC_HDG,               ///< heading (actually: true track)
    RT_AITFC_SPD,               ///< speed in knots
    RT_AITFC_CS,                ///< call sign
    RT_AITFC_TYPE,              ///< ICAO aircraft type (in XTRAFFICPSX: added in parentheses to call sign)
                                // --- following fields only in AITFC ---
    RT_AITFC_TAIL,              ///< registration (tail number)
    RT_AITFC_FROM,              ///< origin airport (IATA code)
    RT_AITFC_TO,                ///< destination airport (IATA code)
                                // --- following field introduced in v7.0.55 only ---
    RT_AITFC_TIMESTAMP,         ///< timestamp for position and others above
                                // --- at some point in time, latest with v9, another field was added, but it is still undocumented and unused
                                // --- count, always last
    RT_AITFC_NUM_FIELDS_MIN     ///< (minimum) number of fields required for a AITFC type message
};
constexpr int RT_XTRAFFICPSX_NUM_FIELDS = RT_AITFC_TYPE+1;
constexpr int RT_MIN_TFC_FIELDS         = RT_XTRAFFICPSX_NUM_FIELDS;

/// Fields in a RealTraffic RTTFC message (since v9 on port 49005)
enum RT_RTTFC_FIELDS_TY {
    RT_RTTFC_REC_TYPE = 0,          ///< "RTTFC"
    RT_RTTFC_HEXID,                 ///< transponder hex code, converted to decimal
    RT_RTTFC_LAT,                   ///< latitude in degrees
    RT_RTTFC_LON,                   ///< longitude in degrees
    RT_RTTFC_ALT_BARO,              ///< altitude in feet (barometric, not adapted for local pressure)
    RT_RTTFC_BARO_RATE,             ///< barometric vertical rate
    RT_RTTFC_AIRBORNE,              ///< airborne flag
    RT_RTTFC_TRACK,                 ///< track
    RT_RTTFC_GSP,                   ///< ground speed
    RT_RTTFC_CS_ICAO,               ///< ICAO call sign
    RT_RTTFC_AC_TYPE,               ///< aircraft type
    RT_RTTFC_AC_TAILNO,             ///< aircraft registration
    RT_RTTFC_FROM_IATA,             ///< origin IATA code
    RT_RTTFC_TO_IATA,               ///< destination IATA code
    RT_RTTFC_TIMESTAMP,             ///< unix epoch timestamp when data was last updated
    RT_RTTFC_SOURCE,                ///< data source
    RT_RTTFC_CS_IATA,               ///< IATA call sign
    RT_RTTFC_MSG_TYPE,              ///< type of message
    RT_RTTFC_ALT_GEOM,              ///< geometric altitude (WGS84 GPS altitude)
    RT_RTTFC_IAS,                   ///< indicated air speed
    RT_RTTFC_TAS,                   ///< true air speed
    RT_RTTFC_MACH,                  ///< Mach number
    RT_RTTFC_TRACK_RATE,            ///< rate of change for track
    RT_RTTFC_ROLL,                  ///< roll in degrees, negative = left
    RT_RTTFC_MAG_HEADING,           ///< magnetic heading
    RT_RTTFC_TRUE_HEADING,          ///< true heading
    RT_RTTFC_GEOM_RATE,             ///< geometric vertical rate
    RT_RTTFC_EMERGENCY,             ///< emergency status
    RT_RTTFC_CATEGORY,              ///< category of the aircraft
    RT_RTTFC_NAV_QNH,               ///< QNH setting navigation is based on
    RT_RTTFC_NAV_ALTITUDE_MCP,      ///< altitude dialled into the MCP in the flight deck
    RT_RTTFC_NAV_ALTITUDE_FMS,      ///< altitude set by the flight management system (FMS)
    RT_RTTFC_NAV_HEADING,           ///< heading set by the MCP
    RT_RTTFC_NAV_MODES,             ///< which modes the autopilot is currently in
    RT_RTTFC_SEEN,                  ///< seconds since any message updated this aircraft state vector
    RT_RTTFC_RSSI,                  ///< signal strength of the receiver
    RT_RTTFC_WINDDIR,               ///< wind direction in degrees true north
    RT_RTTFC_WINDSPD,               ///< wind speed in kts
    RT_RTTFC_OAT,                   ///< outside air temperature / static air temperature
    RT_RTTFC_TAT,                   ///< total air temperature
    RT_RTTFC_ISICAOHEX,                 ///< (40) is this hexid an ICAO-assigned ID
    RT_RTTFC_BARO_ALT_UNCORRECTED,      ///< (41) raw ADS-B baro altitude (1013.25 hPa reference) — v6 occupies this slot; v5 had `augmentation_status` here
    RT_RTTFC_MIN_TFC_FIELDS,            ///< (= 42) strict minimum-fields parser gate, preserved at the pre-v11.1.452 baseline for backward compat with older RT App builds that don't send the new fields below
    // ----- v6 OPTIONAL fields (RealTraffic v11.1.452+) -----
    // These are NOT enforced by the min-fields gate above; readers must
    // bounds-check `tfc.size() > RT_RTTFC_<field>` before accessing.
    RT_RTTFC_AUTHENTICATION = RT_RTTFC_MIN_TFC_FIELDS,  ///< (42) authentication checksum (safe to ignore)
    RT_RTTFC_OPERATOR,                  ///< (43) ICAO operator/airline flag code (e.g. "QFA", "FDX"); empty when the hex is not in the BaseStation DB (~30% of records, mostly private/military). Hex-keyed, so unaffected by wet-lease / codeshare callsign confusion. Used to populate `FDStaticData::opIcao` for livery matching.
};

// map of id to last received datagram (for duplicate datagram detection)
struct RTUDPDatagramTy {
    double posTime;
    std::string datagram;
    
    // we always move the datagram data
    RTUDPDatagramTy(double _time, const char* _data) :
    posTime(_time), datagram(_data) {}
};

//
// MARK: RealTraffic Connection
//
class RealTrafficConnection : public LTFlightDataChannel
{
public:
    enum rtStatusTy {
        RT_STATUS_NONE=0,
        RT_STATUS_STARTING,
        RT_STATUS_CONNECTED_PASSIVELY,      // receive UDP data, but have no active connection to RT server to tell my position
        RT_STATUS_CONNECTED_TO,             // connected to RT server but haven't yet received UDP data
        RT_STATUS_CONNECTED_FULL,           // both connected to, and have received UDP data
        RT_STATUS_STOPPING
    };
    
protected:
    // general lock to synch thread access to object members
    std::recursive_mutex rtMutex;
    // RealTraffic connection status
    volatile rtStatusTy status = RT_STATUS_NONE;
    
    /// RealTraffic License type
    enum RTLicTypeTy : int {
        RT_LIC_UNKNOWN = 0,
        RT_LIC_STANDARD = 1,                ///< Standard RealTraffic license
        RT_LIC_PROFESSIONAL = 2,            ///< Professional RT license, allowing for historical data
    } eLicType = RT_LIC_UNKNOWN;
    /// Data for the current request
    struct CurrTy {
        /// Which kind of call do we need next?
        enum RTRequestTypeTy : int {
            RT_REQU_AUTH = 1,                           ///< Perform Authentication request
            RT_REQU_DEAUTH,                             ///< Perform De-authentication request (closing the session)
            RT_REQU_PARKED,                             ///< Perform Parked Traffic request
            RT_REQU_NEAREST_METAR,                      ///< Perform nearest METAR location request
            RT_REQU_WEATHER,                            ///< Perform Weather request
            RT_REQU_TRAFFIC,                            ///< Perform Traffic request
        } eRequType = RT_REQU_AUTH;                     ///< Which type of request is being performed now?
        std::string sGUID;                              ///< UID returned by RealTraffic upon authentication, valid for 10s only
        positionTy pos;                                 ///< viewer position for which we receive Realtraffic data
        long tOff = 0;                                  ///< [min] time offset for which we request data
    } curr;                                             ///< Data for the current request
    
    /// What's the next time we could send a traffic request?
    std::chrono::time_point<std::chrono::steady_clock> tNextTraffic;
    /// What's the next time we could send a weather request?
    std::chrono::time_point<std::chrono::steady_clock> tNextWeather;
    
    /// METAR entry in the NearestMETAR response
    struct NearestMETAR {
        std::string     ICAO = RT_METAR_UNKN;           ///< ICAO code of METAR station
        double          dist = NAN;                     ///< distance to station
        double          brgTo = NAN;                    ///< bearing to station
        
        NearestMETAR() {}                               ///< Standard constructor, all empty
        NearestMETAR(const JSON_Object* pObj) { Parse (pObj); } ///< Fill from JSON
        
        void clear() { *this = NearestMETAR(); }        ///< reset to defaults
        bool Parse (const JSON_Object* pObj);           ///< parse RT's NearestMETAR response array entry, reutrns if valid
        bool isValid () const                           ///< valid, ie. all fields properly set?
        { return !ICAO.empty() && ICAO != RT_METAR_UNKN && !std::isnan(dist) && !std::isnan(brgTo); }
    };
    
    /// Weather data
    struct WxTy {
        double QNH = NAN;                               ///< baro pressure
        std::chrono::steady_clock::time_point next;     ///< next time to request RealTraffic weather
        positionTy pos;                                 ///< plane position for which we requested Realtraffic weather
        NearestMETAR nearestMETAR;                      ///< info on nearest METAR
        long tOff = 0;                                  ///< time offset for which we requested weather
        int nErr = 0;                                   ///< How many errors did we have during weather requests?
        
        LTWeather w;                                    ///< interface to setting X-Plane's weather
        std::array<LTWeather::InterpolSet,13> interp;   ///< interpolation settings to convert from RT's 20 layers to XP's 13
    protected:
        bool bFirstTime = true;
    public:
        /// Set all relevant values
        void set (double qnh, long _tOff, bool bResetErr = true);
        /// Reset "first time" flag
        void ResetFirstTime () { bFirstTime = true; }
        /// Check if calling for first time, then reset flag
        bool IsFirstTime () { if (!bFirstTime) return false; bFirstTime = false; return true; }
    } rtWx;                                             ///< Data with which latest weather was requested
    /// How many flights does RealTraffic have in total?
    long lTotalFlights = -1;
    /// Shall we check for parked traffic next time around? (Set from main thread after airport data updates)
    bool bDoParkedTraffic = false;
    /// Wall-clock time (`std::time`) of the last parked-traffic fetch.
    /// Drives the periodic re-fetch in `SetRequType` — see
    /// `RT_PARKED_REFRESH_INTVL_S`. 0 = never fetched yet (so the first
    /// `SetRequType` call re-arms immediately, which is harmless because
    /// the connection-init path already arms `bDoParkedTraffic` too).
    time_t tLastParkedRefresh = 0;
    
    // TCP connection to send current position
    std::thread thrTcpServer;               ///< thread of the TCP listening thread (short-lived)
    XPMP2::TCPConnection tcpPosSender;      ///< TCP connection to communicate with RealTraffic
    /// Status of the separate TCP listening thread
    volatile ThrStatusTy eTcpThrStatus = THR_NONE;
    
    // UDP sockets
    XPMP2::UDPReceiver udpTrafficData;      ///< UDP receiver for traffic data (port 49005)
    XPMP2::UDPReceiver udpWeatherData;      ///< UDP receiver for weather data (port 49004)
#if APL == 1 || LIN == 1
    // the self-pipe to shut down the UDP listener thread gracefully
    SOCKET udpPipe[2] = { INVALID_SOCKET, INVALID_SOCKET };
#endif
    /// last simtime that we received UDP traffic
    double lastReceivedTime     = 0.0;
    /// TEMPORARY (FEED_DIAG): per-aircraft last feed-timestamp accepted
    /// by the channel. Used to verify that successive RT positions for
    /// the same hex id arrive with monotonically increasing timestamps,
    /// and to flag backwards / duplicate positions that would explain
    /// rendered aircraft moving backwards. Cleared on connection start.
    std::map<unsigned long, double> lastFeedTs;
    /// last known position to detect fast movement (to request buffered traffic and the like)
    positionTy lastKnownViewPos;
    /// Expecting buffered traffic first?
    bool bWaitForBuffers = false;
    /// expected bu
    // map of last received datagrams for duplicate detection
    std::map<unsigned long,RTUDPDatagramTy> mapDatagrams;
    /// rolling list of timestamp (diff to now) for detecting historic sending
    std::deque<double> dequeTS;
    /// [s] current timestamp adjustment
    double tsAdjust = 0.0;
    
public:
    RealTrafficConnection ();
    
    void Stop (bool bWaitJoin) override;        ///< Stop the UDP listener gracefully
    
    // interface called from LTChannel
    // SetValid also sets internal status
    void SetValid (bool _valid, bool bMsg = true) override;
    
    /// Have connection read traffic data at next chance
    void DoReadParkedTraffic () { bDoParkedTraffic = true; }
    
    //    // shall data of this channel be subject to LTFlightData::DataSmoothing?
    //    bool DoDataSmoothing (double& gndRange, double& airbRange) const override
    //    { gndRange = RT_SMOOTH_GROUND; airbRange = RT_SMOOTH_AIRBORNE; return true; }
    // shall data of this channel be subject to hovering flight detection?
    bool DoHoverDetection () const override { return true; }
    
    // Status
    std::string GetStatusText () const override;            ///< return a human-readable status
    bool isHistoric () const { return curr.tOff > 0; }      ///< serving historic data?
    double GetTSAdjust () const { return tsAdjust; }        ///< current timestamp adjustment
    
protected:
    void Main () override;                                  ///< virtual thread main function
    
    // MARK: Direct Connection by Request/Reply
protected:
    void MainDirect ();                                     ///< thread main function for the direct connection
    /// Which request do we need next and when can we send it?
    std::chrono::time_point<std::chrono::steady_clock> SetRequType (const positionTy& pos);
public:
    int GetNumTrafficBuffers () const                       ///< How many buffers of buffered traffic would we request?
    { return std::min<int>(10, dataRefs.GetFdBufPeriod() / RT_BUFFER_PERIOD); }
    std::string GetURL (const positionTy&) override;        ///< in direct mode return URL and set
    void ComputeBody (const positionTy& pos) override;      ///< in direct mode puts together the POST request with the position data etc.
    bool ProcessFetchedData () override;                    ///< in direct mode process the received data
    bool ProcessTrafficBuffer (const JSON_Object* pBuf);    ///< in direct mode process an object with aircraft data, essentially a fake array
    bool ProcessParkedAcBuffer (const JSON_Object* pData);  ///< in direct mode process an object with parked aircraft data, essentially a fake array
    void ProcessNearestMETAR (const JSON_Array* pData);     ///< in direct mode process NearestMETAR response, find a suitable METAR from the returned array

    // MARK: Weather Processing for both Direct and UDP connections
protected:
    bool PreProcessWeather(const JSON_Object* pData);       ///< weather: process QNH and error, returns if successful
    void ProcessWeather(const JSON_Object* pData);          ///< process detailed weather information
    void ProcessCloudLayer(const JSON_Object* pCL,size_t i);///< process one cloud layer

    // MARK: UDP/TCP via App
protected:
    void MainUDP ();                                        ///< thread main function running the UDP listener

    void SetStatus (rtStatusTy s);
    void SetStatusTcp (bool bEnable, bool _bStopTcp);
    void SetStatusUdp (bool bEnable, bool _bStopUdp);
    bool IsConnected () const { return RT_STATUS_CONNECTED_PASSIVELY <= status && status <= RT_STATUS_CONNECTED_FULL; }
    bool IsConnecting () const { return RT_STATUS_STARTING <= status && status <= RT_STATUS_CONNECTED_FULL; }
    std::string GetStatusStr () const;

    void tcpConnection ();                                  ///< main function of TCP listening thread, lives only until TCP connection established
    void StartTcpConnection ();                             ///< start the TCP listening thread
    void StopTcpConnection ();                              ///< stop the TCP listening thread
    void SendMsg (const char* msg);                         ///< Send and log a message to RealTraffic
    void SendTime (long long ts);                           ///< Send a timestamp to RealTraffic
    void SendXPSimTime(bool bForce);                        ///< Send XP's current simulated time to RealTraffic, adapted to "today or earlier", every once in a while, or if `bForce`
    void SendPos (const positionTy& pos, double speed_m);   ///< Send position/speed info for own ship to RealTraffic
    void SendUsersPlanePos();                               ///< Send user's plane's position/speed to RealTraffic
    void RequestBufferTraffic(const positionTy& pos,
                              double radius_m);             ///< Send request for initial traffic for buffering

    // MARK: Data Processing
    // Process received datagrams
    bool ProcessRecvedTrafficData (const char* traffic);
    /// Process a RTTFC type message
    bool ProcessRTTFC (LTFlightData::FDKeyTy& fdKey, const std::vector<std::string>& tfc, int nBuffer);
    ///< Process a AITFC or XTRAFFICPSX type message
    bool ProcessAITFC (LTFlightData::FDKeyTy& fdKey, const std::vector<std::string>& tfc, int nBuffer);
    bool ProcessRecvedWeatherData (const char* weather);                                      ///< Process UDP weather JSON from RT Application
    std::string GetSlug (unsigned long hex) const;          ///< returns a slug string for a given hex id
    
    /// Determine timestamp adjustment necessary in case of historic data
    void AdjustTimestamp (double& ts, int nBuffer);
    /// Return a string describing the current timestamp adjustment
    std::string GetAdjustTSText () const;
    
    // UDP datagram duplicate check
    // Is it a duplicate? (if not datagram is copied into a map)
    bool IsDatagramDuplicate (unsigned long numId,
                              const char* datagram);
    // remove outdated entries from mapDatagrams
    void CleanupMapDatagrams();
};



#endif /* LTRealTraffic_h */
