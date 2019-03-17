//
//  LTRealTraffic.cpp
//

/*
 * Copyright (c) 2018, Birger Hoppe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// All includes are collected in one header
#include "LiveTraffic.h"

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
    if (status != RT_STATUS_NONE)
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
    if (status == RT_STATUS_NONE) {
        return StartConnections();
    }
    
    // do anything only in a normal status
    if (IsConnected())
    {
        // TODO: Tell RT our position
        
        // cleanup map of last datagrams
        CleanupMapDatagrams();
    }
    
    return true;
}

// closes all connections
void RealTrafficConnection::Close()
{
    StopConnections();
}

// sets the status and updates global text to show elsewhere
void RealTrafficConnection::SetStatus(rtStatusTy s)
{
    status = s;
    LOG_MSG(logINFO, MSG_RT_STATUS,
            s == RT_STATUS_NONE ? "Stopped" : GetStatusStr().c_str());
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

// also take care of status
void RealTrafficConnection::SetValid (bool _valid, bool bMsg)
{
    if (!_valid && status != RT_STATUS_NONE)
        SetStatus(RT_STATUS_STOPPING);
    
    LTOnlineChannel::SetValid(_valid, bMsg);
}

// starts the UDP listening thread
bool RealTrafficConnection::StartConnections()
{
    // the thread should not be running already
    if (thrUdpListener.joinable())
        return true;
    
    // now go start the threads
    SetStatus(RT_STATUS_STARTING);
    thrUdpListener = std::thread (udpListenS, this);
    
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

    // close all connections, this will also break out of all
    // blocking calls for receiving message and hence terminate the threads
    udpTrafficData.Close();
    udpWeatherData.Close();

    // wait for threads to finish (as they wake up periodically only this can take a moment)
    if (thrUdpListener.joinable()) {
        thrUdpListener.join();
        thrUdpListener = std::thread();
    }

    // stopped
    SetStatus(RT_STATUS_NONE);
    return true;
}

//
// MARK: UDP Listen Thread - Traffic
//

// runs in a separate thread, listens for UDP traffic and
// forwards that to the flight data
void RealTrafficConnection::udpListen ()
{
    // sanity check: return in case of wrong status
    if (!IsConnecting())
        return;
    
    try {
        // Open the UDP port
        udpTrafficData.Open (RT_LOCALHOST, RT_UDP_PORT_AITRAFFIC, RT_UDP_BUF_SIZE, RT_UDP_MAX_WAIT);
        udpWeatherData.Open (RT_LOCALHOST, RT_UDP_PORT_WEATHER,   RT_UDP_BUF_SIZE, RT_UDP_MAX_WAIT);
        const int maxSock = std::max(udpTrafficData.getSocket(),
                                     udpWeatherData.getSocket()) + 1;

        // return from the thread when requested
        // (not checking for weather socker...not essential)
        while (udpTrafficData.isOpen() && IsConnecting())
        {
            // wait for a UDP datagram on either socket (traffic, weather)
            fd_set sRead;
            FD_ZERO(&sRead);
            FD_SET(udpTrafficData.getSocket(), &sRead);     // check our sockets
            FD_SET(udpWeatherData.getSocket(), &sRead);
            int retval = select(maxSock, &sRead, NULL, NULL, NULL);
            
            // short-cut if we are to shut down (return from 'select' due to closed socket)
            if (status == RT_STATUS_STOPPING)
                break;

            // select call failed???
            if(retval == -1)
                throw UDPRuntimeError("'select' failed");

            // select successful - traffic data
            if (retval > 0 && FD_ISSET(udpTrafficData.getSocket(), &sRead))
            {
                // read UDP datagram
                long rcvdBytes = udpTrafficData.recv();
                
                // received something?
                if (rcvdBytes > 0)
                {
                    // yea, we received something!
                    if (status == RT_STATUS_STARTING)
                        SetStatus(RT_STATUS_CONNECTED_PASSIVELY);
                    else if (status == RT_STATUS_CONNECTED_TO)
                        SetStatus(RT_STATUS_CONNECTED_FULL);
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
            if (status == RT_STATUS_STOPPING)
                break;
            
            // handling of errors, both from select and from recv
            if (retval < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                // not just a normal timeout?
                char sErr[SERR_LEN];
                strerror_s(sErr, sizeof(sErr), errno);
                LOG_MSG(logERR, ERR_UDP_RCVR_RCVR, RT_LOCALHOST, RT_UDP_PORT_AITRAFFIC, sErr);
                // increase error count...bail out if too bad
                if (!IncErrCnt())
                    break;
            }
        }
    }
    catch (std::runtime_error e) {
        // exception...can only really happen in UDPReceiver::Open
        LOG_MSG(logERR, ERR_UDP_RCVR_OPEN,
                RT_LOCALHOST, RT_UDP_PORT_AITRAFFIC,
                e.what());
        // invalidate the channel
        SetValid(false, true);
    }
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
        if (tfc.size() < RT_AITFC_NUM_FIELDS)
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
    
    // ignore aircrafts, which don't want to be tracked
    if (numId == 0)
        return true;            // ignore silently
    
    // *** position time ***
    // RealTraffic doesn't send one, which really is a pitty
    // so we assume 'now', corrected by network time offset
    using namespace std::chrono;
    const double posTime = // system time in microseconds
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
        LOG_MSG(logINFO,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
        return false;
    }
    
    // is position close enough to current pos?
    if (posCamera.between(pos).dist > dataRefs.GetFdStdDistance_m())
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
            
            // we need the operator for livery, usually it is just the first 3 characters of the call sign
            stat.opIcao         = stat.call.substr(0,3);
            
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
            // even with traffic which definitely sits on the gnd
            // The most likely pattern for gnd traffic is "0,0,1"
            // for alt,vsi,airborn
            // but "0,0,0" would mean the same, wouldn't it?
            // So we test for ALT and VS only:
            if (tfc[RT_TFC_ALT]         == "0" &&
                tfc[RT_TFC_VS]          == "0") {
                dyn.gnd = true;
            } else {
                // probably not on gnd, so take care of altitude
                // altitude comes without local pressure applied
                double alt_f = std::stod(tfc[RT_TFC_ALT]);
                alt_f += (hPa - HPA_STANDARD) * FT_per_HPA;
                pos.alt_m() = alt_f * M_per_FT;
            }
            
            // don't forget gnd-flag in position
            pos.onGrnd = dyn.gnd ? positionTy::GND_ON : positionTy::GND_OFF;

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


// Is it a duplicate? (if not datagram is _moved_ into a map)
bool RealTrafficConnection::IsDatagramDuplicate (unsigned long numId,
                                                 double posTime,
                                                 const char* datagram)
{
    // access to map is guarded by a lock
    std::lock_guard<std::mutex> lock(mapMutex);
    
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
    // access to map is guarded by a lock
    std::lock_guard<std::mutex> lock(mapMutex);

    // cut-off time is current sim time minus buffering period,
    // or in other words: Remove all data that had no updates for
    // an entire buffering time period
    const double cutOff = dataRefs.GetSimTime() - dataRefs.GetFdBufPeriod();
    
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
    
    LOG_MSG(logDEBUG, "Received Weather: %s", weather);
    
    // interpret weather
    JSON_Value* pRoot = json_parse_string(weather);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); return false; }
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot);
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); return false; }

    // fetch QNH, sanity check
    double newQNH = jog_l(pObj, RT_WEATHER_QNH);
    
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

