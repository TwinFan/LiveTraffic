/// @file       LTRealTraffic.cpp
/// @brief      RealTraffic: Receives and processes live tracking data
/// @see        https://rtweb.flyrealtraffic.com/
/// @details    Implements RealTrafficConnection:\n
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

#include "LiveTraffic.h"

#if APL == 1 || LIN == 1
#include <unistd.h>
#include <fcntl.h>
#endif

//
// MARK: RealTraffic Connection
//

// Constructor doesn't do much
RealTrafficConnection::RealTrafficConnection (mapLTFlightDataTy& _fdMap) :
LTChannel(DR_CHANNEL_REAL_TRAFFIC_ONLINE),
LTOnlineChannel(),
LTFlightDataChannel(),
fdMap(_fdMap)
{
    // this pointer makes it easier for settings UI to access status/weather
    dataRefs.pRTConn = this;
}

// Destructor makes sure we are cleaned up
RealTrafficConnection::~RealTrafficConnection ()
{
    StopConnections();
    dataRefs.pRTConn = nullptr;
}
        
// Does not actually fetch data (the UDP thread does that) but
// 1. Starts the connections
// 2. updates the RealTraffic local server with out current position
// 3. cleans up map of datagrams for duplicate check
bool RealTrafficConnection::FetchAllData(const positionTy& pos)
{
    // store camera position for later calculations
    posCamera = pos;
    
    // if we are invalid or disabled we should shut down
    if (!IsValid() || !IsEnabled()) {
        return StopConnections();
    }
    
    // need to start up?
    if (status != RT_STATUS_CONNECTED_FULL &&
        !StartConnections())
        return false;
    
    // do anything only in a normal status
    if (IsConnected())
    {
        // Send current position
        SendUsersPlanePos();
        
        // cleanup map of last datagrams
        CleanupMapDatagrams();
        
        // map is empty? That only happens if we don't receive data
        // continuously
        if (mapDatagrams.empty())
            // se Udp status to unavailable, but keep listener running
            SetStatusUdp(false, false);
    }
    
    return true;
}

// if channel is disabled make sure all connections are closed
void RealTrafficConnection::DoDisabledProcessing()
{
    StopConnections();
}


// closes all connections
void RealTrafficConnection::Close()
{
    StopConnections();
}

// sets the status and updates global text to show elsewhere
void RealTrafficConnection::SetStatus(rtStatusTy s)
{
    // consistent status decision
    std::lock_guard<std::recursive_mutex> lock(rtMutex);

    status = s;
    LOG_MSG(logINFO, MSG_RT_STATUS,
            s == RT_STATUS_NONE ? "Stopped" : GetStatusStr().c_str());
}

void RealTrafficConnection::SetStatusTcp(bool bEnable, bool _bStopTcp)
{
    static bool bInCall = false;
    
    // avoid recursiv calls from error handlers
    if (bInCall)
        return;
    bInCall = true;
    
    // consistent status decision
    std::lock_guard<std::recursive_mutex> lock(rtMutex);

    if (bEnable) switch (status) {
        case RT_STATUS_NONE:
        case RT_STATUS_STARTING:
            SetStatus(RT_STATUS_CONNECTED_TO);
            break;
        case RT_STATUS_CONNECTED_PASSIVELY:
            SetStatus(RT_STATUS_CONNECTED_FULL);
            break;
        case RT_STATUS_CONNECTED_TO:
        case RT_STATUS_CONNECTED_FULL:
        case RT_STATUS_STOPPING:
            // no change
            break;
    } else {
        // Disable - also disconnect, otherwise restart wouldn't work
        if (_bStopTcp)
            StopTcpConnection();
        
        // set status
        switch (status) {
        case RT_STATUS_NONE:
        case RT_STATUS_STARTING:
        case RT_STATUS_CONNECTED_PASSIVELY:
        case RT_STATUS_STOPPING:
            // no change
            break;
        case RT_STATUS_CONNECTED_TO:
            SetStatus(RT_STATUS_STARTING);
            break;
        case RT_STATUS_CONNECTED_FULL:
            SetStatus(RT_STATUS_CONNECTED_PASSIVELY);
            break;

        }
        
    }
    
    bInCall = false;

}

