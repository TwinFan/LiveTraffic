/// @file       Constants.h
/// @brief      Constant definitions for LiveTraffic
/// @details    Version Information.\n
///             Unit Conversions.\n
///             Flight Model defaults.\n
///             Menu item texts.\n
///             Informational, warning, and error message texts.\n
/// @author     Birger Hoppe
/// @copyright  (c) 2020 Birger Hoppe
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

#ifndef Constants_h
#define Constants_h

//
// MARK: Version Information (CHANGE VERSION HERE)
//
constexpr float VERSION_NR = 1.55f;
constexpr bool VERSION_BETA = false;
extern float verXPlaneOrg;          // version on X-Plane.org
extern int verDateXPlaneOrg;        // and its date

//MARK: Window Position
#define WIN_WIDTH       400         // window width
#define WIN_ROW_HEIGHT   20         // height of line of text
#define WIN_FROM_TOP     50
#define WIN_FROM_RIGHT    0

constexpr int WIN_TIME_DISPLAY=8;       // duration of displaying a message windows
constexpr float WIN_TIME_REMAIN=1.0f;   // seconds to keep the msg window after last message

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
constexpr double JAN_FIRST_2019 = 1546344000;   // 01.01.2019
constexpr double HPA_STANDARD   = 1013.25;      // air pressure
constexpr double INCH_STANDARD  = 2992.126;
constexpr double HPA_per_INCH   = HPA_STANDARD/INCH_STANDARD;
// The pressure drops approximately by 11.3 Pa per meter in first 1000 meters above sea level.
constexpr double PA_per_M       = 11.3;         // https://en.wikipedia.org/wiki/Barometric_formula
// ft altitude diff per hPa change
constexpr double FT_per_HPA     = (100/PA_per_M)/M_per_FT;

//MARK: Flight Data-related
constexpr unsigned MAX_TRANSP_ICAO = 0xFFFFFF;  // max transponder ICAO code (24bit)
constexpr double FLIGHT_LOOP_INTVL  = -5.0;     // call ourselves every 5 frames
constexpr double AC_MAINT_INTVL     = 2.0;      // seconds (calling a/c maintenance periodically)
constexpr double TIME_REQU_POS      = 0.5;      // seconds before reaching current 'to' position we request calculation of next position
constexpr double SIMILAR_TS_INTVL = 3;          // seconds: Less than that difference and position-timestamps are considered "similar" -> positions are merged rather than added additionally
constexpr double SIMILAR_POS_DIST = 3;          // [m] if distance between positions less than this then favor heading from flight data over vector between positions
constexpr double FD_GND_AGL =       10;         // [m] consider pos 'ON GRND' if this close to YProbe
constexpr double PROBE_HEIGHT_LIM[] = {5000,1000,500,-999999};  // if height AGL is more than ... feet
constexpr double PROBE_DELAY[]      = {  10,   1,0.5,    0.2};  // delay next Y-probe ... seconds.
constexpr double AC_HIDE_LAT        = -70.645077;       // Neumayer-Station III
constexpr double AC_HIDE_LON        =  -8.264134;
constexpr double AC_HIDE_ALT        = 50;
constexpr double MAX_HOVER_AGL      = 2000;     // [ft] max hovering altitude for hover-along-the-runway detection
constexpr double KEEP_ABOVE_MAX_ALT    = 18000.0 * M_per_FT;///< [m] Maximum altitude to which the "keep above 2.5° glidescope" algorithm is applied (highest airports are below 15,000ft + 3,000 for approach)
constexpr double KEEP_ABOVE_MAX_AGL    =  3000.0 * M_per_FT;///< [m] Maximum height above ground to which the "keep above 2.5° glidescope" algorithm is applied (highest airports are below 15,000ft + 3,000 for approach)
constexpr double KEEP_ABOVE_RATIO      = 0.043495397807572; ///< = tan(2.5°), slope ratio for keeping a plane above the approach to a runway
constexpr double BEZIER_MIN_HEAD_DIFF = 5.0;    ///< [°] turns of less than this will not be modeled with Bezier curves

