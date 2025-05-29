/// @file       LTOpenSky.h
/// @brief      OpenSky Network: Requests and processes live tracking and aircraft master data
/// @see        https://opensky-network.org/
/// @details    Defines OpenSkyConnection and OpenSkyAcMasterdata:\n
///             - Provides a proper REST-conform URL\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
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

#ifndef LTOpenSky_h
#define LTOpenSky_h

#include "LTChannel.h"

//MARK: OpenSky Constants
#define OPSKY_CHECK_NAME        "OpenSky Explorer"
#define OPSKY_CHECK_URL         "https://map.opensky-network.org/?lat=%.3f&lon=%.3f"
#define OPSKY_CHECK_POPUP       "Check OpenSky's coverage"

#define OPSKY_URL_GETTOKEN      "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
#define OPSKY_BODY_GETTOKEN     "grant_type=client_credentials&client_id=%s&client_secret=%s"
#define OPSKY_ACCESS_TOKEN      "access_token"
#define OPSKY_AUTH_BEARER       "Authorization: Bearer "
#define OPSKY_AUTH_EXPIRES      "expires_in"
constexpr long OPSKY_AUTH_EXP_DEFAULT = 1800;       ///< default expiration in case we don't find expiration field

#define OPSKY_NAME              "OpenSky Network"
#define OPSKY_URL_ALL           "https://opensky-network.org/api/states/all?lamin=%.3f&lomin=%.3f&lamax=%.3f&lomax=%.3f"
#define OPSKY_SLUG_FMT          "https://map.opensky-network.org/?icao=%06lx"
#define OPSKY_TIME              "time"
#define OPSKY_AIRCRAFT_ARR      "states"
#define OPSKY_RREMAIN           "x-rate-limit-remaining:"
#define OPSKY_RETRY             "x-rate-limit-retry-after-seconds:"
constexpr int OPSKY_TRANSP_ICAO   = 0;               // icao24
constexpr int OPSKY_CALL          = 1;               // callsign
constexpr int OPSKY_COUNTRY       = 2;               // origin_county
constexpr int OPSKY_POS_TIME      = 3;               // time_position
constexpr int OPSKY_LON           = 5;               // longitude
constexpr int OPSKY_LAT           = 6;               // latitude
constexpr int OPSKY_BARO_ALT      = 7;               // baro_altitude [m]
constexpr int OPSKY_GND           = 8;               // on_ground
constexpr int OPSKY_SPD           = 9;               // velocity
constexpr int OPSKY_HEADING       = 10;              // heading
constexpr int OPSKY_VSI           = 11;              // vertical rate
constexpr int OPSKY_RADAR_CODE    = 14;              // squawk

constexpr double OPSKY_SMOOTH_AIRBORNE = 65.0; // smooth 65s of airborne data
constexpr double OPSKY_SMOOTH_GROUND   = 35.0; // smooth 35s of ground data

//
//MARK: OpenSky
//
class OpenSkyConnection : public LTFlightDataChannel
{
protected:
    enum State {
        OPSKY_STATE_NONE = 0,                   ///< no/initial/unknown status
        OPSKY_STATE_GETTING_TOKEN,              ///< have credentials, but no access token yet
        OPSKY_STATE_GET_PLANES,                 ///< normal operations: fetch planes
    } eState = OPSKY_STATE_NONE;
    struct curl_slist* pHdrForm = nullptr;      ///< HTTP Header (needed during fetching a token)
    struct curl_slist* pHdrToken = nullptr;     ///< HTTP Header containing the bearer token
    float tTokenExpiration = NAN;               ///< when will the token expire?
public:
    OpenSkyConnection ();
    void ResetStatus ();                        ///< used to force fetching a new token, e.g. after change of credentials
    std::string GetURL (const positionTy& pos) override;
    void ComputeBody (const positionTy& pos) override;      ///< only needed for token request, will then form token request body
    bool ProcessFetchedData () override;
    std::string GetStatusText () const override;  ///< return a human-readable staus
//    // shall data of this channel be subject to LTFlightData::DataSmoothing?
//    bool DoDataSmoothing (double& gndRange, double& airbRange) const override
//    { gndRange = OPSKY_SMOOTH_GROUND; airbRange = OPSKY_SMOOTH_AIRBORNE; return true; }
    
    /// @brief Process OpenSKy's 'crendetials.json' file to fetch User ID/Secret from it
    static bool ProcessCredentialsJson (const std::string& sFileName,
                                        std::string& sClientId,
                                        std::string& sSecret);
    
protected:
    void Main () override;          ///< virtual thread main function

    bool InitCurl () override;
    // read header and parse for request remaining
    static size_t ReceiveHeader(char *buffer, size_t size, size_t nitems, void *userdata);

};

//
//MARK: OpenSky Master Data Constants
//
#define OPSKY_MD_CHECK_NAME     "OpenSky Aircraft Database"
#define OPSKY_MD_CHECK_URL      "https://opensky-network.org/aircraft-database"
#define OPSKY_MD_CHECK_POPUP    "Search and update OpenSky's databse of airframes"

