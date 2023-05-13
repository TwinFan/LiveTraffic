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
LTChannel(DR_CHANNEL_REAL_TRAFFIC_ONLINE, REALTRAFFIC_NAME),
LTOnlineChannel(),
LTFlightDataChannel(),
fdMap(_fdMap)
{
    //purely information
    urlName  = RT_CHECK_NAME;
    urlLink  = RT_CHECK_URL;
    urlPopup = RT_CHECK_POPUP;
}

// Destructor makes sure we are cleaned up
RealTrafficConnection::~RealTrafficConnection ()
{
    StopConnections();
}
        
// Does not actually fetch data (the UDP thread does that) but
// 1. Starts the connections
// 2. updates the RealTraffic local server with out current position and time
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
        // Send current position and time
        SendXPSimTime();
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
        case RT_STATUS_STARTING:            return "Waiting for RealTraffic...";
        case RT_STATUS_CONNECTED_PASSIVELY: return "Connected passively";
        case RT_STATUS_CONNECTED_TO:        return "Connected, waiting...";
        case RT_STATUS_CONNECTED_FULL:      return "Fully connected";
        case RT_STATUS_STOPPING:            return "Stopping...";
    }
    return "";
}

std::string RealTrafficConnection::GetStatusText () const
{
    // Partly a copy of LTChannel's version, but we take RT-specific status into account
    
    // invalid (after errors)? Just disabled/off? Or active (but not a source of tracking data)?
    if (!IsValid() || !IsEnabled())
        return LTChannel::GetStatusText();

    // If we are waiting to establish a connection then we return RT-specific texts
    if (status == RT_STATUS_NONE)           return "Starting...";
    if (status == RT_STATUS_STARTING ||
        status == RT_STATUS_STOPPING)
        return GetStatusStr();
    
    // An active source of tracking data...for how many aircraft?
    return LTChannel::GetStatusText();
}