//MARK: Flight Model
constexpr double MDL_ALT_MIN =         -1500;   // [ft] minimum allowed altitude
constexpr double MDL_ALT_MAX =          60000;  // [ft] maximum allowed altitude
constexpr double MDL_CLOSE_TO_GND =     0.5;    // feet height considered "on ground"
constexpr double MDL_MAX_TURN       =    90;    // max turn in flight at a position
constexpr double MDL_MAX_TURN_GND   =   120;    // max turn on the ground
constexpr double MDL_TO_LOOK_AHEAD  =    60.0;  // [s] to look ahead for take off prediction
constexpr float  MDL_EXT_CAMERA_PITCH  = -5;    // initial pitch
constexpr float  MDL_EXT_STEP_MOVE =      0.5f; // [m] to move with one command
constexpr float  MDL_EXT_FAST_MOVE =      5.0f; //               ...a 'fast' command
constexpr float  MDL_EXT_STEP_DEG =       1.0f; // [°] step turn with one command
constexpr float  MDL_EXT_FAST_DEG =       5.0f;
constexpr float  MDL_EXT_STEP_FACTOR =    1.025f; // step factor with one zoom command
constexpr float  MDL_EXT_FAST_FACTOR =    1.1f;
#define MDL_LABEL_COLOR         "LABEL_COLOR"
constexpr double MDL_REVERSERS_TIME = 2.0;  ///< [s] to open/close reversers
constexpr double MDL_SPOILERS_TIME  = 0.5;  ///< [s] to extend/retract spoilers
constexpr double MDL_TIRE_SLOW_TIME = 5.0;  ///< [s] time till tires stop rotating after take-off
constexpr double MDL_TIRE_MAX_RPM = 2000;   ///< [rpm] max tire rotation speed
constexpr double MDL_TIRE_CF_M      = 3.2;  ///< [m] tire circumfence (3.2m for a 40-inch tire)
constexpr double MDL_GEAR_DEFL_TIME = 0.5;  ///< [s] time for gear deflection (one direction...up down is twice this value)
constexpr double MDL_CAR_MAX_TAXI = 80.0;   ///< [kn] Maximum allowed taxi speed for ground vehicles (before they turn into planes)

constexpr int COLOR_YELLOW      = 0xFFFF00;
constexpr int COLOR_RED         = 0xFF0000;
constexpr int COLOR_GREEN       = 0x00FF00;
constexpr int COLOR_BLUE        = 0x00F0F0;     // light blue

//MARK: Airports, Runways, Taxiways
constexpr double ART_EDGE_ANGLE_TOLERANCE=30.0; ///< [°] tolerance of searched heading to edge's angle to be considered a fit
constexpr double ART_EDGE_ANGLE_TOLERANCE_EXT=80.0; ///< [°] extended (second prio) tolerance of searched heading to edge's angle to be considered a fit
constexpr double ART_EDGE_ANGLE_EXT_DIST=5.0;   ///< [m] Second prio angle tolerance wins, if such a node is this much closer than an first priority angle match
constexpr double ART_RWY_TD_POINT_F = 0.10;     ///< [-] Touch-down point is this much into actual runway (so we don't touch down at its actual beginning)
constexpr double ART_RWY_MAX_HEAD_DIFF = 15.0;  ///< [°] maximum heading difference between flight and runway
constexpr double ART_RWY_MAX_DIST = 20.0 * M_per_NM; ///< [m] maximum distance to a runway when searching for one
constexpr double ART_RWY_MAX_VSI_F = 2.0;       ///< [-] descend rate: maximum allowed factor applied to VSI_FINAL
constexpr double ART_RWY_ALIGN_DIST = 500.0;    ///< [m] distance before touch down to be fully aligned with rwy
constexpr double ART_APPR_SPEED_F = 0.8;        ///< [-] ratio of FLAPS_DOWN_SPEED to use as max approach speed
constexpr double ART_FINAL_SPEED_F = 0.7;       ///< [-] ratio of FLAPS_DOWN_SPEED to use as max final speed
constexpr double ART_TAXI_SPEED_F  = 0.8;       ///< [-] ratio of MAX_TAXI_SPEED to use as taxi speed
constexpr double APT_MAX_TAXI_SEGM_TURN = 15.0; ///< [°] Maximum turn angle (compared to original edge's angle) for combining edges
constexpr double APT_MAX_SIMILAR_NODE_DIST_M = 2.0; ///< [m] Max distance for two taxi nodes to be considered "similar", so that only one of them is kept
constexpr double APT_STARTUP_VIA_DIST = 50.0;   ///< [m] distance of StartupLoc::viaLoc from startup location
constexpr double APT_STARTUP_MOVE_BACK = 10.0;  ///< [m] move back startup location so that it sits about in plane's center instead of at its head
constexpr double APT_JOIN_MAX_DIST_M = 15.0;    ///< [m] Max distance for an open node to be joined with another edge
constexpr double APT_JOIN_ANGLE_TOLERANCE=15.0; ///< [°] tolerance of angle for an open node to be joined with another edge
constexpr double APT_JOIN_ANGLE_TOLERANCE_EXT=45.0; ///< [°] extended (second prio) tolerance of angle for an open node to be joined with another edge
constexpr double APT_MAX_PATH_TURN=100.0;       ///< [°] Maximum turn allowed during shortest path calculation
constexpr double APT_PATH_MIN_SEGM_LEN=SIMILAR_POS_DIST*2;      ///< [m] Minimum segment length when taking over a shortest path. Shorter taxi segments are joined into one to avoid too many positions in the fd deque
constexpr double APT_RECT_ANGLE_TOLERANCE=10.0; ///< [°] Tolerance when trying to decide for rectangular angle

