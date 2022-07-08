/// @file       LTADSBEx.h
/// @brief      ADS-B Exchange: Requests and processes live tracking data
/// @see        https://www.adsbexchange.com/
/// @details    Defines ADSBExchangeConnection:\n
///             - Handles the API key\n
///             - Provides a proper REST-conform URL for both the original sevrer as well as for the Rapid API server.\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
///             \n
///             ADSBExchangeHistorical is a definition for historic data that once could be downloaded
///             from ADSBEx, but is no longer available for the average user. This historic data code
///             is no longer maintained and probably defunct. It is no longer accessible through the
///             UI either and should probably be removed.
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

#ifndef LTADSBEx_h
#define LTADSBEx_h

#include "LTChannel.h"

//MARK: ADS-B Exchange Constants
#define ADSBEX_CHECK_NAME       "ADSBX Radar View"
#define ADSBEX_CHECK_URL        "https://globe.adsbexchange.com/?lat=%.3f&lon=%.3f"
#define ADSBEX_SLUG_BASE        "https://globe.adsbexchange.com/?icao=" // + icao24 hex code
#define ADSBEX_CHECK_POPUP      "Check ADS-B Exchange's coverage"

#define ADSBEX_NAME             "ADS-B Exchange Online"
#define ADSBEX_URL              "https://adsbexchange.com/api/aircraft/v2/lat/%f/lon/%f/dist/%d/"
#define ADSBEX_API_AUTH         "api-auth:"     // additional HTTP header

#define ADSBEX_RAPIDAPI_25_URL  "https://adsbx-flight-sim-traffic.p.rapidapi.com/api/aircraft/json/lat/%f/lon/%f/dist/25/"
#define ADSBEX_RAPIDAPI_HOST    "X-RapidAPI-Host:adsbx-flight-sim-traffic.p.rapidapi.com"
#define ADSBEX_RAPIDAPI_KEY     "X-RapidAPI-Key:"
#define ADSBEX_RAPIDAPI_RLIMIT  "X-RateLimit-Requests-Limit:"
#define ADSBEX_RAPIDAPI_RREMAIN "X-RateLimit-Requests-Remaining:"

#define ADSBEX_TOTAL            "total"
#define ADSBEX_NOW              "now"
#define ADSBEX_AIRCRAFT_ARR     "ac"

// Version 2 keys
#define ADSBEX_V2_TRANSP_ICAO   "hex"           // Key data
#define ADSBEX_V2_RADAR_CODE    "squawk"        // Dynamic data
#define ADSBEX_V2_FLIGHT        "flight"
#define ADSBEX_V2_LAT           "lat"
#define ADSBEX_V2_LON           "lon"
#define ADSBEX_V2_ALT_GEOM      "alt_geom"      // geometric altitude
#define ADSBEX_V2_ALT_BARO      "alt_baro"      // barometric altitude
#define ADSBEX_V2_NAV_QNH       "nav_qnh"       // QNH of barometric altitude
#define ADSBEX_V2_HEADING       "true_heading"
#define ADSBEX_V2_TRACK         "track"
#define ADSBEX_V2_SEE_POS       "seen_pos"
#define ADSBEX_V2_SPD           "gs"
#define ADSBEX_V2_VSI_GEOM      "geom_rate"
#define ADSBEX_V2_VSI_BARO      "baro_rate"
#define ADSBEX_V2_REG           "r"
#define ADSBEX_V2_AC_TYPE_ICAO  "t"
#define ADSBEX_V2_AC_CATEGORY   "category"
#define ADSBEX_V2_FLAGS         "dbFlags"

// Version 1 keys
#define ADSBEX_TIME             "ctime"
#define ADSBEX_V1_TRANSP_ICAO   "icao"          // Key data
#define ADSBEX_V1_RADAR_CODE    "sqk"           // Dynamic data
#define ADSBEX_V1_CALL          "call"
#define ADSBEX_V1_LAT           "lat"
#define ADSBEX_V1_LON           "lon"
#define ADSBEX_V1_ELEVATION     "galt"          // geometric altitude
#define ADSBEX_V1_ALT           "alt"           // barometric altitude
#define ADSBEX_V1_HEADING       "trak"
#define ADSBEX_V1_GND           "gnd"
#define ADSBEX_V1_POS_TIME      "postime"
#define ADSBEX_V1_SPD           "spd"
#define ADSBEX_V1_VSI           "vsi"
#define ADSBEX_V1_REG           "reg"
#define ADSBEX_V1_COUNTRY       "cou"
#define ADSBEX_V1_AC_TYPE_ICAO  "type"
#define ADSBEX_V1_MIL           "mil"
#define ADSBEX_V1_OP_ICAO       "opicao"
#define ADSBEX_V1_ORIGIN        "from"
#define ADSBEX_V1_DESTINATION   "to"

