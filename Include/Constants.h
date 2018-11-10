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

//MARK: Window Position
#define WIN_WIDTH       400         // window width
#define WIN_ROW_HEIGHT   20         // height of line of text
#define WIN_FROM_TOP     50
#define WIN_FROM_RIGHT    0

const float WIN_TIME_DISPLAY=8;     // duration of displaying a message windows

//MARK: Unit conversions
const int M_per_NM      = 1852;     // meter per 1 nautical mile = 1/60 of a lat degree
const double M_per_FT   = 0.3048;   // meter per 1 foot
const int M_per_KM      = 1000;
const double KT_per_M_per_S = 1.94384;  // 1m/s = 1.94384kt
const int SEC_per_M     = 60;       // 60 seconds per minute
const int SEC_per_H     = 3600;     // 3600 seconds per hour
const int H_per_D       = 24;       // 24 hours per day
const int M_per_D       = 1440;     // 24*60 minutes per day
const int SEC_per_D     = SEC_per_H * H_per_D;        // seconds per day
const double Ms_per_FTm = M_per_FT / SEC_per_M;     //1 m/s = 196.85... ft/min
const double PI         = 3.1415926535897932384626433832795028841971693993751;
const double EARTH_D_M  = 6371.0 * 2 * 1000;    // earth diameter in meter

//MARK: Flight Data-related
const double FLIGHT_LOOP_INTVL  = 2.0;      // seconds (calling a/c maintenance periodically)
const double TIME_REQU_POS      = 0.5;      // seconds before reaching current 'to' position we request calculation of next position

//const int MAX_NUM_AC =          50;         // how many aircrafts to create at most?
//const int FD_STD_DISTANCE =     25;         // kilometer to look for a/c around myself
//const int FD_REFRESH_INTVL =    20;         // how often to fetch new flight data
//const int FD_BUF_PERIOD =       90;         // seconds to buffer before simulating aircrafts
//const int AC_OUTDATED_INTVL =   50;         // a/c considered outdated if latest flight data more older than this compare to 'now'
const double SIMILAR_TS_INTVL = 5;          // seconds: Less than that difference and position-timestamps are considered "similar" -> positions are merged rather than added additionally
const double SIMILAR_POS_DIST = 5;          // [m] if distance between positions less than this then favor heading from flight data over vector between positions
const double FD_GND_CHECK_AGL = 300;        // [ft] if pos is that close to terrain alt but grndStatus OFF then double-check using YProbe
const double FD_GND_AGL =       10;         // [ft] consider pos 'ON GRND' if this close to YProbe
const double PROBE_HEIGHT_LIM[] = {5000,1000,500,-999999};  // if height AGL is more than ... feet
const double PROBE_DELAY[]      = {  10,   1,0.5,    0.2};  // delay next Y-probe ... seconds.

//MARK: Flight Model
const double MDL_ALT_MIN =         -1500;   // [ft] minimum allowed altitude
const double MDL_ALT_MAX =          60000;  // [ft] maximum allowed altitude
const double MDL_CLOSE_TO_GND =     0.5;    // feet height considered "on ground"

//MARK: Text Constants
#define LIVE_TRAFFIC            "LiveTraffic"
#define LT_VERSION              "1.0"
#define PLUGIN_SIGNATURE        "TwinFan.plugin.LiveTraffic"
#define PLUGIN_DESCRIPTION      "Create Multiplayer Aircrafts based on live traffic."
#define MSG_DISABLED            "Disabled"
#define MSG_WELCOME             "LiveTraffic successfully loaded!"
#define MSG_REQUESTING_LIVE_FD  "Requesting live flight data online..."
#define MSG_READING_HIST_FD     "Reading historic flight data..."
#define MSG_NUM_AC_INIT         "Initially created %d aircrafts"
#define MSG_NUM_AC_ZERO         "No more aircrafts displayed"
#define MSG_BUF_FILL_COUNTDOWN  "Filling buffer: seeing %d aircrafts, displaying %d, still %d seconds to buffer"
#define MSG_HIST_WITH_SYS_TIME  "When using historic data you cannot run X-Plane with 'always track system time',\ninstead, choose the historic date in X-Plane's date/time settings."
#define INFO_AC_ADDED           "Added aircraft %s, a/c model '%s', flight model [%s], bearing %.0f°, distance %.1fkm"
#define INFO_AC_REMOVED         "Removed aircraft %s"
#define INFO_AC_ALL_REMOVED     "Removed all aircrafts"
#define MSG_TOO_MANY_AC         "Reached limit of %d aircrafts, will create new ones only after removing outdated ones."
#define WHITESPACE              " \t\f\v"
#define CURL_USER_AGENT         "LiveTraffic/1.0"
#define CSL_DEFAULT_ICAO        "A320"
#define FM_MAP_SECTION          "Map"
#define FM_PARENT_SEPARATOR     ":"

