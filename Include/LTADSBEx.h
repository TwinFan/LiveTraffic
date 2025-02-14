/// @file       LTADSBEx.h
/// @brief      ADS-B Exchange and adsb.fi: Requests and processes live tracking data
/// @see        ADSBEx: https://www.adsbexchange.com/
///             RAPID API: https://rapidapi.com/adsbx/api/adsbexchange-com1
///             RAPID API Endpoint: https://rapidapi.com/adsbx/api/adsbexchange-com1/playground/endpoint_7dee5835-86b3-40ce-a402-f1ab43240884
///             ADSBEx v2 API documentation:
///             ...on Swagger: https://adsbexchange.com/api/aircraft/v2/docs
///             ...fields: https://www.adsbexchange.com/version-2-api-wip/
/// @see        adsb.fi: https://github.com/adsbfi/opendata
/// @details    Defines a base class handling the ADSBEx data format,
///             which is shared by both ADS-B Exchange and adsb.fi.
/// @details    Defines ADSBExchangeConnection:\n
///             - Handles the API key\n
///             - Provides a proper REST-conform URL for both the original sevrer as well as for the Rapid API server.
/// @details    Defines ADSBfiConnection:\n
///             - Provides a proper REST-conform URL
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2024 Birger Hoppe
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
#define ADSBEX_CHECK_NAME       "ADSBEx Radar View"
#define ADSBEX_CHECK_URL        "https://globe.adsbexchange.com/?lat=%.3f&lon=%.3f"
#define ADSBEX_SLUG_BASE        "https://globe.adsbexchange.com/?icao=" // + icao24 hex code
#define ADSBEX_CHECK_POPUP      "Check ADS-B Exchange's coverage"

#define ADSBEX_NAME             "ADS-B Exchange"
#define ADSBEX_RAPIDAPI_URL     "https://adsbexchange-com1.p.rapidapi.com/v2/lat/%f/lon/%f/dist/%d/"
#define ADSBEX_RAPIDAPI_HOST    "x-rapidapi-host: adsbexchange-com1.p.rapidapi.com"
#define ADSBEX_RAPIDAPI_KEY     "x-rapidapi-key: "
#define ADSBEX_RAPIDAPI_RLIMIT  "x-ratelimit-api-requests-limit: "
#define ADSBEX_RAPIDAPI_RREMAIN "x-ratelimit-api-requests-remaining: "
#define ADSBEX_RAPIDAPI_RESET   "x-ratelimit-api-requests-reset: "

#define ADSBEX_TOTAL            "total"
#define ADSBEX_NOW              "now"
#define ADSBEX_TIME             "ctime"
#define ADSBEX_AIRCRAFT_ARR     "ac"
#define ADSBEX_MSG              "msg"           ///< Error message text field according to documentation
#define ADSBEX_MESSAGE          "message"       ///< Error message text field we actually see in the responses

#define ADSBEX_SUCCESS          "No error"      ///< Content of 'msg' in case of success

// Version 2 keys
#define ADSBEX_V2_TRANSP_ICAO   "hex"           // Key data
#define ADSBEX_V2_TRANSP_TYPE   "type"          ///< type of transponder, or source of data, like "adsb_icao", "adsr_icao", or "tisb_other"
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

#define ADSBEX_V2_TYPE_TISB     "tisb_other"    ///< type value for TIS-B data

// Version 1 keys (only for enabling meaningful error message)
#define ADSBEX_V1_TRANSP_ICAO   "icao"          // Key data

// Testing an API key
#define ADSBEX_VERIFY_RAPIDAPI  "https://adsbexchange-com1.p.rapidapi.com/v2/lat/0.0/lon/0.0/dist/1/"