std::string RealTrafficConnection::GetStatusTextExt() const
{
    // Any extended status only if we are connected in any way
    if (!IsEnabled() ||
        status < RT_STATUS_CONNECTED_PASSIVELY ||
        status > RT_STATUS_CONNECTED_FULL)
        return std::string();
    
    // Turn the RealTraffic status into text
    std::string s (GetStatusStr());
    
    if (IsConnected() && lastReceivedTime > 0.0) {
        char sIntvl[100];
        // add when the last msg was received
        long intvl = std::lround(dataRefs.GetSimTime() - lastReceivedTime);
        snprintf(sIntvl,sizeof(sIntvl),MSG_RT_LAST_RCVD,intvl);
        s += sIntvl;
        // if receiving historic traffic say so
        if (tsAdjust > 1.0) {
            snprintf(sIntvl, sizeof(sIntvl), MSG_RT_ADJUST,
                     GetAdjustTSText().c_str());
            s += sIntvl;
        }
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
    
    // Clear the list of historic time stamp differences
    dequeTS.clear();

    // stopped
    SetStatus(RT_STATUS_NONE);
    return true;
}



//
// MARK: TCP Connection
//

void RealTrafficConnection::tcpConnection ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_RT_TCP", LC_ALL_MASK);

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
            // send our simulated time and first position
            SendXPSimTime();
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


// Send and log a message to RealTraffic
void RealTrafficConnection::SendMsg (const char* msg)
{
    if (!tcpPosSender.IsConnected())
    { LOG_MSG(logWARN,ERR_SOCK_NOTCONNECTED,ChName()); return; }
        
    // send the string
    if (!tcpPosSender.send(msg)) {
        LOG_MSG(logERR,ERR_SOCK_SEND_FAILED,ChName());
        SetStatusTcp(false, true);
    }
    DebugLogRaw(msg);
}

// Send a timestamp to RealTraffic
/// @details Format is Qs123=1674984782616, where the long number is the UTC epoch milliseconds of the simulator time.
void RealTrafficConnection::SendTime (long long ts)
{
    // format the string to send and send it out
    char s[50];
    snprintf(s, sizeof(s), "Qs123=%lld\n", ts);
    SendMsg(s);
}

// Send XP's current simulated time to RealTraffic, adapted to "today or earlier"
void RealTrafficConnection::SendXPSimTime()
{
    if (dataRefs.GetRTSTC() == STC_NO_CTRL)         // Configured to send nothing?
        return;
    
    // Which time stamp to send?
    long long ts;
    if (dataRefs.IsUsingSystemTime()) {             // Sim is using system time, so send system time
        ts = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    } else {
        ts = dataRefs.GetXPSimTime_ms();            // else send simulated time
        if (dataRefs.GetRTSTC() == STC_SIM_TIME_PLUS_BUFFER)
            // add buffering period if requested, so planes match up with simulator time exactly instead of being delayed
            ts += (long long)(dataRefs.GetFdBufPeriod()) * 1000LL;
    }
    
    SendTime(ts);
}


// send position to RealTraffic so that RT knows which area
// we are interested and to give us local weather
// Example:
// “Qs121=6747;289;5.449771266137578;37988724;501908;0.6564195830703577;-2.1443275933742236”
void RealTrafficConnection::SendPos (const positionTy& pos, double speed_m)
{
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
    
    // Send the message
    SendMsg(s);
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
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_RT_UDP", LC_ALL_MASK);

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
        int maxSock = (int)udpTrafficData.getSocket() + 1;
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
        LOG_MSG(logERR, ERR_UDP_SOCKET_CREAT, ChName(),
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
    // close the self-pipe sockets
    for (SOCKET &s: udpPipe) {
        if (s != INVALID_SOCKET) close(s);
        s = INVALID_SOCKET;
    }
#else
    if (!bStopUdp) {                // already closed if stop flag set, avoid rare crashes if called in parallel
        udpTrafficData.Close();
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
// We keep this a bit flexible to be able to work with different formats
bool RealTrafficConnection::ProcessRecvedTrafficData (const char* traffic)
{
    // sanity check: not empty
    if (!traffic || !traffic[0])
        return false;
    
    // Raw data logging
    DebugLogRaw(traffic);
    lastReceivedTime = dataRefs.GetSimTime();
    
    // split the datagram up into its parts, keeping empty positions empty
    std::vector<std::string> tfc = str_tokenize(traffic, ",()", false);
    
    // not enough fields found for any message?
    if (tfc.size() < RT_MIN_TFC_FIELDS)
    { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }
    
    // *** Duplicaton Check ***
    
    // comes in all 3 formats at position 1 and in decimal form
    const unsigned long numId = std::stoul(tfc[RT_AITFC_HEXID]);
    
    // ignore aircraft, which don't want to be tracked
    if (numId == 0)
        return true;            // ignore silently
    
    // RealTraffic sends bursts of data often, but that doesn't necessarily
    // mean that anything really moved. Data could be stale.
    // So here we just completely ignore data which looks exactly like the previous datagram
    if (IsDatagramDuplicate(numId, traffic))
        return true;            // ignore silently

    // key is most likely an Icao transponder code, but could also be a Realtraffic internal id
    LTFlightData::FDKeyTy fdKey (numId <= MAX_TRANSP_ICAO ? LTFlightData::KEY_ICAO : LTFlightData::KEY_RT,
                                 numId);
    
    // not matching a/c filter? -> skip it
    const std::string acFilter ( dataRefs.GetDebugAcFilter() );
    if ((!acFilter.empty() && (fdKey != acFilter)))
        return true;            // silently

    // *** Process different formats ****
    
    // There are 3 formats we are _really_ interested in: RTTFC, AITFC, and XTRAFFICPSX
    // Check for them and their correct number of fields
    if (tfc[RT_RTTFC_REC_TYPE] == RT_TRAFFIC_RTTFC) {
        if (tfc.size() < RT_RTTFC_MIN_TFC_FIELDS)
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }

        return ProcessRTTFC(fdKey, tfc);
    }
    else if (tfc[RT_AITFC_REC_TYPE] == RT_TRAFFIC_AITFC) {
        if (tfc.size() < RT_AITFC_NUM_FIELDS_MIN)
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }

        return ProcessAITFC(fdKey, tfc);
    }
    else if (tfc[RT_AITFC_REC_TYPE] == RT_TRAFFIC_XTRAFFICPSX) {
        if (tfc.size() < RT_XTRAFFICPSX_NUM_FIELDS)
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }

        return ProcessAITFC(fdKey, tfc);
    }
    else {
        // other format than AITFC or XTRAFFICPSX
        LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic);
        return false;
    }
}
    

