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
RealTrafficConnection::RealTrafficConnection () :
LTChannel(DR_CHANNEL_REAL_TRAFFIC_ONLINE),
LTOnlineChannel(),
LTFlightDataChannel()
{}

// Destructor makes sure we are cleaned up
RealTrafficConnection::~RealTrafficConnection ()
{
    if (status != RT_STATUS_NONE)
        StopConnections();
}
        
// Does not actually fetch data (the UDP thread does that) but
// 1. Starts the connections
// 2. updates the RealTraffic local server with out current position
bool RealTrafficConnection::FetchAllData(const positionTy& /*pos*/)
{
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
    switch (s) {
        case RT_STATUS_NONE:                dataRefs.realTrafficStatus = ""; break;
        case RT_STATUS_STARTING:            dataRefs.realTrafficStatus = "Starting..."; break;
        case RT_STATUS_CONNECTED_PASSIVELY: dataRefs.realTrafficStatus = "Connected passively"; break;
        case RT_STATUS_CONNECTED_TO:        dataRefs.realTrafficStatus = "Connected, waiting..."; break;
        case RT_STATUS_CONNECTED_FULL:      dataRefs.realTrafficStatus = "Fully connected"; break;
        case RT_STATUS_STOPPING:            dataRefs.realTrafficStatus = "Stopping..."; break;
    }
    
    LOG_MSG(logINFO, MSG_RT_STATUS,
            s == RT_STATUS_NONE ? "Stopped" : dataRefs.realTrafficStatus.c_str());
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
    if (thrTrafficData.joinable())
        return true;
    
    // now go start the threads
    SetStatus(RT_STATUS_STARTING);
    thrTrafficData = std::thread (udpListenS, this);
    
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

    // wait for threads to finish (as they wake up periodically only this can take a moment)
    if (thrTrafficData.joinable()) {
        thrTrafficData.join();
        thrTrafficData = std::thread();
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
        // Open the UDP port (without timeOut...we rely on closing the socket)
        udpTrafficData.Open (RT_LOCALHOST, RT_UDP_PORT_AITRAFFIC,
                             RT_UDP_BUF_SIZE);

        // return from the thread when requested
        while (udpTrafficData.isOpen() && IsConnecting())
        {
            // wait for a UDP datagram
            long rcvdBytes = udpTrafficData.recv(); //   udpRcvr.timedRecv(RT_UDP_MAX_WAIT);
            
            // short-cut if we are to shut down
            if (status == RT_STATUS_STOPPING)
                break;
            
            // received something?
            if (rcvdBytes > 0)
            {
                // yea, we received something!
                if (status == RT_STATUS_STARTING)
                    SetStatus(RT_STATUS_CONNECTED_PASSIVELY);
                else if (status == RT_STATUS_CONNECTED_TO)
                    SetStatus(RT_STATUS_CONNECTED_FULL);
                
                // TODO: do something reasonable with it...
                LOG_MSG(logDEBUG, "Received from %s:%d: %s",
                        RT_LOCALHOST, RT_UDP_PORT_AITRAFFIC,
                        udpTrafficData.getBuf().c_str());
                
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
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
