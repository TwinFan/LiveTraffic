//
//  LTForeFlight.h
//  LiveTraffic

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

#ifndef LTForeFlight_h
#define LTForeFlight_h

#include "LTChannel.h"
#include "Network.h"

//
// MARK: ForeFlight Constants
//

#define FOREFLIGHT_NAME        "ForeFlight"

//
// MARK: ForeFlight Sender
//
class ForeFlightSender : public LTOnlineChannel, LTFlightDataChannel
{
protected:
    // the map of flight data, data that we send out to ForeFlight
    mapLTFlightDataTy& fdMap;

public:
    ForeFlightSender (mapLTFlightDataTy& _fdMap);

    virtual std::string GetURL (const positionTy&) { return ""; }   // don't need URL, no request/reply
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRAFFIC_SENDER; }
    virtual const char* ChName() const { return FOREFLIGHT_NAME; }
    
    // interface called from LTChannel
    virtual bool FetchAllData(const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy&) { return true; }
    virtual void DoDisabledProcessing();
    virtual void Close ();
};

#endif /* LTForeFlight_h */
