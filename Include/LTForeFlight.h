/// @file       LTForeFlight.h
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

#ifndef LTForeFlight_h
#define LTForeFlight_h

#include "LTChannel.h"
#include "Network.h"

//
// MARK: ForeFlight Constants
//

#define FOREFLIGHT_NAME        "ForeFlight"
#define FF_LOCALHOST            "0.0.0.0"
constexpr size_t FF_NET_BUF_SIZE    = 512;

// sending intervals in milliseonds
constexpr std::chrono::milliseconds FF_INTVL_GPS    = std::chrono::milliseconds(1000); // 1 Hz
constexpr std::chrono::milliseconds FF_INTVL_ATT    = std::chrono::milliseconds( 200); // 5 Hz
constexpr std::chrono::milliseconds FF_INTVL        = std::chrono::milliseconds(  20); // Interval between two

#define MSG_FF_OPENED           "ForeFlight: Starting to send"
#define MSG_FF_STOPPED          "ForeFlight: Stopped"

//
// MARK: ForeFlight Sender
//
class ForeFlightSender : public LTOnlineChannel, LTFlightDataChannel
{
protected:
    // the map of flight data, data that we send out to ForeFlight
    mapLTFlightDataTy& fdMap;
    // thread
    std::thread thrUdpSender;
    volatile bool bStopUdpSender  = true;   // tells thread to stop
    std::mutex  ffStopMutex;                // supports wake-up and stop synchronization
    std::condition_variable ffStopCV;
    // UDP sender
    UDPReceiver udpSender;
    bool    bSendUsersPlane = true;
    bool    bSendAITraffic  = true;
    // time points last sent something
    std::chrono::steady_clock::time_point nextGPS;
    std::chrono::steady_clock::time_point nextAtt;
    std::chrono::steady_clock::time_point nextTraffic;
    std::chrono::steady_clock::time_point lastStartOfTraffic;

public:
    ForeFlightSender (mapLTFlightDataTy& _fdMap);
    virtual ~ForeFlightSender ();

    virtual std::string GetURL (const positionTy&) { return ""; }   // don't need URL, no request/reply
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRAFFIC_SENDER; }
    virtual const char* ChName() const { return FOREFLIGHT_NAME; }
    
    // interface called from LTChannel
    virtual bool FetchAllData(const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy&) { return true; }
    virtual void DoDisabledProcessing();
    virtual void Close ();
    
protected:
    // Start/Stop
    bool StartConnection ();
    bool StopConnection ();
    
    // send positions
    void udpSend();                 // thread's main function
    static void udpSendS (ForeFlightSender* me) { me->udpSend(); }
    void SendGPS (const positionTy& pos, double speed_m, double track); // position of user's aircraft
    void SendAtt (const positionTy& pos, double speed_m, double track); // attitude of user's aircraft
    void SendAllTraffic (); // other traffic
    void SendTraffic (const LTFlightData& fd);
};

#endif /* LTForeFlight_h */