/// Helper to return first element larger than zero from the data array
double firstPositive (const std::vector<std::string>& tfc,
                      std::initializer_list<size_t> li)
{
    for (size_t i: li) {
        const double d = std::stod(tfc[i]);
        if (d > 0.0)
            return d;
    }
    return 0.0;
}


// Process a RTTFC  type message
/// @details RTTraffic format (port 49005), introduced in v9 of RealTraffic
///            RTTFC,hexid, lat, lon, baro_alt, baro_rate, gnd, track, gsp,
///            cs_icao, ac_type, ac_tailno, from_iata, to_iata, timestamp,
///            source, cs_iata, msg_type, alt_geom, IAS, TAS, Mach, track_rate,
///            roll, mag_heading, true_heading, geom_rate, emergency, category,
///            nav_qnh, nav_altitude_mcp, nav_altitude_fms, nav_heading,
///            nav_modes, seen, rssi, winddir, windspd, OAT, TAT,
///            isICAOhex,augmentation_status,authentication
/// @details Example:
///            RTTFC,11234042,-33.9107,152.9902,26400,1248,0,90.12,490.00,
///            AAL72,B789, N835AN,SYD,LAX,1645144774.2,X2,AA72,adsb_icao,
///            27575,320,474,0.780, 0.0,0.0,78.93,92.27,1280,none,A5,1012.8,
///            35008,-1,71.02, autopilot|vnav|lnav|tcas,0.0,-21.9,223,24,
///            -30,0,1,170124
bool RealTrafficConnection::ProcessRTTFC (LTFlightData::FDKeyTy& fdKey,
                                          const std::vector<std::string>& tfc)
{
    // *** Potentially skip static objects ***
    const std::string& sCat = tfc[RT_RTTFC_CATEGORY];
    if (dataRefs.GetHideStaticTwr() &&              // shall ignore static objects?
        (// Aircraft's category is C3, C4, or C5?
         (sCat.length() == 2 && sCat[0] == 'C' && (sCat[1] == '3' || sCat[1] == '4' || sCat[1] == '5')) ||
         // - OR - tail and type are 'TWR' (as in previous msg types)
         (tfc[RT_RTTFC_AC_TAILNO] == "TWR" &&
          tfc[RT_RTTFC_AC_TYPE] == "TWR")
       ))
        return true;
    
    // *** position time ***
    double posTime = std::stod(tfc[RT_RTTFC_TIMESTAMP]);
    AdjustTimestamp(posTime);

    // *** Process received data ***

    // *** position ***
    // RealTraffic always provides data 100km around current position
    // Let's check if the data falls into our configured range and discard it if not
    positionTy pos (std::stod(tfc[RT_RTTFC_LAT]),
                    std::stod(tfc[RT_RTTFC_LON]),
                    0,              // we take care of altitude later
                    posTime);
    
    // position is rather important, we check for validity
    // (we do allow alt=NAN if on ground)
    if ( !pos.isNormal(true) ) {
        LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
        return false;
    }
    
    // RealTraffic always sends data of 100km or so around current pos
    // Filter data that the user didn't want based on settings
    const positionTy viewPos = dataRefs.GetViewPos();
    const double dist = pos.dist(viewPos);
    if (dist > dataRefs.GetFdStdDistance_m() )
        return true;            // silently
    
    try {
        // from here on access to fdMap guarded by a mutex
        // until FD object is inserted and updated
        std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
        
        // There's a flag telling us if a key is an ICAO code
        if (tfc[RT_RTTFC_ISICAOHEX] != "1")
            fdKey.eKeyType = LTFlightData::KEY_RT;
        
        // Check for duplicates with OGN/FLARM, potentially replaces the key type
        if (fdKey.eKeyType == LTFlightData::KEY_ICAO)
            LTFlightData::CheckDupKey(fdKey, LTFlightData::KEY_FLARM);
        
        // get the fd object from the map, key is the transpIcao
        // this fetches an existing or, if not existing, creates a new one
        LTFlightData& fd = fdMap[fdKey];
        
        // also get the data access lock once and for all
        // so following fetch/update calls only make quick recursive calls
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        // now that we have the detail lock we can release the global one
        mapFdLock.unlock();

        // completely new? fill key fields
        if ( fd.empty() )
            fd.SetKey(fdKey);
        
        // -- fill static data --
        LTFlightData::FDStaticData stat;
        
        stat.acTypeIcao     = tfc[RT_RTTFC_AC_TYPE];
        stat.call           = tfc[RT_RTTFC_CS_ICAO];
        stat.reg            = tfc[RT_RTTFC_AC_TAILNO];
        stat.setOrigDest(tfc[RT_RTTFC_FROM_IATA], tfc[RT_RTTFC_TO_IATA]);
        stat.catDescr       = GetADSBEmitterCat(sCat);

        // -- dynamic data --
        LTFlightData::FDDynamicData dyn;
        
        // non-positional dynamic data
        dyn.gnd         = tfc[RT_RTTFC_AIRBORNE] == "0";
        dyn.heading     = firstPositive(tfc, {RT_RTTFC_TRUE_HEADING, RT_RTTFC_TRACK, RT_RTTFC_MAG_HEADING});
        dyn.spd         = std::stod(tfc[RT_RTTFC_GSP]);
        dyn.vsi         = firstPositive(tfc, {RT_RTTFC_GEOM_RATE, RT_RTTFC_BARO_RATE});
        dyn.ts          = posTime;
        dyn.pChannel    = this;
        
        // Altitude
        if (dyn.gnd)
            pos.alt_m() = NAN;          // ground altitude to be determined in scenery
        else {
            // Since RealTraffic v10, it delivers "corrected" altitude in the Baremtric Alt field, we prefer this value and don't need to apply pressure correction
            double alt = std::stod(tfc[RT_RTTFC_ALT_BARO]);
            if (alt > 0.0)
                pos.SetAltFt(alt);
            // Otherwise we try using geometric altitude
            else
                if ((alt = std::stod(tfc[RT_RTTFC_ALT_GEOM])) > 0.0)
                    pos.SetAltFt(alt);
        }
        // don't forget gnd-flag in position
        pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;

        // Vehicle?
        if (stat.acTypeIcao == "GRND" || stat.acTypeIcao == "GND")  // some vehicles come with type 'GRND'...
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        else if (sCat.length() == 2 && sCat[0] == 'C' && (sCat[1] == '1' || sCat[1] == '2'))
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        else if (sCat.empty() && dyn.gnd && stat.acTypeIcao.empty() && stat.reg.empty())
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();

        // add the static data
        fd.UpdateData(std::move(stat), dist);

        // add the dynamic data
        fd.AddDynData(dyn, 0, 0, &pos);

    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        return false;
    }

    // success
    return true;
}