//MARK: Version Information
extern char LT_VERSION[];               // like "1.0"
extern char LT_VERSION_FULL[];          // like "1.0.181231" with last digits being build date
extern char HTTP_USER_AGENT[];          // like "LiveTraffic/1.0"
extern time_t LT_BETA_VER_LIMIT;        // BETA versions are limited
extern char LT_BETA_VER_LIMIT_TXT[];
#define BETA_LIMITED_VERSION    "BETA limited to %s"
#define BETA_LIMITED_EXPIRED    "BETA-Version limited to %s has EXPIRED -> SHUTTING DOWN! Get an up-to-date version from X-Plane.org."
constexpr int LT_NEW_VER_CHECK_TIME = 48;   // [h] between two checks of a new

//MARK: Text Constants
#define LIVE_TRAFFIC            "LiveTraffic"
#define LT_CFG_VER_NM_CONV      "1.0"        // version of config file format, from which to convert distances from km to nm
#define LT_CFG_VERSION          "1.1"        // current version of config file format
#define LT_FM_VERSION           "2.0"        // version of flight model file format
#define PLUGIN_SIGNATURE        "TwinFan.plugin.LiveTraffic"
#define PLUGIN_DESCRIPTION      "Create Multiplayer Aircraft based on live traffic."
#define LT_DOWNLOAD_URL         "https://forums.x-plane.org/index.php?/files/file/61021-livetraffic-for-xp10/"
#define LT_DOWNLOAD_CH          "X-Plane.org"
#define MSG_DISABLED            "Disabled"
#define MSG_STARTUP             "LiveTraffic %s starting up..."
#define MSG_WELCOME             "LiveTraffic %s successfully loaded!"
#define MSG_NOT_MODERN_DRIVER   "LiveTraffic %s will NOT work under Vulkan/Metal!"
#define MSG_NOT_MODERN_DRIVER2  "Until v2.0 is availble deactivate Vulkan/Metal in XP's Graphic setting!"
#define MSG_REINIT              "LiveTraffic is re-initializing itself"
#define MSG_DISABLE_MYSELF      "LiveTraffic disables itself due to unhandable exceptions"
#define MSG_LT_NEW_VER_AVAIL    "The new version %01.2f of LiveTraffic is available at X-Plane.com!"
#define MSG_TIMESTAMPS          "Current System time is %sZ, current simulated time is %sZ"
#define MSG_AI_LOAD_ACF         "Changing AI control: X-Plane is now loading AI Aircraft models..."
#define MSG_REQUESTING_LIVE_FD  "Requesting live flight data online..."
#define MSG_READING_HIST_FD     "Reading historic flight data..."
#define MSG_NUM_AC_INIT         "Initially created %d aircraft"
#define MSG_NUM_AC_ZERO         "No more aircraft displayed"
#define MSG_BUF_FILL_COUNTDOWN  "Filling buffer: seeing %d aircraft, displaying %d, still %ds to buffer"
#define MSG_HIST_WITH_SYS_TIME  "When using historic data you cannot run X-Plane with 'always track system time',\ninstead, choose the historic date in X-Plane's date/time settings."
#define MSG_ADSBEX_LIMITE       "%ld / %ld requests left"
#define INFO_AC_ADDED           "Added aircraft %s, operator '%s', a/c model '%s', flight model [%s], bearing %.0f, distance %.1fnm, from channel %s"
#define INFO_AC_MDL_CHANGED     "Changed CSL model for aircraft %s, operator '%s': a/c model now '%s'"
#define INFO_GND_VEHICLE_APT    "Vehicle %s: Decided for ground vehicle based on operator name '%s'"
#define INFO_GND_VEHICLE_CALL   "Vehicle %s: Decided for ground vehicle based on call sign '%s'"
#define INFO_AC_REMOVED         "Removed aircraft %s"
#define INFO_AC_ALL_REMOVED     "Removed all aircraft"
#define INFO_WND_AUTO_AC        "AUTO"
#define INFO_AC_HIDDEN          "A/c %s hidden"
#define INFO_AC_HIDDEN_AUTO     "A/c %s automatically hidden"
#define INFO_AC_SHOWN           "A/c %s visible"
#define INFO_AC_SHOWN_AUTO      "A/c %s automatically visible"
#define MSG_TOO_MANY_AC         "Reached limit of %d aircraft, will render nearest aircraft only."
#define MSG_CSL_PACKAGE_LOADED  "Successfully loaded CSL package %s"
#define MSG_MDL_FORCED          "Settings > Debug: Model matching forced to '%s'/'%s'/'%s'"
#define MSG_MDL_NOT_FORCED      "Settings > Debug: Model matching no longer forced"
#define WHITESPACE              " \t\f\v\r\n"
#define CSL_DEFAULT_ICAO_TYPE   "A320"
#define CSL_CAR_ICAO_TYPE       "ZZZC"      // fake code for a ground vehicle
#define FM_MAP_SECTION          "Map"
#define FM_CAR_SECTION          "GroundVehicles"
#define FM_PARENT_SEPARATOR     ":"
#define CFG_CSL_SECTION         "[CSLPaths]"
#define CFG_DEFAULT_AC_TYPE     "DEFAULT_AC_TYPE"
#define CFG_DEFAULT_CAR_TYPE    "DEFAULT_CAR_TYPE"
#define CFG_DEFAULT_AC_TYP_INFO "Default a/c type is '%s'"
#define CFG_DEFAULT_CAR_TYP_INFO "Default car type is '%s'"
#define CFG_ADSBEX_API_KEY      "ADSBEX_API_KEY"
#define XPPRF_RENOPT_HDR        "renopt_HDR"					// XP10
#define XPPRF_EFFECTS_04		"renopt_effects_04"				// XP11, if >= 3 then includes HDR
#define XPPRF_RENOPT_HDR_ANTIAL "renopt_HDR_antial"

