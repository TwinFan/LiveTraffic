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

//
// MARK: RealTraffic Constants
//

#define REALTRAFFIC_NAME        "RealTraffic"

//
// MARK: RealTraffic Connection
//
class RealTrafficConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    RealTrafficConnection () :
    LTChannel(DR_CHANNEL_REAL_TRAFFIC_ONLINE),
    LTOnlineChannel(),
    LTFlightDataChannel()  {}

    virtual std::string GetURL (const positionTy& pos) { return ""; }   // don't need URL, no request/reply
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual const char* ChName() const { return REALTRAFFIC_NAME; }

    virtual bool FetchAllData(const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap) { return true; }
};



#endif /* LTRealTraffic_h */
