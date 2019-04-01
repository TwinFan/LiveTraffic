//
//  LTForeFlight.cpp
//  LiveTraffic
//

/*
 * Copyright (c) 2019, Birger Hoppe
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

#include "LiveTraffic.h"

//
// MARK: ForeFlight Sender
//

// Constructor doesn't do much
ForeFlightSender::ForeFlightSender (mapLTFlightDataTy& _fdMap) :
LTChannel(DR_CHANNEL_FORE_FLIGHT_SENDER),
LTOnlineChannel(),
LTFlightDataChannel(),
fdMap(_fdMap)
{}

ForeFlightSender::~ForeFlightSender()
{
    // make sure everything's cleaned up
    StopConnection();
}

// if called makes sure the UDP client is up and running
bool ForeFlightSender::FetchAllData(const positionTy& pos)
{
    // if we are invalid or disabled we should shut down
    if (!IsValid() || !IsEnabled()) {
        return StopConnection();
    }
    
    // Update bSendUserPlane/AITraffic
    bSendUsersPlane = DataRefs::GetCfgBool(DR_CFG_FF_SEND_USER_PLANE);
    bSendAITraffic  = DataRefs::GetCfgBool(DR_CFG_FF_SEND_TRAFFIC);
    
    // make sure we have a socket and a thread
    if (!StartConnection())
        return false;
    
    return true;
}


void ForeFlightSender::DoDisabledProcessing()
{
    // make sure everything's cleaned up
    StopConnection();
}

void ForeFlightSender::Close ()
{
    // make sure everything's cleaned up
    StopConnection();
}


// Start/Stop the thread, which sends the data
bool ForeFlightSender::StartConnection ()
{
    bStopUdpSender = false;
    if (!thrUdpSender.joinable())
        thrUdpSender = std::thread (udpSendS, this);
    return true;
}

bool ForeFlightSender::StopConnection ()
{
    // is there a main thread running? -> stop it and wait for it to return
    if ( thrUdpSender.joinable() )
    {
        // stop the main thread
        bStopUdpSender = true;              // the message is: Stop!
        ffStopCV.notify_all();              // wake up the thread for stop
        thrUdpSender.join();                // wait for thread to finish
        
        thrUdpSender = std::thread();
    }
    return true;
}

//
// MARK: Send information
//

// thread main function
void ForeFlightSender::udpSend()
{
    //
    // *** open the UDP socket ***
    //
    if (!udpSender.isOpen())
    {
        // open the UDP socket with broadcast permission
        int port = DataRefs::GetCfgInt(DR_CFG_FF_SEND_PORT);
        try {
            udpSender.Open (FF_LOCALHOST, port, FF_NET_BUF_SIZE, 0, true);
            LOG_MSG(logINFO, MSG_FF_OPENED);
        }
        catch (std::runtime_error e) {
            // exception...can only really happen in UDPReceiver::Open
            LOG_MSG(logERR, ERR_UDP_RCVR_OPEN, ChName(),
                    FF_LOCALHOST, port,
                    e.what());
        }
    }
    
    // now - do we have a socket now?
    if (!udpSender.isOpen()) {
        // no: invalidate the channel
        SetValid(false, true);
        return;
    }
    
    //
    // *** Loop for sending data ***
    //
    while ( !bStopUdpSender )
    {
        // send all data, SendAll tells us when to wake up next
        std::chrono::time_point<std::chrono::steady_clock> nextWakeup =
        SendAll();
        
        // if we get 0 in return then there was a problem and we break out
        if (nextWakeup == std::chrono::time_point<std::chrono::steady_clock>())
            break;

        // sleep until time or if woken up for termination
        // by condition variable trigger
        {
            std::unique_lock<std::mutex> lk(ffStopMutex);
            ffStopCV.wait_until(lk, nextWakeup,
                                       [this]{return bStopUdpSender;});
            lk.unlock();
        }
    }
    
    //
    // *** close the UDP socket ***
    //
    udpSender.Close();
    LOG_MSG(logINFO, MSG_FF_STOPPED);
}

// main sending function, decides what to send and calls detail send functions
// returns next wakeup
std::chrono::time_point<std::chrono::steady_clock> ForeFlightSender::SendAll()
{
    // can only do with a UDP socket
    if (!udpSender.isOpen())
    {   LOG_MSG(logWARN,ERR_SOCK_NOTCONNECTED,ChName());
        return std::chrono::time_point<std::chrono::steady_clock>(); }
    
    // time now is:
    lastAtt = std::chrono::steady_clock::now();
    
    // info on user's own plane:
    if (bSendUsersPlane) {
        double airSpeed_m   = 0.0;
        double track        = 0.0;
        positionTy pos = dataRefs.GetUsersPlanePos(airSpeed_m, track);
        if (!pos.isFullyValid())
        {   LOG_MSG(logWARN,ERR_SOCK_INV_POS,ChName());
            return std::chrono::time_point<std::chrono::steady_clock>(); }

        // GPS info every second only
        if (lastAtt - lastGPS >= FF_INTVL_GPS) {
            SendGPS(pos, airSpeed_m, track);
            lastGPS = lastAtt;
        }
        
        // we always send attitude info
        SendAtt(pos, airSpeed_m, track);
    }
    
    // info on AI planes
    const std::chrono::seconds ffIntvlTraffic =
        std::chrono::seconds(DataRefs::GetCfgInt(DR_CFG_FF_SEND_TRAFFIC_INTVL));
    if (bSendAITraffic) {
        if (lastAtt - lastTraffic >= ffIntvlTraffic) {
            SendAllTraffic();
            lastTraffic = lastAtt;
        }
    }
    
    // when to call next depends on what we want to send
    return
    bSendUsersPlane ? lastAtt + FF_INTVL_ATT :
    bSendAITraffic  ? lastTraffic + ffIntvlTraffic :
    std::chrono::time_point<std::chrono::steady_clock>();
}

// Send all traffic aircrafts' data
void ForeFlightSender::SendAllTraffic ()
{
    // from here on access to fdMap guarded by a mutex
    // until FD object is inserted and updated
    std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
    
    // loop over all flight data objects
    for (const std::pair<const LTFlightData::FDKeyTy,LTFlightData>& fdPair: fdMap)
    {
        // get the fd object from the map, key is the transpIcao
        // this fetches an existing or, if not existing, creates a new one
        const LTFlightData& fd = fdPair.second;
        
        // also get the data access lock for consistent data
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        
        // Send traffic data for this object
        SendTraffic(fd);
    }
}

// MARK: Format broadcasts
// Format specification of ForeFlight:
// https://www.foreflight.com/support/network-gps/

// send position of user's aircraft
// "XGPSMy Sim,-80.11,34.55,1200.1,359.05,55.6"
void ForeFlightSender::SendGPS (const positionTy& pos, double speed_m, double track)
{
    // format the string to send
    char s[200];
    snprintf(s,sizeof(s),
             "XGPSLiveTraffic,%.3f,%.3f,%.1f,%.3f,%.1f",
             pos.lon(),                         // longitude
             pos.lat(),                         // latitude
             pos.alt_m(),                       // altitude
             track,                             // track
             speed_m);                          // ground speed
    
    // send the string
    if (!udpSender.broadcast(s)) {
        LOG_MSG(logERR,ERR_SOCK_SEND_FAILED,ChName());
        // increase error count...bail out if too bad
        if (!IncErrCnt()) {
            SetValid(false,true);
            return;
        }
    } else {
        DebugLogRaw(s);
    }
}


// send attitude of user's aircraft
// "XATTMy Sim,180.2,0.1,0.2"
void ForeFlightSender::SendAtt (const positionTy& pos, double speed_m, double track)
{
    // format the string to send
    char s[200];
    snprintf(s,sizeof(s),
             "XATTLiveTraffic,%.1f,%.1f,%.1f",
             pos.heading(),                     // heading
             pos.pitch(),                       // pitch
             pos.roll());                       // roll
    
    // send the string
    if (!udpSender.broadcast(s)) {
        LOG_MSG(logERR,ERR_SOCK_SEND_FAILED,ChName());
        // increase error count...bail out if too bad
        if (!IncErrCnt()) {
            SetValid(false,true);
            return;
        }
    }
    // (no DebugLogRaw...not at 5Hz!)
}

// send other traffic's data
// "XTRAFFICMy Sim,168,33.85397339,-118.32486725,3749.9,-213.0,1,68.2,126.0,KS6"
void ForeFlightSender::SendTraffic (const LTFlightData& fd)
{
    // need an aircraft...
    const LTAircraft* pAc = fd.GetAircraft();
    if (!pAc)
        return;
    const positionTy& ppos = pAc->GetPPos();
    
    // ease data access
    const LTFlightData::FDStaticData& stat = fd.GetUnsafeStat();
    
    // format the string to send
    char s[200];
    snprintf(s,sizeof(s),
             "XTRAFFICLiveTraffic,%lu,%.3f,%.3f,%.1f,%.1f,%d,%.1f,%.1f,%s",
             fd.key().num,                  // hex transp code (or something else)
             ppos.lat(),                    // latitude     (other way round than in GPS!)
             ppos.lon(),                    // longitude
             ppos.alt_ft(),                 // altitude     (here in feet...)
             pAc->GetVSI_ft(),              // VSI
             !ppos.IsOnGnd(),               // airborne flag
             pAc->GetTrack(),               // track
             pAc->GetSpeed_kt(),            // speed
             stat.call.empty() ?            // call sign
             stat.acId("").c_str() :        // (or some other id)
             stat.call.c_str());
             
    
    // send the string
    if (!udpSender.broadcast(s)) {
        LOG_MSG(logERR,ERR_SOCK_SEND_FAILED,ChName());
        // increase error count...bail out if too bad
        if (!IncErrCnt()) {
            SetValid(false,true);
            return;
        }
    } else {
        DebugLogRaw(s);
    }

}