#define ERR_ADSBEX_KEY_TECH     "ADSBEx: Technical problem while testing key: %d - %s"
#define MSG_ADSBEX_KEY_SUCCESS  "ADS-B Exchange: API Key tested SUCCESSFULLY"
#define ERR_ADSBEX_KEY_FAILED   "ADS-B Exchange: API Key test FAILED: %s"
#define ERR_ADSBEX_KEY_UNKNOWN  "ADS-B Exchange: API Key test responded with unknown answer: %s"
#define ERR_ADSBEX_NO_KEY_DEF   "ADS-B Exchange: API Key missing. Get one at rapidapi.com/adsbx/api/adsbexchange-com1 and enter it in Basic Settings."
#define ERR_ADSBEX_OTHER        "ADS-B Exchange: Received an ERRor response: %s"

constexpr double ADSBEX_SMOOTH_AIRBORNE = 65.0; // smooth 65s of airborne data
constexpr double ADSBEX_SMOOTH_GROUND   = 35.0; // smooth 35s of ground data

//
// MARK: Base class for ADSBEx format
//

class ADSBBase : public LTFlightDataChannel
{
protected:
    const std::string sSlugBase;                ///< base URL for aircraft slugs
protected:
    ADSBBase (dataRefsLT ch, const char* chName, const char* slugBase) :
        LTFlightDataChannel(ch, chName), sSlugBase(slugBase) {}
    /// Process ADSBEx foramtted data
    bool ProcessFetchedData () override;
    /// Process v2 data
    void ProcessV2 (const JSON_Object* pJAc, LTFlightData::FDKeyTy& fdKey,
                    const double tBufPeriod, const double adsbxTime,
                    const positionTy& viewPos);
    /// Give derived class chance for channel-specific error-checking
    virtual bool ProcessErrors (const JSON_Object* pObj) = 0;
    /// Return the 'msg' content, if any
    static std::string FetchMsg (const char* buf);
};

//
// MARK: ADS-B Exchange
//
class ADSBExchangeConnection : public ADSBBase
{
protected:
    std::string apiKey;
    struct curl_slist* slistKey = NULL;
public:
    ADSBExchangeConnection ();
    std::string GetURL (const positionTy& pos) override;
    std::string GetStatusText () const override;  ///< return a human-readable staus
//    // shall data of this channel be subject to LTFlightData::DataSmoothing?
//    bool DoDataSmoothing (double& gndRange, double& airbRange) const override
//    { gndRange = ADSBEX_SMOOTH_GROUND; airbRange = ADSBEX_SMOOTH_AIRBORNE; return true; }
    
protected:
    void Main () override;          ///< virtual thread main function

    // need to add/cleanup API key
    bool InitCurl () override;
    void CleanupCurl () override;
    
    /// Specific handling for authentication errors
    bool ProcessErrors (const JSON_Object* pObj) override;

    // make list of HTTP header fields
    static struct curl_slist* MakeCurlSList (const std::string theKey);
    // read header and parse for request limit/remaining
    static size_t ReceiveHeader(char *buffer, size_t size, size_t nitems, void *userdata);
    
public:
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

//
// MARK: adsb.fi
//

#define ADSBFI_CHECK_NAME       "adsb.fi Map"
#define ADSBFI_CHECK_URL        "https://globe.adsb.fi/?lat=%.3f&lon=%.3f"
#define ADSBFI_SLUG_BASE        "https://globe.adsb.fi/?icao=" // + icao24 hex code
#define ADSBFI_CHECK_POPUP      "Check adsb.fi's coverage"

#define ADSBFI_NAME             "adsb.fi"
#define ADSBFI_URL              "https://opendata.adsb.fi/api/v2/lat/%f/lon/%f/dist/%d/"

#define ADSBFI_AIRCRAFT_ARR     "aircraft"

class ADSBfiConnection : public ADSBBase
{
public:
    ADSBfiConnection ();                                    ///< Constructor
    std::string GetURL (const positionTy& pos) override;    ///< Compile adsb.fi request URL

protected:
    void Main () override;                                  ///< virtual thread main function
    bool ProcessErrors (const JSON_Object*) override        ///< No specific error processing for adsb.fi
    { return true; }
};


#endif /* LTADSBEx_h */
