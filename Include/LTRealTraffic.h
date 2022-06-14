/// @file       LTRealTraffic.h
/// @brief      RealTraffic: Receives and processes live tracking data
/// @see        https://rtweb.flyrealtraffic.com/
/// @details    Defines RealTrafficConnection:\n
///             - Sends current position to RealTraffic app\n
///             - Receives tracking and weather data via UDP\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
/// @author     Birger Hoppe
/// @copyright  (c) 2019-2020 Birger Hoppe
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

#ifndef LTRealTraffic_h
#define LTRealTraffic_h

#include "LTChannel.h"
#include "Network.h"

//
// MARK: RealTraffic Constants
//

#define RT_CHECK_NAME           "RealTraffic's web site"
#define RT_CHECK_URL            "https://rtweb.flyrealtraffic.com/"
#define RT_CHECK_POPUP          "Open RealTraffic's web site, which has a traffic status overview"

#define REALTRAFFIC_NAME        "RealTraffic"
#define RT_LOCALHOST            "0.0.0.0"
constexpr size_t RT_NET_BUF_SIZE    = 512;

constexpr double RT_SMOOTH_AIRBORNE = 65.0; // smooth 65s of airborne data
constexpr double RT_SMOOTH_GROUND   = 35.0; // smooth 35s of ground data
constexpr double RT_VSI_AIRBORNE    = 80.0; ///< if VSI is more than this then we assume "airborne"

#define MSG_RT_STATUS           "RealTraffic network status changed to: %s"
#define MSG_RT_LAST_RCVD        " | last: %lds ago"
#define MSG_RT_ADJUST           " | historic traffic from %s ago"

#define INFO_RT_REAL_TIME       "RealTraffic: Tracking data is real-time again."
#define INFO_RT_ADJUST_TS       "RealTraffic: Detected tracking data from %s in the past, will adjust them to display now."
#define ERR_RT_CANTLISTEN       "RealTraffic: Cannot listen to network, can't tell RealTraffic our position"
#define ERR_RT_WEATHER_QNH      "RealTraffic reports unreasonable QNH %ld - ignored"
#define ERR_RT_DISCARDED_MSG    "RealTraffic: Discarded invalid message: %s"
#define ERR_SOCK_NOTCONNECTED   "%s: Cannot send position: not connected"
#define ERR_SOCK_INV_POS        "%s: Cannot send position: position not fully valid"

// Traffic data format and fields
#define RT_TRAFFIC_RTTFC        "RTTFC"
#define RT_TRAFFIC_AITFC        "AITFC"
#define RT_TRAFFIC_XTRAFFICPSX  "XTRAFFICPSX"
#define RT_TRAFFIC_XATTPSX      "XATTPSX"
#define RT_TRAFFIC_XGPSPSX      "XGPSPSX"

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
    RT_RTTFC_GND,                   ///< ground flag
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
    RT_RTTFC_ISICAOHEX,             ///< is this hexid an ICAO assigned ID.
    RT_RTTFC_AUGMENTATION_STATUS,   ///< has this record been augmented from multiple sources
    RT_RTTFC_MIN_TFC_FIELDS         ///< always last, minimum number of fields
};

// Weather JSON fields
#define RT_WEATHER_ICAO         "ICAO"
#define RT_WEATHER_QNH          "QNH"
#define RT_WEATHER_METAR        "METAR"

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
class RealTrafficConnection : public LTOnlineChannel, LTFlightDataChannel
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
    // the map of flight data, where we deliver our data to
    mapLTFlightDataTy& fdMap;

    // tcp connection to send current position
    std::thread thrTcpServer;
    TCPConnection tcpPosSender;
    volatile bool bStopTcp = false;
    volatile bool thrTcpRunning = false;
    // current position which serves as center
    positionTy posCamera;

    // udp thread and its sockets
    std::thread thrUdpListener;
    UDPReceiver udpTrafficData;
    UDPReceiver udpWeatherData;
#if APL == 1 || LIN == 1
    // the self-pipe to shut down the UDP listener thread gracefully
    SOCKET udpPipe[2] = { INVALID_SOCKET, INVALID_SOCKET };
