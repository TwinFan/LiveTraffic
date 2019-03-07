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
        // Open the UDP port (without timeOut...we rely on closing the socket)
        udpTrafficData.Open (RT_LOCALHOST, RT_UDP_PORT_AITRAFFIC, RT_UDP_BUF_SIZE);
        udpWeatherData.Open (RT_LOCALHOST, RT_UDP_PORT_WEATHER,   RT_UDP_BUF_SIZE);
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

// process received traffic data
bool RealTrafficConnection::ProcessRecvedTrafficData (std::string traffic)
{
    // TODO: do something reasonable with it...
    // too many entries - LOG_MSG(logDEBUG, "Received Traffic: %s", traffic.c_str());
    return true;
}

bool RealTrafficConnection::ProcessRecvedWeatherData (std::string weather)
{
    // LOG_MSG(logDEBUG, "Received Weather: %s", weather.c_str());
    
    // interpret weather
    JSON_Value* pRoot = json_parse_string(weather.c_str());
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); return false; }
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot);
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); return false; }

    // fetch QNH, sanity check
    long newQNH = jog_l(pObj, RT_WEATHER_QNH);
    if (2600 <= newQNH && newQNH <= 3400) {
        metarIcao = jog_s(pObj, RT_WEATHER_ICAO);
        metar =     jog_s(pObj, RT_WEATHER_METAR);
        if (qnh != newQNH)                          // report a change in the log
            LOG_MSG(logDEBUG, MSG_RT_WEATHER_IS, metarIcao.c_str(), newQNH, metar.c_str());
        qnh = (int)newQNH;
        return true;
    } else {
        LOG_MSG(logWARN, ERR_RT_WEATHER_QNH, metarIcao.c_str(), newQNH);
        return false;
    }
}
