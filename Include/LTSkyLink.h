/// @file       LTSkyLink.h
/// @brief      SkyLink: Requests and processes live tracking data
/// @see        https://api.skylinkapi.com/docs
///             RAPID API: https://rapidapi.com/skylink-api-skylink-api-default/api/skylink-api
/// @details    Defines a base class handling the SkyLink data format,
/// @details    Defines SkyLinkConnection:\n
///             - Handles the API key\n
///             - Provides a proper REST-conform URL for both the original sevrer as well as for the Rapid API server.
/// @author     Birger Hoppe
/// @copyright  (c) 2025 Birger Hoppe
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

#ifndef LTSkyLink_h
#define LTSkyLink_h

#include "LTChannel.h"

//MARK: SkyLink Constants
#define SKYLINK_CHECK_NAME       "SkyLink Server Status"
#define SKYLINK_CHECK_URL        "https://status.skylinkapi.com/"
#define SKYLINK_CHECK_POPUP      "Check SkyLink's Server Status"

#define SKYLINK_NAME             "SkyLink"
#define SKYLINK_RAPIDAPI_URL     "https://skylink-api.p.rapidapi.com/v2/adsb/aircraft?lat=%.3f&lon=%.3f&radius=%d"
#define SKYLINK_RAPIDAPI_HEALTH  "https://skylink-api.p.rapidapi.com/v2/adsb/health"
#define SKYLINK_RAPIDAPI_HOST    "x-rapidapi-host: skylink-api.p.rapidapi.com"
#define SKYLINK_RAPIDAPI_KEY     "x-rapidapi-key: "
#define SKYLINK_RAPIDAPI_RLIMIT  "x-ratelimit-requests-limit: "
#define SKYLINK_RAPIDAPI_RREMAIN "x-ratelimit-requests-remaining: "
#define SKYLINK_RAPIDAPI_RESET   "x-ratelimit-requests-reset: "

#define SKYLINK_AIRCRAFT_ARR     "aircraft"
#define SKYLINK_ERROR            "error"
#define SKYLINK_DETAIL           "detail"
#define RAPIDAPI_MESSAGE         "message"

// Field keys
#define SKYLINK_KEY_ICAO        "icao24"            // "3C66AE"
#define SKYLINK_CALL            "callsign"          // "DLH8EJ"
#define SKYLINK_LAT             "latitude"          // 50.040001
#define SKYLINK_LON             "longitude"         // 8.557518
#define SKYLINK_ALT             "altitude"          // 0
#define SKYLINK_SPD             "ground_speed"      // 139.864227
#define SKYLINK_TRACK           "track"             // 249.491898
#define SKYLINK_VSI             "vertical_rate"     // -768
#define SKYLINK_ON_GND          "is_on_ground"      // true
#define SKYLINK_TIMESTAMP       "last_seen"         // "2025-12-07T13:18:29.460833"
#define SKYLINK_REG             "registration"      // D-AIUN
#define SKYLINK_AC_TYPE_ICAO    "aircraft_type"     // A320
#define SKYLINK_AIRLINE         "airline"           // Lufthansa

#define SKYLINK_HEALTH_STATUS   "status"            // "healthy"
#define SKYLINK_HEALTH_CONNECT  "connected"         // true


// Testing an API key
#define ERR_SKYLINK_KEY_TECH     "SkyLink: Technical problem while testing key: %d - %s"
#define MSG_SKYLINK_KEY_SUCCESS  "SkyLink: API Key tested SUCCESSFULLY"
#define ERR_SKYLINK_KEY_FAILED   "SkyLink: API Key test FAILED: %s"
#define ERR_SKYLINK_KEY_UNKNOWN  "SkyLink: API Key test responded with unknown answer: %s"
#define ERR_SKYLINK_NO_KEY_DEF   "SkyLink: API Key missing. Get one at rapidapi.com/adsbx/api/adsbexchange-com1 and enter it in Basic Settings."
#define ERR_SKYLINK_OTHER        "SkyLink: Received HTTP error %ld: %s"


//
// MARK: SkyLink
//

class SkyLinkConnection : public LTFlightDataChannel
{
protected:
    std::string apiKey;
    struct curl_slist* slistKey = NULL;
public:
    SkyLinkConnection ();
    std::string GetURL (const positionTy& pos) override;
    std::string GetStatusText () const override;  ///< return a human-readable staus
protected:
    ///< virtual thread main function
    void Main () override;
    
    /// Process ADSBEx foramtted data
    bool ProcessFetchedData () override;
    /// Return content of 'detail'/'error' field, if any
    static std::string FetchDetail (const char* buf);

    // need to add/cleanup API key
    bool InitCurl () override;
    void CleanupCurl () override;
    
    // make list of HTTP header fields
    static struct curl_slist* MakeCurlSList (const std::string theKey);
    // read header and parse for request limit/remaining
    static size_t ReceiveHeader(char *buffer, size_t size, size_t nitems, void *userdata);
    
public:
    // Just quickly sends one simple request and checks if the response is not "NO KEY"
    // Does a SHOW_MSG about the result and saves the key to dataRefs on success.
    static void TestAPIKey (const std::string newKey);
    // Fetch result of last test, which is running in a separate thread
    // returns if the result is available. If available, actual result is returned in bIsKeyValid
    static bool TestAPIKeyResult (bool& bIsKeyValid);
protected:
    // actual test, blocks, should be called via std::async
    static bool DoTestAPIKey (const std::string newKey);
    static size_t DoTestAPIKeyCB (char *ptr, size_t, size_t nmemb, void* userdata);
};

#endif /* LTSkyLink_h */
