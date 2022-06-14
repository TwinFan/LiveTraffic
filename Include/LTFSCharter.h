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
#define FSC_CHECK_URL           "https://fscharter.net/flight-board"
#define FSC_CHECK_POPUP         "See who's flying in FSCharter just now"

#define FSC_NAME                "FSCharter"

#define FSC_BASE_URL            "https://%s/"
#define FSC_GET_TRAFFIC         "api/live-traffic"
#define FSC_LOGIN               "oauth/token"
#define FSC_CURR_FLIGHT         "flights?flight="      // + flightSlug

// HTTP headers to send
#define FSC_HEADER_JSON_SEND            "Content-Type: application/json"
#define FSC_HEADER_JSON_ACCEPT          "Accept: application/json"
#define FSC_HEADER_AUTHORIZATION        "Authorization: %s %s"

// Response fields
#define FSC_DATA_FLIGHTS                "data.flights"
#define FSC_FLIGHT_ID                   "id"
#define FSC_FLIGHT_REG_NO               "registration_number"
#define FSC_FLIGHT_ICAO                 "aircraft_icao"
#define FSC_FLIGHT_MANU                 "manufacturer"
#define FSC_FLIGHT_MODEL                "model"
#define FSC_FLIGHT_VARIANT              "variant"
#define FSC_FLIGHT_TS                   "timestamp"
#define FSC_FLIGHT_LAT                  "latitude"
#define FSC_FLIGHT_LON                  "longitude"
#define FSC_FLIGHT_HEADING              "heading"
#define FSC_FLIGHT_ALT_FT               "altitude"
#define FSC_FLIGHT_ON_GND               "on_gnd"
#define FSC_FLIGHT_COMPANY              "company"
#define FSC_FLIGHT_CO_ICAO              "company_icao"
#define FSC_FLIGHT_PILOT                "pilot"
#define FSC_FLIGHT_ROUTE_NO             "route_number"
#define FSC_FLIGHT_JOB_NO               "job_number"
#define FSC_FLIGHT_DEP                  "departure_ident"
#define FSC_FLIGHT_ARR                  "arrival_ident"
#define FSC_FLIGHT_SLUG                 "flight_slug"

//
//MARK: FSCharter
//
class FSCConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    /// FSC-specific connection status
    enum FSCStatusTy : int {
        FSC_STATUS_LOGIN_FAILED = -1,
        FSC_STATUS_NONE = 0,
        FSC_STATUS_LOGGING_IN,
        FSC_STATUS_OK,
    };
    
protected:
    std::string base_url;           ///< basis for all URL requests
    /// FSC-specific connection status
    FSCStatusTy fscStatus = FSC_STATUS_NONE;
    /// HTTP Header
    struct curl_slist* pCurlHeader = nullptr;
    /// The authentification token to be used in all requests (except for OAuth/token, which logs in and receives it)
    std::string token;
    /// The type of authentication token, typically "Bearer"
    std::string token_type;
    
    // error information
    std::string error_status;       ///< text of `status` tag in response, like "success" or "error"
    std::string error_message;      ///< text of `message` tag in error response
    long        error_code=0;       ///< value of `code` tag in error response

public:
    FSCConnection ();
    
    bool IsLiveFeed() const override { return true; }
    LTChannelType GetChType() const override { return CHT_TRACKING_DATA; }

    std::string GetStatusStr () const;              ///< Get FSC-specific status string
    std::string GetStatusText () const override;    ///< get status info, considering FSC-specific texts

    bool InitCurl () override;
    void CleanupCurl () override;
    std::string GetURL (const positionTy& pos) override;
    void ComputeBody (const positionTy& pos) override;
    bool ProcessFetchedData (mapLTFlightDataTy& fdMap) override;
    bool FetchAllData(const positionTy& pos) override { return LTOnlineChannel::FetchAllData(pos); }
//    // shall data of this channel be subject to LTFlightData::DataSmoothing?
//    virtual bool DoDataSmoothing (double& gndRange, double& airbRange) const
//    { gndRange = FSC_SMOOTH_GROUND; airbRange = FSC_SMOOTH_AIRBORNE; return true; }
    
    /// Extracts all error texts from `response` into the `error*` fields
    bool ExtractErrorTexts (const JSON_Object* pObj = nullptr);
    
    // do something while disabled?
    void DoDisabledProcessing () override;
    // (temporarily) close a connection, (re)open is with first call to FetchAll/ProcessFetchedData
    void Close () override;
    
protected:
    /// Remove all traces of login
    void ClearLogin ();
};


#endif /* LTFSCharter_h */