constexpr std::chrono::duration OPSKY_WAIT_BETWEEN = std::chrono::milliseconds( 300);   ///< Wait between immediate requests to OpenSky Master
constexpr std::chrono::duration OPSKY_WAIT_NOQUEUE = std::chrono::milliseconds(3000);   ///< Wait if there is no request in the queue
#define OPSKY_MD_NAME           "OpenSky Masterdata Online"
#define OPSKY_MD_URL            "https://opensky-network.org/api/metadata/aircraft/icao/"
#define OPSKY_MD_TRANSP_ICAO    "icao24"
#define OPSKY_MD_COUNTRY        "country"
#define OPSKY_MD_MAN            "manufacturerName"
#define OPSKY_MD_MDL            "model"
#define OPSKY_MD_OP_ICAO        "operatorIcao"
#define OPSKY_MD_OP             "owner"
#define OPSKY_MD_REG            "registration"
#define OPSKY_MD_AC_TYPE_ICAO   "typecode"
#define OPSKY_MD_CAT_DESCR      "categoryDescription"
#define OPSKY_MD_TEXT_VEHICLE   "Surface Vehicle"
constexpr size_t OPSKY_MD_TEXT_VEHICLE_LEN = 20;    ///< length after which category description might contain useful text in case of a Surface Vehicle
#define OPSKY_MD_TEXT_NO_CAT    "No ADS-B Emitter Category Information"

#define OPSKY_ROUTE_URL         "https://opensky-network.org/api/routes?callsign="
#define OPSKY_ROUTE_CALLSIGN    "callsign"
#define OPSKY_ROUTE_ROUTE       "route"
#define OPSKY_ROUTE_OP_IATA     "operatorIata"
#define OPSKY_ROUTE_FLIGHT_NR   "flightNumber"

//
//MARK: OpenSkyAcMasterdata
//

/// Represents the OpenSky Master data channel, which requests aircraft master data and route information from OpenSky Networks
class OpenSkyAcMasterdata : public LTACMasterdataChannel
{
public:
    OpenSkyAcMasterdata ();                                         ///< Constructor sets channel, name, and URLs
public:
    std::string GetURL (const positionTy& pos) override;            ///< Returns the master data or route URL to query
    bool ProcessFetchedData () override;                            ///< Process received master or route data
protected:
    bool AcceptRequest (const acStatUpdateTy& requ) override;       ///< accept requests that aren't in the ignore lists
    void Main () override;                                          ///< virtual thread main function
    bool ProcessMasterData (JSON_Object* pJAc);                     ///< Process received aircraft master data
    bool ProcessRouteInfo (JSON_Object* pJRoute);                   ///< Process received route info
};

//
//MARK: OpenSkyAcMasterFile
//

#define OPSKY_MDF_NAME          "OpenSky Masterdata File"
#define OPSKY_MDF_URL           "https://s3.opensky-network.org/data-samples/metadata/"
#define OPSKY_MDF_FILE_BEGIN    "aircraft-database-complete-"
#define OPSKY_MDF_FILE          "aircraft-database-complete-%04d-%02d.csv"

// Field names of interest within the database file
#define OPSKY_MDF_HEXID         "icao24"
#define OPSKY_MDF_CATDESCR      "categoryDescription"
#define OPSKY_MDF_COUNTRY       "country"
#define OPSKY_MDF_MAN           "manufacturerName"
#define OPSKY_MDF_MANICAO       "manufacturerIcao"
#define OPSKY_MDF_MDL           "model"
#define OPSKY_MDF_OP            "operatorCallsign"
#define OPSKY_MDF_OWNER         "owner"
#define OPSKY_MDF_OPICAO        "operatorIcao"
#define OPSKY_MDF_REG           "registration"
#define OPSKY_MDF_ACTYPE        "typecode"

/// Every how many lines to we save file position information?
constexpr unsigned long OPSKY_NUM_LN_PER_POS = 250;

/// Represents downloading and reading from the OpenSky Master data file `aircraft-database-complete-YYYY-MM.csv`
class OpenSkyAcMasterFile : public LTACMasterdataChannel
{
protected:
    char sAcDbfileName[50] = {0};                                   ///< Aircraft Database file name
    std::ifstream fAcDb;                                            ///< Aircraft Database file
    std::string ln;                                                 ///< a line in the database file

    std::map<std::string, std::size_t> mapFieldPos;                 ///< map of field names to field indexes
    typedef std::map<unsigned long,std::ifstream::pos_type> mapPosTy;///< map of a/c ids to file positions
    std::size_t numFields = 0;                                      ///< number of fields expected in each row
    mapPosTy mapPos;                                                ///< map of a/c ids to file positions
public:
    OpenSkyAcMasterFile ();                                         ///< Constructor sets channel, name, and URLs
public:
    std::string GetURL (const positionTy&) override { return ""; }  ///< No URL for the standard request processing
    bool ProcessFetchedData () override;                            ///< Process looked up master data
    std::string GetStatusText () const override;                    ///< adds the database date to the status text
    protected:
    bool AcceptRequest (const acStatUpdateTy& requ) override;       ///< accept only master data requests
    void Main () override;                                          ///< virtual thread main function
    bool LookupData ();                                             ///< perform the file lookup
    bool OpenDatabaseFile ();                                       ///< find an aircraft database file to open/download
    bool TryOpenDbFile (int year, int month);                       ///< open/download the aircraft database file for the given month
};


#endif /* LTOpenSky_h */