void RealTrafficConnection::SetStatusUdp(bool bEnable, bool _bStopUdp)
{
    static bool bInCall = false;
    
    // avoid recursiv calls from error handlers
    if (bInCall)
        return;
    bInCall = true;
    
    // consistent status decision
    std::lock_guard<std::recursive_mutex> lock(rtMutex);
    
    if (bEnable) switch (status) {
        case RT_STATUS_NONE:
        case RT_STATUS_STARTING:
            SetStatus(RT_STATUS_CONNECTED_PASSIVELY);
            break;
        case RT_STATUS_CONNECTED_TO:
            SetStatus(RT_STATUS_CONNECTED_FULL);
            break;
        case RT_STATUS_CONNECTED_PASSIVELY:
        case RT_STATUS_CONNECTED_FULL:
        case RT_STATUS_STOPPING:
            // no change
            break;
    } else {
        // Disable - also disconnect, otherwise restart wouldn't work
        if (_bStopUdp)
            StopUdpConnection();
        
        // reset weather
        InitWeather();
        
        // set status
        switch (status) {
        case RT_STATUS_NONE:
        case RT_STATUS_STARTING:
        case RT_STATUS_CONNECTED_TO:
        case RT_STATUS_STOPPING:
            // no change
            break;
        case RT_STATUS_CONNECTED_PASSIVELY:
            SetStatus(RT_STATUS_STARTING);
            break;
        case RT_STATUS_CONNECTED_FULL:
            SetStatus(RT_STATUS_CONNECTED_TO);
            break;
        }
    }
    
    bInCall = false;
}
    
std::string RealTrafficConnection::GetStatusStr() const
{
    switch (status) {
        case RT_STATUS_NONE:                return "";
        case RT_STATUS_STARTING:            return "Starting...";
        case RT_STATUS_CONNECTED_PASSIVELY: return "Connected passively";
        case RT_STATUS_CONNECTED_TO:        return "Connected, waiting...";
        case RT_STATUS_CONNECTED_FULL:      return "Fully connected";
        case RT_STATUS_STOPPING:            return "Stopping...";
    }
    return "";
}

std::string RealTrafficConnection::GetStatusWithTimeStr() const
{
    std::string s (GetStatusStr());
    if (IsConnected() && lastReceivedTime > 0.0) {
        char sIntvl[50];
        long intvl = std::lround(dataRefs.GetSimTime() - lastReceivedTime);
        snprintf(sIntvl,sizeof(sIntvl),MSG_RT_LAST_RCVD,intvl);
        s += sIntvl;
    }
    return s;
}

// also take care of status
void RealTrafficConnection::SetValid (bool _valid, bool bMsg)
{
    if (!_valid && status != RT_STATUS_NONE)
        SetStatus(RT_STATUS_STOPPING);
    
    LTOnlineChannel::SetValid(_valid, bMsg);
}

// starts all networking threads
bool RealTrafficConnection::StartConnections()
{
    // don't start if we shall stop
    if (status == RT_STATUS_STOPPING)
        return false;
    
    // set startup status
    if (status == RT_STATUS_NONE)
        SetStatus(RT_STATUS_STARTING);
    
    // *** TCP server for RealTraffic to connect to ***
    if (!thrTcpRunning && !tcpPosSender.IsConnected()) {
        if (thrTcpServer.joinable())
            thrTcpServer.join();
        thrTcpRunning = true;
        thrTcpServer = std::thread (tcpConnectionS, this);
    }
    
    // *** UDP data listener ***
    if (!thrUdpRunning) {
        if (thrUdpListener.joinable())
            thrUdpListener.join();
        thrUdpRunning = true;
        thrUdpListener = std::thread (udpListenS, this);
    }
    
    // looks ok
    return true;
}

// stop the UDP listening thread
bool RealTrafficConnection::StopConnections()
{
    // not running?
    if (status == RT_STATUS_NONE)
        return true;
    
    // tell the threads to stop now
    SetStatus(RT_STATUS_STOPPING);

    // stop both TCP and UDP
    StopTcpConnection();
    StopUdpConnection();

    // stopped
    SetStatus(RT_STATUS_NONE);
    return true;
}



//
// MARK: TCP Connection
//