#define ADSBEX_V1_TYPE_GND      "-GND"


// Testing an API key
#define ADSBEX_VERIFY_KEY_URL   "https://adsbexchange.com/api/aircraft/icao/000000"
#define ADSBEX_ERR              "msg"
#define ADSBEX_SUCCESS          "No error"
#define ADSBEX_NO_API_KEY       "You need an authorized API key."

#define ADSBEX_VERIFY_RAPIDAPI  "https://adsbx-flight-sim-traffic.p.rapidapi.com/api/aircraft/json/lat/0.0/lon/0.0/dist/25/"
#define ADSBEX_RAPID_ERR        "message"
#define ADSBEX_NO_RAPIDAPI_KEY  "Key doesn't exists"

#define ERR_ADSBEX_KEY_TECH     "ADSBEx: Technical problem while testing key: %d - %s"
#define MSG_ADSBEX_KEY_SUCCESS  "ADS-B Exchange: API Key tested SUCCESSFULLY"
#define ERR_ADSBEX_KEY_FAILED   "ADS-B Exchange: API Key INVALID"
#define ERR_ADSBEX_KEY_UNKNOWN  "ADS-B Exchange: API Key test responded with unknown answer"
#define ERR_ADSBEX_NO_KEY_DEF   "ADS-B Exchange: API Key missing. Get one at adsbexchange.com and enter it in Basic Settings."
#define ERR_ADSBEX_OTHER        "ADS-B Exchange: Received an ERRor response: %s"

constexpr double ADSBEX_SMOOTH_AIRBORNE = 65.0; // smooth 65s of airborne data
constexpr double ADSBEX_SMOOTH_GROUND   = 35.0; // smooth 35s of ground data

//
//MARK: ADS-B Exchange
//
class ADSBExchangeConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    enum keyTypeE { ADSBEX_KEY_NONE=0, ADSBEX_KEY_EXCHANGE, ADSBEX_KEY_RAPIDAPI };
    
protected:
    std::string apiKey;
    keyTypeE keyTy = ADSBEX_KEY_NONE;
    struct curl_slist* slistKey = NULL;
public:
    ADSBExchangeConnection ();
    virtual std::string GetURL (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual bool FetchAllData(const positionTy& pos) { return LTOnlineChannel::FetchAllData(pos); }
    virtual std::string GetStatusText () const;  ///< return a human-readable staus
//    // shall data of this channel be subject to LTFlightData::DataSmoothing?
//    virtual bool DoDataSmoothing (double& gndRange, double& airbRange) const
//    { gndRange = ADSBEX_SMOOTH_GROUND; airbRange = ADSBEX_SMOOTH_AIRBORNE; return true; }
    
protected:
    // need to add/cleanup API key
    virtual bool InitCurl ();
    virtual void CleanupCurl ();
    
    /// Process v2 data
    void ProcessV2 (JSON_Object* pJAc, LTFlightData::FDKeyTy& fdKey,
                    mapLTFlightDataTy& fdMap,
                    const double tsCutOff, const double adsbxTime,
                    const positionTy& viewPos);
    /// Process v1 data
    void ProcessV1 (JSON_Object* pJAc, LTFlightData::FDKeyTy& fdKey,
                    mapLTFlightDataTy& fdMap,
                    const double tsCutOff, const double adsbxTime,
                    const positionTy& viewPos);

    // make list of HTTP header fields
    static struct curl_slist* MakeCurlSList (keyTypeE keyTy, const std::string theKey);
    // read header and parse for request limit/remaining
    static size_t ReceiveHeader(char *buffer, size_t size, size_t nitems, void *userdata);
    
public:
    static keyTypeE GetKeyType (const std::string theKey);
    // Just quickly sends one simple request to ADSBEx and checks if the response is not "NO KEY"
    // Does a SHOW_MSG about the result and saves the key to dataRefs on success.
    static void TestADSBExAPIKey (const std::string newKey);
    // Fetch result of last test, which is running in a separate thread
    // returns if the result is available. If available, actual result is returned in bIsKeyValid
    static bool TestADSBExAPIKeyResult (bool& bIsKeyValid);
protected:
    // actual test, blocks, should by called via std::async
    static bool DoTestADSBExAPIKey (const std::string newKey);
    static size_t DoTestADSBExAPIKeyCB (char *ptr, size_t, size_t nmemb, void* userdata);
};


#endif /* LTADSBEx_h */