//MARK: Menu Items
#define MENU_TOGGLE_AIRCRAFTS   "Aircrafts displayed"
#define MENU_AC_INFO_WND        "Aircraft Info..."
#define MENU_SETTINGS_UI        "Settings..."
#ifdef DEBUG
#define MENU_RELOAD_PLUGINS     "Reload Plugins (Developer!)"
#endif

//MARK: File Paths
#define PATH_RESOURCES_LT       "Resources/plugins/LiveTraffic/Resources"
#define PATH_RESOURCES_XSB      "Resources/plugins/XSquawkBox/Resources"
#define PATH_CONFIG_FILE        "Output/preferences/LiveTraffic.prf"
#define PATH_FLIGHT_MODELS      "Resources/plugins/LiveTraffic/FlightModels.prf"
#define PATH_STD_SEPARATOR      "/"             // the one used here in the constants
#define PATH_HFS_SEPARATOR      ":"
#define PATH_POSIX_SEPARATOR    "/"
#define FILE_CSL                "CSL"           // actually a folder, but XPLMGetDirectoryContents doesn't distinguish
#define FILE_RELATED_TXT        "related.txt"
#define FILE_LIGHTS_PNG         "lights.png"
#define FILE_DOC8643_TXT        "Doc8643.txt"

//MARK: Error Texsts
const long HTTP_OK =            200;
const long HTTP_NOT_FOUND =     404;
const int CH_MAC_ERR_CNT =      5;          // max number of tolerated errors, afterwards invalid channel
#define ERR_INIT_XPMP           "Could not initialize XPMPMultiplayer: %s"
#define ERR_LOAD_CSL            "Could not load CSL Package: %s"
#define ERR_ENABLE_XPMP         "Could not enable XPMPMultiplayer: %s"
#define ERR_APPEND_MENU_ITEM    "Could not append menu item"
#define ERR_CREATE_MENU         "Could not create menu"
#define ERR_CURL_INIT           "Could not initialize CURL: %s"
#define ERR_CURL_EASY_INIT      "Could not initialize easy CURL"
#define ERR_CURL_PERFORM        "%s: Could not get network data: %s"
#define ERR_CURL_HTTP_RESP      "%s: HTTP response is not OK but %ld"
#define ERR_CH_UNKNOWN_NAME     "(unknown channel)"
#define ERR_CH_INVALID          "%s: Channel invalid and disabled"
#define ERR_CH_MAX_ERR_INV      "%s: Channel invalid and disabled after too many errors"
#define ERR_CH_INV_DATA         "%s: Ignoring invalid data for '%s'"
#define ERR_DATAREF_FIND        "Could not find DataRef: %s"
#define ERR_DATAREF_ACCESSOR    "Could not register accessor for DataRef: %s"
#define ERR_DIR_CONTENT         "Could not retrieve directory content for %s"
#define ERR_RES_NOT_FOUND       "Did not find resources (CSL directory, related.txt, Doc8643.txt, lights.png) in:"
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
#define ERR_POS_UNNORMAL        "Ignoring invalid pos for a/c %s: %s"
#define ERR_INV_TRANP_ICAO      "Ignoring data for invalid transponder code '%s'"
#define ERR_TIME_NONLINEAR      "Time moved non-linear/jumped by %.1f seconds, will re-init aircrafts."
#define ERR_TOP_LEVEL_EXCEPTION "Caught top-level exception! %s"
#define ERR_WIDGET_CREATE       "Could not create widget required for settings UI"
#define ERR_CFG_FILE_OPEN_OUT   "Could not create config file '%s': %s"
#define ERR_CFG_FILE_WRITE      "Could not write into config file '%s': %s"
#define ERR_CFG_FILE_OPEN_IN    "Could not open '%s': %s"
#define ERR_CFG_FILE_VER        "Config file '%s' first line: Wrong format or version"
#define ERR_CFG_FILE_IGNORE     "Ignoring unkown entry '%s' from config file '%s'"
#define ERR_CFG_FILE_WORDS      "More than two words (key, value) in config file '%s', line starting with '%s %s', ignored"
#define ERR_CFG_FILE_READ       "Could not read from '%s': %s"
#define ERR_CFG_LINE_READ       "Could not read from file '%s', line %d: %s"
#define ERR_CFG_FILE_TOOMANY    "Too many warnings"
#define ERR_CFG_FILE_VALUE      "%s: Could not convert '%s' to a number: %s"
#define ERR_CFG_FORMAT          "Format mismatch in '%s', line %d: %s"
#define ERR_CFG_VAL_INVALID     "Value invalid in '%s', line %d: %s"
#define ERR_FM_NOT_AFTER_MAP    "Remainder after [Map] section ignored"
#define ERR_FM_NOT_BEFORE_SEC   "Lines before first section ignored"
#define ERR_FM_UNKNOWN_NAME     "Unknown parameter in '%s', line %d: %s"
#define ERR_FM_UNKNOWN_SECTION  "Referring to unknown model section in '%s', line %d: %s"
#define ERR_FM_UNKNOWN_PARENT   "Parent section missing in '%s', line %d: %s"
#define ERR_FM_REGEX            "%s in '%s', line %d: %s"
const int ERR_CFG_FILE_MAXWARN = 5;     // maximum number of warnings while reading config file, then: dead