void RealTrafficConnection::tcpConnection ()
{
#if IBM
    // On Windows threads can be named
    SetThreadDescription(GetCurrentThread(), L"LiveTraffic_RT_TCP");
#endif

    // sanity check: return in case of wrong status
    if (!IsConnecting()) {
        thrTcpRunning = false;
        return;
    }
    
    // port to use is configurable
    int tcpPort = DataRefs::GetCfgInt(DR_CFG_RT_LISTEN_PORT);
    
    try {
        bStopTcp = false;
        tcpPosSender.Open (RT_LOCALHOST, tcpPort, RT_NET_BUF_SIZE);
        if (tcpPosSender.listenAccept()) {
            // so we did accept a connection!
            SetStatusTcp(true, false);
            // send our first position
            SendUsersPlanePos();
        }
        else
        {
            // short-cut if we are to shut down (return from 'select' due to closed socket)
            if (!bStopTcp) {
                // not forced to shut down...report other problem
                SHOW_MSG(logERR,ERR_RT_CANTLISTEN);
                SetStatusTcp(false, true);
            }
        }
    }
    catch (std::runtime_error& e) {
        LOG_MSG(logERR, ERR_TCP_LISTENACCEPT, ChName(),
                RT_LOCALHOST, std::to_string(tcpPort).c_str(),
                e.what());
        // invalidate the channel
        SetStatusTcp(false, true);
        SetValid(false, true);
    }
    
    // We make sure that, once leaving this thread, there is no
    // open listener (there might be a connected socket, though)
#if IBM
    if (!bStopTcp)                  // already closed if stop flag set, avoid rare crashes if called in parallel
#endif
        tcpPosSender.CloseListenerOnly();
    thrTcpRunning = false;
}

bool RealTrafficConnection::StopTcpConnection ()
{
    // close all connections, this will also break out of all
    // blocking calls for receiving message and hence terminate the threads
    bStopTcp = true;
    tcpPosSender.Close();
    
    // wait for threads to finish (if I'm not myself this thread...)
    if (std::this_thread::get_id() != thrTcpServer.get_id()) {
        if (thrTcpServer.joinable())
            thrTcpServer.join();
        thrTcpServer = std::thread();
    }
    
    return true;
}


// send position to RealTraffic so that RT knows which area
// we are interested and to give us local weather
// Example:
// “Qs121=6747;289;5.449771266137578;37988724;501908;0.6564195830703577;-2.1443275933742236”
void RealTrafficConnection::SendPos (const positionTy& pos, double speed_m)
{
    if (!tcpPosSender.IsConnected())
    { LOG_MSG(logWARN,ERR_SOCK_NOTCONNECTED,ChName()); return; }
        
    if (!pos.isFullyValid())
    { LOG_MSG(logWARN,ERR_SOCK_INV_POS,ChName()); return; }

    // format the string to send
    char s[200];
    snprintf(s,sizeof(s),
             "Qs121=%ld;%ld;%.15f;%ld;%ld;%.15f;%.15f\n",
             lround(deg2rad(pos.pitch()) * 100000.0),   // pitch
             lround(deg2rad(pos.roll()) * 100000.0),    // bank/roll
             deg2rad(pos.heading()),                    // heading
             lround(pos.alt_ft() * 1000.0),             // altitude
             lround(speed_m),                           // speed
             deg2rad(pos.lat()),                        // latitude
             deg2rad(pos.lon())                         // longitude
    );
    
    // send the string
    if (!tcpPosSender.send(s)) {
        LOG_MSG(logERR,ERR_SOCK_SEND_FAILED,ChName());
        SetStatusTcp(false, true);
    }
    DebugLogRaw(s);
}

// send the position of the user's plane
void RealTrafficConnection::SendUsersPlanePos()
{
    double airSpeed_m = 0.0;
    double track = 0.0;
    positionTy pos = dataRefs.GetUsersPlanePos(airSpeed_m,track);
    SendPos(pos, airSpeed_m);
}



//
// MARK: UDP Listen Thread - Traffic
//

