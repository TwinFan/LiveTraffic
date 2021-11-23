/// @file       LTFSCharter.h
/// @brief      FSCharter: Requests and processes FSC tracking data
/// @see        https://fscharter.net/
/// @details    Defines FSCConnection:\n
///             - Takes care of login (OAuth)\n
///             - Provides a proper REST-conform URL\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
/// @author     Birger Hoppe
/// @copyright  (c) 2021 Birger Hoppe
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

#ifndef LTFSCharter_h
#define LTFSCharter_h

#include "LTFSCharter.h"

//MARK: FSCharter Constants
#define FSC_CHECK_NAME          "FSCharter Flight Board"
#define FSC_CHECK_URL           "https://fscharter.net/flight_board"
#define FSC_CHECK_POPUP         "See who's flying in FSCharter just now"

#define FSC_NAME                "FSCharter"

#define FSC_URL                 "https://%s/api/get-traffic?lamin=%.3f&lomin=%.3f&lamax=%.3f&lomax=%.3f"

//
//MARK: FSCharter
//
class FSCConnection : public LTOnlineChannel, LTFlightDataChannel
{
protected:
    /// The authentification token to be used in all requests (except for OAuth/token, which logs in and receives it)
    std::string token;
    /// The type of authentication token, typically "Bearer"
    std::string token_type;
public:
    FSCConnection ();
    virtual std::string GetURL (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual bool FetchAllData(const positionTy& pos) { return LTOnlineChannel::FetchAllData(pos); }
//    // shall data of this channel be subject to LTFlightData::DataSmoothing?
//    virtual bool DoDataSmoothing (double& gndRange, double& airbRange) const
//    { gndRange = FSC_SMOOTH_GROUND; airbRange = FSC_SMOOTH_AIRBORNE; return true; }
};


#endif /* LTFSCharter_h */
