//
//  LTRealTraffic.h
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

#ifndef LTRealTraffic_h
#define LTRealTraffic_h

#include "LTChannel.h"
#include "Network.h"

//
// MARK: RealTraffic Constants
//

#define REALTRAFFIC_NAME        "RealTraffic"
#define RT_LOCALHOST            "localhost"
constexpr int RT_UDP_PORT_WEATHER   = 49004;
constexpr int RT_UDP_PORT_AITRAFFIC = 49003;
constexpr size_t RT_UDP_BUF_SIZE    = 512;
constexpr int RT_UDP_MAX_WAIT       = 1000;     // millisecond

#define MSG_RT_STATUS           "RealTraffic network status changed to: %s"

#define ERR_UDP_RCVR_OPEN       "Error creating UDP receiver for %s:%d: %s"
#define ERR_UDP_RCVR_RCVR       "Error receiving from %s:%d: %s"

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
    volatile rtStatusTy status = RT_STATUS_NONE;
    // traffic data
    std::thread thrTrafficData;
    UDPReceiver udpTrafficData;

public:
    RealTrafficConnection ();
    virtual ~RealTrafficConnection ();

    virtual std::string GetURL (const positionTy& pos) { return ""; }   // don't need URL, no request/reply
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual const char* ChName() const { return REALTRAFFIC_NAME; }

    // interface called from LTChannel
    virtual bool FetchAllData(const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap) { return true; }
    virtual void Close ();
    // SetValid also sets internal status
    virtual void SetValid (bool _valid, bool bMsg = true);

    inline rtStatusTy GetStatus () const { return status; }
    inline bool IsConnected () const { return RT_STATUS_CONNECTED_PASSIVELY <= status && status <= RT_STATUS_CONNECTED_FULL; }
    inline bool IsConnecting () const { return RT_STATUS_STARTING <= status && status <= RT_STATUS_CONNECTED_FULL; }
    void SetStatus (rtStatusTy s);
    bool StartConnections ();
    bool StopConnections ();
    
protected:
    // UDP Listen: the main function for receiving UDP broadcasts
    void udpListen ();
    // just a wrapper to call a member function
    static void udpListenS (RealTrafficConnection* me) { me->udpListen();}
};



#endif /* LTRealTraffic_h */