#endif
    volatile bool bStopUdp = false;
    volatile bool thrUdpRunning = false;
    double lastReceivedTime     = 0.0;  // copy of simTime
    // map of last received datagrams for duplicate detection
    std::map<unsigned long,RTUDPDatagramTy> mapDatagrams;
    // weather, esp. current barometric pressure to correct altitude values
    std::string lastWeather;            // for duplicate detection
    /// rolling list of timestamp (diff to now) for detecting historic sending
    std::deque<double> dequeTS;
    /// current timestamp adjustment
    double tsAdjust = 0.0;

public:
    RealTrafficConnection (mapLTFlightDataTy& _fdMap);
    virtual ~RealTrafficConnection ();

    virtual std::string GetURL (const positionTy&) { return ""; }   // don't need URL, no request/reply
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }

    // interface called from LTChannel
    virtual bool FetchAllData(const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy&) { return true; }
    virtual void DoDisabledProcessing();
    virtual void Close ();
    // SetValid also sets internal status
    virtual void SetValid (bool _valid, bool bMsg = true);
//    // shall data of this channel be subject to LTFlightData::DataSmoothing?
//    virtual bool DoDataSmoothing (double& gndRange, double& airbRange) const
//    { gndRange = RT_SMOOTH_GROUND; airbRange = RT_SMOOTH_AIRBORNE; return true; }
    // shall data of this channel be subject to hovering flight detection?
    virtual bool DoHoverDetection () const { return true; }

    // Status
    inline rtStatusTy GetStatus () const { return status; }
    double GetLastRcvdTime () const { return lastReceivedTime; }
    std::string GetStatusStr () const;
    virtual std::string GetStatusText () const;  ///< return a human-readable staus
    virtual std::string GetStatusTextExt () const;

    inline bool IsConnected () const { return RT_STATUS_CONNECTED_PASSIVELY <= status && status <= RT_STATUS_CONNECTED_FULL; }
    inline bool IsConnecting () const { return RT_STATUS_STARTING <= status && status <= RT_STATUS_CONNECTED_FULL; }
    void SetStatus (rtStatusTy s);
    void SetStatusTcp (bool bEnable, bool _bStopTcp);
    void SetStatusUdp (bool bEnable, bool _bStopUdp);
    
protected:
    // Start/Stop
    bool StartConnections ();
    bool StopConnections ();
    
    // MARK: TCP
    void tcpConnection ();
    static void tcpConnectionS (RealTrafficConnection* me) { me->tcpConnection();}
    bool StopTcpConnection ();

    void SendPos (const positionTy& pos, double speed_m);
    void SendUsersPlanePos();

    // MARK: UDP
    // UDP Listen: the main function for receiving UDP broadcasts
    void udpListen ();
    // just a wrapper to call a member function
    static void udpListenS (RealTrafficConnection* me) { me->udpListen();}
    bool StopUdpConnection ();

    // Process received datagrams
    bool ProcessRecvedTrafficData (const char* traffic);
    bool ProcessRTTFC (LTFlightData::FDKeyTy& fdKey, const std::vector<std::string>& tfc);    ///< Process a RTTFC type message
    bool ProcessAITFC (LTFlightData::FDKeyTy& fdKey, const std::vector<std::string>& tfc);    ///< Process a AITFC or XTRAFFICPSX type message
    bool ProcessRecvedWeatherData (const char* weather);
    
    /// Determine timestamp adjustment necessairy in case of historic data
    void AdjustTimestamp (double& ts);
    /// Return a string describing the current timestamp adjustment
    std::string GetAdjustTSText () const;
    
    // UDP datagram duplicate check
    // Is it a duplicate? (if not datagram is copied into a map)
    bool IsDatagramDuplicate (unsigned long numId,
                              const char* datagram);
    // remove outdated entries from mapDatagrams
    void CleanupMapDatagrams();
    
    // initialize weather info
    void InitWeather();
};



#endif /* LTRealTraffic_h */