// Process a AITFC or XTRAFFICPSX type message
/// @details AITraffic format (port 49003), which has more fields:
///            AITFC,531917901,40.9145,-73.7625,1975,64,1,218,140,DAL9936,BCS1,N101DU,BOS,LGA
///          and the Foreflight format (broadcasted on port 49002):
///            XTRAFFICPSX,531917901,40.9145,-73.7625,1975,64,1,218,140,DAL9936(BCS1)
///
bool RealTrafficConnection::ProcessAITFC (LTFlightData::FDKeyTy& fdKey,
                                          const std::vector<std::string>& tfc)
{
    // *** Potentially skip static objects ***
    if (dataRefs.GetHideStaticTwr() &&
        tfc[RT_AITFC_CS]    == "TWR" &&
        tfc[RT_AITFC_TYPE]  == "TWR")
        return true;
    
    // *** position time ***
    // There are 2 possibilities:
    // 1. As of v7.0.55 RealTraffic can send a timestamp (when configured
    //    to use the "LiveTraffic" as Simulator in use, I assume)
    // 2. Before that or with other settings there is no timestamp
    //    so we assume 'now'
    
    double posTime;
    // Timestamp included?
    if (tfc.size() > RT_AITFC_TIMESTAMP)
    {
        // use that delivered timestamp and (potentially) adjust it if it is in the past
        posTime = std::stod(tfc[RT_AITFC_TIMESTAMP]);
        AdjustTimestamp(posTime);
    }
    else
    {
        // No Timestamp provided: assume 'now'
        using namespace std::chrono;
        posTime =
        // system time in microseconds
        double(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count())
        // divided by 1000000 to create seconds with fractionals
        / 1000000.0;
    }

    // *** Process received data ***

    // *** position ***
    // RealTraffic always provides data 100km around current position
    // Let's check if the data falls into our configured range and discard it if not
    positionTy pos (std::stod(tfc[RT_AITFC_LAT]),
                    std::stod(tfc[RT_AITFC_LON]),
                    0,              // we take care of altitude later
                    posTime);
    
    // position is rather important, we check for validity
    // (we do allow alt=NAN if on ground)
    if ( !pos.isNormal(true) ) {
        LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
        return false;
    }
    
    // RealTraffic always sends data of 100km or so around current pos
    // Filter data that the user didn't want based on settings
    const positionTy viewPos = dataRefs.GetViewPos();
    const double dist = pos.dist(viewPos);
    if (dist > dataRefs.GetFdStdDistance_m() )
        return true;            // silently
    
    try {
        // from here on access to fdMap guarded by a mutex
        // until FD object is inserted and updated
        std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
        
        // Check for duplicates with OGN/FLARM, potentially replaces the key type
        if (fdKey.eKeyType == LTFlightData::KEY_ICAO)
            LTFlightData::CheckDupKey(fdKey, LTFlightData::KEY_FLARM);
        
        // get the fd object from the map, key is the transpIcao
        // this fetches an existing or, if not existing, creates a new one
        LTFlightData& fd = fdMap[fdKey];
        
        // also get the data access lock once and for all
        // so following fetch/update calls only make quick recursive calls
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        // now that we have the detail lock we can release the global one
        mapFdLock.unlock();

        // completely new? fill key fields
        if ( fd.empty() )
            fd.SetKey(fdKey);
        
        // -- fill static data --
        LTFlightData::FDStaticData stat;
        
        stat.acTypeIcao     = tfc[RT_AITFC_TYPE];
        stat.call           = tfc[RT_AITFC_CS];
        
        if (tfc.size() > RT_AITFC_TO) {
            stat.reg            = tfc[RT_AITFC_TAIL];
            stat.setOrigDest(tfc[RT_AITFC_FROM], tfc[RT_AITFC_TO]);
        }

        // -- dynamic data --
        LTFlightData::FDDynamicData dyn;
        
        // non-positional dynamic data
        dyn.gnd =               tfc[RT_AITFC_AIRBORNE] == "0";
        dyn.spd =               std::stoi(tfc[RT_AITFC_SPD]);
        dyn.heading =           std::stoi(tfc[RT_AITFC_HDG]);
        dyn.vsi =               std::stoi(tfc[RT_AITFC_VS]);
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
        if (tfc[RT_AITFC_ALT]         == "0") {
            // skip this dynamic record in case VSI is too large
            if (std::abs(dyn.vsi) > RT_VSI_AIRBORNE)
                return true;
            // have proper gnd altitude calculated
            pos.alt_m() = NAN;
            dyn.gnd = true;
        } else {
            // probably not on gnd, so take care of altitude
            // altitude comes without local pressure applied
            pos.SetAltFt(BaroAltToGeoAlt_ft(std::stod(tfc[RT_AITFC_ALT]), dataRefs.GetPressureHPA()));
        }
        
        // don't forget gnd-flag in position
        pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;

        // -- Ground vehicle identification --
        // is really difficult with RealTraffic as we only have very few information:
        if (stat.acTypeIcao.empty() &&      // don't know a/c type yet
            dyn.gnd &&                      // on the ground
            dyn.spd < 50.0 &&               // reasonable speed
            stat.reg.empty() &&             // no tail number
            stat.dest().empty())            // no destination airport
        {
            // we assume ground vehicle
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        }

        // add the static data
        fd.UpdateData(std::move(stat), dist);

        // add the dynamic data
        fd.AddDynData(dyn, 0, 0, &pos);

    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        return false;
    }

    // success
    return true;
}


