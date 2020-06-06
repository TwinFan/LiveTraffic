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

#define REALTRAFFIC_NAME        "RealTraffic"
#define RT_LOCALHOST            "0.0.0.0"
constexpr size_t RT_NET_BUF_SIZE    = 512;

constexpr double RT_SMOOTH_AIRBORNE = 65.0; // smooth 65s of airborne data
constexpr double RT_SMOOTH_GROUND   = 35.0; // smooth 35s of ground data
constexpr double RT_VSI_AIRBORNE    = 80.0; ///< if VSI is more than this then we assume "airborne"

#define MSG_RT_STATUS           "RealTraffic network status changed to: %s"
#define MSG_RT_WEATHER_IS       "RealTraffic weather: %s reports %ld hPa and '%s'"
#define MSG_RT_LAST_RCVD        " | last: %lds ago"

#define ERR_RT_CANTLISTEN       "RealTraffic: Cannot listen to network, can't tell RealTraffic our position"
#define ERR_RT_WEATHER_QNH      "RealTraffic: %s reports unreasonable QNH %ld - ignored"
#define ERR_RT_DISCARDED_MSG    "RealTraffic: Discarded invalid message: %s"

// Traffic data format and fields
#define RT_TRAFFIC_AITFC        "AITFC"
#define RT_TRAFFIC_XTRAFFICPSX  "XTRAFFICPSX"
#define RT_TRAFFIC_XATTPSX      "XATTPSX"
#define RT_TRAFFIC_XGPSPSX      "XGPSPSX"

enum RT_TFC_FIELDS_TY {         // fields in an AITFC message
    RT_TFC_MSG_TYPE = 0,        // "AITFC" or "XTRAFFICPSX"
    RT_TFC_HEXID,               // transponder hex code, converted to decimal
    RT_TFC_LAT,                 // latitude in degrees
    RT_TFC_LON,                 // longitude in degrees
    RT_TFC_ALT,                 // altitude in feet (not adapted for local pressure)
    RT_TFC_VS,                  // vertical speed in ft/min
    RT_TFC_AIRBORNE,            // airborne: 1 or 0
    RT_TFC_HDG,                 // heading (actually: true track)
    RT_TFC_SPD,                 // speed in knots
    RT_TFC_CS,                  // call sign
    RT_TFC_TYPE,                // ICAO aircraft type (in XTRAFFICPSX: added in parentheses to call sign)
                                // --- following fields only in AITFC ---
    RT_TFC_TAIL,                // registration (tail number)
    RT_TFC_FROM,                // origin airport (IATA code)
    RT_TFC_TO,                  // destination airport (IATA code)
                                // --- following field introduced in v7.0.55 only ---
    RT_TFC_TIMESTAMP,           // timestamp for position and others above
};
constexpr int RT_XTRAFFICPSX_NUM_FIELDS = RT_TFC_TYPE+1;
constexpr int RT_AITFC_NUM_FIELDS_MIN   = RT_TFC_TO+1;
constexpr int RT_AITFC_NUM_FIELDS_MAX   = RT_TFC_TIMESTAMP+1;

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
    double hPa = HPA_STANDARD;
    std::string lastWeather;            // for duplicate detection
    std::string metar;
    std::string metarIcao;

public:
    RealTrafficConnection (mapLTFlightDataTy& _fdMap);
    virtual ~RealTrafficConnection ();

    virtual std::string GetURL (const positionTy&) { return ""; }   // don't need URL, no request/reply
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual const char* ChName() const { return REALTRAFFIC_NAME; }

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
    std::string GetStatusWithTimeStr () const;
    inline bool IsConnected () const { return RT_STATUS_CONNECTED_PASSIVELY <= status && status <= RT_STATUS_CONNECTED_FULL; }
    inline bool IsConnecting () const { return RT_STATUS_STARTING <= status && status <= RT_STATUS_CONNECTED_FULL; }
    void SetStatus (rtStatusTy s);
    void SetStatusTcp (bool bEnable, bool _bStopTcp);
    void SetStatusUdp (bool bEnable, bool _bStopUdp);
    
    // Weather
    inline double GetHPA() const { return hPa; }
    inline std::string GetMetar() const { return metar; }
    inline std::string GetMetarIcao() const { return metarIcao; }

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
    bool ProcessRecvedWeatherData (const char* weather);
    
    // UDP datagram duplicate check
    // Is it a duplicate? (if not datagram is copied into a map)
    bool IsDatagramDuplicate (unsigned long numId,
                              double posTime,
                              const char* datagram);
    // remove outdated entries from mapDatagrams
    void CleanupMapDatagrams();
    
    // initialize weather info
    void InitWeather();
};



#endif /* LTRealTraffic_h */
