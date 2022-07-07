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
#define OPSKY_CHECK_URL         "https://opensky-network.org/network/explorer"
#define OPSKY_CHECK_POPUP       "Check OpenSky's coverage"

#define OPSKY_NAME              "OpenSky Live Online"
#define OPSKY_URL_ALL           "https://opensky-network.org/api/states/all?lamin=%.3f&lomin=%.3f&lamax=%.3f&lomax=%.3f"
#define OPSKY_SLUG_FMT          "https://opensky-network.org/network/explorer?icao24=%06lx"
#define OPSKY_TIME              "time"
#define OPSKY_AIRCRAFT_ARR      "states"
#define OPSKY_RREMAIN           "X-Rate-Limit-Remaining:"
#define OPSKY_RETRY             "X-Rate-Limit-Retry-After-Seconds:"
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
class OpenSkyConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    OpenSkyConnection ();
    virtual std::string GetURL (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual bool FetchAllData(const positionTy& pos) { return LTOnlineChannel::FetchAllData(pos); }
    virtual std::string GetStatusText () const;  ///< return a human-readable staus
//    // shall data of this channel be subject to LTFlightData::DataSmoothing?
//    virtual bool DoDataSmoothing (double& gndRange, double& airbRange) const
//    { gndRange = OPSKY_SMOOTH_GROUND; airbRange = OPSKY_SMOOTH_AIRBORNE; return true; }
protected:
    virtual bool InitCurl ();
    // read header and parse for request remaining
    static size_t ReceiveHeader(char *buffer, size_t size, size_t nitems, void *userdata);

};

//MARK: OpenSky Master Data Constats
#define OPSKY_MD_CHECK_NAME     "OpenSky Aircraft Database"
#define OPSKY_MD_CHECK_URL      "https://opensky-network.org/aircraft-database"
#define OPSKY_MD_CHECK_POPUP    "Search and update OpenSky's databse of airframes"

constexpr double OPSKY_WAIT_BETWEEN = 0.5;          // seconds to pause between 2 requests
#define OPSKY_MD_NAME           "OpenSky Masterdata Online"
#define OPSKY_MD_URL            "https://opensky-network.org/api/metadata/aircraft/icao/"
#define OPSKY_MD_GROUP          "MASTER"        // made-up group of master data fields
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
#define OPSKY_MD_TEXT_NO_CAT    "No  ADS-B Emitter Category Information"
#define OPSKY_MD_MDL_UNKNOWN    "[?]"

#define OPSKY_ROUTE_URL         "https://opensky-network.org/api/routes?callsign="
#define OPSKY_ROUTE_GROUP       "ROUTE"         // made-up group of route information fields
#define OPSKY_ROUTE_CALLSIGN    "callsign"
#define OPSKY_ROUTE_ROUTE       "route"
#define OPSKY_ROUTE_OP_IATA     "operatorIata"
#define OPSKY_ROUTE_FLIGHT_NR   "flightNumber"

//
//MARK: OpenSkyAcMasterdata
//
class OpenSkyAcMasterdata : public LTOnlineChannel, LTACMasterdataChannel
{
protected:
    listStringTy invIcaos;          // list of not-to-query-again icaos
    listStringTy invCallSigns;      // list of not-to-query-again call signs
public:
    OpenSkyAcMasterdata ();
public:
    virtual bool FetchAllData (const positionTy& pos);
    virtual std::string GetURL (const positionTy& pos);
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_MASTER_DATA; }
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
};


#endif /* LTOpenSky_h */