// runs in a separate thread, listens for UDP traffic and
// forwards that to the flight data
void RealTrafficConnection::udpListen ()
{
#if IBM
    // On Windows threads can be named
    SetThreadDescription(GetCurrentThread(), L"LiveTraffic_RT_UDP");
#endif

    // sanity check: return in case of wrong status
    if (!IsConnecting()) {
        thrUdpRunning = false;
        return;
    }
    
    int port = 0;
    try {
        // Open the UDP port
        bStopUdp = false;
        udpTrafficData.Open (RT_LOCALHOST,
                             port = DataRefs::GetCfgInt(DR_CFG_RT_TRAFFIC_PORT),
                             RT_NET_BUF_SIZE);
        udpWeatherData.Open (RT_LOCALHOST,
                             port = DataRefs::GetCfgInt(DR_CFG_RT_WEATHER_PORT),
                             RT_NET_BUF_SIZE);
        int maxSock = std::max((int)udpTrafficData.getSocket(),
                               (int)udpWeatherData.getSocket()) + 1;
#if APL == 1 || LIN == 1
        // the self-pipe to shut down the UDP socket gracefully
        if (pipe(udpPipe) < 0)
            throw NetRuntimeError("Couldn't create pipe");
        fcntl(udpPipe[0], F_SETFL, O_NONBLOCK);
        maxSock = std::max(maxSock, udpPipe[0]+1);
#endif

        // return from the thread when requested
        // (not checking for weather socker...not essential)
        while (udpTrafficData.isOpen() && IsConnecting() && !bStopUdp)
        {
            // wait for a UDP datagram on either socket (traffic, weather)
            fd_set sRead;
            FD_ZERO(&sRead);
            FD_SET(udpTrafficData.getSocket(), &sRead);     // check our sockets
            FD_SET(udpWeatherData.getSocket(), &sRead);
#if APL == 1 || LIN == 1
            FD_SET(udpPipe[0], &sRead);
#endif
            int retval = select(maxSock, &sRead, NULL, NULL, NULL);
            
            // short-cut if we are to shut down (return from 'select' due to closed socket)
            if (bStopUdp)
                break;

            // select call failed???
            if(retval == -1)
                throw NetRuntimeError("'select' failed");

            // select successful - traffic data
            if (retval > 0 && FD_ISSET(udpTrafficData.getSocket(), &sRead))
            {
                // read UDP datagram
                long rcvdBytes = udpTrafficData.recv();
                
                // received something?
                if (rcvdBytes > 0)
                {
                    // yea, we received something!
                    SetStatusUdp(true, false);

                    // have it processed
                    ProcessRecvedTrafficData(udpTrafficData.getBuf());
                }
                else
                    retval = -1;
            }
            
            // select successful - weather data
            if (retval > 0 && FD_ISSET(udpWeatherData.getSocket(), &sRead))
            {
                // read UDP datagram
                long rcvdBytes = udpWeatherData.recv();
                
                // received something?
                if (rcvdBytes > 0)
                {
                    // have it processed
                    ProcessRecvedWeatherData(udpWeatherData.getBuf());
                }
                else
                    retval = -1;
            }
            
            // short-cut if we are to shut down
            if (bStopUdp)
                break;
            
            // handling of errors, both from select and from recv
            if (retval < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                // not just a normal timeout?
                char sErr[SERR_LEN];
                strerror_s(sErr, sizeof(sErr), errno);
                LOG_MSG(logERR, ERR_UDP_RCVR_RCVR, ChName(),
                        sErr);
                // increase error count...bail out if too bad
                if (!IncErrCnt()) {
                    SetStatusUdp(false, true);
                    break;
                }
            }
        }
    }
    catch (std::runtime_error& e) {
        // exception...can only really happen in UDPReceiver::Open
        LOG_MSG(logERR, ERR_UDP_RCVR_OPEN, ChName(),
                RT_LOCALHOST, port,
                e.what());
        // invalidate the channel
        SetStatusUdp(false, true);
        SetValid(false, true);
    }
    
    // Let's make absolutely sure that any connection is really closed
    // once we return from this thread
#if APL == 1 || LIN == 1
    udpTrafficData.Close();
    udpWeatherData.Close();
    // close the self-pipe sockets
    for (SOCKET &s: udpPipe) {
        if (s != INVALID_SOCKET) close(s);
        s = INVALID_SOCKET;
    }
#else
    if (!bStopUdp) {                // already closed if stop flag set, avoid rare crashes if called in parallel
        udpTrafficData.Close();
        udpWeatherData.Close();
    }
#endif
    thrUdpRunning = false;
}

