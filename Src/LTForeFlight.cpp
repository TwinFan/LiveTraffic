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
// MARK: RealTraffic Connection
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
    
    // FIXME: Update bSendUserPlane/AITraffic
    
    // make sure we have a socket and a thread
    if (!StartConnection())
        return false;

    // FIXME: Move to thread
    SendAll();
    
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


// Start/Stop
bool ForeFlightSender::StartConnection ()
{
    // already open?
    if (udpSender.isOpen())
        return true;
    
    // open the UDP socket with broadcast permission
    int port = DataRefs::GetCfgInt(DR_CFG_FF_SEND_PORT);
    try {
        udpSender.Open (FF_LOCALHOST, port, FF_NET_BUF_SIZE, 0, true);
        LOG_MSG(logINFO, MSG_FF_OPENED);
        return udpSender.isOpen();
    }
    catch (std::runtime_error e) {
        // exception...can only really happen in UDPReceiver::Open
        LOG_MSG(logERR, ERR_UDP_RCVR_OPEN, ChName(),
                FF_LOCALHOST, port,
                e.what());
        // invalidate the channel
        SetValid(false, true);
    }
    return false;
}

bool ForeFlightSender::StopConnection ()
{
    if (udpSender.isOpen()) {
        udpSender.Close();
        LOG_MSG(logINFO, MSG_FF_STOPPED);
    }
    return true;
}

//
// MARK: Send information
//

// main function, decides what to send and calls detail send functions
void ForeFlightSender::SendAll()
{
    if (!udpSender.isOpen())
    { LOG_MSG(logWARN,ERR_SOCK_NOTCONNECTED,ChName()); return; }
    
    // time now is:
    lastAtt = std::chrono::steady_clock::now();
    
    // info on user's own plane:
    if (bSendUsersPlane) {
        double airSpeed_m   = 0.0;
        double track        = 0.0;
        positionTy pos = dataRefs.GetUsersPlanePos(airSpeed_m, track);
        if (!pos.isFullyValid())
        { LOG_MSG(logWARN,ERR_SOCK_INV_POS,ChName()); return; }

        // GPS info every second only
        if (lastAtt - lastGPS >= FF_INTVL_GPS) {
            SendGPS(pos, airSpeed_m, track);
            lastGPS = lastAtt;
        }
        
        // we always send attitude info
        SendAtt(pos, airSpeed_m, track);
    }
    
    // info on AI planes
    if (bSendAITraffic) {
        if (lastAtt - lastTraffic >= FF_INTVL_TRAFFIC) {
            SendAllTraffic();
            lastTraffic = lastAtt;
        }
    }
}

// send position of user's aircraft
void ForeFlightSender::SendGPS (const positionTy& pos, double speed_m, double track)
{
    // format the string to send
    char s[200];
    snprintf(s,sizeof(s),
             "XGPSLiveTraffic,%.3f,%.3f,%.1f,%.3f,%.1f",
             pos.lat(),                         // latitude
             pos.lon(),                         // longitude
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

// other traffic
void ForeFlightSender::SendAllTraffic ()
{
    
}

void ForeFlightSender::SendTraffic (const LTFlightData& fd)
{
    
}