//MARK: Menu Items
#define MENU_AC_INFO_WND        "Aircraft Info..."
#define MENU_AC_INFO_WND_POPOUT "Aircraft Info... (Popped out)"
#define MENU_AC_INFO_WND_SHOWN  "Aircraft Info shown"
#define MENU_AC_INFO_WND_CLOSEALL "Close All Windows"
#define MENU_TOGGLE_AIRCRAFT    "Aircraft displayed"
#define MENU_TOGGLE_AC_NUM      "Aircraft displayed (%d shown)"
#define MENU_HAVE_TCAS          "TCAS controlled"
#define MENU_TOGGLE_LABELS      "Labels shown"
#define MENU_SETTINGS_UI        "Settings..."
#define MENU_HELP               "Help"
#define MENU_HELP_DOCUMENTATION "Documentation"
#define MENU_HELP_FAQ           "FAQ"
#define MENU_HELP_MENU_ITEMS    "Menu Items"
#define MENU_HELP_AC_INFO_WND   "A/C Info Window"
#define MENU_HELP_SETTINGS      "Settings"
#define MENU_NEWVER             "New Version %01.2f available!"
#ifdef DEBUG
#define MENU_RELOAD_PLUGINS     "Reload all Plugins (Caution!)"
#define MENU_REMOVE_ALL_BUT     "Remove all but selected a/c"
#endif

//MARK: Help URLs
#define HELP_URL                "https://twinfan.gitbook.io/livetraffic/v/v1.50/"
#define HELP_FAQ                "reference/faq"
#define HELP_MENU_ITEMS         "using-lt/menu-items"
#define HELP_AC_INFO_WND        "using-lt/aircraft-information-window"
#define HELP_SETTINGS           "setup/configuration#settings-ui"
#define HELP_SETTINGS_PATHS {                       \
    "setup/configuration/settings-basics",          \
    "setup/configuration/settings-a-c-labels",      \
    "setup/configuration/settings-advanced",        \
    "setup/configuration/settings-csl",             \
    "setup/configuration/settings-debug",           \
}