bool RealTrafficConnection::StopUdpConnection ()
{
    bStopUdp = true;
#if APL == 1 || LIN == 1
    // Mac/Lin: Try writing something to the self-pipe to stop gracefully
    if (udpPipe[1] == INVALID_SOCKET ||
        write(udpPipe[1], "STOP", 4) < 0)
    {
        // if the self-pipe didn't work:
#endif
        // close all connections, this will also break out of all
        // blocking calls for receiving message and hence terminate the threads
        udpTrafficData.Close();
        udpWeatherData.Close();
#if APL == 1 || LIN == 1
    }
#endif

    // wait for thread to finish if I'm not this thread myself
    if (std::this_thread::get_id() != thrUdpListener.get_id()) {
        if (thrUdpListener.joinable())
            thrUdpListener.join();
        thrUdpListener = std::thread();
    }
    
    return true;
}


// MARK: Traffic
// Process received traffic data.
// We keep this a bit flexible to be able to work with both
// AITraffic format (port 49003), which is preferred as it has more fields:
//      AITFC,531917901,40.9145,-73.7625,1975,64,1,218,140,DAL9936,BCS1,N101DU,BOS,LGA
// and the Foreflight format (broadcasted on port 49002):
//      XATTPSX,0.0,0.0,-0.0
//      XGPSPSX,-73.77869444,40.63992500,0.0,0.00,0.0
//      XTRAFFICPSX,531917901,40.9145,-73.7625,1975,64,1,218,140,DAL9936(BCS1)
//
bool RealTrafficConnection::ProcessRecvedTrafficData (const char* traffic)
{
    // sanity check: not empty
    if (!traffic || !traffic[0])
        return false;
    
    // Raw data logging
    DebugLogRaw(traffic);
    lastReceivedTime = dataRefs.GetSimTime();
    
    // any a/c filter defined for debugging purposes?
    const std::string acFilter ( dataRefs.GetDebugAcFilter() );

    // split the datagram up into its parts, keeping empty positions empty
    std::vector<std::string> tfc = str_tokenize(traffic, ",()", false);
    
    // nothing found at all???
    if (tfc.size() < 1)
    { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }
    
    // There are two formats we are _really_ interested in: AITFC and XTRAFFICPSX
    // Check for them and their correct number of fields
    if (tfc[RT_TFC_MSG_TYPE] == RT_TRAFFIC_AITFC) {
        if (tfc.size() < RT_AITFC_NUM_FIELDS_MIN)
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }
    } else if (tfc[RT_TFC_MSG_TYPE] == RT_TRAFFIC_XTRAFFICPSX) {
        if (tfc.size() < RT_XTRAFFICPSX_NUM_FIELDS)
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }
    } else
        // other format than AITFC or XTRAFFICPSX
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }

    // *** transponder code ***
    // comes in decimal form, convert to proper upper case hex
    const unsigned long numId = std::stoul(tfc[RT_TFC_HEXID]);
    
    // ignore aircraft, which don't want to be tracked
    if (numId == 0)
        return true;            // ignore silently
    
    // *** position time ***
    using namespace std::chrono;
    // There are 2 possibilities:
    // 1. As of v7.0.55 RealTraffic can send a timestamp (when configured
    //    to use the "LiveTraffic" as Simulator in use, I assume)
    // 2. Before that or with other settings there is no timestamp
    //    so we assume 'now', corrected by network time offset
    
    const double posTime =
    // Timestamp included?
    (tfc[RT_TFC_MSG_TYPE] == RT_TRAFFIC_AITFC &&
     tfc.size() >= RT_TFC_TIMESTAMP+1) ?
    // use that delivered timestamp
    std::stod(tfc[RT_TFC_TIMESTAMP]) :
    // system time in microseconds
    double(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count())
    // divided by 1000000 to create seconds with fractionals
    / 1000000.0
    // corrected by network time diff (which only works if also OpenSky or ADSBEx are active)
    + dataRefs.GetChTsOffset();
    
    // check for duplicate data
    // RealTraffic sends bursts of data every 10s, but that doesn't necessarily
    // mean that anything really moved. Data could be stale.
    // But as data doesn't come with a timestamp we have no means of identifying it.
    // So here we just completely ignore data which looks exactly like the previous datagram
    if (IsDatagramDuplicate(numId, posTime, traffic))
        return true;            // ignore silently

    // *** Process received data ***

    // key is most likely an Icao transponder code, but could also be a Realtraffic internal id
    const LTFlightData::FDKeyTy fdKey (numId <= MAX_TRANSP_ICAO ? LTFlightData::KEY_ICAO : LTFlightData::KEY_RT,
                                       numId);
    
    // not matching a/c filter? -> skip it
    if ((!acFilter.empty() && (fdKey != acFilter)))
        return true;            // silently

    // *** position ***
    // RealTraffic always provides data 100km around current position
    // Let's check if the data falls into our configured range and discard it if not
    positionTy pos (std::stod(tfc[RT_TFC_LAT]),
                    std::stod(tfc[RT_TFC_LON]),
                    0,              // we take care of altitude later
                    posTime);
    
    // position is rather important, we check for validity
    // (we do allow alt=NAN if on ground)
    if ( !pos.isNormal(true) ) {
        LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
        return false;
    }
    
    // is position close enough to current pos?
    if (posCamera.dist(pos) > dataRefs.GetFdStdDistance_m())
        return true;                // ignore silently, no error
    
    try {
        // from here on access to fdMap guarded by a mutex
        // until FD object is inserted and updated
        std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
        
        // get the fd object from the map, key is the transpIcao
        // this fetches an existing or, if not existing, creates a new one
        LTFlightData& fd = fdMap[fdKey];
        
        // also get the data access lock once and for all
        // so following fetch/update calls only make quick recursive calls
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        
        // completely new? fill key fields
        if ( fd.empty() )
            fd.SetKey(fdKey);
        
        // fill static data
        {
            LTFlightData::FDStaticData stat;
            
            stat.acTypeIcao     = tfc[RT_TFC_TYPE];
            stat.call           = tfc[RT_TFC_CS];
            
            if (tfc[RT_TFC_MSG_TYPE] == RT_TRAFFIC_AITFC) {
                stat.reg            = tfc[RT_TFC_TAIL];
                stat.originAp       = tfc[RT_TFC_FROM];
                stat.destAp         = tfc[RT_TFC_TO];
            }

            fd.UpdateData(std::move(stat));
        }
        
        // dynamic data
        {   // unconditional...block is only for limiting local variables
            LTFlightData::FDDynamicData dyn;
            
            // non-positional dynamic data
            dyn.gnd =               tfc[RT_TFC_AIRBORNE] == "0";
            dyn.heading =           std::stoi(tfc[RT_TFC_HDG]);
            dyn.spd =               std::stoi(tfc[RT_TFC_SPD]);
            dyn.vsi =               std::stoi(tfc[RT_TFC_VS]);
            dyn.ts =                posTime;
            dyn.pChannel =          this;
            
            // *** gnd detection hack ***
            // RealTraffic keeps the airborne flag always 1,
            // even with traffic which definitely sits on the gnd.
            // Also, reported altitude never seems to become negative,
            // though this would be required in high pressure weather
            // at airports roughly at sea level.
            // And altitude is rounded to 250ft which means that close
            // to the ground it could be rounded down to 0!
            //
            // If "0" is reported we need to assume "on gnd" and bypass
            // the pressure correction.
            // If at the same time VSI is reported significantly (> +/- 100)
            // then we assume plane is already/still flying, but as we
            // don't know exact altitude we just skip this record.
            if (tfc[RT_TFC_ALT]         == "0") {
                // skip this dynamic record in case VSI is too large
                if (std::abs(dyn.vsi) > RT_VSI_AIRBORNE)
                    return true;
                // have proper gnd altitude calculated
                pos.alt_m() = NAN;
                dyn.gnd = true;
            } else {
                // probably not on gnd, so take care of altitude
                // altitude comes without local pressure applied
                double alt_f = std::stod(tfc[RT_TFC_ALT]);
                alt_f += (hPa - HPA_STANDARD) * FT_per_HPA;
                pos.SetAltFt(alt_f);
            }
            
            // don't forget gnd-flag in position
            pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;

            // add dynamic data
            fd.AddDynData(dyn, 0, 0, &pos);
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        return false;
    }

    // success
    return true;
}


