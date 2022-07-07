/// @file       DataRefs.cpp
/// @brief      Access to global data like dataRefs, `doc8643` and `model_typecode` files.
/// @details    Implements classes Doc8643, DataRefs, and read access to `model_typecode` file.\n
///             There is exactly one instance of DataRefs, which is the global variable `dataRefs`,
///             in which all globally relevant values are stored, beyond just XP's dataRefs:\n
///             - LiveTraffic's configuration options including reading/writing of the config file\n
///             - readable callbacks for other plugins' access to LiveTraffic's data
///             - LTAPI interface
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

#include "LiveTraffic.h"

#include <fstream>
#include <errno.h>

constexpr int DEFAULT_MODES_ID = 0xFFFF00;  ///< used if no modeS id is available for user aircraft while exporting data

//
//MARK: external references
//

// return color into a RGB array as XP likes it
void conv_color (int inCol, float outColor[4])
{
    outColor[0] = float((inCol & 0xFF0000) >> 16) / 255.0f;    // red
    outColor[1] = float((inCol & 0x00FF00) >>  8) / 255.0f;    // green
    outColor[2] = float((inCol & 0x0000FF)      ) / 255.0f;    // blue
    outColor[3] = 1.0f;
}



//
// MARK: Doc8643
//

// global map, which stores the content of the doc8643 file
std::map<std::string, Doc8643> mapDoc8643;
const Doc8643 DOC8643_EMPTY;    // objet returned if Doc8643::get fails

// constructor setting all elements
Doc8643::Doc8643 (std::string&& _manufacturer,
                  std::string&& _model,
                  std::string&& _typeDesignator,
                  std::string&& _classification,
                  std::string&& _wtc) :
manufacturer    (std::move(_manufacturer)),
model           (std::move(_model)),
typeDesignator  (std::move(_typeDesignator)),
classification  (std::move(_classification)),
wtc             (std::move(_wtc))
{}

// return the string for FlightModel matching
Doc8643::operator std::string() const
{
    return wtc + ';' + classification + ';' + typeDesignator + ';' +
    model + ';' + manufacturer;
}

//
// Static functions
//

// reads the Doc8643 file into mapDoc8643
bool Doc8643::ReadDoc8643File ()
{
    // clear the map, just in case
    mapDoc8643.clear();
    
    // Put together path to Doc8643.txt
    std::string path (LTCalcFullPluginPath(PATH_DOC8643_TXT));
    
    // open the file for reading
    std::ifstream fIn (path);
    if (!fIn) {
        // if there is no config file output a warning (we can use defaults)
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                 path.c_str(), sErr);
        return false;
    }
    
    // regular expression to extract individual values, separated by TABs
    enum { DOC_MANU=1, DOC_MODEL, DOC_TYPE, DOC_CLASS, DOC_WTC, DOC_EXPECTED };
    const std::regex re("^([^\\t]+)\\t"                   // manufacturer
                        "([^\\t]+)\\t"                    // model
                        "([[:alnum:]]{2,4})\\t"           // type designator
                        "(-|[AGHLST][C1-8][EJPRT])\\t"    // classification
                        "(-|[HLMJ]|L/M)");                // wtc

    // loop over lines of the file
    std::string text;
    int errCnt = 0;
    for (int ln=1; fIn && errCnt <= ERR_CFG_FILE_MAXWARN; ln++) {
        // read entire line
        safeGetline(fIn, text);
        if (text.empty())           // skip empty lines silently
            continue;
        
        // apply the regex to extract values
        std::smatch m;
        std::regex_search(text, m, re);
        
        // add to map (if matched)
        if (m.size() == DOC_EXPECTED) {
            mapDoc8643.emplace(m[DOC_TYPE],
                               Doc8643(m[DOC_MANU],
                                       m[DOC_MODEL],
                                       m[DOC_TYPE],
                                       m[DOC_CLASS],
                                       m[DOC_WTC]));
        } else if (fIn) {
            // I/O was good, but line didn't match
            SHOW_MSG(logWARN, ERR_CFG_LINE_READ,
                     path.c_str(), ln, text.c_str());
            errCnt++;
        } else if (!fIn && !fIn.eof()) {
            // I/O error
			char sErr[SERR_LEN];
			strerror_s(sErr, sizeof(sErr), errno);
			SHOW_MSG(logWARN, ERR_CFG_LINE_READ,
                     path.c_str(), ln, sErr);
            errCnt++;
        }
    }
    
    // close file
    fIn.close();
    
    // too many warnings?
    if (errCnt > ERR_CFG_FILE_MAXWARN) {
        SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                 path.c_str(), ERR_CFG_FILE_TOOMANY);
        return false;
    }
    
    // looks like success
    return true;
}

// return the matching Doc8643 object from the global map
const Doc8643& Doc8643::get (const std::string& _type)
try
{
    return mapDoc8643.at(_type);
}
catch (...)
{
    return DOC8643_EMPTY;
}

//
// MARK: ModelIcaoType
//

namespace ModelIcaoType
{
    /// Map, which stores the content of the model_typecode.txt file:
    /// human-readable model text maps to ICAO type cide
    std::map<std::string, std::string> mapModelIcaoType;
    
    /// global empty string returned if nothing is found in map
    const std::string gEmptyString;

    // Read the `model_typecode.txt` file
    bool ReadFile ()
    {
        // clear the map
        mapModelIcaoType.clear();
        
        // Put together path to model_typecode.txt
        std::string path (LTCalcFullPluginPath(PATH_MODEL_TYPECODE_TXT));
        
        // open the file for reading
        std::ifstream fIn (path);
        if (!fIn) {
            // if there is no config file output a warning, but otherwise no worries
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                     path.c_str(), sErr);
            return false;
        }
        
        // loop over lines of the file
        std::string text;
        int errCnt = 0;
        for (int ln=1; fIn && errCnt <= ERR_CFG_FILE_MAXWARN; ln++) {
            // read entire line
            safeGetline(fIn, text);
            if (text.empty())           // skip empty lines silently
                continue;
            
            // ignore the first line, if it is just the column header
            if (ln == 1 && text == "MODEL|TYPECODE")
                continue;
            
            // Elements model and type code are separate by pipe.
            // Find the last pipe in line (just in case there happens to a pipe in the model name, which is the first field)
            const std::string::size_type pos = text.rfind('|');
            // not found a pipe?
            if (pos == std::string::npos ||
                // pipe is first or last character? not good either as either string would be empty then
                pos == 0 || pos == text.size()-1) {
                // I/O was good, but line has wrong format
                SHOW_MSG(logWARN, ERR_CFG_LINE_READ,
                         path.c_str(), ln, text.c_str());
                errCnt++;
            }
            
            // everything to the left is mode text
            std::string mdl = text.substr(0,pos);
            str_toupper(trim(mdl));
            // everything to the right is type code, which should exist
            // (but if not we use it anyway...)
            std::string type = text.substr(pos+1);
            str_toupper(trim(type));
#if TEST_MODEL_TYPE_CODES
            // quite many invalid codes are in the file, which originates
            // in the OpenSky aircraft database...we don't warn about all of them
            if (Doc8643::get(type) == DOC8643_EMPTY) {
                LOG_MSG(logWARN, ERR_CFG_TYPE_INVALID,
                        path.c_str(), ln,
                        type.c_str());
            }
#endif
            
            // and just move it to the map:
            mapModelIcaoType.emplace(std::move(mdl),
                                     std::move(type));
        }

        // close file
        fIn.close();
        
        // too many warnings?
        if (errCnt > ERR_CFG_FILE_MAXWARN) {
            SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                     path.c_str(), ERR_CFG_FILE_TOOMANY);
            return false;
        }
        
        // looks like success
        return true;
    }
    
    // Lookup ICAO type designator for human-readable model text, empty if nothing found
    const std::string& getIcaoType (const std::string& _model)
    {
        try {
            // lookup a value on the map, throws std::out_of_range if nothing found
            return mapModelIcaoType.at(str_toupper_c(_model));
        }
        catch (...) {
            // caught exception -> nothing found in map, return something empty
            return gEmptyString;
        }
    }

}

//
// MARK: WndRect
//


// Write WndRect into config file ("left,top,right.bottom")
std::ostream& operator<< (std::ostream& _stream, const WndRect& _r)
{
    return _stream << _r.left() << ',' << _r.top() << ',' << _r.right() << ',' << _r.bottom();
}

// Set from config file string ("left,top,right.bottom")
void WndRect::set (const std::string& _s)
{
    std::vector<std::string> tok = str_tokenize(_s, ",");
    if (tok.size() == 4) {
        left()      = std::stoi(tok[0]);
        top()       = std::stoi(tok[1]);
        right()     = std::stoi(tok[2]);
        bottom()    = std::stoi(tok[3]);
    } else {
        LOG_MSG(logERR, "Window pos expects 4 numbers but got: %s", _s.c_str());
    }
}


//MARK: X-Plane Datarefs
const char* DATA_REFS_XP[] = {
    "sim/network/misc/network_time_sec",        // float	n	seconds	The current elapsed time synched across the network (used as timestamp in Log.txt)
    "sim/time/local_time_sec",
    "sim/time/local_date_days",
    "sim/time/use_system_time",
    "sim/time/zulu_time_sec",
    "sim/operation/prefs/replay_mode",          //    int    y    enum    Are we in replay mode?
    "sim/graphics/view/view_is_external",
    "sim/graphics/view/view_type",
    "sim/graphics/view/using_modern_driver",    // boolean: Vulkan/Metal in use? (since XP11.50)

    "sim/multiplayer/camera/tcas_idx",          // Shared data refs filled by LiveTraffic with aircraft under camera
    "sim/multiplayer/camera/modeS_id",          // Shared data refs filled by LiveTraffic with aircraft under camera

    "sim/flightmodel/position/latitude",
    "sim/flightmodel/position/longitude",
    "sim/flightmodel/position/elevation",
    "sim/flightmodel/position/true_theta",      // pitch
    "sim/flightmodel/position/true_phi",        // roll
    "sim/flightmodel/position/true_psi",        // true heading
    "sim/flightmodel/position/mag_psi",         // magnetic heading
    "sim/flightmodel/position/hpath",           // track
    "sim/flightmodel/position/indicated_airspeed", // KIAS (apparently really in knots)
    "sim/flightmodel/position/true_airspeed",   // TAS
    "sim/flightmodel/position/groundspeed",     // GS
    "sim/flightmodel/position/vh_ind",          // float n meters/second VVI (vertical velocity in meters per second)"
    "sim/flightmodel/failures/onground_any",
    "sim/aircraft/view/acf_tailnum",            // byte[40] y string    Tail number
    "sim/aircraft/view/acf_modeS_id",           // int      y integer   24bit (0-16777215 or 0-0xFFFFFF) unique ID of the airframe. This is also known as the ADS-B "hexcode".
    "sim/aircraft/view/acf_ICAO",               // byte[40] y string    ICAO code for aircraft (a string) entered by author
    "sim/weather/wind_direction_degt",          // float    n    [0-359)    The effective direction of the wind at the plane's location.
    "sim/weather/wind_speed_kt",                // float    n    msc    >= 0        The effective speed of the wind at the plane's location. WARNING: this dataref is in meters/second - the dataref NAME has a bug.
    "sim/graphics/VR/enabled",
};

static_assert(sizeof(DATA_REFS_XP) / sizeof(DATA_REFS_XP[0]) == CNT_DATAREFS_XP,
    "dataRefsXP and DATA_REFS_XP[] differ in number of elements");

