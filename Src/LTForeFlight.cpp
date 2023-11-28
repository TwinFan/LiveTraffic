/// @file       LTForeFlight.cpp
/// @brief      ForeFlight: Output channel to send LiveTraffic's aircraft positions to the local network
/// @see        https://www.foreflight.com/support/network-gps/
/// @details    Starts/stops a separate thread to send out UDP broadcast.\n
///             Formats and sends UDP packages.\n
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
// MARK: ForeFlight Sender
//

// Constructor doesn't do much
ForeFlightSender::ForeFlightSender (mapLTFlightDataTy& _fdMap) :
LTOutputChannel(DR_CHANNEL_FORE_FLIGHT_SENDER, FOREFLIGHT_NAME),
fdMap(_fdMap)
{
    // purely informational
    urlName  = FF_CHECK_NAME;
    urlLink  = FF_CHECK_URL;
    urlPopup = FF_CHECK_POPUP;
}

ForeFlightSender::~ForeFlightSender()
{
    // make sure everything's cleaned up
    StopConnection();
}

// if called makes sure the UDP client is up and running
bool ForeFlightSender::FetchAllData(const positionTy&)
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
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_ForeFlight", LC_ALL_MASK);

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
        catch (std::runtime_error& e) {
            // exception...can only really happen in UDPReceiver::Open
            LOG_MSG(logERR, ERR_UDP_SOCKET_CREAT, ChName(),
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
    // We place 20ms pause between any two broadcasts in order not to overtax
    // the network. Increases reliability. The logic is then as follows:
    // The loop keeps running till stop.
    // We check if it is time for ATT or GPS data and send that if so.
    // If not we continue with the traffic planes.
    //   Once we reach the end of all traffic we won't start again
    //   before reaching the proper time.
    // Between any two broadcasts there is 20ms break.
    //
    LTFlightData::FDKeyTy lastKey;          // last traffic sent out
    while ( !bStopUdpSender )
    {
        bool bDidSendSomething = false;
        
        // now
        std::chrono::time_point<std::chrono::steady_clock> now =
        std::chrono::steady_clock::now();
        
        // send user's plane at all?
        if (bSendUsersPlane) {
            double airSpeed_m   = 0.0;
            double track        = 0.0;
            positionTy pos;

            // time for GPS?
            if (now >= nextGPS)
            {
                pos = dataRefs.GetUsersPlanePos(airSpeed_m, track);
                SendGPS(pos, airSpeed_m, track);
                nextGPS = now + FF_INTVL_GPS;
                bDidSendSomething = true;
            }
            
            // time for ATT?
            if (now >= nextAtt)
            {
                if (!pos.isNormal())
                    pos = dataRefs.GetUsersPlanePos(airSpeed_m, track);
                SendAtt(pos, airSpeed_m, track);
                nextAtt = now + FF_INTVL_ATT;
                bDidSendSomething = true;
            }
        }

        // send traffic at all?
        // not yet send GPS/ATT?
        // time to send some traffic?
        if (bSendAITraffic && !bDidSendSomething &&
            now >= nextTraffic)
        {
            // from here on access to fdMap guarded by a mutex
            std::unique_lock<std::mutex> lock (mapFdMutex, std::try_to_lock);
            if (lock) {
                if (!fdMap.empty()) {
                    // just starting with a new round?
                    if (lastKey == LTFlightData::FDKeyTy())
                        lastStartOfTraffic = now;
                    
                    // next key to send? (shall have an actual a/c)
                    mapLTFlightDataTy::const_iterator mapIter;
                    for (mapIter = fdMap.upper_bound(lastKey);
                         mapIter != fdMap.cend() && !mapIter->second.hasAc();
                         mapIter++);
                    
                    // something left?
                    if (mapIter != fdMap.cend()) {
                        // send that plane's info
                        SendTraffic(mapIter->second);
                        // wake up soon again for the rest
                        lastKey = mapIter->first;
                        nextTraffic = now + FF_INTVL;
                    }
                    else {
                        // we're done with one round, start over
                        lastKey = LTFlightData::FDKeyTy();
                        nextTraffic = lastStartOfTraffic + std::chrono::seconds(DataRefs::GetCfgInt(DR_CFG_FF_SEND_TRAFFIC_INTVL));
                    }
                }
                else {
                    // map's empty, so we are done
                    lastKey = LTFlightData::FDKeyTy();
                    nextTraffic = lastStartOfTraffic + std::chrono::seconds(DataRefs::GetCfgInt(DR_CFG_FF_SEND_TRAFFIC_INTVL));
                }
            } else {
                // wake up soon again for the rest
                nextTraffic = now + FF_INTVL;
            }
        }
        
        // sleep until time or if woken up for termination
        // by condition variable trigger
        if (!bStopUdpSender)
        {
            std::chrono::time_point<std::chrono::steady_clock> nextWakeup =
            bSendUsersPlane && bSendAITraffic  ? std::min({nextGPS, nextAtt, nextTraffic}) :
            bSendUsersPlane && !bSendAITraffic ? std::min(nextGPS, nextAtt) :
            nextTraffic;
            
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

// Send all traffic aircraft's data
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
        
        //
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
void ForeFlightSender::SendAtt (const positionTy& pos, double /*speed_m*/, double /*track*/)
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