//MARK: OpenSky
#define OPSKY_NAME              "OpenSky Live Online"
#define OPSKY_URL_ALL           "https://opensky-network.org/api/states/all?lamin=%.3f&lomin=%.3f&lamax=%.3f&lomax=%.3f"
#define OPSKY_AIRCRAFT_ARR      "states"
const int OPSKY_TRANSP_ICAO   = 0;               // icao24
const int OPSKY_CALL          = 1;               // callsign
const int OPSKY_COUNTRY       = 2;               // origin_county
const int OPSKY_POS_TIME      = 3;               // time_position
const int OPSKY_LON           = 5;               // longitude
const int OPSKY_LAT           = 6;               // latitude
const int OPSKY_ELEVATION     = 7;               // geo_altitude
const int OPSKY_GND           = 8;               // on_ground
const int OPSKY_SPD           = 9;               // velocity
const int OPSKY_HEADING       = 10;              // heading
const int OPSKY_VSI           = 11;              // vertical rate
const int OPSKY_RADAR_CODE    = 14;              // squawk

//MARK: OpenSky Master Data
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

#define ADSBEX_HIST_NAME        "ADSB Exchange Historic File"
const int ADSBEX_HIST_MIN_CHARS   = 20;             // minimum nr chars per line to be a 'reasonable' line
const int ADSBEX_HIST_MAX_ERR_CNT = 5;              // after that many errorneous line we stop reading
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
#define DBG_FILTER_AC           "DEBUG Filtering for a/c '%s'"
#define DBG_MERGED_POS          "DEBUG MERGED POS %s into updated TS %.1f"
#define DBG_POS_DATA            "DEBUG POS DATA: %s"
#define DBG_NO_MORE_POS_DATA    "DEBUG NO MORE LIVE POS DATA: %s"
#define DBG_SKIP_NEW_POS        "DEBUG SKIPPED NEW POS: %s"
#define DBG_INVENTED_STOP_POS   "DEBUG INVENTED STOP POS: %s"
#define DBG_INVENTED_TD_POS     "DEBUG INVENTED TOUCH-DOWN POS: %s"
#define DBG_INVENTED_TO_POS     "DEBUG INVENTED TAKE-OFF POS: %s"
#define DBG_AC_SWITCH_POS       "DEBUG A/C SWITCH POS: %s"
#define DBG_AC_FLIGHT_PHASE     "DEBUG A/C FLIGHT PHASE CHANGED from %i %s to %i %s"

#endif /* Constants_h */