// Is it a duplicate? (if not datagram is copied into a map)
bool RealTrafficConnection::IsDatagramDuplicate (unsigned long numId,
                                                 double posTime,
                                                 const char* datagram)
{
    // access is guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(rtMutex);
    
    // is the plane, identified by numId unkown?
    auto it = mapDatagrams.find(numId);
    if (it == mapDatagrams.end()) {
        // add the datagram the first time for this plane
        mapDatagrams.emplace(numId, RTUDPDatagramTy(posTime,datagram));
        // no duplicate
        return false;
    }
    
    // plane known...is the data identical? -> duplicate
    RTUDPDatagramTy& d = it->second;
    if (d.datagram == datagram)
        return true;
        
    // plane known, but data different, replace data in map
    d.posTime = posTime;
    d.datagram = datagram;
    
    // no duplicate
    return false;
}

// remove outdated entries from mapDatagrams
void RealTrafficConnection::CleanupMapDatagrams()
{
    // access is guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(rtMutex);

    // cut-off time is current sim time minus outdated interval,
    // or in other words: Remove all data that had no updates for
    // the outdated period, planes will vanish soon anyway
    const double cutOff = dataRefs.GetSimTime() - dataRefs.GetAcOutdatedIntvl();
    
    for (auto it = mapDatagrams.begin(); it != mapDatagrams.end(); ) {
        if (it->second.posTime < cutOff)
            it = mapDatagrams.erase(it);
        else
            ++it;            
    }
}


