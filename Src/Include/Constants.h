//
//  Constants.h
//  LiveTraffic

/*
 * Copyright (c) 2018, Birger Hoppe
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

#ifndef Constants_h
#define Constants_h

//
// MARK: Version Information (CHANGE VERSION HERE)
//
constexpr float VERSION_NR = 0.92f;
constexpr bool VERSION_BETA = true;

//MARK: Window Position
#define WIN_WIDTH       400         // window width
#define WIN_ROW_HEIGHT   20         // height of line of text
#define WIN_FROM_TOP     50
#define WIN_FROM_RIGHT    0

constexpr int WIN_TIME_DISPLAY=8;       // duration of displaying a message windows

//MARK: Unit conversions
constexpr int M_per_NM      = 1852;     // meter per 1 nautical mile = 1/60 of a lat degree
constexpr double M_per_FT   = 0.3048;   // meter per 1 foot
constexpr int M_per_KM      = 1000;
constexpr double KT_per_M_per_S = 1.94384;  // 1m/s = 1.94384kt
constexpr int SEC_per_M     = 60;       // 60 seconds per minute
constexpr int SEC_per_H     = 3600;     // 3600 seconds per hour
constexpr int H_per_D       = 24;       // 24 hours per day
constexpr int M_per_D       = 1440;     // 24*60 minutes per day
constexpr int SEC_per_D     = SEC_per_H * H_per_D;        // seconds per day
constexpr double Ms_per_FTm = M_per_FT / SEC_per_M;     //1 m/s = 196.85... ft/min
constexpr double PI         = 3.1415926535897932384626433832795028841971693993751;
constexpr double EARTH_D_M  = 6371.0 * 2 * 1000;    // earth diameter in meter

//MARK: Flight Data-related
constexpr double FLIGHT_LOOP_INTVL  = -5.0;     // call ourselves every 5 frames
constexpr double AC_MAINT_INTVL     = 2.0;      // seconds (calling a/c maintenance periodically)
constexpr double TIME_REQU_POS      = 0.5;      // seconds before reaching current 'to' position we request calculation of next position
constexpr double SIMILAR_TS_INTVL = 3;          // seconds: Less than that difference and position-timestamps are considered "similar" -> positions are merged rather than added additionally
constexpr double SIMILAR_POS_DIST = 3;          // [m] if distance between positions less than this then favor heading from flight data over vector between positions
constexpr double FD_GND_CHECK_AGL = 300;        // [ft] if pos is that close to terrain alt but grndStatus OFF then double-check using YProbe
constexpr double FD_GND_AGL =       10;         // [ft] consider pos 'ON GRND' if this close to YProbe
constexpr double PROBE_HEIGHT_LIM[] = {5000,1000,500,-999999};  // if height AGL is more than ... feet
constexpr double PROBE_DELAY[]      = {  10,   1,0.5,    0.2};  // delay next Y-probe ... seconds.

//MARK: Flight Model
constexpr double MDL_ALT_MIN =         -1500;   // [ft] minimum allowed altitude
constexpr double MDL_ALT_MAX =          60000;  // [ft] maximum allowed altitude
constexpr double MDL_CLOSE_TO_GND =     0.5;    // feet height considered "on ground"
constexpr double MDL_CLOSE_TO_GND_SLOW = 25;    // feet height considered "on ground" if moving with less than max taxi speed
constexpr double MDL_MAX_TURN       =    90;    // max turn in flight at a position
constexpr double MDL_MAX_TURN_GND   =   120;    // max turn on the ground
#define MDL_LABEL_COLOR         "LABEL_COLOR"

constexpr int COLOR_YELLOW      = 0xFFFF00;
constexpr int COLOR_RED         = 0xFF0000;
constexpr int COLOR_GREEN       = 0x00FF00;
constexpr int COLOR_BLUE        = 0x00F0F0;     // light blue

//MARK: Version Information
extern char LT_VERSION[];               // like "1.0"
extern char LT_VERSION_FULL[];          // like "1.0.181231" with last digits being build date
extern char HTTP_USER_AGENT[];          // like "LiveTraffic/1.0"
extern time_t LT_BETA_VER_LIMIT;        // BETA versions are limited
extern char LT_BETA_VER_LIMIT_TXT[];
#define BETA_LIMITED_VERSION    "BETA limited to %s"
#define BETA_LIMITED_EXPIRED    "BETA-Version limited to %s has EXPIRED -> SHUTTING DOWN!"

//MARK: Text Constants
#define LIVE_TRAFFIC            "LiveTraffic"
#define LT_CFG_VERSION          "1.0"        // version of config file format
#define LT_FM_VERSION           "1.0"        // version of flight model file format
#define PLUGIN_SIGNATURE        "TwinFan.plugin.LiveTraffic"
#define PLUGIN_DESCRIPTION      "Create Multiplayer Aircrafts based on live traffic."
#define MSG_DISABLED            "Disabled"
#define MSG_STARTUP             "LiveTraffic %s starting up..."
#define MSG_WELCOME             "LiveTraffic %s successfully loaded!"
#define MSG_REQUESTING_LIVE_FD  "Requesting live flight data online..."
#define MSG_READING_HIST_FD     "Reading historic flight data..."
#define MSG_NUM_AC_INIT         "Initially created %d aircrafts"
#define MSG_NUM_AC_ZERO         "No more aircrafts displayed"
#define MSG_BUF_FILL_COUNTDOWN  "Filling buffer: seeing %d aircrafts, displaying %d, still %d seconds to buffer"
#define MSG_HIST_WITH_SYS_TIME  "When using historic data you cannot run X-Plane with 'always track system time',\ninstead, choose the historic date in X-Plane's date/time settings."
#define INFO_AC_ADDED           "Added aircraft %s, operator '%s', a/c model '%s', flight model [%s], bearing %.0f, distance %.1fkm"
#define INFO_AC_REMOVED         "Removed aircraft %s"
#define INFO_AC_ALL_REMOVED     "Removed all aircrafts"
#define INFO_WND_AUTO_AC        "AUTO"
#define MSG_TOO_MANY_AC         "Reached limit of %d aircrafts, will create new ones only after removing outdated ones."
#define MSG_CSL_PACKAGE_LOADED  "Successfully loaded CSL package %s"
#define MSG_MDL_FIXATED         "Settings > Debug: Model matching fixated to '%s'/'%s'/'%s'"
#define MSG_MDL_NOT_FIXATED     "Settings > Debug: Model matching no longer fixated"
#define WHITESPACE              " \t\f\v\r\n"
#define CSL_DEFAULT_ICAO_TYPE   "A320"
#define CSL_CAR_ICAO_TYPE       "ZZZC"      // fake code for a ground vehicle
#define FM_MAP_SECTION          "Map"
#define FM_PARENT_SEPARATOR     ":"
#define CFG_CSL_SECTION         "[CSLPaths]"
#define CFG_DEFAULT_AC_TYPE     "DEFAULT_AC_TYPE"
#define CFG_DEFAULT_CAR_TYPE    "DEFAULT_CAR_TYPE"
#define CFG_DEFAULT_AC_TYP_INFO "Default a/c type is '%s'"
#define CFG_DEFAULT_CAR_TYP_INFO "Default car type is '%s'"
#define XPPRF_RENOPT_HDR        "renopt_HDR"					// XP10
#define XPPRF_EFFECTS_04		"renopt_effects_04"				// XP11, if >= 3 then includes HDR
#define XPPRF_RENOPT_HDR_ANTIAL "renopt_HDR_antial"

//MARK: Menu Items
#define MENU_TOGGLE_AIRCRAFTS   "Aircrafts displayed"
#define MENU_AC_INFO_WND        "Aircraft Info..."
#define MENU_AC_INFO_WND_AUTO   "Aircraft Info (Auto-select)"
#define MENU_SETTINGS_UI        "Settings..."
#define MENU_RELOAD_PLUGINS     "Reload all Plugins (Caution!)"

//MARK: File Paths
// these are under the plugins directory
#define PATH_FLIGHT_MODELS      "Resources/FlightModels.prf"
#define PATH_RELATED_TXT        "Resources/related.txt"
#define PATH_LIGHTS_PNG         "Resources/lights.png"
#define PATH_DOC8643_TXT        "Resources/Doc8643.txt"
#define PATH_RESOURCES_CSL      "Resources/CSL"
#define PATH_RESOURCES_SCSL     "Resources/ShippedCSL"
// these are under X-Plane's root dir
#define PATH_DEBUG_RAW_FD       "LTRawFD.log"   // this is under X-Plane's system dir
#define PATH_RES_PLUGINS        "Resources/plugins"
#define PATH_CONFIG_FILE        "Output/preferences/LiveTraffic.prf"
#define PATH_XPLANE_PRF         "Output/preferences/X-Plane.prf"

//MARK: Error Texsts
constexpr long HTTP_OK =            200;
constexpr long HTTP_BAD_REQUEST =   400;
constexpr long HTTP_NOT_FOUND =     404;
constexpr long HTTP_NOT_AVAIL =     503;        // "Service not available"
constexpr int CH_MAC_ERR_CNT =      5;          // max number of tolerated errors, afterwards invalid channel
constexpr int SERR_LEN = 100;                   // size of buffer for IO error texts (strerror_s) 
#define ERR_INIT_XPMP           "Could not initialize XPMPMultiplayer: %s"
#define ERR_LOAD_CSL            "Could not load CSL Package: %s"
#define ERR_XPMP_ENABLE         "Could not enable XPMPMultiplayer: %s"
#define ERR_XPMP_ADD_CSL        "Could not add additional CSL package: %s"
#define ERR_APPEND_MENU_ITEM    "Could not append menu item"
#define ERR_CREATE_MENU         "Could not create menu"
#define ERR_CURL_INIT           "Could not initialize CURL: %s"
#define ERR_CURL_EASY_INIT      "Could not initialize easy CURL"
#define ERR_CURL_PERFORM        "%s: Could not get network data: %d - %s"
#define ERR_CURL_HTTP_RESP      "%s: HTTP response is not OK but %ld"
#define ERR_CURL_REVOKE_MSG     "0x80092012"                // appears in error text if querying revocation list fails
#define ERR_CURL_DISABLE_REV_QU "%s: Querying revocation list failed - have set CURLSSLOPT_NO_REVOKE and am trying again"
#define ERR_CH_UNKNOWN_NAME     "(unknown channel)"
#define ERR_CH_INVALID          "%s: Channel invalid and disabled"
#define ERR_CH_MAX_ERR_INV      "%s: Channel invalid and disabled after too many errors"
#define ERR_CH_INV_DATA         "%s: Data for '%s' (man '%s', mdl '%s') lacks ICAO a/c type code, will be rendered with standard a/c %s"
#define ERR_DATAREF_FIND        "Could not find DataRef: %s"
#define ERR_DATAREF_ACCESSOR    "Could not register accessor for DataRef: %s"
#define ERR_DIR_CONTENT         "Could not retrieve directory content for %s"
#define ERR_JSON_PARSE          "Parsing flight data as JSON failed"
#define ERR_JSON_MAIN_OBJECT    "JSON: Getting main object failed"
#define ERR_JSON_ACLIST         "JSON: List of aircrafts (%s) not found"
#define ERR_JSON_AC             "JSON: Could not get %d. aircraft in '%s'"
#define ERR_NEW_OBJECT          "Could not create new object (memory?): %s"
#define ERR_LOCK_ERROR          "Could not acquire lock for '%s': %s"
#define ERR_MALLOC              "Could not (re)allocate %ld bytes of memory"
#define ERR_ASSERT              "ASSERT FAILED: %s"
#define ERR_AC_NO_POS           "No positional data available when creating aircraft %s"
#define ERR_AC_CALC_PPOS        "Could calculate position when creating aircraft %s"
#define ERR_Y_PROBE             "Y Probe returned %d at %s"
#define ERR_POS_UNNORMAL        "A/c %s reached invalid pos: %s"
#define ERR_IGNORE_POS          "A/c %s: Ignoring data leading to sharp turn or invalid speed: %s"
#define ERR_INV_TRANP_ICAO      "Ignoring data for invalid transponder code '%s'"
#define ERR_TIME_NONLINEAR      "Time moved non-linear/jumped by %.1f seconds, will re-init aircrafts."
#define ERR_TOP_LEVEL_EXCEPTION "Caught top-level exception! %s"
#define ERR_WIDGET_CREATE       "Could not create widget required for settings UI"
#define ERR_CFG_FILE_OPEN_OUT   "Could not create config file '%s': %s"
#define ERR_CFG_FILE_WRITE      "Could not write into config file '%s': %s"
#define ERR_CFG_FILE_OPEN_IN    "Could not open '%s': %s"
#define ERR_CFG_FILE_VER        "Config file '%s' first line: Wrong format or version"
#define ERR_CFG_FILE_IGNORE     "Ignoring unkown entry '%s' from config file '%s'"
#define ERR_CFG_FILE_WORDS      "Expected two words (key, value) in config file '%s', line '%s': ignored"
#define ERR_CFG_FILE_READ       "Could not read from '%s': %s"
#define ERR_CFG_LINE_READ       "Could not read from file '%s', line %d: %s"
#define ERR_CFG_FILE_TOOMANY    "Too many warnings"
#define ERR_CFG_FILE_VALUE      "%s: Could not convert '%s' to a number: %s"
#define ERR_CFG_FORMAT          "Format mismatch in '%s', line %d: %s"
#define ERR_CFG_VAL_INVALID     "Value invalid in '%s', line %d: %s"
#define ERR_CFG_CSL_INVALID     "CSL Path config invalid in '%s': '%s'"
#define ERR_CFG_CSL_DISABLED    "CSL Path '%s' disabled, skipping"
#define ERR_CFG_CSL_EMPTY       "CSL Path '%s' does not exist or is empty, skipping"
#define ERR_CFG_CSL_NONE        "No valid CSL Paths configured, verify Settings > CSL!"
#define ERR_CFG_AC_DEFAULT      "A/c default ICAO type '%s' invalid, still using '%s' as default. Verify Settings > CSL!"
#define ERR_CFG_CAR_DEFAULT     "Car default ICAO type '%s' invalid, still using '%s' as default. Verify Settings > CSL!"
#define ERR_FM_NOT_AFTER_MAP    "Remainder after [Map] section ignored"
#define ERR_FM_NOT_BEFORE_SEC   "Lines before first section ignored"
#define ERR_FM_UNKNOWN_NAME     "Unknown parameter in '%s', line %d: %s"
#define ERR_FM_UNKNOWN_SECTION  "Referring to unknown model section in '%s', line %d: %s"
#define ERR_FM_UNKNOWN_PARENT   "Parent section missing in '%s', line %d: %s"
#define ERR_FM_REGEX            "%s in '%s', line %d: %s"
constexpr int ERR_CFG_FILE_MAXWARN = 5;     // maximum number of warnings while reading config file, then: dead

//MARK: OpenSky
#define OPSKY_NAME              "OpenSky Live Online"
#define OPSKY_URL_ALL           "https://opensky-network.org/api/states/all?lamin=%.3f&lomin=%.3f&lamax=%.3f&lomax=%.3f"
#define OPSKY_AIRCRAFT_ARR      "states"
constexpr int OPSKY_TRANSP_ICAO   = 0;               // icao24
constexpr int OPSKY_CALL          = 1;               // callsign
constexpr int OPSKY_COUNTRY       = 2;               // origin_county
constexpr int OPSKY_POS_TIME      = 3;               // time_position
constexpr int OPSKY_LON           = 5;               // longitude
constexpr int OPSKY_LAT           = 6;               // latitude
constexpr int OPSKY_GND           = 8;               // on_ground
constexpr int OPSKY_SPD           = 9;               // velocity
constexpr int OPSKY_HEADING       = 10;              // heading
constexpr int OPSKY_VSI           = 11;              // vertical rate
constexpr int OPSKY_ELEVATION     = 13;              // geo_altitude
constexpr int OPSKY_RADAR_CODE    = 14;              // squawk

//MARK: OpenSky Master Data
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
#define OPSKY_MD_TEX_NO_CAT		"No ADS-B Emitter Category Information"

#define OPSKY_ROUTE_URL         "https://opensky-network.org/api/routes?callsign="
#define OPSKY_ROUTE_GROUP       "ROUTE"         // made-up group of route information fields
#define OPSKY_ROUTE_CALLSIGN    "callsign"
#define OPSKY_ROUTE_ROUTE       "route"
#define OPSKY_ROUTE_OP_IATA     "operatorIata"
#define OPSKY_ROUTE_FLIGHT_NR   "flightNumber"


//MARK: ADS-B Exchange
#define ADSBEX_NAME             "ADSB Exchange Live Online"
#define ADSBEX_URL_ALL          "https://public-api.adsbexchange.com/VirtualRadar/AircraftList.json?lat=%f&lng=%f&fDstU=%d"
#define ADSBEX_URL_AC           "https://public-api.adsbexchange.com/VirtualRadar/AircraftList.json?fIcoQ=%s"
#define ADSBEX_AIRCRAFT_ARR     "acList"
#define ADSBEX_TRANSP_ICAO      "Icao"          // Key data
#define ADSBEX_TRT              "Trt"
#define ADSBEX_RCVR             "Rcvr"
#define ADSBEX_SIG              "Sig"
#define ADSBEX_RADAR_CODE       "Sqk"           // Dynamic data
#define ADSBEX_CALL             "Call"
#define ADSBEX_C_MSG            "CMsgs"
#define ADSBEX_LAT              "Lat"
#define ADSBEX_LON              "Long"
#define ADSBEX_ELEVATION        "GAlt"
#define ADSBEX_HEADING          "Trak"
#define ADSBEX_GND              "Gnd"
#define ADSBEX_IN_HG            "InHg"
#define ADSBEX_POS_TIME         "PosTime"
#define ADSBEX_POS_STALE        "PosStale"
#define ADSBEX_BRNG             "Brng"
#define ADSBEX_DST              "Dst"
#define ADSBEX_SPD              "Spd"
#define ADSBEX_VSI              "Vsi"
#define ADSBEX_REG              "Reg"
#define ADSBEX_COUNTRY          "Cou"
#define ADSBEX_AC_TYPE_ICAO     "Type"
#define ADSBEX_MAN              "Man"
#define ADSBEX_MDL              "Mdl"
#define ADSBEX_YEAR             "Year"
#define ADSBEX_MIL              "Mil"
#define ADSBEX_OP               "Op"
#define ADSBEX_OP_ICAO          "OpIcao"
#define ADSBEX_COS              "Cos"               // array of short trails
#define ADSBEX_ENG_TYPE         "EngType"
#define ADSBEX_ENG_MOUNT        "EngMount"
#define ADSBEX_ORIGIN           "From"
#define ADSBEX_DESTINATION      "To"

#define ADSBEX_HIST_NAME        "ADSB Exchange Historic File"
constexpr int ADSBEX_HIST_MIN_CHARS   = 20;             // minimum nr chars per line to be a 'reasonable' line
constexpr int ADSBEX_HIST_MAX_ERR_CNT = 5;              // after that many errorneous line we stop reading
#define ADSBEX_HIST_PATH        "Custom Data/ADSB"  // TODO: Move to options: relative to XP main
#define ADSBEX_HIST_PATH_2      "Custom Data/ADSB2" // TODO: Move to options: fallback, if first one doesn't work
#define ADSBEX_HIST_DATE_PATH   "%c%04d-%02d-%02d"
#define ADSBEX_HIST_FILE_NAME   "%c%04d-%02d-%02d-%02d%02dZ.json"
#define ADSBEX_HIST_PATH_EMPTY  "Historic Data Path doesn't exist or folder empty at %s"
#define ADSBEX_HIST_TRY_FALLBACK "Trying fallback as primary Historic Data Path doesn't exist or folder empty at %s"
#define ADSBEX_HIST_FALLBACK_EMPTY  "Also fallback Historic Data Path doesn't exist or folder empty at %s"
#define ADSBEX_HIST_FILE_ERR    "Could not open historic file '%s': %s"
#define ADSBEX_HIST_READ_FILE   "Reading from historic file %s"
#define ADSBEX_HIST_LN1_END     "\"acList\":["      // end of first line
#define ADSBEX_HIST_LAT         "\"Lat\":"          // latitude tag
#define ADSBEX_HIST_LONG        "\"Long\":"         // longitude tag
#define ADSBEX_HIST_COS         "\"Cos\":["         // start of short trails
#define ADSBEX_HIST_LAST_LN     "]"                 // begin of last line
#define ADSBEX_HIST_LN1_UNEXPECT "First line doesn't look like hist file: %s"
#define ADSBEX_HIST_LN_ERROR    "Error reading line %d of hist file %s"
#define ADSBEX_HIST_TRAIL_ERR   "Trail data not quadrupels (%s @ %f)"
#define ADSBEX_HIST_START_FILE  "START OF FILE "
#define ADSBEX_HIST_END_FILE    "END OF FILE "

//MARK: Debug Texts
#define DBG_MENU_CREATED        "Menu created"
#define DBG_WND_CREATED_UNTIL   "Created window, display until total running time %.2f, for text: %s"
#define DBG_WND_DESTROYED       "Window destroyed"
#define DBG_LT_MAIN_INIT        "LTMainInit initialized"
#define DBG_LT_MAIN_ENABLE      "LTMainEnable enabled"
#define DBG_MAP_DUP_INSERT      "Duplicate insert into LTAircraftMap with key %s"
#define DBG_SENDING_HTTP        "%s: Sending HTTP: %s"
#define DBG_RECEIVED_BYTES      "%s: Received %ld characters"
#define DBG_RAW_FD_START        "DEBUG Starting to log raw flight data to %s"
#define DBG_RAW_FD_STOP         "DEBUG Stopped logging raw flight data to %s"
#define DBG_RAW_FD_ERR_OPEN_OUT "DEBUG Could not open output file %s: %s"
#define DBG_FILTER_AC           "DEBUG Filtering for a/c '%s'"
#define DBG_FILTER_AC_REMOVED   "DEBUG Filtering for a/c REMOVED"
#define DBG_MERGED_POS          "DEBUG MERGED POS %s into updated TS %.1f"
#define DBG_POS_DATA            "DEBUG POS DATA: %s"
#define DBG_NO_MORE_POS_DATA    "DEBUG NO MORE LIVE POS DATA: %s"
#define DBG_SKIP_NEW_POS        "DEBUG SKIPPED NEW POS: %s"
#define DBG_INVENTED_STOP_POS   "DEBUG INVENTED STOP POS: %s"
#define DBG_INVENTED_TD_POS     "DEBUG INVENTED TOUCH-DOWN POS: %s"
#define DBG_INVENTED_TO_POS     "DEBUG INVENTED TAKE-OFF POS: %s"
#define DBG_INV_POS_REMOVED     "DEBUG %s: Removed an invalid position: %s"
#define DBG_INV_POS_AC_REMOVED  "DEBUG %s: Removed a/c due to invalid positions"
#define DBG_AC_SWITCH_POS       "DEBUG A/C SWITCH POS: %s"
#define DBG_AC_FLIGHT_PHASE     "DEBUG A/C FLIGHT PHASE CHANGED from %i %s to %i %s"
#define DBG_AC_CHANNEL_SWITCH   "DEBUG %s: SWITCHED CHANNEL from '%s' to '%s'"
#ifdef DEBUG
#define DBG_DEBUG_BUILD         "DEBUG BUILD with additional run-time checks and no optimizations"
#endif

#endif /* Constants_h */