// Determine timestamp adjustment necessairy in case of historic data
void RealTrafficConnection::AdjustTimestamp (double& ts)
{
    // the assumed 'now' is simTime + buffering period
    const double now = dataRefs.GetSimTime() + dataRefs.GetFdBufPeriod();
    
    // *** Keep the rolling list of timestamps diffs, max length: 11 ***
    dequeTS.push_back(now - ts);
    while (dequeTS.size() > 11)
        dequeTS.pop_front();
    
    // *** Determine Median of timestamp differences ***
    double medianTs;
    if (dequeTS.size() >= 3)
    {
        // To find the (lower) Median while at the same time preserve the deque in its order,
        // we do a partial sort into another array
        std::vector<double> v((dequeTS.size()+1)/2);
        std::partial_sort_copy(dequeTS.cbegin(), dequeTS.cend(),
                               v.begin(), v.end());
        medianTs = v.back();
    }
    // with less than 3 sample we just pick the last
    else {
        medianTs = dequeTS.back();
    }
    
    // *** Need to change the timestamp adjustment?
    // Priority has to change back to zero if we are half the buffering period away from "now"
    const int halfBufPeriod = dataRefs.GetFdBufPeriod()/2;
    if (medianTs < 0.0 ||
        std::abs(medianTs) <= halfBufPeriod) {
        if (tsAdjust > 0.0) {
            tsAdjust = 0.0;
            SHOW_MSG(logINFO, INFO_RT_REAL_TIME);
        }
    }
    // ...if that median is more than half the buffering period away from current adjustment
    else if (std::abs(medianTs - tsAdjust) > halfBufPeriod)
    {
        // new adjustment is that median, rounded to 10 seconds
        tsAdjust = std::round(medianTs / 10.0) * 10.0;
        SHOW_MSG(logINFO, INFO_RT_ADJUST_TS, GetAdjustTSText().c_str());
    }

    // Adjust the passed-in timestamp by the determined adjustment
    ts += tsAdjust;
}