// MARK: Weather
// Process regular weather messages. They are important for QNH,
// as RealTraffic doesn't adapt altitude readings in the traffic data.
// Weather comes in JSON format, pretty-printed looking like this:
// {
//     "ICAO": "KJFK",
//     "QNH": 2996,             # inches mercury in US!
//     "METAR": "KJFK 052051Z 25016KT 10SM FEW050 FEW250 00/M09 A2996 RMK AO2 SLP144 T00001094 56025",
//     "NAME": "John F Kennedy International Airport",
//     "IATA": "JFK",
//     "DISTNM": 0.1
// }
//
// or like this:
// {
//     "ICAO": "OMDB",
//     "QNH": 1015,             # hPa outside US
//     "METAR": "OMDB 092300Z 27012KT 9999 BKN036 19/10 Q1015 NOSIG",
//     "NAME": "Dubai International Airport",
//     "IATA": "DXB",
//     "DISTNM": 0.3
// }

bool RealTrafficConnection::ProcessRecvedWeatherData (const char* weather)
{
    // sanity check: not empty
    if (!weather || !weather[0])
        return false;
    
    // Raw data logging
    DebugLogRaw(weather);
    
    // duplicate?
    if (lastWeather == weather)
        return true;            // ignore silently
    lastWeather = weather;
    
    LOG_MSG(logDEBUG, "Received Weather: %s", weather);
    
    // interpret weather
    JSON_Value* pRoot = json_parse_string(weather);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); return false; }
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot);
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); return false; }

    // fetch QNH
    // This value seems to be sent without (in the very first message)
    // and with quotes (thereafter), so we try both ways to get a reasonable value:
    
    double newQNH = jog_sl(pObj, RT_WEATHER_QNH);
    if (newQNH < 1.0)
        newQNH = jog_l(pObj, RT_WEATHER_QNH);

    // this could be inch mercury in the US...convert to hPa
    if (2600 <= newQNH && newQNH <= 3400)
        newQNH *= HPA_per_INCH;

    // process a change
    if (800 <= newQNH && newQNH <= 1100) {
        metarIcao = jog_s(pObj, RT_WEATHER_ICAO);
        metar =     jog_s(pObj, RT_WEATHER_METAR);
        if (!dequal(hPa, newQNH))                          // report a change in the log
            LOG_MSG(logINFO, MSG_RT_WEATHER_IS, metarIcao.c_str(), std::lround(newQNH), metar.c_str());
        hPa = newQNH;
        return true;
    } else {
        LOG_MSG(logWARN, ERR_RT_WEATHER_QNH, metarIcao.c_str(), newQNH);
        return false;
    }
}

// initialize weather info
void RealTrafficConnection::InitWeather()
{
    hPa = HPA_STANDARD;
    lastWeather.clear();
    metar.clear();
    metarIcao.clear();
}