//MARK: File Paths
// these are under the plugins directory
#define PATH_FLIGHT_MODELS      "Resources/FlightModels.prf"
#define PATH_RELATED_TXT        "Resources/related.txt"
#define PATH_LIGHTS_PNG         "Resources/lights.png"
#define PATH_DOC8643_TXT        "Resources/Doc8643.txt"
#define PATH_MODEL_TYPECODE_TXT "Resources/model_typecode.txt"
#define PATH_RESOURCES          "Resources"
#define PATH_RESOURCES_CSL      "Resources/CSL"
#define PATH_RESOURCES_SCSL     "Resources/ShippedCSL"
// these are under X-Plane's root dir
#define PATH_DEBUG_RAW_FD       "LTRawFD.log"   // this is under X-Plane's system dir
#define PATH_RES_PLUGINS        "Resources/plugins"
#define PATH_CONFIG_FILE        "Output/preferences/LiveTraffic.prf"

//MARK: Error Texsts
constexpr long HTTP_OK =            200;
constexpr long HTTP_BAD_REQUEST =   400;
constexpr long HTTP_UNAUTHORIZED =  401;
constexpr long HTTP_FORBIDDEN =     403;
constexpr long HTTP_NOT_FOUND =     404;
constexpr long HTTP_NOT_AVAIL =     503;        // "Service not available"
constexpr int CH_MAC_ERR_CNT =      5;          // max number of tolerated errors, afterwards invalid channel
constexpr int SERR_LEN = 100;                   // size of buffer for IO error texts (strerror_s)
#define ERR_XPLANE_ONLY         "LiveTraffic works in X-Plane only, version 10 or higher"
#define ERR_INIT_XPMP           "Could not initialize XPMPMultiplayer: %s"
#define ERR_LOAD_CSL            "Could not load CSL Package: %s"
#define ERR_XPMP_ENABLE         "Could not enable XPMPMultiplayer: %s"
#define ERR_NO_TCAS             "%s controls AI planes. LiveTraffic can therefore not create TCAS blibs!"
#define ERR_XPMP_ADD_CSL        "Could not add additional CSL package: %s"
#define ERR_APPEND_MENU_ITEM    "Could not append a menu item"
#define ERR_CREATE_MENU         "Could not create menu %s"
#define ERR_CURL_INIT           "Could not initialize CURL: %s"
#define ERR_CURL_EASY_INIT      "Could not initialize easy CURL"
#define ERR_CURL_PERFORM        "%s: Could not get network data: %d - %s"
#define ERR_CURL_NOVERCHECK     "Could not browse X-Plane.org for version info: %d - %s"
#define ERR_CURL_HTTP_RESP      "%s: HTTP response is not OK but %ld for %s"
#define ERR_CURL_REVOKE_MSG     {"revocation","80092012","80092013"}  // appear in error text if querying revocation list fails
#define ERR_CURL_DISABLE_REV_QU "%s: Querying revocation list failed - have set CURLSSLOPT_NO_REVOKE and am trying again"
#define ERR_HTTP_NOT_OK         "HTTP response was not HTTP_OK"
#define ERR_FOUND_NO_VER_INFO   "Found no version info in response"
#define ERR_CH_NONE_ACTIVE      "No enabled active channel for tracking data! Check Basic Settings and enable channels."
#define ERR_CH_UNKNOWN_NAME     "(unknown channel)"
#define ERR_CH_INVALID          "%s: Channel invalid and disabled"
#define ERR_CH_MAX_ERR_INV      "%s: Channel invalid and disabled after too many errors"
#define ERR_NO_AC_TYPE          "Tracking data for '%s' (man '%s', mdl '%s') lacks ICAO a/c type code, can't derive type -> will be rendered with standard a/c %s"
#define ERR_NO_AC_TYPE_BUT_MDL  "Tracking data for '%s' (man '%s', mdl '%s') lacks ICAO a/c type code, but derived %s from mdl text"
#define ERR_DATAREF_FIND        "Could not find DataRef/CmdRef: %s"
#define ERR_DATAREF_ACCESSOR    "Could not register accessor for DataRef: %s"
#define ERR_CREATE_COMMAND      "Could not create command %s"
#define ERR_DIR_CONTENT         "Could not retrieve directory content for %s"
#define ERR_JSON_PARSE          "Parsing flight data as JSON failed"
#define ERR_JSON_MAIN_OBJECT    "JSON: Getting main object failed"
#define ERR_JSON_ACLIST         "JSON: List of aircraft (%s) not found"
#define ERR_JSON_AC             "JSON: Could not get %lu. aircraft in '%s'"
#define ERR_NEW_OBJECT          "Could not create new object (memory?): %s"
#define ERR_LOCK_ERROR          "Could not acquire lock for '%s': %s"
#define ERR_MALLOC              "Could not (re)allocate %ld bytes of memory"
#define ERR_ASSERT              "ASSERT FAILED: %s"
#define ERR_AC_NO_POS           "No positional data available when creating aircraft %s"
#define ERR_AC_CALC_PPOS        "Could not calculate position when creating aircraft %s"
#define ERR_Y_PROBE             "Y Probe returned %d at %s"
#define ERR_POS_UNNORMAL        "A/c %s reached invalid pos: %s"
#define ERR_IGNORE_POS          "A/c %s: Ignoring data leading to sharp turn or invalid speed: %s"
#define ERR_INV_TRANP_ICAO      "Ignoring data for invalid transponder code '%s'"
#define ERR_TIME_NONLINEAR      "Time moved non-linear/jumped by %.1f seconds, will re-init aircraft."
#define ERR_TOP_LEVEL_EXCEPTION "Caught top-level exception! %s"
#define ERR_EXCEPTION_AC_CREATE "Exception occured while creating a/c %s of type %s: %s\nPosDeque before was:\n%s"
#define ERR_UNKN_EXCP_AC_CREATE "Unknown " ERR_EXCEPTION_AC_CREATE
#define ERR_WIDGET_CREATE       "Could not create widget required for settings UI"
#define ERR_CFG_FILE_OPEN_OUT   "Could not create config file '%s': %s"
#define ERR_CFG_FILE_WRITE      "Could not write into config file '%s': %s"
#define ERR_CFG_FILE_OPEN_IN    "Could not open '%s': %s"
#define ERR_CFG_FILE_VER        "Config file '%s' first line: Unsupported format or version: %s"
#define ERR_CFG_FILE_VER_UNEXP  "Config file '%s' first line: Unexpected version %s, expected %s...trying to continue"
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
#define ERR_CFG_TYPE_INVALID    "%s, line %d: ICAO type designator '%s' unknown"
#define ERR_FM_NOT_AFTER_MAP    "Unknown section after [Map] section ignored"
#define ERR_FM_NOT_BEFORE_SEC   "Lines before first section ignored"
#define ERR_FM_UNKNOWN_NAME     "Unknown parameter in '%s', line %d: %s"
#define ERR_FM_UNKNOWN_SECTION  "Referring to unknown model section in '%s', line %d: %s"
#define ERR_FM_UNKNOWN_PARENT   "Parent section missing in '%s', line %d: %s"
#define ERR_FM_REGEX            "%s in '%s', line %d: %s"
#define ERR_FM_NOT_FOUND        "Found no flight model for ICAO %s/match-string %s: will use default"
constexpr int ERR_CFG_FILE_MAXWARN = 5;     // maximum number of warnings while reading config file, then: dead

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
#define DBG_KEEP_ABOVE          "DEBUG POS LIFTED TO 2.5deg GLIDESCOPE from %.0fft: %s"
#define DBG_NO_MORE_POS_DATA    "DEBUG NO MORE LIVE POS DATA: %s"
#define DBG_SKIP_NEW_POS        "DEBUG SKIPPED NEW POS: %s"
#define DBG_ADDED_NEW_POS       "DEBUG ADDED   NEW POS: %s"
#define DBG_INVENTED_STOP_POS   "DEBUG INVENTED STOP POS: %s"
#define DBG_INVENTED_TD_POS     "DEBUG INVENTED TOUCH-DOWN POS: %s"
#define DBG_INVENTED_TO_POS     "DEBUG INVENTED TAKE-OFF POS: %s"
#define DBG_REUSING_TO_POS      "DEBUG RE-USED POS FOR TAKE-OFF: %s"
#define DBG_INV_POS_REMOVED     "DEBUG %s: Removed an invalid position: %s"
#define DBG_INV_POS_AC_REMOVED  "DEBUG %s: Removed a/c due to invalid positions"
#define DBG_HOVER_POS_REMOVED   "DEBUG %s: Removed a hovering position: %s"
#define DBG_AC_SWITCH_POS       "DEBUG A/C SWITCH POS: %s"
#define DBG_AC_FLIGHT_PHASE     "DEBUG A/C FLIGHT PHASE CHANGED from %i %s to %i %s"
#define DBG_AC_CHANNEL_SWITCH   "DEBUG %s: SWITCHED CHANNEL from '%s' to '%s'"
#ifdef DEBUG
#define DBG_DEBUG_BUILD         "DEBUG BUILD with additional run-time checks and no optimizations"
#endif

#endif /* Constants_h */