// Return a string describing the current timestamp adjustment
std::string RealTrafficConnection::GetAdjustTSText () const
{
    char timeTxt[100];
    if (tsAdjust < 300.0)               // less than 5 minutes: tell seconds
        snprintf(timeTxt, sizeof(timeTxt), "%.0fs ago", tsAdjust);
    else if (tsAdjust < 86400.0)        // less than 1 day
        snprintf(timeTxt, sizeof(timeTxt), "%ld:%02ldh ago",
                 long(tsAdjust/3600.0),         // hours
                 long(tsAdjust/60.0) % 60);     // minutes
    else {
        // More than a day ago, compute full UTC time the data is from
        struct tm tm;
        time_t t;
        time(&t);
        t -= time_t(tsAdjust);
        gmtime_s(&tm, &t);
        
        snprintf(timeTxt, sizeof(timeTxt), "%ldd %ld:%02ldh ago (%4d-%02d-%02d %02d:%02d UTC)",
                 long(tsAdjust/86400),          // days
                 long(tsAdjust/3600.0) % 24,    // hours
                 long(tsAdjust/60.0) % 60,      // minutes
                 // UTC timestamp
                 tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min);
    }
    return std::string(timeTxt);
}


// Is it a duplicate? (if not datagram is copied into a map)
bool RealTrafficConnection::IsDatagramDuplicate (unsigned long numId,
                                                 const char* datagram)
{
    // access is guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(rtMutex);
    
    // is the plane, identified by numId unkown?
    auto it = mapDatagrams.find(numId);
    if (it == mapDatagrams.end()) {
        // add the datagram the first time for this plane
        mapDatagrams.emplace(std::piecewise_construct,
                             std::forward_as_tuple(numId),
                             std::forward_as_tuple(dataRefs.GetSimTime(),datagram));
        // no duplicate
        return false;
    }
    
    // plane known...is the data identical? -> duplicate
    RTUDPDatagramTy& d = it->second;
    if (d.datagram == datagram)
        return true;
        
    // plane known, but data different, replace data in map
    d.posTime = dataRefs.GetSimTime();
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