//MARK: X-Plane Command Refs
const char* CMD_REFS_XP[] = {
    "sim/general/left",
    "sim/general/right",
    "sim/general/left_fast",
    "sim/general/right_fast",
    "sim/general/forward",
    "sim/general/backward",
    "sim/general/forward_fast",
    "sim/general/backward_fast",
    "sim/general/hat_switch_left",
    "sim/general/hat_switch_right",
    "sim/general/hat_switch_up",
    "sim/general/hat_switch_down",
    "sim/general/hat_switch_up_left",
    "sim/general/hat_switch_up_right",
    "sim/general/hat_switch_down_left",
    "sim/general/hat_switch_down_right",
    "sim/general/up",
    "sim/general/down",
    "sim/general/up_fast",
    "sim/general/down_fast",
    "sim/general/rot_left",
    "sim/general/rot_right",
    "sim/general/rot_left_fast",
    "sim/general/rot_right_fast",
    "sim/general/rot_up",
    "sim/general/rot_down",
    "sim/general/rot_up_fast",
    "sim/general/rot_down_fast",
    "sim/general/zoom_in",
    "sim/general/zoom_out",
    "sim/general/zoom_in_fast",
    "sim/general/zoom_out_fast",
    
    "sim/view/free_camera",
    "sim/view/forward_with_2d_panel",
    "sim/view/forward_with_hud",
    "sim/view/forward_with_nothing",
    "sim/view/linear_spot",
    "sim/view/still_spot",
    "sim/view/runway",
    "sim/view/circle",
    "sim/view/tower",
    "sim/view/ridealong",
    "sim/view/track_weapon",
    "sim/view/chase",
    "sim/view/3d_cockpit_cmnd_look",
};

static_assert(sizeof(CMD_REFS_XP) / sizeof(CMD_REFS_XP[0]) == CNT_CMDREFS_XP,
    "cmdRefsXP and CMD_REFS_XP[] differ in number of elements");

// For informing dataRe Editor and tool see
// http://www.xsquawkbox.net/xpsdk/mediawiki/DataRefEditor and
// https://github.com/leecbaker/datareftool/blob/master/src/plugin_custom_dataref.cpp

// DataRef editors, which we inform about our dataRefs
#define MSG_ADD_DATAREF 0x01000000
const char* DATA_REF_EDITORS[] = {
    "xplanesdk.examples.DataRefEditor",
    "com.leecbaker.datareftool"
};

/// Map view types to view commands
struct mapViewTypesTy {
    XPViewTypes     e = VIEW_UNKNOWN;       ///< enum value
    cmdRefsXP       cr = CR_NO_COMMAND;     ///< command ref enum value
};

mapViewTypesTy MAP_VIEW_TYPES[] = {
    { VIEW_FWD_2D,      CR_VIEW_FWD_2D      },
    { VIEW_EXT_TOWER,   CR_VIEW_EXT_TOWER   },
    { VIEW_EXT_RNWY,    CR_VIEW_EXT_RNWY    },
    { VIEW_EXT_CHASE,   CR_VIEW_EXT_CHASE   },
    { VIEW_EXT_CIRCLE,  CR_VIEW_EXT_CIRCLE  },
    { VIEW_EXT_STILL,   CR_VIEW_EXT_STILL   },
    { VIEW_EXT_LINEAR,  CR_VIEW_EXT_LINEAR  },
    { VIEW_FWD_HUD,     CR_VIEW_FWD_HUD     },
    { VIEW_FWD_NODISP,  CR_VIEW_FWD_NODISP  },
    { VIEW_FWD_3D,      CR_VIEW_FWD_3D      },
    { VIEW_FREE_CAM,    CR_VIEW_FREE_CAM    },
    { VIEW_EXT_RIDE,    CR_VIEW_EXT_RIDE    },
};

//
//MARK: DataRefs::dataRefDefinitionT
//

// constant used in dataRefDefinitionT::refCon but denoting to query the address of the respective variable
void* GET_VAR = reinterpret_cast<void*>(INT_MIN);

// list of all datRef definitions offered by LiveTraffic:
DataRefs::dataRefDefinitionT DATA_REFS_LT[CNT_DATAREFS_LT] = {
    // a/c information
    {"livetraffic/ac/key",                          DataRefs::LTGetAcInfoI, DataRefs::LTSetAcKey,   (void*)DR_AC_KEY, false },
    {"livetraffic/ac/num",                          DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_NUM, false },
    {"livetraffic/ac/on_gnd",                       DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_ON_GND, false },
    {"livetraffic/ac/phase",                        DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_PHASE, false },
    {"livetraffic/ac/lat",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_LAT, false },
    {"livetraffic/ac/lon",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_LON, false },
    {"livetraffic/ac/alt",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_ALT, false },
    {"livetraffic/ac/heading",                      DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_HEADING, false },
    {"livetraffic/ac/roll",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_ROLL, false },
    {"livetraffic/ac/pitch",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_PITCH, false },
    {"livetraffic/ac/speed",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_SPEED, false },
    {"livetraffic/ac/vsi",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_VSI, false },
    {"livetraffic/ac/terrain_alt",                  DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_TERRAIN_ALT, false },
    {"livetraffic/ac/height",                       DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_HEIGHT, false },
    {"livetraffic/ac/flaps",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_FLAPS, false },
    {"livetraffic/ac/gear",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_GEAR, false },
    {"livetraffic/ac/lights/beacon",                DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_BEACON, false },
    {"livetraffic/ac/lights/strobe",                DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_STROBE, false },
    {"livetraffic/ac/lights/nav",                   DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_NAV, false },
    {"livetraffic/ac/lights/landing",               DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_LANDING, false },
    {"livetraffic/ac/bearing",                      DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_BEARING, false },
    {"livetraffic/ac/dist",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_DIST, false },
    
    {"livetraffic/bulk/quick",                      DataRefs::LTGetBulkAc,  NULL,                   (void*)DR_AC_BULK_QUICK, false },
    {"livetraffic/bulk/expensive",                  DataRefs::LTGetBulkAc,  NULL,                   (void*)DR_AC_BULK_EXPENSIVE, false },

    {"livetraffic/sim/date",                        DataRefs::LTGetSimDateTime, NULL,               (void*)1, false },
    {"livetraffic/sim/time",                        DataRefs::LTGetSimDateTime, NULL,               (void*)2, false },

    {"livetraffic/ver/nr",                          GetLTVerNum,  NULL, NULL, false },
    {"livetraffic/ver/date",                        GetLTVerDate, NULL, NULL, false },
    
    // UI information
    {"livetraffic/ui/opacity",                      DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/ui/font_scale",                   DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/ui/settings/transparent",         DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/ui/aci/collapsed",                DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },

    // configuration options
    {"livetraffic/cfg/aircrafts_displayed",         DataRefs::LTGetInt, DataRefs::LTSetAircraftDisplayed, GET_VAR, false },
    {"livetraffic/cfg/auto_start",                  DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/ai_on_request",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/ai_controlled",               DataRefs::HaveAIUnderControl, NULL,             NULL,    false },
    {"livetraffic/cfg/ai_not_on_gnd",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/labels",                      DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_shown",                 DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_max_dist",              DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_visibility_cut_off",    DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/cfg/label_col_dyn",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_color",                 DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/log_level",                   DataRefs::LTGetInt, DataRefs::LTSetLogLevel,    GET_VAR, true },
    {"livetraffic/cfg/msg_area_level",              DataRefs::LTGetInt, DataRefs::LTSetLogLevel,    GET_VAR, true },
    {"livetraffic/cfg/log_list_len",                DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/max_num_ac",                  DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/fd_std_distance",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/fd_snap_taxi_dist",           DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/fd_refresh_intvl",            DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/fd_buf_period",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/ac_outdated_intvl",           DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/network_timeout",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/lnd_lights_taxi",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/hide_below_agl",              DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/hide_taxiing",                DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/hide_parking",                DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/hide_nearby_gnd",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/hide_nearby_air",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true, true },
    {"livetraffic/cfg/hide_in_replay",              DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/cfg/hide_static_twr",             DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
    {"livetraffic/cfg/copy_obj_files",              DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/remote_support",              DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/external_camera_tool",        DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
    {"livetraffic/cfg/last_check_new_ver",          DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },

    // debug options
    {"livetraffic/dbg/ac_filter",                   DataRefs::LTGetInt, DataRefs::LTSetDebugAcFilter, GET_VAR, false },
    {"livetraffic/dbg/ac_pos",                      DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/dbg/log_raw_fd",                  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/dbg/model_matching",              DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/dbg/export_fd",                   DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/dbg/export_user_ac",              DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/dbg/export_normalize_ts",         DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/dbg/export_format",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },

    // channel configuration options
    {"livetraffic/channel/fscharter/environment",   DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/open_glider/use_requrepl",DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/real_traffic/listen_port",DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/real_traffic/traffic_port",DataRefs::LTGetInt,DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/real_traffic/weather_port",DataRefs::LTGetInt,DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/fore_flight/send_port",   DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/fore_flight/user_plane",  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/fore_flight/traffic",     DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/fore_flight/interval",    DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },

    // channels, in ascending order of priority
    {"livetraffic/channel/futuredatachn/online",    DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/channel/fore_flight/sender",      DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
    {"livetraffic/channel/fscharter/online",        DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
    {"livetraffic/channel/open_glider/online",      DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
    {"livetraffic/channel/adsb_exchange/online",    DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
    {"livetraffic/channel/adsb_exchange/historic",  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/channel/open_sky/online",         DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
    {"livetraffic/channel/open_sky/ac_masterdata",  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
    {"livetraffic/channel/real_traffic/online",     DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true, true },
};

// returns the actual address of the variable within DataRefs, which stores the value of interest as per dataRef definition
// (called in case dataRefDefinitionT::refCon == GET_VAR)
void* DataRefs::getVarAddr (dataRefsLT dr)
{
    switch (dr) {
        // UI information
        case DR_UI_OPACITY:                 return &UIopacity;
        case DR_UI_FONT_SCALE:              return &UIFontScale;
        case DR_UI_SETTINGS_TRANSP:         return &SUItransp;
        case DR_UI_ACI_COLLAPSED:           return &ACIcollapsed;

        // configuration options
        case DR_CFG_AIRCRAFT_DISPLAYED:     return &bShowingAircraft;
        case DR_CFG_AUTO_START:             return &bAutoStart;
        case DR_CFG_AI_ON_REQUEST:          return &bAIonRequest;
        case DR_CFG_AI_NOT_ON_GND:          return &bAINotOnGnd;
        case DR_CFG_LABELS:                 return &labelCfg;
        case DR_CFG_LABEL_SHOWN:            return &labelShown;
        case DR_CFG_LABEL_MAX_DIST:         return &labelMaxDist;
        case DR_CFG_LABEL_VISIBILITY_CUT_OFF: return &bLabelVisibilityCUtOff;
        case DR_CFG_LABEL_COL_DYN:          return &bLabelColDynamic;
        case DR_CFG_LABEL_COLOR:            return &labelColor;
        case DR_CFG_LOG_LEVEL:              return &iLogLevel;
        case DR_CFG_MSG_AREA_LEVEL:         return &iMsgAreaLevel;
        case DR_CFG_LOG_LIST_LEN:           return &logListLen;
        case DR_CFG_MAX_NUM_AC:             return &maxNumAc;
        case DR_CFG_FD_STD_DISTANCE:        return &fdStdDistance;
        case DR_CFG_FD_SNAP_TAXI_DIST:      return &fdSnapTaxiDist;
        case DR_CFG_FD_REFRESH_INTVL:       return &fdRefreshIntvl;
        case DR_CFG_FD_BUF_PERIOD:          return &fdBufPeriod;
        case DR_CFG_AC_OUTDATED_INTVL:      return &acOutdatedIntvl;
        case DR_CFG_NETW_TIMEOUT:           return &netwTimeout;
        case DR_CFG_LND_LIGHTS_TAXI:        return &bLndLightsTaxi;
        case DR_CFG_HIDE_BELOW_AGL:         return &hideBelowAGL;
        case DR_CFG_HIDE_TAXIING:           return &hideTaxiing;
        case DR_CFG_HIDE_PARKING:           return &hideParking;
        case DR_CFG_HIDE_NEARBY_GND:        return &hideNearbyGnd;
        case DR_CFG_HIDE_NEARBY_AIR:        return &hideNearbyAir;
        case DR_CFG_HIDE_IN_REPLAY:         return &hideInReplay;
        case DR_CFG_HIDE_STATIC_TWR:        return &hideStaticTwr;
        case DR_CFG_COPY_OBJ_FILES:         return &cpyObjFiles;
        case DR_CFG_REMOTE_SUPPORT:         return &remoteSupport;
        case DR_CFG_EXTERNAL_CAMERA:        return &bUseExternalCamera;
        case DR_CFG_LAST_CHECK_NEW_VER:     return &lastCheckNewVer;

        // debug options
        case DR_DBG_AC_FILTER:              return &uDebugAcFilter;
        case DR_DBG_AC_POS:                 return &bDebugAcPos;
        case DR_DBG_LOG_RAW_FD:             return &bDebugLogRawFd;
        case DR_DBG_MODEL_MATCHING:         return &bDebugModelMatching;
        case DR_DBG_EXPORT_FD:              return &bDebugExportFd;
        case DR_DBG_EXPORT_USER_AC:         return &bDebugExportUserAc;
        case DR_DBG_EXPORT_NORMALIZE_TS:    return &bDebugExportNormTS;
        case DR_DBG_EXPORT_FORMAT:          return &eDebugExportFdFormat;

        // channel configuration options
        case DR_CFG_FSC_ENV:                return &fscEnv;
        case DR_CFG_OGN_USE_REQUREPL:       return &ognUseRequRepl;
        case DR_CFG_RT_LISTEN_PORT:         return &rtListenPort;
        case DR_CFG_RT_TRAFFIC_PORT:        return &rtTrafficPort;
        case DR_CFG_RT_WEATHER_PORT:        return &rtWeatherPort;
        case DR_CFG_FF_SEND_PORT:           return &ffSendPort;
        case DR_CFG_FF_SEND_USER_PLANE:     return &bffUserPlane;
        case DR_CFG_FF_SEND_TRAFFIC:        return &bffTraffic;
        case DR_CFG_FF_SEND_TRAFFIC_INTVL:  return &ffSendTrfcIntvl;

        default:
            // flight channels
            if (DR_CHANNEL_FIRST <= dr && dr <= DR_CHANNEL_LAST)
                return &bChannel[dr-DR_CHANNEL_FIRST];
            
            // else: must not happen
            LOG_ASSERT(NULL);
            return NULL;
    }
}

//MARK: LiveTraffic Command Refs
struct cmdRefDescrTy {
    const char* cmdName;
    const char* cmdDescr;
} CMD_REFS_LT[] = {
    {"LiveTraffic/Info_Staus_Wnd/Open",                 "Opens/Closes the Information/Status window"},
    {"LiveTraffic/Aircraft_Info_Wnd/Open",              "Opens an Aircraft Information Window"},
    {"LiveTraffic/Aircraft_Info_Wnd/Open_Popped_Out",   "Opens a popped out Aircraft Information Window (separate OS-level window)"},
    {"LiveTraffic/Aircraft_Info_Wnd/Hide_Show",         "Hides/Shows all Aircraft Information Windows, but does not close"},
    {"LiveTraffic/Aircraft_Info_Wnd/Close_All",         "Closes all Aircraft Information Windows"},
    {"LiveTraffic/Aircrafts/Display",                   "Starts/Stops display of live aircraft"},
    {"LiveTraffic/Aircrafts/TCAS_Control",              "TCAS Control toggle: Tries to take control over AI aircraft, or release it"},
    {"LiveTraffic/Aircrafts/Toggle_Labels",             "Toggle display of labels in current view"},
    {"LiveTraffic/Settings/Open",                       "Opens/Closes the Settings window"},
};

static_assert(sizeof(CMD_REFS_LT) / sizeof(CMD_REFS_LT[0]) == CNT_CMDREFS_LT,
              "cmdRefsLT and CMD_REFS_LT[] differ in number of elements");

// MARK: CSLPathCfgTy

DataRefs::CSLPathCfgTy::CSLPathCfgTy (bool b, const std::string& p) :
bEnabled(b), path(LTRemoveXPSystemPath(p))
{}

// tests path for existence
bool DataRefs::CSLPathCfgTy::exists() const
{
    if (bPathExists)                    // stored result available?
        return bPathExists > 0;         // 1 = "exists"
    // need to test the file system
    return LTNumFilesInPath(LTCalcFullPath(path)) > 0;
}

// tests path for existence, saves test result
bool DataRefs::CSLPathCfgTy::existsSave()
{
    bPathExists = exists();
    return bPathExists > 0;
}

// assign new path
const std::string& DataRefs::CSLPathCfgTy::operator= (const std::string& _p)
{
    path = LTRemoveXPSystemPath(_p);        // store (shortened) path
    existsSave();                           // check for existence
    return path;
}

//MARK: DataRefs Constructor - just plain variable init, no API calls

/// Mutex guarding updates to cached values
static std::recursive_mutex mutexDrUpdate;
/// Flag to ignore this sharedDataref callback
static bool gbIgnoreItsMe = false;

DataRefs::DataRefs ( logLevelTy initLogLevel ) :
iLogLevel (initLogLevel),
#ifdef DEBUG
bDebugAcPos (true),
#endif
SUIrect (0, 500, 690, 0),                   // (left=bottom=0 means: initially centered)
ACIrect (0, 530, 320, 0),
ILWrect (0, 400, 965, 0)
{
    // override log level in Beta and DEBUG cases
    // (config file is read later, that may reduce the level again)
#ifdef DEBUG
    iLogLevel = logDEBUG;
#else
    if ( LT_BETA_VER_LIMIT )
        iLogLevel = logDEBUG;
#endif
    
    // disable all channels
    for ( int& i: bChannel )
        i = false;

    // enable OpenSky and OGN by default
    bChannel[DR_CHANNEL_OPEN_SKY_ONLINE         - DR_CHANNEL_FIRST] = true;
    bChannel[DR_CHANNEL_OPEN_SKY_AC_MASTERDATA  - DR_CHANNEL_FIRST] = true;
    bChannel[DR_CHANNEL_OPEN_GLIDER_NET         - DR_CHANNEL_FIRST] = true;

    // Clear the dataRefs arrays
    memset ( adrXP, 0, sizeof(adrXP));
    memset ( adrLT, 0, sizeof(adrLT));
    
    // X-Plane only knows about day in the year, but not the actual year
    // so we assume something:
    //   if LocalDateDays < today's day of year --> current year
    //   if LocalDateDays >= today's day of year --> previous year
    {
        std::tm tm;
        time_t now = time(nullptr);
        localtime_s(&tm, &now);

        dataRefs.iTodaysDayOfYear = tm.tm_yday;
        
        // also compute start of this and last year for sim-time computations
        tm.tm_sec = tm.tm_min = tm.tm_hour = 0;     // 00:00:00h
        tm.tm_mday = 1;                             // 1st of
        tm.tm_mon = 0;                              // January
        tm.tm_isdst = 0;                            // no DST
        tStartThisYear = mktime_utc(tm);
        
        // previous year
        tm.tm_year--;
        tStartPrevYear = mktime_utc(tm);
    }
}

// Find and register dataRefs
bool DataRefs::Init ()
{
    // XP System Path
    char aszPath[512];
    XPLMGetSystemPath ( aszPath );
    XPSystemPath = aszPath;
    
    // Directory Separator provided by XP
    DirSeparator = XPLMGetDirectorySeparator();
    
    // my own plugin path
    pluginID = XPLMGetMyID();
    aszPath[0] = 0;
    XPLMGetPluginInfo(pluginID, NULL, aszPath, NULL, NULL);
    LTPluginPath = aszPath;
    LOG_ASSERT(!LTPluginPath.empty());
    
    // LTPluginPath is now something like "...:Resources:plugins:LiveTraffic:64:mac.xpl"
    // we now reduce the path to the beginning of the plugin:
    // remove the file name
    std::string::size_type pos = LTPluginPath.rfind(DirSeparator);
    LOG_ASSERT(pos != std::string::npos);
    LTPluginPath.erase(pos);
    // remove the 64 subdirectory, but leave the separator at the end
    pos = LTPluginPath.rfind(DirSeparator);
    LOG_ASSERT(pos != std::string::npos);
    LTPluginPath.erase(pos+1);
    
    // Create the two shared dataRefs for aircraft under camera
    if (!XPLMShareData(DATA_REFS_XP[DR_CAMERA_TCAS_IDX], xplmType_Int, ClearCameraAc, nullptr) ||
        !XPLMShareData(DATA_REFS_XP[DR_CAMERA_AC_ID],    xplmType_Int, nullptr, nullptr))
    {
        LOG_MSG(logERR,ERR_SHARED_DATAREF);
    }
    
    // Fetch all XP-provided data refs and verify if OK
    for ( int i=0; i < CNT_DATAREFS_XP; i++ )
    {
        if ( (adrXP[i] = XPLMFindDataRef (DATA_REFS_XP[i])) == NULL )
        {
            // for XP10 compatibility we accept if we don't find a few,
            // all else stays an error
            if (i != DR_VR_ENABLED &&
                i != DR_MODERN_DRIVER &&
                i != DR_CAMERA_TCAS_IDX &&      // don't insist on publishing the a/c under camera
                i != DR_CAMERA_AC_ID &&
                i != DR_PLANE_MODES_ID)         // Came with XP 11.50 only
            {
                LOG_MSG(logFATAL,ERR_DATAREF_FIND,DATA_REFS_XP[i]);
                return false;
            }
        }
    }

    // Fetch all XP-provided cmd refs and verify if OK
    for (int i = 0; i < CNT_CMDREFS_XP; i++)
    {
        if ((cmdXP[i] = XPLMFindCommand(CMD_REFS_XP[i])) == NULL)
        {
            // Not finding a command is not fatal.
            // Just be aware that it could come back zero when using.
            LOG_MSG(logWARN, ERR_DATAREF_FIND, CMD_REFS_XP[i]);
        }
    }

    // register all LiveTraffic-provided dataRefs and commands
    if (!RegisterDataAccessors() || !RegisterCommands())
        return false;
    
    // Using a modern graphics driver? (Metal, Vulkan)
    bUsingModernDriver = adrXP[DR_MODERN_DRIVER] ? XPLMGetDatai(adrXP[DR_MODERN_DRIVER]) != 0 : false;

    // read Doc8643 file (which we could live without)
    Doc8643::ReadDoc8643File();
    
    // read model_typecode file (which we could live without)
    ModelIcaoType::ReadFile();
    
    // read configuration file if any
    if (!LoadConfigFile())
        return false;
    
    // *** Flarm ac type definitions - Defaults ***
    OGNFillDefaultFlarmAcTypes();

    // *** CSL path defaults ***
    // We'll try making this fool-proof but expert-changeable:
    // There are two paths under LiveTraffic that in all normal
    // installations should be supported, especially on initial setup.
    // Experts, however, may want to remove them and keep their CSLs elsewhere
    // or just deactivate the standard directories.
    // So the logic is:
    // 1. Underlying directory _does_ exist and is not empty
    // 2. Entry in vCSLPath does _not_ yet exist
    // then: add an activated entry
    for (std::string stdCSL: { PATH_RESOURCES_CSL, PATH_RESOURCES_SCSL }) {
        // 1. Underlying directory _does_ exist and is not empty
        const std::string path (LTCalcFullPluginPath(stdCSL));
        if (LTNumFilesInPath(path) > 0) {
            // 2. Entry in vCSLPath does _not_ yet exist
            CSLPathCfgTy cfg (true, LTRemoveXPSystemPath(path));
            if (std::find(vCSLPaths.cbegin(), vCSLPaths.cend(), cfg) == vCSLPaths.cend()) {
                // insert at beginning
                vCSLPaths.emplace(vCSLPaths.cbegin(), std::move(cfg));
            }
        }
    }
    
    // Read initial cached values
    UpdateCachedValues();
    
    return true;
}

// tell DRE and DRT our dataRefs
void DataRefs::InformDataRefEditors ()
{
    // loop over all available data ref editor signatures
    for (const char* szDREditor: DATA_REF_EDITORS) {
        // find the plugin by signature
        XPLMPluginID PluginID = XPLMFindPluginBySignature(szDREditor);
        if (PluginID != XPLM_NO_PLUGIN_ID) {
            // send message regarding each dataRef we offer
            for ( const DataRefs::dataRefDefinitionT& def: DATA_REFS_LT )
                XPLMSendMessageToPlugin(PluginID,
                                        MSG_ADD_DATAREF,
                                        (void*)def.getDataName());
            // And then there are 2 shared dataRefs
            if (adrXP[DR_CAMERA_TCAS_IDX] && adrXP[DR_CAMERA_AC_ID])
            {
                XPLMSendMessageToPlugin(PluginID,
                                        MSG_ADD_DATAREF,
                                        (void*)DATA_REFS_XP[DR_CAMERA_TCAS_IDX]);
                XPLMSendMessageToPlugin(PluginID,
                                        MSG_ADD_DATAREF,
                                        (void*)DATA_REFS_XP[DR_CAMERA_AC_ID]);
            }
        }
    }
}

// Unregister (destructor would be too late for reasonable API calls)
void DataRefs::Stop ()
{
    // Stop all file writing
    if (AnyExportData()) {
        SetAllExportData(false);
        LTFlightData::ExportOpenClose();
    }
    
    // unregister our dataRefs
    for (XPLMDataRef& dr: adrLT) {
        XPLMUnregisterDataAccessor(dr);
        dr = NULL;
    }
    
    // Unshare shared dataRefs
    XPLMUnshareData(DATA_REFS_XP[DR_CAMERA_TCAS_IDX], xplmType_Int, ClearCameraAc, nullptr);
    XPLMUnshareData(DATA_REFS_XP[DR_CAMERA_AC_ID],    xplmType_Int, nullptr, nullptr);

    // save config file
    SaveConfigFile();    
}

// call XPLMRegisterDataAccessor
bool DataRefs::RegisterDataAccessors ()
{
    bool bRet = true;
    // loop over all data ref definitions
    for (int i=0; i < CNT_DATAREFS_LT; i++)
    {
        dataRefsLT eDataRef = dataRefsLT(i);
        dataRefDefinitionT& def = DATA_REFS_LT[i];
        
        // look up _and update_ refCon first if required
        // (can look up variable addresses only when object is known but not at compile time in definition of DATA_REFS_LT)
        if (def.getRefCon() == GET_VAR)
            def.setRefCon(getVarAddr(eDataRef));
        
        // register data accessor
        if ( (adrLT[i] =
              XPLMRegisterDataAccessor(def.getDataName(),       // inDataName
                                       def.getDataType(),       // inDataType
                                       def.isWriteable(),       // inIsWritable
                                       def.getDatai_f(),        // int
                                       def.setDatai_f(),
                                       def.getDataf_f(),        // float
                                       def.setDataf_f(),
                                       NULL,NULL,               // double
                                       NULL,NULL,               // int array
                                       NULL,NULL,               // float array
                                       def.getDatab_f(),        // data (read only)
                                       NULL,
                                       def.getRefCon(),         // read refCon
                                       def.getRefCon()          // write refCon
                                       )) == NULL )
        { LOG_MSG(logERR,ERR_DATAREF_ACCESSOR,def.getDataName()); bRet = false; }
    }
    return bRet;
}

// call XPLMRegisterDataAccessor
bool DataRefs::RegisterCommands()
{
    bool bRet = true;
    // loop over all data ref definitions
    for (int i=0; i < CNT_CMDREFS_LT; i++)
    {
        // register command
        if ( (cmdLT[i] =
              XPLMCreateCommand(CMD_REFS_LT[i].cmdName,
                                CMD_REFS_LT[i].cmdDescr)) == NULL )
        { LOG_MSG(logERR,ERR_CREATE_COMMAND,CMD_REFS_LT[i].cmdName); bRet = false; }
    }
    return bRet;
}

// Return current network time
// In main thred read directly from the dataRef, otherwise a cached value
float DataRefs::GetMiscNetwTime() const
{
    if (IsXPThread())
        return XPLMGetDataf(adrXP[DR_MISC_NETW_TIME]);
    else
        return lastNetwTime;
}

/// Set the view type, translating from XPViewTypes to command ref needed
void DataRefs::SetViewType(XPViewTypes vt)
{
    // search the view in the map of view types
    for (const mapViewTypesTy& mvt: MAP_VIEW_TYPES)
        if (mvt.e == vt) {
            XPLMCommandOnce(cmdXP[mvt.cr]);
            return;
        }
    LOG_MSG(logWARN, "Didn't find the requested view type %d", int(vt));
}


// return user's plane pos
positionTy DataRefs::GetUsersPlanePos(double& trueAirspeed_m, double& track ) const
{
    if (IsXPThread()) {
        // running in XP's main thread we can just return the values
        trueAirspeed_m = lastUsersTrueAirspeed;
        track = lastUsersTrack;
        return lastUsersPlanePos;
    } else {
        // in a worker thread, we need to have the lock, and copy before release
        std::unique_lock<std::recursive_mutex> lock(mutexDrUpdate);
        positionTy ret = lastUsersPlanePos;
        trueAirspeed_m = lastUsersTrueAirspeed;
        track = lastUsersTrack;
        lock.unlock();
        return ret;
    }
}

void DataRefs::UpdateUsersPlanePos ()
{
    positionTy pos
    (
     XPLMGetDatad(adrXP[DR_PLANE_LAT]),
     XPLMGetDatad(adrXP[DR_PLANE_LON]),
     XPLMGetDatad(adrXP[DR_PLANE_ELEV]),
     GetSimTime(),
     XPLMGetDataf(adrXP[DR_PLANE_HEADING]),
     XPLMGetDataf(adrXP[DR_PLANE_PITCH]),
     XPLMGetDataf(adrXP[DR_PLANE_ROLL]),
     XPLMGetDatai(adrXP[DR_PLANE_ONGRND]) ? GND_ON : GND_OFF
    );
    
    // make invalid pos invalid
    if (pos.lat() < -75 || pos.lat() > 75)
        pos.lat() = NAN;
    
    // cache the position
    lastUsersPlanePos = pos;
    
    // also fetch true airspeed and track
    lastUsersTrueAirspeed   = XPLMGetDataf(adrXP[DR_PLANE_TAS]);
    lastUsersTrack          = XPLMGetDataf(adrXP[DR_PLANE_TRACK]);
}

void DataRefs::ExportUserAcData()
{
    // only if enabled and even then only every few seconds earliest
    if (!LTFlightData::ExportOpenClose() ||
        !GetDebugExportUserAc() ||
        GetMiscNetwTime() - lastExportUserAc < EXPORT_USER_AC_PERIOD)
        return;
    lastExportUserAc = GetMiscNetwTime();
    
    // get user plane's ICAO and tail number
    char userIcao[41], userReg[41];
    XPLMGetDatab(adrXP[DR_PLANE_ICAO], userIcao, 0, sizeof(userIcao)-1);
    XPLMGetDatab(adrXP[DR_PLANE_REG],  userReg,  0, sizeof(userReg)-1);
    userIcao[sizeof(userIcao)-1] = 0;           // I trust nobody: zero-termination
    userReg[sizeof(userReg)-1] = 0;
    
    // modeS ID is only available from XP 11.50 onward
    int modeS_ID = 0;
    if (adrXP[DR_PLANE_MODES_ID])
        modeS_ID = XPLMGetDatai(adrXP[DR_PLANE_MODES_ID]);
    if (!modeS_ID)
        modeS_ID = DEFAULT_MODES_ID;

    // output a tracking data record
    char buf[1024];
    switch (dataRefs.GetDebugExportFormat()) {
        case EXP_FD_AITFC:
            snprintf(buf, sizeof(buf), "AITFC,%u,%.6f,%.6f,%.0f,%.0f,%c,%.0f,%.0f,%s,%s,%s,,,%.0f\n",
                     modeS_ID,                                                              // hexid
                     lastUsersPlanePos.lat(), lastUsersPlanePos.lon(),                      // lat, lon
                     nanToZero(dataRefs.WeatherPressureAlt_ft(lastUsersPlanePos.alt_ft())), // alt
                     XPLMGetDataf(adrXP[DR_PLANE_VVI]),                                     // vs
                     (lastUsersPlanePos.IsOnGnd() ? '0' : '1'),                             // airborne
                     lastUsersPlanePos.heading(), lastUsersTrueAirspeed * KT_per_M_per_S,   // hdg, spd
                     EXPORT_USER_CALL,                                                      // cs
                     userIcao, userReg,                                                     // type, tail,
                     lastUsersPlanePos.ts() - nanToZero(LTFlightData::fileExportTsBase));   // timestamp: if requested normalize timestamp in output
            break;
            
        case EXP_FD_RTTFC:
            snprintf(buf, sizeof(buf),
                     "RTTFC,%u,%.6f,%.6f,%.0f,%.0f,%c,%.0f,%.0f,%s,%s,%s,,,%.0f,"
                     "%s,%s,%s,%.0f,"
                     "%.1f,%.1f,-1,-1,%.0f,%.0f,"                                // IAS, TAS, (Mach, track_rate), roll, mag_heading
                     "%.2f,%.0f,%s,%s,"
                     "-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,"                        // nav_qnh, nav_altitude_mcp, nav_altitude_fms, nav_heading, nav_modes, seen, rssi, winddir, windspd, OAT, TAT
                     "0,,\n",                                                   // isICAOhex
                     // equivalent to AITFC
                     modeS_ID,                                                              // hexid
                     lastUsersPlanePos.lat(), lastUsersPlanePos.lon(),                      // lat, lon
                     nanToZero(dataRefs.WeatherPressureAlt_ft(lastUsersPlanePos.alt_ft())), // baro_alt
                     XPLMGetDataf(adrXP[DR_PLANE_VVI]),                                     // baro_rate
                     (lastUsersPlanePos.IsOnGnd() ? '1' : '0'),                             // gnd
                     lastUsersTrack, XPLMGetDataf(adrXP[DR_PLANE_GS]) * KT_per_M_per_S,     // track, gsp
                     EXPORT_USER_CALL,                                                      // cs_icao
                     userIcao, userReg,                                                     // ac_type, ac_tailno
                     lastUsersPlanePos.ts() - nanToZero(LTFlightData::fileExportTsBase),    // timestamp: if requested normalize timestamp in output
                     // additions by RTTFC
                     "XP",                                                      // source
                     EXPORT_USER_CALL,                                          // cs_iata (copy of cs_icao)
                     "lt_export",                                               // msg_type
                     nanToZero(lastUsersPlanePos.alt_ft()),                     // alt_geom
                     XPLMGetDataf(adrXP[DR_PLANE_KIAS]),                        // IAS
                     lastUsersTrueAirspeed * KT_per_M_per_S,                    // TAS
                     lastUsersPlanePos.roll(),                                  // roll
                     XPLMGetDataf(adrXP[DR_PLANE_MAG_HEADING]),                 // mag_heading
                     lastUsersPlanePos.heading(),                               // true_heading
                     XPLMGetDataf(adrXP[DR_PLANE_VVI]),                         // geom_rate
                     "none",                                                    // emergency
                     "");                                                       // category
            break;
    }

    LTFlightData::ExportAddOutput((unsigned long)std::lround(lastUsersPlanePos.ts()), buf);
}

//
//MARK: Generic Callbacks
//
// Generic get callbacks: just return the value pointed to...
int     DataRefs::LTGetInt(void* p)     { return *reinterpret_cast<int*>(p); }
float   DataRefs::LTGetFloat(void* p)   { return *reinterpret_cast<float*>(p); }

void    DataRefs::LTSetBool(void* p, int i)
{
    // If any channel changed we forward
    if (dataRefs.bChannel <= p && p <= &dataRefs.bChannel[CNT_DR_CHANNELS-1]) {
        dataRefs.SetChannelEnabled(dataRefsLT(DR_CHANNEL_FIRST + ((int*)(p)-dataRefs.bChannel)), i != 0);
        return;
    }

    // otherwise just do it
    *reinterpret_cast<int*>(p) = i != 0;
    
    // If label config changes we need to tell XPMP2
    if (p == &dataRefs.bLabelVisibilityCUtOff)
        XPMPSetAircraftLabelDist(float(dataRefs.labelMaxDist), dataRefs.bLabelVisibilityCUtOff);
    
    LogCfgSetting(p, i);
}

//
// MARK: Bulk dataRef
//

/// @brief Bulk data access to transfer a lot of a/c info to LTAPI
/// @param inRefcon DR_AC_BULK_QUICK or DR_AC_BULK_EXPENSIVE
/// @param[out] outData Points to buffer provided by caller, can be NULL to "negotiate" struct size
/// @param inStartPos array position to start with, multiple of agreed struct size
/// @param inNumBytes Number of byte to copy, multiple of agreed struct size / or caller's struct size during "negotiation"
/// @return number of bytes stored
/// @note This dataRef is "rogue" as per XPLMGetDatab API documentation:
///       It has other semantics than X-Plane's API describes.
///       When passing in NULL for `outData`, then `inNumAc` has to be filled
///       with caller's structur size and LiveTraffic returns LiveTraffic's
///       struct size.
///       LTAPI will for its convenience always use its size, makes it easier
///       to allocate array memory there.
///       LiveTraffic will only fill as much as requested and use the
///       passed in size to advance the array pointer.
///       This way version differences between LiveTraffic and LTAPI can
///       be overcome and still an array of a/c info can be transferred.
int DataRefs::LTGetBulkAc (void* inRefcon, void * outData,
                           int inStartPos, int inNumBytes)
{
    // quick or expensive one?
    dataRefsLT dr = (dataRefsLT)reinterpret_cast<long long>(inRefcon);
    LOG_ASSERT(dr == DR_AC_BULK_QUICK || dr == DR_AC_BULK_EXPENSIVE);

    // "Negotiation": In case of version differences between calling app
    // and LiveTraffic we need to be conscious of the callers struct size.
    static int size_quick = 0, size_expensive = 0;
    if (!outData)
    {
        if (dr == DR_AC_BULK_QUICK) {
            size_quick = inNumBytes;
            return (int)sizeof(LTAPIAircraft::LTAPIBulkData);
        } else {
            size_expensive = inNumBytes;
            return (int)sizeof(LTAPIAircraft::LTAPIBulkInfoTexts);
        }
    }
    
    // Validations before starting normal operations
    // Size must have been negotiated first
    int size = dr == DR_AC_BULK_QUICK ? size_quick : size_expensive;
    if (!size) return 0;

    // Positions / copy size must both be multiples of agreed size
    // (This is also a safeguard against attempts to access the bulk
    //  dataRefs by apps that don't implement proper array semantics and
    //  "negotiation", read: Don't use LTAPI.)
    if ((inStartPos % size != 0) ||
        (inNumBytes % size != 0))
        return 0;
    
    // Normal operation: loop over requested a/c
    const int startAc = 1 + inStartPos / size;      // first a/c index (1-based)
    const int endAc = startAc + (inNumBytes / size);// last+1 a/c index (passed-the-end)
    char* pOut = (char*)outData;                    // point to current output position
    int iAc = startAc;                              // current a/c index (1-based)
    for (mapLTFlightDataTy::iterator fdIter = mapFdAcByIdx(iAc);
         fdIter != mapFd.end() && iAc < endAc;
         // iterator to next FlighData _with_ aircraft; advance output pointer
         fdIter = mapFdNextWithAc(fdIter), iAc++, pOut += size)
    {
        // copy data of the current aircraft
        const LTAircraft& ac = *fdIter->second.GetAircraft();
        if (dr == DR_AC_BULK_QUICK)
            ac.CopyBulkData ((LTAPIAircraft::LTAPIBulkData*)pOut, (size_t)size);
        else
            ac.CopyBulkData((LTAPIAircraft::LTAPIBulkInfoTexts*)pOut, (size_t)size);
    }
    
    // how many bytes copied?
    return (iAc - startAc) * size;
}


//
//MARK: Aircraft Information
//

// finds the a/c ptr based on the key (transpIcao)
bool DataRefs::FetchPAc ()
{
    // if there is no a/c defined yet, BUT there is a debug a/c defined
    // then we use that debug a/c
    if ( keyAc.empty() )
        keyAc = GetDebugAcFilter();
    
    // short-cut if there is still no key
    if ( keyAc.empty() )
        return false;
    
    // find that key's element
    LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO, keyAc);
    mapLTFlightDataTy::const_iterator fdIter = mapFd.find(fdKey);
    if (fdIter != mapFd.end()) {
        // found, save ptr to a/c
        pAc = fdIter->second.GetAircraft();
        // that pointer might be NULL if a/c has not yet been created!
        return pAc != nullptr;
    }

    // not found, clear all ptr/keys
    pAc = nullptr;
    keyAc.clear();
    return false;
}


// Set a key to define which a/c we return data for
// can be an array index or a transpIcao
void DataRefs::LTSetAcKey(void*, int key)
{
    // sanity check
    if ( key < 0 || (unsigned)key > MAX_TRANSP_ICAO )
        return;
    
    // default: nothing found
    dataRefs.pAc = nullptr;
    dataRefs.keyAc.clear();
    
    // 0 means reset
    if (key == 0) {
        return;
    }
    // key can be either index or the decimal representation of an transpIcao
    // for any number below number of a/c displayed we assume: index
    else if ( key <= dataRefs.cntAc )
    {
        // let's find the i-th aircraft
        mapLTFlightDataTy::iterator fdIter = mapFdAcByIdx(key);
        if (fdIter != mapFd.end()) {
            dataRefs.keyAc = fdIter->second.key();
            dataRefs.pAc = fdIter->second.GetAircraft();
            return;
        }
    }
    // so we deal with a transpIcao code
    else
    {
        // the key into mapFd is a 6-digit hex string
        char keyHex[10];
        snprintf ( keyHex, sizeof(keyHex), "%06X", (unsigned int)key );
        
        // now try to find that key and set the pAc ptr
        dataRefs.keyAc = keyHex;
        dataRefs.FetchPAc();
    }
}

// return info 
int DataRefs::LTGetAcInfoI(void* p)
{
    // don't need an a/c pointer for this one:
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_NUM: return dataRefs.cntAc;
    }

    // verify a/c ptr is available
    if ( !dataRefs.pAc && !dataRefs.FetchPAc() )
        return 0;

    // return a/c info
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_KEY: return (int)dataRefs.pAc->fd.key().num;
        case DR_AC_ON_GND: return dataRefs.pAc->IsOnGrnd();
        case DR_AC_PHASE: return dataRefs.pAc->GetFlightPhase();
        case DR_AC_LIGHTS_BEACON: return dataRefs.pAc->GetLightsBeacon();
        case DR_AC_LIGHTS_STROBE: return dataRefs.pAc->GetLightsStrobe();
        case DR_AC_LIGHTS_NAV: return dataRefs.pAc->GetLightsNav();
        case DR_AC_LIGHTS_LANDING: return dataRefs.pAc->GetLightsLanding();
        default:
            LOG_ASSERT(false);              // not allowed...we should handle all value types!
            return 0;
    }
}

float DataRefs::LTGetAcInfoF(void* p)
{
    if ( !dataRefs.pAc && !dataRefs.FetchPAc() )
        return 0.0;
    
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_LAT:         return (float)dataRefs.pAc->GetPPos().lat();
        case DR_AC_LON:         return (float)dataRefs.pAc->GetPPos().lon();
        case DR_AC_ALT:         return (float)dataRefs.pAc->GetAlt_ft();
        case DR_AC_HEADING:     return (float)dataRefs.pAc->GetHeading();
        case DR_AC_ROLL:        return (float)dataRefs.pAc->GetRoll();
        case DR_AC_PITCH:       return (float)dataRefs.pAc->GetPitch();
        case DR_AC_SPEED:       return (float)dataRefs.pAc->GetSpeed_kt();
        case DR_AC_VSI:         return (float)dataRefs.pAc->GetVSI_ft();
        case DR_AC_TERRAIN_ALT: return (float)dataRefs.pAc->GetTerrainAlt_ft();
        case DR_AC_HEIGHT:      return (float)dataRefs.pAc->GetPHeight_ft();
        case DR_AC_FLAPS:       return (float)dataRefs.pAc->GetFlapsPos();
        case DR_AC_GEAR:        return (float)dataRefs.pAc->GetGearPos();
        case DR_AC_BEARING:     return (float)dataRefs.pAc->GetVecView().angle;
        case DR_AC_DIST:        return (float)dataRefs.pAc->GetVecView().dist;
        default:
            LOG_ASSERT(false);              // not allowed...we should handle all value types!
            return 0.0;
    }
}

// sets the data of the shared datarefs to point to `ac` as the current aircraft under the camera
void DataRefs::SetCameraAc(const LTAircraft* pCamAc)
{
    // requires that we could define and find the shared dataRef
    if (!adrXP[DR_CAMERA_TCAS_IDX] ||
        !adrXP[DR_CAMERA_AC_ID])
        return;
    
    // These are shared dataRefs, so any "set" can trigger a notification.
    // By convention, we set modeS_id first, so it is guaranteed that
    // by the time we set tcas_id (and potentially trigger the recommended
    // notification callback) both modeS_id and tcas_id are available and valid.
    gbIgnoreItsMe = true;                       // don't react to the notification callbacks!
    XPLMSetDatai(adrXP[DR_CAMERA_AC_ID],
                 pCamAc ? (int)pCamAc->fd.key().num : 0);
    XPLMSetDatai(adrXP[DR_CAMERA_TCAS_IDX],
                 pCamAc ? (int)pCamAc->GetTcasTargetIdx() : 0);
    gbIgnoreItsMe = false;
}

// shared dataRef callback: Whenever someone else writes to the shared dataRef we clear our a/c camera information
void DataRefs::ClearCameraAc(void*)
{
    if (gbIgnoreItsMe)                      // only if it's somebody else!
        return;
    
    // if camera is controlled externally anyway, then we can try finding
    // that aircraft and _make_ it the a/c under the camera...LiveTraffic will
    // not switch on the camera but will displayer the active camera button
    if (dataRefs.ShallUseExternalCamera() && dataRefs.adrXP[DR_CAMERA_AC_ID]) {
        char keyHex[10];
        snprintf ( keyHex, sizeof(keyHex), "%06X",
                  (unsigned int)XPLMGetDatai(dataRefs.adrXP[DR_CAMERA_AC_ID]) );
        mapLTFlightDataTy::iterator iter = mapFdSearchAc(keyHex);
        LTAircraft::SetCameraAcExternally(iter != mapFd.end() ? iter->second.GetAircraft() : nullptr);
    }
    else
        // Clear our aircraft under the camera
        LTAircraft::SetCameraAcExternally(nullptr);
}

//
//MARK: Config Options
//

/// Compute simulated time (seconds since Unix epoch, including fractionals)
void DataRefs::UpdateSimTime()
{
    // we use current system time (no matter what X-Plane simulates),
    // but lagging behind by the buffering time
    using namespace std::chrono;
    lastSimTime =
        // system time in seconds with fractionals
        GetSysTime()
        // minus the buffering time
        - GetFdBufPeriod()
#ifdef DEBUG
        // minus debugging delay
        - fdBufDebug
#endif
        // plus the offset compared to network (this corrects for wrong system clock time as compared to reality)
        + GetChTsOffset();
}

// Current sim time as a human readable string, including 10th of seconds
std::string DataRefs::GetSimTimeString() const
{
    return ts2string(GetSimTime());
}


int DataRefs::LTGetSimDateTime(void* p)
{
    long bDateTime = (long)reinterpret_cast<long long>(p);
    
    // current simulated time, converted to structure
    time_t simTime = (time_t)dataRefs.GetSimTime();
    std::tm tm;
    gmtime_s(&tm, &simTime);

    // asked for date? Return date as human readable number yyyymmdd:
    if ( bDateTime == 1 ) {
        return
            (tm.tm_year + 1900) * 10000 +           // year
            (tm.tm_mon + 1)     *   100 +           // month
            (tm.tm_mday);                           // day of month
    } else {
        // asked for time, return time as human readable number hhmmss:
        return
            tm.tm_hour          * 10000 +           // hour
            tm.tm_min           *   100 +           // minute
            tm.tm_sec;                              // second
    }
}

// Enable/Disable display of aircraft
void DataRefs::LTSetAircraftDisplayed(void*, int i)
{ dataRefs.SetAircraftDisplayed (i); }

void DataRefs::SetAircraftDisplayed ( int bEnable )
{
    // Do what we are asked to do
    if ( bEnable )
    {
        bShowingAircraft = LTMainShowAircraft();
    }
    else
    {
        LTMainHideAircraft();
        bShowingAircraft = 0;
    }
    
    // update menu item's checkmark
    MenuUpdateAllItemStatus();
}

int DataRefs::ToggleAircraftDisplayed ()
{
    // set the switch to the other value
    SetAircraftDisplayed ( !bShowingAircraft );
    // return the new status
    return bShowingAircraft;
}

// Log Level
void DataRefs::LTSetLogLevel(void* p, int i)
{
    LOG_ASSERT (p == &dataRefs.iLogLevel || p == &dataRefs.iMsgAreaLevel);
    if ( p == &dataRefs.iMsgAreaLevel )
        dataRefs.SetMsgAreaLevel(i);
    else
        dataRefs.SetLogLevel(i);
}

void DataRefs::SetLogLevel ( int i )
{
    if ( i <= logDEBUG )       iLogLevel = logDEBUG;
    else if ( i >= logFATAL )  iLogLevel = logFATAL ;
    else                       iLogLevel = logLevelTy(i);
}

void DataRefs::SetMsgAreaLevel ( int i )
{
    if ( i <= logDEBUG )       iMsgAreaLevel = logDEBUG;
    else if ( i >= logFATAL )  iMsgAreaLevel = logFATAL ;
    else                       iMsgAreaLevel = logLevelTy(i);
}

// switch usage of historic data, return success
void DataRefs::ForceDataReload ()
{
    // if we change this setting while running
    // we 'simulate' a re-initialization
    if (pluginState >= STATE_ENABLED) {
        // remove all existing aircraft
        bool bShowAc = dataRefs.AreAircraftDisplayed();
        dataRefs.SetAircraftDisplayed(false);

        // disable myself / stop all connections
        LTMainDisable();
        
        // create the connections to flight data
        if ( LTMainEnable() ) {
            // display aircraft (if that was the case previously)
            dataRefs.SetAircraftDisplayed(bShowAc);
        }
    }
}

// set a config value and validate (all of) them
void DataRefs::LTSetCfgValue (void* p, int val)
{ dataRefs.SetCfgValue(p, val); }

bool DataRefs::SetCfgValue (void* p, int val)
{
    // If fdSnapTaxiDist changes we might want to enable/disable airport reading
    int oldFdSnapTaxiDist = fdSnapTaxiDist;
    
    // we don't exactly know which parameter p points to...
    // ...we just set it, validate all of them, and reset in case validation fails
    int oldVal = *reinterpret_cast<int*>(p);
    *reinterpret_cast<int*>(p) = val;
    
    // Setting the refresh interval (a value more or less likely to be touched by users)
    // might require adapting buffering period and A/c outdated interval, too
    if (p == &fdRefreshIntvl) {
        if (fdBufPeriod < fdRefreshIntvl)
            fdBufPeriod = fdRefreshIntvl;
        if (acOutdatedIntvl < 2*fdRefreshIntvl)
            acOutdatedIntvl = 2*fdRefreshIntvl;
    }
    
    // any configuration value invalid?
    if (labelColor      < 0                 || labelColor       > 0xFFFFFF ||
#ifdef DEBUG
        maxNumAc        < 1                 || maxNumAc         > MAX_NUM_AIRCRAFT   ||
#else
        maxNumAc        < 5                 || maxNumAc         > MAX_NUM_AIRCRAFT   ||
#endif
        fdStdDistance   < 5                 || fdStdDistance    > 100   ||
        fdRefreshIntvl  < 10                || fdRefreshIntvl   > 180   ||
        fdBufPeriod     < fdRefreshIntvl    || fdBufPeriod      > 180   ||
        acOutdatedIntvl < 2*fdRefreshIntvl  || acOutdatedIntvl  > 180   ||
        fdSnapTaxiDist  < 0                 || fdSnapTaxiDist   > 50    ||
        netwTimeout     < 10                ||
        hideBelowAGL    < 0                 || hideBelowAGL     > MDL_ALT_MAX ||
        hideNearbyGnd   < 0                 || hideNearbyGnd    > 500   ||
        hideNearbyAir   < 0                 || hideNearbyAir    > 5000  ||
        rtListenPort    < 1024              || rtListenPort     > 65535 ||
        rtTrafficPort   < 1024              || rtTrafficPort    > 65535 ||
        rtWeatherPort   < 1024              || rtWeatherPort    > 65535 ||
        ffSendPort      < 1024              || ffSendPort       > 65535 ||
        fscEnv          < 0                 || fscEnv           > 1
        )
    {
        // undo change
        *reinterpret_cast<int*>(p) = oldVal;
        return false;
    }
    
    // Special handling for fdSnapTaxiDist:
    if (oldFdSnapTaxiDist != fdSnapTaxiDist)        // snap taxi dist did change
    {
        // switched from on to off?
        if (oldFdSnapTaxiDist > 0 && fdSnapTaxiDist == 0)
            LTAptDisable();
        // switched from off to on?
        else if (oldFdSnapTaxiDist == 0 && fdSnapTaxiDist > 0)
            LTAptEnable();
    }
    
    // If label draw distance changes we need to tell XPMP2
    if (p == &labelMaxDist)
        XPMPSetAircraftLabelDist(float(labelMaxDist), bLabelVisibilityCUtOff);
    
    // success
    LogCfgSetting(p, val);
    return true;
}

// generic config access (not as fast as specific access, but good for rare access)
bool  DataRefs::GetCfgBool  (dataRefsLT dr)
{
    return GetCfgInt(dr) != 0;
}

int   DataRefs::GetCfgInt   (dataRefsLT dr)
{
    assert(0 <= dr && dr < CNT_DATAREFS_LT);
    return DATA_REFS_LT[dr].getDatai();
}

float DataRefs::GetCfgFloat (dataRefsLT dr)
{
    assert(0 <= dr && dr < CNT_DATAREFS_LT);
    return DATA_REFS_LT[dr].getDataf();
}


// Find dataRef definition based on the pointer to its member variable
const DataRefs::dataRefDefinitionT* DataRefs::FindDRDef (void* p)
{
    for (const DataRefs::dataRefDefinitionT& dr: DATA_REFS_LT)
        if (dr.getRefCon() == p)
            return &dr;
    return nullptr;
}

// Save config(change) info to the log
void DataRefs::LogCfgSetting (void* p, int val)
{
    // only if logging set to DEBUG
    if (dataRefs.GetLogLevel() > logDEBUG)
        return;
    
    // Find the dataRef definition
    const dataRefDefinitionT* pDR = FindDRDef(p);
    if (pDR && pDR->isDebugLogging())
        LOG_MSG(logDEBUG, "%s = %d", pDR->getDataName(), val);
}


// more than 24h passed since last version check?
bool DataRefs::NeedNewVerCheck () const
{
    if (!lastCheckNewVer)
        return true;
    
    // 'now' in hours since the epoch
    int nowH = (int)std::chrono::duration_cast<std::chrono::hours>
    (std::chrono::system_clock::now().time_since_epoch()).count();
    
    return nowH - lastCheckNewVer > LT_NEW_VER_CHECK_TIME;
}

// saves the fact that we just checked for a new version
void DataRefs::SetLastCheckedNewVerNow ()
{
    lastCheckNewVer = (int)
    std::chrono::duration_cast<std::chrono::hours>
    (std::chrono::system_clock::now().time_since_epoch()).count();
}

// return color into a RGB array as XP likes it
void DataRefs::GetLabelColor (float outColor[4]) const
{
    conv_color(labelColor, outColor);
}

//
//MARK: Debug Options
//

// return current a/c filter
std::string DataRefs::GetDebugAcFilter() const
{
    char key[7];
    if ( !uDebugAcFilter ) return std::string();
    
    // convert to hex representation
    snprintf(key,sizeof(key),"%06X",uDebugAcFilter);
    return key;
}

// sets A/C filter
void DataRefs::LTSetDebugAcFilter( void* /*inRefcon*/, int i )
{
    bool bWasFilterDefined = dataRefs.uDebugAcFilter != 0;
    
    // match hex range of transpIcao codes
    if ( 0x000000 <= i && (unsigned)i <= MAX_TRANSP_ICAO ) {
        dataRefs.uDebugAcFilter = unsigned(i);
        
        // also set the key for the a/c info datarefs
        if (i > 0x000000) {
            LTSetAcKey(reinterpret_cast<void*>(long(DR_AC_KEY)), i);
            LOG_MSG(logWARN,DBG_FILTER_AC,
                    dataRefs.GetDebugAcFilter().c_str());
        } else if (bWasFilterDefined) {
            LOG_MSG(logWARN,DBG_FILTER_AC_REMOVED);
        }
    }
}

//
// MARK: DataRefs::dataRefDefinitionT
//

// get the actual current value (by calling the getData?_f function)
int DataRefs::dataRefDefinitionT::getDatai () const
{
    if (dataType != xplmType_Int || ifRead == NULL)
        return 0;

    LOG_ASSERT(refCon != GET_VAR);
    return (ifRead)(refCon);
}

float DataRefs::dataRefDefinitionT::getDataf () const
{
    if (dataType != xplmType_Float || ffRead == NULL)
        return NAN;

    LOG_ASSERT(refCon != GET_VAR);
    return (ffRead)(refCon);
}


// compiles string for storage in config file
std::string DataRefs::dataRefDefinitionT::GetConfigString() const
{
    // short-cut: not for saving in config file
    if (!bCfgFile)
        return std::string();
    
    // put together dataRef's dataName and
    // add the current value, which might be int or float
    switch (dataType) {
        case xplmType_Int:
            return dataName + ' ' + std::to_string(getDatai());
        case xplmType_Float:
            return dataName + ' ' + std::to_string(getDataf());
        default:
            // else: must not happen
            LOG_ASSERT(NULL);
            return std::string();
    }
}


// write values to the dataRef
void DataRefs::dataRefDefinitionT::setData (int i)
{
    if (dataType == xplmType_Int && ifWrite != NULL) {
        LOG_ASSERT(refCon != GET_VAR);
		(ifWrite) (refCon, i);
    }
}

void DataRefs::dataRefDefinitionT::setData (float f)
{
    if (dataType == xplmType_Float && ffWrite != NULL) {
        LOG_ASSERT(refCon != GET_VAR);
		(ffWrite) (refCon, f);
    }
}

// assumes the string is a number, which will be converted to the
// appropriate type and then passed on to the other setData functions
void DataRefs::dataRefDefinitionT::setData (const std::string& s)
{
    try {
        switch (dataType) {
            case xplmType_Int:
                setData (std::stoi(s));
                break;
            case xplmType_Float:
                setData (std::stof(s));
                break;
            default:
                // else: must not happen
                LOG_ASSERT(NULL);
        }
    }
    catch (const std::invalid_argument& e) {
        LOG_MSG(logWARN,ERR_CFG_FILE_VALUE,dataName.c_str(),s.c_str(),e.what());
    }
    catch (const std::out_of_range& e) {
        LOG_MSG(logWARN,ERR_CFG_FILE_VALUE,dataName.c_str(),s.c_str(),e.what());
    }
}

//MARK: Increse/decrease number of a/c

// increase number of a/c, update menu item with that number
int DataRefs::IncNumAc()
{
    return ++cntAc;
}

// decreses number of aircraft
// which by itself is simplistic, but as the just removed a/c
// _could_ be the one we are monitoring in our dataRefs (we don't know)
// we better invalidate the pAc ptr and force the dataRef to find the a/c again
int DataRefs::DecNumAc()
{
    pAc=nullptr;
    return --cntAc;
}


//MARK: Config File


bool DataRefs::LoadConfigFile()
{
    /// GNF_COUNT is not available in DataRefs.h (due to order of include files), make _now_ sure that aFlarmToIcaoAcTy has the correct size
    assert (aFlarmToIcaoAcTy.size() == size_t(FAT_UAV)+1);

    // which conversion to do with the (older) version of the config file?
    enum cfgFileConvE { CFG_NO_CONV=0, CFG_V3 } conv = CFG_NO_CONV;
    
    // open a config file
    std::string sFileName (LTCalcFullPath(PATH_CONFIG_FILE));
    std::ifstream fIn (sFileName);
    if (!fIn) {
        // if there is no config file just return...that's no problem, we use defaults
        if (errno == ENOENT)
            return true;
        
        // something else happened
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
		LOG_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                sFileName.c_str(), sErr);
        return false;
    }
    
    // *** VERSION ***
    // first line is supposed to be the version, read entire line
    std::vector<std::string> ln;
    std::string lnBuf;
    if (!safeGetline(fIn, lnBuf) ||                     // read a line
        (ln = str_tokenize(lnBuf, " ")).size() != 2 ||  // split into two words
        ln[0] != LIVE_TRAFFIC)                          // 1. is LiveTraffic
    {
        LOG_MSG(logERR, ERR_CFG_FILE_VER, sFileName.c_str(), lnBuf.c_str());
        return false;
    }
    
    // 2. is version / test for older version for which a conversion is to be done?
    if (ln[1] == LT_VERSION)                conv = CFG_NO_CONV;
    else {
        // Version update!
        SHOW_MSG(logMSG, MSG_LT_UPDATED, LT_VERSION);
        // Any pre-v3 version?
        if (ln[1][0] < '3')
            conv = CFG_V3;
    }
    
    // *** Delete LiveTraffic_imgui.prf? ***
    if (conv == CFG_V3)                 // added column to the aircraft list
        std::remove(IMGUI_INI_PATH);
    
    // *** DataRefs ***
    // then follow the config entries
    // supposingly they are just 'dataRef <space> value',
    // but to prevent misuse we certainly validate that we support
    // those dataRefs.
    // The file could end with a [CSLPaths] section, too.
    int errCnt = 0;
    while (fIn && errCnt <= ERR_CFG_FILE_MAXWARN) {
        // read line and break into tokens, delimited by spaces
        safeGetline(fIn, lnBuf);
        // skip empty lines without warning
        if (lnBuf.empty()) continue;
        // break out of loop if reading the start of another section indicated by [ ]
        if (lnBuf.front() == '[' && lnBuf.back() == ']') break;

        // otherwise should be 2 tokens
        ln = str_tokenize(lnBuf, " ");
        if (ln.size() != 2) {
            // wrong number of words in that line
            LOG_MSG(logWARN,ERR_CFG_FILE_WORDS, sFileName.c_str(), lnBuf.c_str());
            errCnt++;
            continue;
        }
        
        // did read a name and a value?
        const std::string& sDataRef = ln[0];
        std::string& sVal     = ln[1];
        if (!sDataRef.empty() && !sVal.empty()) {
            // verify that we know that name
            dataRefDefinitionT* i = std::find_if(std::begin(DATA_REFS_LT),
                                                 std::end(DATA_REFS_LT),
                                                 [&](const DataRefs::dataRefDefinitionT& def)
                                                 { return def.getDataNameStr() == sDataRef; } );
            if ( i != nullptr && i != std::cend(DATA_REFS_LT) &&
                 i->isCfgFile())        // and it is a configurable one
            {
                // conversion of older config file formats
                switch (conv) {
                    case CFG_NO_CONV: break;
                    case CFG_V3:
                        if (*i == DATA_REFS_LT[DR_CFG_RT_TRAFFIC_PORT]) {
                            // With v3 preferred port changes from 49003 to 49005
                            if (sVal == "49003")
                                sVal = "49005";
                        }
                        break;
                }
                
                // *** valid config entry, now process it ***
                i->setData(sVal);
            }
            
            // *** Window positions ***
            else if (sDataRef == CFG_WNDPOS_SUI)
                SUIrect.set(sVal);
            else if (sDataRef == CFG_WNDPOS_ACI)
                ACIrect.set(sVal);
            else if (sDataRef == CFG_WNDPOS_ILW)
                ILWrect.set(sVal);
            
            // *** Strings ***
            else if (sDataRef == CFG_DEFAULT_AC_TYPE)
                SetDefaultAcIcaoType(sVal);
            else if (sDataRef == CFG_DEFAULT_CAR_TYPE)
                SetDefaultCarIcaoType(sVal);
            else if (sDataRef == CFG_OPENSKY_USER)
                SetOpenSkyUser(sVal);
            else if (sDataRef == CFG_OPENSKY_PWD)
                SetOpenSkyPwd(Cleartext(sVal));
            else if (sDataRef == CFG_ADSBEX_API_KEY)
                // With v3 we start obfuscating the API key
                SetADSBExAPIKey(conv == CFG_V3 ? sVal : Cleartext(sVal));
            else if (sDataRef == CFG_FSC_USER)
                SetFSCharterUser(sVal);
            else if (sDataRef == CFG_FSC_PWD)
                SetFSCharterPwd(Cleartext(sVal));
            else
            {
                // unknown config entry, ignore
                LOG_MSG(logWARN,ERR_CFG_FILE_IGNORE,
                        sDataRef.c_str(), sFileName.c_str());
                errCnt++;
            }
        }
        
    }
    
    // *** [FlarmAcTypes] ***
    // maybe there's a [FlarmAcTypes] section?
    if (fIn && errCnt <= ERR_CFG_FILE_MAXWARN &&
        lnBuf == CFG_FLARM_ACTY_SECTION)
    {
        
        // loop until EOF
        while (fIn && errCnt <= ERR_CFG_FILE_MAXWARN)
        {
            // read line and break into tokens, delimited by spaces
            safeGetline(fIn, lnBuf);
            // skip empty lines without warning
            if (lnBuf.empty()) continue;
            // break out of loop if reading the start of another section indicated by [ ]
            if (lnBuf.front() == '[' && lnBuf.back() == ']') break;

            // line has to start with a reasonable number, followed by =, followed by a string
            ln = str_tokenize(lnBuf, "=");
            if (ln.size() != 2 ||
                !between<int>(std::stoi(ln[0]), FAT_UNKNOWN, FAT_UAV) ||
                ln[1].size() < 2)
            {
                LOG_MSG(logWARN, ERR_CFG_CSL_INVALID, sFileName.c_str(), lnBuf.c_str());
                errCnt++;
                continue;
            }
            
            // the second part is again an array of icao types, separated by space or comma or so
            aFlarmToIcaoAcTy[std::stoul(ln[0])] = str_tokenize(ln[1], " ,;/");
        }
    }
    
    // *** [CSLPaths] ***
    // maybe there's a [CSLPath] section?
    if (fIn && errCnt <= ERR_CFG_FILE_MAXWARN &&
        lnBuf == CFG_CSL_SECTION)
    {
        
        // loop until EOF
        while (fIn && errCnt <= ERR_CFG_FILE_MAXWARN)
        {
            // read line and break into tokens, delimited by spaces
            safeGetline(fIn, lnBuf);
            // skip empty lines without warning
            if (lnBuf.empty()) continue;
            // break out of loop if reading the start of another section indicated by [ ]
            if (lnBuf.front() == '[' && lnBuf.back() == ']') break;

            // line has to start with 0 or 1 and | to separate "enabled?" from path
            ln = str_tokenize(lnBuf, "|");
            if (ln.size() != 2 ||
                ln[0].size() != 1 ||
                (ln[0][0] != '0' && ln[0][0] != '1'))
            {
                LOG_MSG(logWARN, ERR_CFG_CSL_INVALID, sFileName.c_str(), lnBuf.c_str());
                errCnt++;
                continue;
            }
            
            // enabled?
            bool bEnabled = ln[0][0] == '1';
            
            // add the path to the list (unvalidated!) if no duplicate
            if (std::find(vCSLPaths.begin(), vCSLPaths.end(), ln[1]) == vCSLPaths.end())
                vCSLPaths.emplace_back(bEnabled, std::move(ln[1]));
        }
    }

    // problem was not just eof?
    if (!fIn && !fIn.eof()) {
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        LOG_MSG(logERR, ERR_CFG_FILE_READ,
                sFileName.c_str(), sErr);
        return false;
    }
    
    // close file
    fIn.close();

    // too many warnings?
    if (errCnt > ERR_CFG_FILE_MAXWARN) {
        LOG_MSG(logERR, ERR_CFG_FILE_READ,
                sFileName.c_str(), ERR_CFG_FILE_TOOMANY);
        return false;
    }
    
    // looks like success
    return true;
}

bool DataRefs::SaveConfigFile()
{
    // open an output config file
    std::string sFileName (LTCalcFullPath(PATH_CONFIG_FILE));
    std::ofstream fOut (sFileName, std::ios_base::out | std::ios_base::trunc);
    if (!fOut) {
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
		SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_OUT,
                 sFileName.c_str(), sErr);
        return false;
    }
    
    // *** VERSION ***
    // save application and version first
    fOut << LIVE_TRAFFIC << ' ' << LT_VERSION << '\n';
    
    // *** DataRefs ***
    // loop over our LiveTraffic values and store those meant to be stored
    for (const DataRefs::dataRefDefinitionT& def: DATA_REFS_LT)
        if (def.isCfgFile())                   // only for values which are to be saved
            fOut << def.GetConfigString() << '\n';
    
    // *** Window positions ***
    fOut << CFG_WNDPOS_SUI << ' ' << SUIrect << '\n';
    fOut << CFG_WNDPOS_ACI << ' ' << ACIrect << '\n';
    fOut << CFG_WNDPOS_ILW << ' ' << ILWrect << '\n';
    
    // *** Strings ***
    fOut << CFG_DEFAULT_AC_TYPE << ' ' << GetDefaultAcIcaoType() << '\n';
    fOut << CFG_DEFAULT_CAR_TYPE << ' ' << GetDefaultCarIcaoType() << '\n';
    if (!sOpenSkyUser.empty())
        fOut << CFG_OPENSKY_USER << ' ' << sOpenSkyUser << '\n';
    if (!sOpenSkyPwd.empty())
        fOut << CFG_OPENSKY_PWD << ' ' << Obfuscate(sOpenSkyPwd) << '\n';
    if (!GetADSBExAPIKey().empty())
        fOut << CFG_ADSBEX_API_KEY << ' ' << Obfuscate(GetADSBExAPIKey()) << '\n';
    if (!sFSCUser.empty())
        fOut << CFG_FSC_USER << ' ' << sFSCUser << '\n';
    if (!sFSCPwd.empty())
        fOut << CFG_FSC_PWD << ' ' << Obfuscate(sFSCPwd) << '\n';

    // *** [FlarmAcTypes] ***
    fOut << '\n' << CFG_FLARM_ACTY_SECTION << '\n';
    for (size_t i = 0; i < dataRefs.aFlarmToIcaoAcTy.size(); i++) {
        fOut << i << " =";
        for (const std::string& s: dataRefs.aFlarmToIcaoAcTy[i])
            fOut << ' ' << s;
        fOut << '\n';
    }

    // *** [CSLPatchs] ***
    // add section of CSL paths to the end
    if (!vCSLPaths.empty()) {
        fOut << '\n' << CFG_CSL_SECTION << '\n';
        for (const DataRefs::CSLPathCfgTy& cslPath: vCSLPaths)
            if (!cslPath.empty())
                fOut << (cslPath.enabled() ? "1|" : "0|") <<
                cslPath.getPath() << '\n';
    }
    
    // some error checking towards the end
    if (!fOut) {
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_WRITE,
                 sFileName.c_str(), sErr);
        fOut.close();
        return false;
    }

    // flush (which we explicitely did not do for each line for performance reasons) and close
    fOut.flush();
    fOut.close();
        
    return true;
}

// Load a CSL package interactively from a given path
bool DataRefs::LoadCSLPackage(const std::string& _path)
{
    // path could be relative to X-Plane
    const std::string path = LTCalcFullPath(_path);
    
    // doesn't exist? has no files?
    if (LTNumFilesInPath(path) < 1) {
        SHOW_MSG(logERR, ERR_CFG_CSL_EMPTY, path.c_str());
    }
    else
    {
        // try loading the package
        const char* cszResult = XPMPLoadCSLPackage(path.c_str());
        
        // Addition of CSL package failed?
        if ( cszResult[0] ) {
            SHOW_MSG(logERR,ERR_XPMP_ADD_CSL, _path.c_str(), cszResult);
        } else {
            SHOW_MSG(logMSG,MSG_CSL_PACKAGE_LOADED, _path.c_str());
            // successfully loaded...now update all CSL models in use
            LTFlightData::UpdateAllModels();
            return true;
        }
    }
    
    // didn't work for some reason
    return false;
}


// sets the default a/c icao type after validation with Doc8643
bool DataRefs::SetDefaultAcIcaoType(const std::string type)
{
    if (Doc8643::get(type) != DOC8643_EMPTY) {
        sDefaultAcIcaoType = type;
        XPMPSetDefaultPlaneICAO(type.c_str());  // inform libxplanemp
        LOG_MSG(logINFO,CFG_DEFAULT_AC_TYP_INFO,sDefaultAcIcaoType.c_str());
        return true;
    }

    // invalid type
    SHOW_MSG(logWARN,ERR_CFG_AC_DEFAULT,type.c_str(),
             sDefaultAcIcaoType.c_str());
    return false;
}

// sets default car type. this is a fake value, so no validation agains Doc8643
// but still needs to be 1 through 4 characters long
bool DataRefs::SetDefaultCarIcaoType(const std::string type)
{
    if (1 <= type.length() && type.length() <= 4) {
        sDefaultCarIcaoType = type;
        XPMPSetDefaultPlaneICAO(nullptr, type.c_str());  // inform libxplanemp
        LOG_MSG(logINFO,CFG_DEFAULT_CAR_TYP_INFO,sDefaultCarIcaoType.c_str());
        return true;
    }
    
    // invalid
    LOG_MSG(logWARN,ERR_CFG_CAR_DEFAULT,type.c_str(),
            sDefaultCarIcaoType.c_str());
    return false;
}

// Set the channel's status
void DataRefs::SetChannelEnabled (dataRefsLT ch, bool bEnable)
{
    bChannel[ch - DR_CHANNEL_FIRST] = bEnable;
    
    // If OpenSky Tracking is enabled then make sure OpenSky Master is also
    if (IsChannelEnabled(DR_CHANNEL_OPEN_SKY_ONLINE))
        bChannel[DR_CHANNEL_OPEN_SKY_AC_MASTERDATA - DR_CHANNEL_FIRST] = true;
    
    // if OGN just got enabled download a fresh a/c list from OGN
    if (bEnable && ch == DR_CHANNEL_OPEN_GLIDER_NET)
        OGNDownloadAcList();
    
    // if a channel got disabled check if any tracking data channel is left
    if (!bEnable && AreAircraftDisplayed() &&   // something just got disabled? And A/C are currently displayed?
        !LTFlightDataAnyTrackingChEnabled())    // but no tracking data channel left active?
    {
        SHOW_MSG(logERR, ERR_CH_NONE_ACTIVE);
    }
}


// how many channels are enabled?
int DataRefs::CntChannelEnabled () const
{
    return (int)std::count(std::begin(bChannel),
                           std::end(bChannel),
                           1);
}

// add another offset to the offset calculation (network time vs. system clock)
// This is just a simple average calculation, not caring too much about rounding issues
void DataRefs::ChTsOffsetAdd (double aNetTS)
{
    // after some calls we keep our offset stable (each chn has the chance twice)
    if (!ChTsAcceptMore())
        return;
    
    // for TS to become an offset we need to remove current system time;
    // yes...since we received that timestamp time has passed...but this is all
    // not about milliseconds...if it is plus/minus 5s we are good enough!
    using namespace std::chrono;
    aNetTS -=
        // system time in microseconds
        double(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count())
        // divided by 1000000 to create seconds with fractionals
        / 1000000.0;
    
    // now for the average
    chTsOffset *= chTsOffsetCnt;
    chTsOffset += aNetTS;
    chTsOffset /= ++chTsOffsetCnt;
}

//
// MARK: Update cached values for thread-safe access
//

// update all cached values for thread-safe access
void DataRefs::UpdateCachedValues ()
{
    std::lock_guard<std::recursive_mutex> lock(mutexDrUpdate);

    lastNetwTime = XPLMGetDataf(adrXP[DR_MISC_NETW_TIME]);
    lastReplay = XPLMGetDatai(adrXP[DR_REPLAY_MODE]);
    lastVREnabled =                         // is VR enabled?
    #ifdef DEBUG
        bSimVREntered ? true :              // simulate some aspects of VR
    #endif
        // for XP10 compatibility we accept not having this dataRef
        adrXP[DR_VR_ENABLED] ? XPLMGetDatai(adrXP[DR_VR_ENABLED]) != 0 : false;

    UpdateSimTime();
    UpdateViewPos();
    UpdateUsersPlanePos();
    UpdateSimWind();
    ExportUserAcData();
}


// Local (in sim!) wind at user's plane
void DataRefs::UpdateSimWind ()
{
    lastWind.angle = (double)XPLMGetDataf(adrXP[DR_WIND_DIR]);
    lastWind.speed = (double)XPLMGetDataf(adrXP[DR_WIND_SPEED]);
}

//
// MARK: Processed values (static functions)
//

// last read camera position
positionTy DataRefs::lastCamPos;

// fetch and save the camera's position in world coordinates
void DataRefs::UpdateViewPos()
{
    XPLMCameraPosition_t camPos = {NAN, NAN, NAN, 0.0f, 0.0f, 0.0f, 0.0f};
    // get the dataref values for current view pos, which are in local coordinates
    XPLMReadCameraPosition(&camPos);
    // convert to world coordinates and return them
    double lat, lon, alt;
    XPLMLocalToWorld(camPos.x, camPos.y, camPos.z,
                     &lat, &lon, &alt);
    
    lastCamPos = positionTy(lat, lon, alt,
                            dataRefs.GetSimTime(),
                            camPos.heading,
                            camPos.pitch,
                            camPos.roll);
}

// return the camera's position in world coordinates
positionTy DataRefs::GetViewPos()
{
    // If in main thread just return latest pos
    if (dataRefs.IsXPThread())
        return lastCamPos;
    else
    {
        // calling from another thread: safely copy the cached value
        std::unique_lock<std::recursive_mutex> lock(mutexDrUpdate);
        positionTy camPos = lastCamPos;
        lock.unlock();
        return camPos;
    }
}

// return the direction the camera is looking to
double DataRefs::GetViewHeading()
{
    // If in main thread just return latest heading
    if (dataRefs.IsXPThread())
        return lastCamPos.heading();
    else
    {
        // calling from another thread: safely copy the cached value
        std::unique_lock<std::recursive_mutex> lock(mutexDrUpdate);
        double h = lastCamPos.heading();
        lock.unlock();
        return h;
    }
}

// in current situation, shall we draw labels?
bool DataRefs::ShallDrawLabels() const
{
    // user doesn't want labels in VR but is in VR mode? -> no labels
    if (!labelShown.bVR && IsVREnabled())
        return false;
    
    // now depends on internal or external view
    return IsViewExternal() ? labelShown.bExternal : labelShown.bInternal;
}

// in current situation, toggle label drawing
bool DataRefs::ToggleLabelDraw()
{
    // Situation = VR?
    if (IsVREnabled())
        return (labelShown.bVR = !labelShown.bVR);
    // Situation = External View?
    if (IsViewExternal())
        return (labelShown.bExternal = !labelShown.bExternal);
    // Situation = Internal View
    else
        return (labelShown.bInternal = !labelShown.bInternal);
}

//
// MARK: Weather
//

constexpr float WEATHER_TRY_PERIOD = 120.0f;            ///< [s] Don't _try_ to read weather more often than this
constexpr float WEATHER_UPD_PERIOD = 600.0f;            ///< [s] Weather to be updated at leas this often
constexpr double WEATHER_UPD_DIST_M = 25.0 * M_per_NM;  ///< [m] Weather to be updated if moved more than this far from last weather update position
constexpr float  WEATHER_SEARCH_RADIUS_NM = 25;         ///< [nm] Search for latest weather reports in this radius

// check if weather updated needed, then do
bool DataRefs::WeatherUpdate ()
{
    // protected against updates from the weather thread
    std::lock_guard<std::recursive_mutex> lock(mutexDrUpdate);

    // Our current camera position
    positionTy camPos = GetViewPos();
    camPos.LocalToWorld();
    
    // So...do we need an update?
    if (// never try more often than TRY_PERIOD says to avoid flooding
        (lastWeatherAttempt + WEATHER_TRY_PERIOD < GetMiscNetwTime()) &&
        (   // had no weather yet at all?
            std::isnan(lastWeatherPos.lat()) ||
            // moved far away from last weather pos?
            camPos.dist(lastWeatherPos) > WEATHER_UPD_DIST_M ||
            // enough time passed since last weather update?
            lastWeatherUpd + WEATHER_UPD_PERIOD < GetMiscNetwTime()
        ))
    {
        // Trigger a weather update; this is an asynch operation
        lastWeatherAttempt = GetMiscNetwTime();
        return ::WeatherUpdate(camPos, WEATHER_SEARCH_RADIUS_NM);   // travel distances [m] doubles as weather search distance [nm]
    }
    return false;
}

// Called by the asynch process spawned by ::WeatherUpdate to inform us of the weather
void DataRefs::SetWeather (float hPa, float lat, float lon,
                           const std::string& stationId,
                           const std::string& METAR)
{
    // protected against reads from the main thread
    std::lock_guard<std::recursive_mutex> lock(mutexDrUpdate);
    
    // Compute the new altitude correction and save its position and time
    altPressCorr_ft = (hPa - HPA_STANDARD) * FT_per_HPA;
    lastWeatherPos = GetViewPos();              // here and...
    lastWeatherUpd = GetMiscNetwTime();         // ...now
    lastWeatherStationId = stationId;
    lastWeatherMETAR = METAR;
    
    // If we didn't get a station id we can find a matching airport now
    if (lastWeatherStationId.empty() && !std::isnan(lat) && !std::isnan(lon)) {
        lastWeatherStationId = GetNearestAirportId(lat, lon);
    }
    
    // Did weather change?
    if (!dequal(lastWeatherHPA, hPa)) {
        LOG_MSG(logINFO, INFO_WEATHER_UPDATED, hPa,
                lastWeatherStationId.c_str(),
                lastWeatherPos.lat(), lastWeatherPos.lon());
    }
    
    // Finally: Save the new pressure and potentially export it with the tracking data
    lastWeatherHPA = hPa;
    LTFlightData::ExportLastWeather();
}

// Thread-safely gets current weather info
void DataRefs::GetWeather (float& hPa, std::string& stationId, std::string& METAR)
{
    // protected against reads from the main thread
    std::lock_guard<std::recursive_mutex> lock(mutexDrUpdate);
    
    hPa = lastWeatherHPA;
    stationId = lastWeatherStationId;
    METAR = lastWeatherMETAR;
}
