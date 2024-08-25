/// @file       LTWeather.cpp
/// @brief      Set X-Plane weather / Fetch real weather information from AWC
///
/// @details    Set X-Plane weather based on weather information received from channels like RealTraffic
/// @note       Available with X-Plane 12 only
/// @see        https://developer.x-plane.com/article/weather-datarefs-in-x-plane-12/
///
/// @details    Fetch METAR information from Aviation Weather,
///             mainly to learn about QNH for altitude correction
/// @see        https://aviationweather.gov/data/api/#/Dataserver/dataserverMetars
/// @see        Example request: Latest weather somewhere around EDDL, limited to the fields we are interested in:
///             https://aviationweather.gov//cgi-bin/data/dataserver.php?requestType=retrieve&dataSource=metars&format=xml&boundingBox=45.5,0.5,54.5,9,5&hoursBeforeNow=2&mostRecent=true&fields=raw_text,station_id,latitude,longitude,altim_in_hg
/// @details    Example response:
///                 @code{.xml}
///                     <response xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XML-Schema-instance" version="1.2" xsi:noNamespaceSchemaLocation="http://aviationweather.gov/adds/schema/metar1_2.xsd">
///                     <request_index>71114711</request_index>
///                     <data_source name="metars"/>
///                     <request type="retrieve"/>
///                     <errors/>
///                     <warnings/>
///                     <time_taken_ms>249</time_taken_ms>
///                     <data num_results="1">
///                     <METAR>
///                     <raw_text>KL18 222035Z AUTO 23009G16KT 10SM CLR A2990 RMK AO2</raw_text>
///                     <station_id>KL18</station_id>
///                     <latitude>33.35</latitude>
///                     <longitude>-117.25</longitude>
///                     <altim_in_hg>29.899607</altim_in_hg>
///                     </METAR>
///                     </data>
///                     </response>
///                 @endcode
///
///             Example empty response (no weather reports found):
///                 @code{.xml}
///                 <response xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XML-Schema-instance" version="1.2" xsi:noNamespaceSchemaLocation="http://aviationweather.gov/adds/schema/metar1_2.xsd">
///                 <request_index>60222216</request_index>
///                 <data_source name="metars"/>
///                 <request type="retrieve"/>
///                 <errors/>
///                 <warnings/>
///                 <time_taken_ms>7</time_taken_ms>
///                 <data num_results="0"/>
///                 </response>
///                 @endcode
///
///             Example error response:
///                 @code{.xml}
///                 <response xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XML-Schema-instance" version="1.2" xsi:noNamespaceSchemaLocation="http://aviationweather.gov/adds/schema/metar1_2.xsd">
///                 <request_index>59450188</request_index>
///                 <data_source name="metars"/>
///                 <request type="retrieve"/>
///                 <errors>
///                 <error>Query must be constrained by time</error>
///                 </errors>
///                 <warnings/>
///                 <time_taken_ms>0</time_taken_ms>
///                 </response>
///                 @endcode
///
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

#include "LiveTraffic.h"

// metaf header-only library for parsing METAR
// see https://github.com/nnaumenko/metaf
#include "metaf.hpp"

//
// MARK: Set X-Plane Weather
//

/// Represents access to dataRefs
struct XDR {
    XPLMDataRef             dr = NULL;      ///< handle to the dataRef
    bool find (const char* sDR)             ///< find the dataRef, return if found
    { return (dr = XPLMFindDataRef(sDR)) != NULL; }
    bool good () const { return dr != NULL; }
};

/// Represents a float dataRef
struct XDR_float : public XDR {
    float get () const { return XPLMGetDataf(dr); }     ///< read the float value
    operator float () const { return get(); }           ///< read the float value
    void set (float v) { if (std::isfinite(v)) XPLMSetDataf(dr, v); }   ///< set the float value
    XDR_float& operator = (float v) { set(v); return *this; }
};

/// Represents a float array dataRef
template<std::size_t N>
struct XDR_farr : public XDR {
    void get (std::array<float,N>& v) const { XPLMGetDatavf(dr,v.data(),0,N ); }  ///< get data from X-Plane into local storage
    void set (const std::array<float,N> & v)            ///< write data to X-Plane (if element 0 is finite, ie. not NAN)
    {
        // Set only if _any_ of the elements is finite
        if (std::any_of(v.begin(), v.end(), [](float f){return std::isfinite(f);})) {
            // but we must not set any non-finite values, that can crash XP or other plugins,
            // so we make a copy that transforms all non-finite values to 0.0
            std::array<float,N> cv;
            std::transform(v.begin(), v.end(), cv.begin(),
                           [](const float f){ return std::isfinite(f) ? f : 0.0f; });
            // and then send that copy to XP:
            XPLMSetDatavf(dr,cv.data(),0,N );
        }
    }
};

/// Represents an int dataRef
struct XDR_int : public XDR {
    int get () const { return XPLMGetDatai(dr); }       ///< read the int value
    operator int () const { return get(); }             ///< read the int value
    void set (int v) {if(v >= 0) XPLMSetDatai(dr, v);}  ///< set the int value (assuming only non-negative are valid)
    XDR_int& operator = (int v) { set(v); return *this; }
};

    
// Weather Region dataRefs
XDR_float    wdr_visibility_reported_sm;    ///< float      y    statute_miles  = 0. The reported visibility (e.g. what the METAR/weather window says).
XDR_float    wdr_sealevel_pressure_pas;     ///< float      y    pascals        Pressure at sea level, current planet
XDR_float    wdr_sealevel_temperature_c;    ///< float      y    degreesC       The temperature at sea level.
XDR_float    wdr_qnh_base_elevation;        ///< float      y    float          Base elevation for QNH. Takes into account local physical variations from a spheroid.
XDR_float    wdr_qnh_pas;                   ///< float      y    float          Base elevation for QNH. Takes into account local physical variations from a spheroid.
XDR_float    wdr_rain_percent;              ///< float      y    ratio          [0.0 - 1.0] The percentage of rain falling.
XDR_int      wdr_change_mode;               ///< int        y    enum           How the weather is changing. 0 = Rapidly Improving, 1 = Improving, 2 = Gradually Improving, 3 = Static, 4 = Gradually Deteriorating, 5 = Deteriorating, 6 = Rapidly Deteriorating, 7 = Using Real Weather
XDR_int      wdr_weather_source;            ///< int        n    enum           What system is currently controlling the weather. 0 = Preset, 1 = Real Weather, 2 = Controlpad, 3 = Plugin.
XDR_int      wdr_update_immediately;        ///< int        y    bool           If this is true, any weather region changes EXCEPT CLOUDS will take place immediately instead of at the next update interval (currently 60 seconds).
XDR_farr<13> wdr_atmosphere_alt_levels_m;   ///< float[13]  n    meters         The altitudes for the thirteen atmospheric layers returned in other sim/weather/region datarefs.
XDR_farr<13> wdr_wind_altitude_msl_m;       ///< float[13]  y    meters         >= 0. The center altitude of this layer of wind in MSL meters.
XDR_farr<13> wdr_wind_speed_msc;            ///< float[13]  y    kts            >= 0. The wind speed in knots.
XDR_farr<13> wdr_wind_direction_degt;       ///< float[13]  y    degrees        [0 - 360] The direction the wind is blowing from in degrees from true north clockwise.
XDR_farr<13> wdr_shear_speed_msc;           ///< float[13]  y    kts            >= 0. The gain from the shear in knots.
XDR_farr<13> wdr_shear_direction_degt;      ///< float[13]  y    degrees        [0 - 360]. The direction for a wind shear, per above.
XDR_farr<13> wdr_turbulence;                ///< float[13]  y    float          [0 - 10] A turbulence factor, 0-10, the unit is just a scale.
XDR_farr<13> wdr_dewpoint_deg_c;            ///< float[13]  y    degreesC       The dew point at specified levels in the atmosphere.
XDR_farr<13> wdr_temperature_altitude_msl_m;///< float[13]  y    meters         >= 0. Altitudes used for the temperatures_aloft_deg_c array.
XDR_farr<13> wdr_temperatures_aloft_deg_c;  ///< float[13]  y    degreesC       Temperature at pressure altitudes given in sim/weather/region/atmosphere_alt_levels. If the surface is at a higher elevation, the ISA difference at wherever the surface is is assumed to extend all the way down to sea level.
XDR_farr<3>  wdr_cloud_type;                ///< float[3]   y    float          Blended cloud types per layer. 0 = Cirrus, 1 = Stratus, 2 = Cumulus, 3 = Cumulo-nimbus. Intermediate values are to be expected.
XDR_farr<3>  wdr_cloud_coverage_percent;    ///< float[3]   y    float          Cloud coverage per layer, range 0 - 1.
XDR_farr<3>  wdr_cloud_base_msl_m;          ///< float[3]   y    meters         MSL >= 0. The base altitude for this cloud layer.
XDR_farr<3>  wdr_cloud_tops_msl_m;          ///< float[3]   y    meters         >= 0. The tops for this cloud layer.
XDR_float    wdr_tropo_temp_c;              ///< float      y    degreesC       Temperature at the troposphere
XDR_float    wdr_tropo_alt_m;               ///< float      y    meters         Altitude of the troposphere
XDR_float    wdr_thermal_rate_ms;           ///< float      y    m/s            >= 0 The climb rate for thermals.
XDR_float    wdr_wave_amplitude;            ///< float      y    meters         Amplitude of waves in the water (height of waves)
XDR_float    wdr_wave_length;               ///< float      n    meters         Length of a single wave in the water - not writable starting in v12
XDR_float    wdr_wave_speed;                ///< float      n    m/s            Speed of water waves - not writable starting in v12
XDR_float    wdr_wave_dir;                  ///< float      y    degrees        Direction of waves.
XDR_float    wdr_runway_friction;           ///< int/float  y    enum           The friction constant for runways (how wet they are).  Dry = 0, wet(1-3), puddly(4-6), snowy(7-9), icy(10-12), snowy/icy(13-15)
XDR_float    wdr_variability_pct;           ///< float      y    ratio          How randomly variable the weather is over distance. Range 0 - 1.
XDR_int      wdr_weather_preset;            ///< int        y    enum           Read the UI weather preset that is closest to the current conditions, or set an UI preset. Clear(0), VFR Few(1), VFR Scattered(2), VFR Broken(3), VFR Marginal(4), IFR Non-precision(5), IFR Precision(6), Convective(7), Large-cell Storms(8)

bool WeatherInitDataRefs ()
{
    return true
    && wdr_visibility_reported_sm     .find("sim/weather/region/visibility_reported_sm")
    && wdr_sealevel_pressure_pas      .find("sim/weather/region/sealevel_pressure_pas")
    && wdr_sealevel_temperature_c     .find("sim/weather/region/sealevel_temperature_c")
    && wdr_qnh_base_elevation         .find("sim/weather/region/qnh_base_elevation")
    && wdr_qnh_pas                    .find("sim/weather/region/qnh_pas")
    && wdr_rain_percent               .find("sim/weather/region/rain_percent")
    && wdr_change_mode                .find("sim/weather/region/change_mode")
    && wdr_weather_source             .find("sim/weather/region/weather_source")
    && wdr_update_immediately         .find("sim/weather/region/update_immediately")
    && wdr_atmosphere_alt_levels_m    .find("sim/weather/region/atmosphere_alt_levels_m")
    && wdr_wind_altitude_msl_m        .find("sim/weather/region/wind_altitude_msl_m")
    && wdr_wind_speed_msc             .find("sim/weather/region/wind_speed_msc")
    && wdr_wind_direction_degt        .find("sim/weather/region/wind_direction_degt")
    && wdr_shear_speed_msc            .find("sim/weather/region/shear_speed_msc")
    && wdr_shear_direction_degt       .find("sim/weather/region/shear_direction_degt")
    && wdr_turbulence                 .find("sim/weather/region/turbulence")
    && wdr_dewpoint_deg_c             .find("sim/weather/region/dewpoint_deg_c")
    && wdr_temperature_altitude_msl_m .find("sim/weather/region/temperature_altitude_msl_m")
    && wdr_temperatures_aloft_deg_c   .find("sim/weather/region/temperatures_aloft_deg_c")
    && wdr_cloud_type                 .find("sim/weather/region/cloud_type")
    && wdr_cloud_coverage_percent     .find("sim/weather/region/cloud_coverage_percent")
    && wdr_cloud_base_msl_m           .find("sim/weather/region/cloud_base_msl_m")
    && wdr_cloud_tops_msl_m           .find("sim/weather/region/cloud_tops_msl_m")
    && wdr_tropo_temp_c               .find("sim/weather/region/tropo_temp_c")
    && wdr_tropo_alt_m                .find("sim/weather/region/tropo_alt_m")
    && wdr_thermal_rate_ms            .find("sim/weather/region/thermal_rate_ms")
    && wdr_wave_amplitude             .find("sim/weather/region/wave_amplitude")
    && wdr_wave_length                .find("sim/weather/region/wave_length")
    && wdr_wave_speed                 .find("sim/weather/region/wave_speed")
    && wdr_wave_dir                   .find("sim/weather/region/wave_dir")
    && wdr_runway_friction            .find("sim/weather/region/runway_friction")
    && wdr_variability_pct            .find("sim/weather/region/variability_pct")
    && wdr_weather_preset             .find("sim/weather/region/weather_preset")
    ;
}

// Constructor sets all arrays to all `NAN`
LTWeather::LTWeather()
{
    wdr_atmosphere_alt_levels_m.get(atmosphere_alt_levels_m);
    wind_altitude_msl_m.fill(NAN);
    wind_speed_msc.fill(NAN);
    wind_direction_degt.fill(NAN);
    shear_speed_msc.fill(NAN);
    shear_direction_degt.fill(NAN);
    turbulence.fill(NAN);
    dewpoint_deg_c.fill(NAN);
    temperature_altitude_msl_m.fill(NAN);
    temperatures_aloft_deg_c.fill(NAN);
    cloud_type.fill(NAN);
    cloud_coverage_percent.fill(NAN);
    cloud_base_msl_m.fill(NAN);
    cloud_tops_msl_m.fill(NAN);
}

// Set the given weather in X-Plane
void LTWeather::Set () const
{
    wdr_update_immediately.set(update_immediately);

    wdr_change_mode.set(3);                         // 3 - Static (this also switches off XP's real weather)

    wdr_visibility_reported_sm.set(visibility_reported_sm);
    wdr_sealevel_temperature_c.set(sealevel_temperature_c);
    wdr_qnh_base_elevation.set(qnh_base_elevation);
    if (!std::isnan(qnh_pas))                       // prefer QNH as it is typically as reported by METAR
        wdr_qnh_pas.set(qnh_pas);
    else
        wdr_sealevel_pressure_pas.set(sealevel_pressure_pas);
    wdr_rain_percent.set(rain_percent);
    wdr_wind_altitude_msl_m.set(wind_altitude_msl_m);
    wdr_wind_speed_msc.set(wind_speed_msc);
    wdr_wind_direction_degt.set(wind_direction_degt);
    wdr_shear_speed_msc.set(shear_speed_msc);
    wdr_shear_direction_degt.set(shear_direction_degt);
    wdr_turbulence.set(turbulence);
    wdr_dewpoint_deg_c.set(dewpoint_deg_c);
    wdr_temperature_altitude_msl_m.set(temperature_altitude_msl_m);
    wdr_temperatures_aloft_deg_c.set(temperatures_aloft_deg_c);
    wdr_cloud_type.set(cloud_type);
    wdr_cloud_coverage_percent.set(cloud_coverage_percent);
    wdr_cloud_base_msl_m.set(cloud_base_msl_m);
    wdr_cloud_tops_msl_m.set(cloud_tops_msl_m);
    wdr_tropo_temp_c.set(tropo_temp_c);
    wdr_tropo_alt_m.set(tropo_alt_m);
    wdr_thermal_rate_ms.set(thermal_rate_ms);
    wdr_wave_amplitude.set(wave_amplitude);
    wdr_wave_dir.set(wave_dir);
    wdr_runway_friction.set(float(runway_friction));
    wdr_variability_pct.set(variability_pct);

    if (dataRefs.ShallLogWeather())
        Log(update_immediately ? "Set Weather immediately" : "Set Weather");
}

// Read weather from X-Plane
void LTWeather::Get (const std::string& logMsg)
{
    visibility_reported_sm      = wdr_visibility_reported_sm.get();
    sealevel_pressure_pas       = wdr_sealevel_pressure_pas.get();
    sealevel_temperature_c      = wdr_sealevel_temperature_c.get();
    qnh_base_elevation          = wdr_qnh_base_elevation.get();
    qnh_pas                     = wdr_qnh_pas.get();
    rain_percent                = wdr_rain_percent.get();
    wdr_atmosphere_alt_levels_m.get(atmosphere_alt_levels_m);
    wdr_wind_altitude_msl_m.get(wind_altitude_msl_m);
    wdr_wind_speed_msc.get(wind_speed_msc);
    wdr_wind_direction_degt.get(wind_direction_degt);
    wdr_shear_speed_msc.get(shear_speed_msc);
    wdr_shear_direction_degt.get(shear_direction_degt);
    wdr_turbulence.get(turbulence);
    wdr_dewpoint_deg_c.get(dewpoint_deg_c);
    wdr_temperature_altitude_msl_m.get(temperature_altitude_msl_m);
    wdr_temperatures_aloft_deg_c.get(temperatures_aloft_deg_c);
    wdr_cloud_type.get(cloud_type);
    wdr_cloud_coverage_percent.get(cloud_coverage_percent);
    wdr_cloud_base_msl_m.get(cloud_base_msl_m);
    wdr_cloud_tops_msl_m.get(cloud_tops_msl_m);
    tropo_temp_c                = wdr_tropo_temp_c.get();
    tropo_alt_m                 = wdr_tropo_alt_m.get();
    thermal_rate_ms             = wdr_thermal_rate_ms.get();
    wave_amplitude              = wdr_wave_amplitude.get();
    wave_dir                    = wdr_wave_dir.get();
    runway_friction             = (int)std::lround(wdr_runway_friction.get());
    variability_pct             = wdr_variability_pct.get();
    update_immediately          = bool(wdr_update_immediately.get());
    change_mode                 = wdr_change_mode.get();
    weather_source              = wdr_weather_source.get();
    weather_preset              = wdr_weather_preset.get();
    
    pos                         = dataRefs.GetViewPos();
    
    if (!logMsg.empty())
        Log(logMsg);
    else if (dataRefs.ShallLogWeather())
        Log("Got Weather");
}

// Log values to Log.txt
void LTWeather::Log (const std::string& msg) const
{
    std::ostringstream lOut;
    lOut << std::fixed << std::setprecision(1);
    lOut <<  "pos:         " << std::string(pos)         << "\n";
    lOut <<  "vis:         " << visibility_reported_sm   << "sm, ";
    lOut <<  "sea_pressure: "<< sealevel_pressure_pas    << "pas, ";
    lOut <<  "sea_temp: "    << sealevel_temperature_c   << "C, ";
    lOut <<  "qnh_base_elev: "<<qnh_base_elevation       << "?, ";
    lOut <<  "qnh_pas:"      << qnh_pas                  << ", ";
    lOut <<  "rain: "        << rain_percent             << "%,\n";

#define LOG_WARR(label, var, unit)                          \
lOut << label;                                              \
for (const float& f: var) lOut << std::setw(8) << f << " "; \
lOut << unit "\n";
    
    LOG_WARR("wind_alt:    ",   wind_altitude_msl_m,        "m");
    LOG_WARR("wind_speed:  ",   wind_speed_msc,             "kts");
    LOG_WARR("wind_dir:    ",   wind_direction_degt,        "deg");
    LOG_WARR("shear_speed: ",   shear_speed_msc,            "kts");
    LOG_WARR("shear_dir:   ",   shear_direction_degt,       "deg");
    LOG_WARR("turbulence:  ",   turbulence,                 "");
    LOG_WARR("deqpoint:    ",   dewpoint_deg_c,             "C");
    LOG_WARR("temp_alt:    ",   temperature_altitude_msl_m, "m");
    LOG_WARR("temp:        ",   temperatures_aloft_deg_c,   "C");
    LOG_WARR("cloud_type:  ",   cloud_type,                 "0=Ci, 1=St, 2=Cu, 3=Cb");
    LOG_WARR("cloud_cover: ",   cloud_coverage_percent,     "%");
    LOG_WARR("cloud_base:  ",   cloud_base_msl_m,           "m");
    LOG_WARR("cloud_tops:  ",   cloud_tops_msl_m,           "m");
    lOut << std::setw(0);

    lOut <<  "tropo_temp:  " << tropo_temp_c             << "c, ";
    lOut <<  "tropo_alt: "   << tropo_alt_m              << "m, ";
    lOut <<  "thermal_rate: "<< thermal_rate_ms          << "m/s, ";
    lOut <<  "wave_amp: "    << wave_amplitude           << "m, ";
    lOut <<  "wave_dir: "    << wave_dir                 << "deg, ";
    lOut <<  "rwy_fric: "    << runway_friction          << ", ";
    lOut <<  "variability: " << variability_pct          << "%\n";
    
    // if any of the follow is filled (e.g. after getting weather from X-Plane for logging purposes)
    if (change_mode >= 0 || weather_source >= 0 || weather_preset >= 0) {
        lOut << "immediately: " << update_immediately << ", ";
        lOut << "change mode: " << change_mode << ", ";
        lOut << "source: " << weather_source << ", ";
        lOut << "preset: " << weather_preset << "\n";
    }
    
    lOut <<  "METAR: "       << (metar.empty() ? "(nil)" : metar.c_str()) << "\n";

    LOG_MSG(logDEBUG, "%s\n%s", msg.c_str(), lOut.str().c_str());
}

// Compute interpolation settings to fill one array from a differently sized one
std::array<LTWeather::InterpolSet,13> LTWeather::ComputeInterpol (const std::vector<float>& from,
                                                                  const std::array<float,13>& to)
{
    std::array<LTWeather::InterpolSet,13> ret;
    for (size_t i = 0; i < to.size(); ++i)                          // for each element of the destination
    {
        const float f = to[i];
        if (f <= from.front()) {                                    // Smaller than even lowest `from` value->use first value only
            ret[i].i = 0;
            ret[i].w = 1.0f;
        }
        else if (f >= from.back()) {                                // Larger than even largest `from` value
            ret[i].i = from.size()-2;                               // Use only last value
            ret[i].w = 0.0f;                                        // (Start with 2nd last index, but with weight 0, that means, use the next index, the last one, with weight 1)
        }
        else {
            // Search for f in `from`, find where it would fit inbetween
            // (as border cases are covered above must find something)
            const auto iter = std::adjacent_find(from.begin(), from.end(),
                                                 [f](const float a, const float b)
                                                 { return a <= f && f <= b; });
            if(iter != from.end()) {
                ret[i].i = (size_t)std::distance(from.begin(), iter);
                ret[i].w = 1.0f - (f - *iter)/(*(iter+1) - *iter);
            }
        }
    }
    
    return ret;
}

// Fill values from a differently sized input vector based on interpolation
void LTWeather::Interpolate (const std::array<InterpolSet,13>& aInterpol,
                             const std::vector<float>& from,
                             std::array<float,13>& to)
{
    for (size_t i = 0; i < aInterpol.size(); ++i) {
        const InterpolSet& is = aInterpol[i];
        if (is.i+1 < from.size()) {
            to[i] = from[is.i] * is.w + from[is.i+1] * (1-is.w);
        }
        else to[i] = NAN;
    }
}

// Fill directions/headings from a differently sized input vector based on interpolation
// Headings need to be interpolated separately as the average of 359 and 001 is 000 rather than 180...
void LTWeather::InterpolateDir (const std::array<InterpolSet,13>& aInterpol,
                                const std::vector<float>& from,
                                std::array<float,13>& to)
{
    for (size_t i = 0; i < aInterpol.size(); ++i) {
        const InterpolSet& is = aInterpol[i];
        if (is.i+1 < from.size()) {
            double h1 = HeadingNormalize(from[is.i]);
            double h2 = HeadingNormalize(from[is.i+1]);
            const double hDiff = h1 - h2;
            if (hDiff > 180.0)              // case of 359 and 001
                h2 += 360.0;
            else if (hDiff < -180.0)        // case of 001 and 359
                h1 += 360.0;
            to[i] = (float)HeadingNormalize(h1 * is.w + h2 * (1-is.w));
        }
        else to[i] = NAN;
    }
}

// Get interpolated value for a given altitude
float LTWeather::GetInterpolated (const std::array<float,13>& levels_m,
                                  const std::array<float,13>& vals,
                                  float alt_m)
{
    // Smaller than lowest altitude?
    if (alt_m <= levels_m.front())  return vals.front();
    // Find if it's something inbetween
    for (size_t i = 0; i < levels_m.size() - 1; ++i) {
        if (levels_m[i] <= alt_m && alt_m <= levels_m[i+1]) {
            const float w = (alt_m - levels_m[i]) / (levels_m[i+1] - levels_m[i]);  // Weight: How close are we to the i+1'th value?
            return (1.0f-w) * vals[i] + w * vals[i+1];
        }
    }
    // must be larger than last
    return vals.back();
}

// Fill value equally up to given altitude
void LTWeather::FillUp (const std::array<float,13>& levels_m,
                        std::array<float,13>& to,
                        float alt_m,
                        float val,
                        bool bInterpolateNext)
{
    std::size_t i = 0;
    for (; i < levels_m.size() && levels_m[i] <= alt_m; ++i)    // while level is less than alt_m set value
        to[i] = val;
    // i now points to first element _after_ those we set to 'val'
    if (bInterpolateNext && i >= 1 && i+1 < levels_m.size()) {        // to align with values above interpolate the next value
        to[i] = to[i-1] + (to[i+1]-to[i-1])*(levels_m[i]-levels_m[i-1])/(levels_m[i+1]-levels_m[i-1]);
    }
}

// Fill value equally to the given minimum up to given altitude (ie. don't overwrite values that are already larger)
void LTWeather::FillUpMin (const std::array<float,13>& levels_m,
                           std::array<float,13>& to,
                           float alt_m,
                           float valMin,
                           bool bInterpolateNext)
{
    std::size_t i = 0;
    bool bSetLastVal = false;                                   // did we set/modify the last value?
    for (; i < levels_m.size() && levels_m[i] <= alt_m; ++i)     // while level is less than alt_m set value
        if ((bSetLastVal = to[i] < valMin))
            to[i] = valMin;
    // i now points to first element _after_ those we set to 'val'
    if (bInterpolateNext && bSetLastVal && i >= 1 && i+1 < levels_m.size()) {        // to align with values above interpolate the next value
        to[i] = to[i-1] + (to[i+1]-to[i-1])*(levels_m[i]-levels_m[i-1])/(levels_m[i+1]-levels_m[i-1]);
    }
}

//
// MARK: Parse METAR
//

using namespace metaf;

constexpr float CAVOK_VISIBILITY_M      = 10000.0f;                     ///< CAVOK minimum Visibility [m]
constexpr float CAVOK_MIN_CLOUD_BASE_M  =  1500.0f;                     ///< CAVOK: no clouds below this height AGL [m]
constexpr float NSC_MIN_CLOUD_BASE_M    =  6000.0f * float(M_per_FT);   ///< NSC: no clouds below this height AGL [m]
constexpr float CLR_MIN_CLOUD_BASE_M    = 12000.0f * float(M_per_FT);   ///< CLR: no clouds below this height AGL [m]
constexpr float SKC_MIN_CLOUD_BASE_M    = FLT_MAX;                      ///< SKC: no clouds at all
constexpr float RAIN_DRIZZLE            = 0.1f;                         ///< rain in case of DRIZZLE
constexpr int   RAIN_DRIZZLE_FRIC       = 1;                            ///< rwy friction in case of light rain
constexpr float RAIN_LIGHT              = 0.3f;                         ///< light rain
constexpr int   RAIN_LIGHT_FRIC         = 2;                            ///< rwy friction in case of light rain
constexpr float RAIN_MODERATE           = 0.5f;                         ///< moderate rain
constexpr int   RAIN_MODERATE_FRIC      = 3;                            ///< rwy friction in case of moderate rain
constexpr float RAIN_HEAVY              = 0.9f;                         ///< heavy rain
constexpr int   RAIN_HEAVY_FRIC         = 5;                            ///< rwy friction in case of light rain
constexpr float SPRAY_HEIGHT_M          = 10.0f;                        ///< height AGL up to which we simulate SPRAY
constexpr float MIST_MAX_VISIBILITY_SM  = 7.0f;                         ///< max visibility in case of MIST
constexpr float FOG_MAX_VISIBILITY_SM   = 5.f/8.f;                      ///< max visibility in case of FOG
constexpr float TEMP_RWY_ICED           = -7.5f;                        ///< temperature under which we consider rwy icy

/// @brief metaf visitor, ie. functions that are called when traversing the parsed METAR
/// @details Set of visit functions that perform the actual processing of the information in the METAR.
///          Each function returns a `bool`, which reads "Shall processing be stopped?"
class LTWeatherVisitor : public Visitor<bool> {
public:
    LTWeather& w;                               ///< The weather we are to modify
    float fieldAlt_m = 0.0f;                    ///< field altitude in meter
    bool bCloseToGnd = false;                   ///< is position close enough to ground to weigh METAR higher than weather data?
    size_t iCloud = 0;                          ///< which cloud layer is to be filled next?
    int bThunderstorms = 0;                     ///< thunderstorms anywhere? (0-None, 1-Light, 2-Normal, 3-Heavy) -> PostProcessing() makes sure CB exists and adds turbulence
    bool bRwyFrictionDefined = false;           ///< Rwy friction defined based on Rwy State Group in METAR, don't touch again in PostProcessing()

public:
    /// Constructor sets the reference to the weather that we are to modify
    LTWeatherVisitor (LTWeather& weather) : w(weather) {}
    
    /// @brief Convert cloud cover to X-Plane percentage
    /// @note We aren't using metaf's CloudType::okta() function because it returns the _highest_ possible okta value,
    ///       we want the average.
    static float toXPCloudCover (const CloudGroup& cg)
    {
        switch (cg.amount()) {
            case CloudGroup::Amount::NOT_REPORTED:              // various ways of saying 'none'
            case CloudGroup::Amount::NCD:
            case CloudGroup::Amount::NSC:
            case CloudGroup::Amount::NONE_CLR:
            case CloudGroup::Amount::NONE_SKC:
            case CloudGroup::Amount::OBSCURED:                  // obscured, clouds aren't visible, so we don't know
                return 0.0f;
            case CloudGroup::Amount::FEW:                       // Few clouds (1/8 to 2/8 sky covered).
                return 1.5f/8.0f;
            case CloudGroup::Amount::VARIABLE_FEW_SCATTERED:    // Cloud cover is variable between FEW and SCATTERED -> between 1/8 and 4/8
                return 2.5f/8.0f;
            case CloudGroup::Amount::SCATTERED:                 // Scattered clouds (3/8 to 4/8 sky covered).
                return 3.5f/8.0f;
            case CloudGroup::Amount::VARIABLE_SCATTERED_BROKEN: // Cloud cover is variable between SCATTERED and BROKEN -> between 3/8 and 7/8
                return 5.0f/8.0f;
            case CloudGroup::Amount::BROKEN:                    // Broken clouds (5/8 to 7/8 sky covered).
                return 6.0f/8.0f;
            case CloudGroup::Amount::VARIABLE_BROKEN_OVERCAST:  // Cloud cover is variable between BROKEN and OVERCAST -> between 5/8 and 8/8
                return 6.5f/8.0f;
            case CloudGroup::Amount::OVERCAST:                  // Overcast (8/8 sky covered)
                return 8.0f/8.0f;
        }
        return 0.0f;
    }
    
    /// Convert a METAR cloud type to X-Plane
    static float toXPCloudType (const CloudGroup& cg)
    {
        const CloudGroup::ConvectiveType convTy = cg.convectiveType();
        if (convTy == CloudGroup::ConvectiveType::CUMULONIMBUS)
            return 3.0f;
        if (convTy == CloudGroup::ConvectiveType::TOWERING_CUMULUS)
            return 2.5f;

        const std::optional<CloudType>& optClTy = cg.cloudType();
        if (!optClTy)                           // if nothing specified, go for Cirrus
            return 0.0f;
        switch (optClTy.value().type()) {
            case CloudType::Type::NOT_REPORTED:      return 0.0f;
            //Low clouds
            case CloudType::Type::CUMULONIMBUS:      return 3.0f;
            case CloudType::Type::TOWERING_CUMULUS:  return 2.5f;
            case CloudType::Type::CUMULUS:           return 2.0f;
            case CloudType::Type::CUMULUS_FRACTUS:   return 1.8f;
            case CloudType::Type::STRATOCUMULUS:     return 1.5f;
            case CloudType::Type::NIMBOSTRATUS:      return 1.0f;
            case CloudType::Type::STRATUS:           return 1.0f;
            case CloudType::Type::STRATUS_FRACTUS:   return 0.8f;
            //Med clouds
            case CloudType::Type::ALTOSTRATUS:       return 1.0f;
            case CloudType::Type::ALTOCUMULUS:       return 2.0f;
            case CloudType::Type::ALTOCUMULUS_CASTELLANUS: return 2.3f;
            //High clouds
            case CloudType::Type::CIRRUS:            return 0.0f;
            case CloudType::Type::CIRROSTRATUS:      return 0.5;
            case CloudType::Type::CIRROCUMULUS:      return 1.5f;
            //Obscurations
            default:                                 return 0.0f;
        }
    }
    
    /// Remove a given cloud layer
    void RemoveClouds (size_t i)
    {
        LOG_ASSERT(i < w.cloud_type.size());
        w.cloud_type[i] = 0.0f;
        w.cloud_base_msl_m[i] = -1.0f;
        w.cloud_tops_msl_m[i] = -1.0f;
        w.cloud_coverage_percent[i] = 0.0f;
    }
    
    /// @brief Remove clouds up to given height AGL, remove thunderstorm clouds upon request
    /// @details Called with varyiing parameters for SKC, CLR, CAVOK, NSC, NCD
    void ReduceClouds (float maxHeight_m, bool bRemoveTSClouds)
    {
        // No cloud below `maxHeight_m`
        maxHeight_m += fieldAlt_m;                              // add field altitude to get altitude
        for (size_t i = 0; i < w.cloud_base_msl_m.size(); ++i) {
            if (w.cloud_tops_msl_m[i] < maxHeight_m)            // entire cloud layer too low -> remove
                RemoveClouds(i);
            else if (w.cloud_base_msl_m[i] < maxHeight_m)       // only base too low? -> lift base, but keep tops (and hence the layer as such)
                w.cloud_base_msl_m[i] = maxHeight_m;
        }
        // No cumulonimbus or towering cumulus clouds
        if (bRemoveTSClouds)
            for (float& ct: w.cloud_type)
                if (ct > 2.0f) ct = 2.0f;                       // reduce Cumulo-nimbus to Cumulus
    }
    
    // --- visit functions ---
    
    /// Location, ie. the ICAO code, for which we fetch position / altitude
    bool visitLocationGroup(const LocationGroup & lg,
                            ReportPart, const std::string&) override
    {
        w.posMetarField = GetAirportLoc(lg.toString());         // determin the field's location and especially altitude
        fieldAlt_m = float(w.posMetarField.alt_m());            // save field's altitude for easier access in later visitor functions
        if (std::isnan(fieldAlt_m)) {
            fieldAlt_m = 0.0f;
            LOG_MSG(logWARN, "Couldn't determine altitude of field '%s', clouds may appear too low", lg.toString().c_str());
        }
        
        // Are we flying/viewing "close" to ground so that we prefer METAR data?
        bCloseToGnd = float(w.pos.alt_m()) < fieldAlt_m + dataRefs.GetWeatherMaxMetarHeight_m();

        return false;
    }
    
    /// Keyword: If CAVOK then change weather accordingly
    bool visitKeywordGroup(const KeywordGroup & kwg,
                            ReportPart, const std::string&) override
    {
        if (kwg.type() == KeywordGroup::Type::CAVOK) {
            // Visibility 10 km or more in all directions.
            if (std::isnan(w.visibility_reported_sm) ||
                w.visibility_reported_sm * float(M_per_SM) < CAVOK_VISIBILITY_M)
                w.visibility_reported_sm = CAVOK_VISIBILITY_M / float(M_per_SM);
            // No clouds below 1.500m AGL, no CB/TCU
            ReduceClouds(CAVOK_MIN_CLOUD_BASE_M, true);
        }
        return false;
    }

    /// Trend Groups: Whatever the details: As we are only interested in _current_ weather we stop processing once reaching any trend group
    bool visitTrendGroup(const TrendGroup &, ReportPart, const std::string&) override
    { return true; }

    /// Clouds
    bool visitCloudGroup(const CloudGroup & cg,
                         ReportPart, const std::string&) override
    {
        switch (cg.type()) {
            case CloudGroup::Type::NO_CLOUDS: {
                // There are various ways of saying "no clouds" an they all mean a bit a different thing:
                switch (cg.amount()) {
                    case CloudGroup::Amount::NSC:
                        ReduceClouds(NSC_MIN_CLOUD_BASE_M, true);
                        break;
                    case CloudGroup::Amount::NONE_CLR:
                        ReduceClouds(CLR_MIN_CLOUD_BASE_M, false);
                        break;
                    case CloudGroup::Amount::NONE_SKC:
                        ReduceClouds(SKC_MIN_CLOUD_BASE_M, true);
                        break;
                    default:
                        // not touching cloud information
                        break;
                }
                break;
            }
            case CloudGroup::Type::CLOUD_LAYER: {
                float base_m = cg.height().toUnit(Distance::Unit::METERS).value_or(NAN);
                if (std::isnan(base_m)) break;                      // can't work without cloud base height
                
                // XP can only do 3 layers...if there are more in the METAR then shift/add layers until _one_ is above camera
                if (iCloud >= w.cloud_type.size()) {
                    // is last layer _below_ camera altitude?
                    if (w.cloud_base_msl_m.back() < w.pos.alt_m()) {
                        // shift layers down, so the top-most layer becomes available
                        std::move(std::next(w.cloud_type.begin()), w.cloud_type.end(), w.cloud_type.begin());
                        std::move(std::next(w.cloud_coverage_percent.begin()), w.cloud_coverage_percent.end(), w.cloud_coverage_percent.begin());
                        std::move(std::next(w.cloud_base_msl_m.begin()), w.cloud_base_msl_m.end(), w.cloud_base_msl_m.begin());
                        std::move(std::next(w.cloud_tops_msl_m.begin()), w.cloud_tops_msl_m.end(), w.cloud_tops_msl_m.begin());
                        iCloud = w.cloud_type.size()-1;
                    }
                }
                
                // Save the cloud layer, if there is still room in the cloud array
                if (iCloud < w.cloud_type.size()) {
                    // check first if we can make out any coverage, no need to waste a cloud layer for 0 coverage
                    const float cover = toXPCloudCover(cg);
                    if (cover > 0.0f) {
                        w.cloud_coverage_percent[iCloud] = cover;
                        w.cloud_type[iCloud] = toXPCloudType(cg);
                        w.cloud_base_msl_m[iCloud] = fieldAlt_m + cg.height().toUnit(Distance::Unit::METERS).value_or(0.0f);
                        if (w.cloud_type[iCloud] < 2.5f)                // non-convective cloud
                            w.cloud_tops_msl_m[iCloud] = w.cloud_base_msl_m[iCloud] + WEATHER_METAR_CLOUD_HEIGHT_M;
                        else                                            // Cumulo-nimbus are higher
                            w.cloud_tops_msl_m[iCloud] = w.cloud_base_msl_m[iCloud] + WEATHER_METAR_CB_CLOUD_HEIGHT_M;
                        ++iCloud;
                    }
                }
                break;
            }

            case CloudGroup::Type::VERTICAL_VISIBILITY:
            case CloudGroup::Type::CEILING:
            case CloudGroup::Type::VARIABLE_CEILING:
            case CloudGroup::Type::CHINO:
            case CloudGroup::Type::CLD_MISG:
            case CloudGroup::Type::OBSCURATION:
                break;
        }
        return false;
    }
    
    /// Visibility, use only if close to ground
    bool visitVisibilityGroup(const VisibilityGroup & vg,
                              ReportPart, const std::string&) override
    {
        if (bCloseToGnd) {
            switch (vg.type()) {
                case VisibilityGroup::Type::PREVAILING:
                case VisibilityGroup::Type::PREVAILING_NDV:
                case VisibilityGroup::Type::SURFACE:
                case VisibilityGroup::Type::TOWER:
                {
                    const std::optional<float> v = vg.visibility().toUnit(Distance::Unit::STATUTE_MILES);
                    if (v.has_value())
                        w.visibility_reported_sm = v.value();
                    break;
                }
                    
                case VisibilityGroup::Type::VARIABLE_PREVAILING:
                {
                    const std::optional<float> v1 = vg.minVisibility().toUnit(Distance::Unit::STATUTE_MILES);
                    const std::optional<float> v2 = vg.maxVisibility().toUnit(Distance::Unit::STATUTE_MILES);
                    if (v1.has_value() && v2.has_value())
                        w.visibility_reported_sm = (v1.value() + v2.value()) / 2.0f;
                    break;
                }
                    
                default:
                    break;
            }
        }
        return false;
    }
    
    /// Weather group, for weather phenomena like RA, TS etc.
    bool visitWeatherGroup(const WeatherGroup& wg,
                           ReportPart, const std::string&) override
    {
        // Only interested in CURRENT reports
        if (wg.type() == WeatherGroup::Type::CURRENT) {
            for (const WeatherPhenomena& wp: wg.weatherPhenomena()) {
                if (wp.qualifier() == WeatherPhenomena::Qualifier::RECENT)
                    continue;                           // skip of RECENT phenomena, only interested in current
                
                // Thunderstorms
                if (wp.descriptor() == WeatherPhenomena::Descriptor::THUNDERSTORM) {
                    switch (wp.qualifier()) {
                        case WeatherPhenomena::Qualifier::NONE:
                        case WeatherPhenomena::Qualifier::MODERATE:
                            bThunderstorms = 2;
                            break;
                        case WeatherPhenomena::Qualifier::LIGHT:
                        case WeatherPhenomena::Qualifier::VICINITY:
                        case WeatherPhenomena::Qualifier::RECENT:
                            bThunderstorms = 1;
                            break;
                        case WeatherPhenomena::Qualifier::HEAVY:
                            bThunderstorms = 3;
                            break;
                    }
                }
                
                // Weather Phenomena
                for (WeatherPhenomena::Weather wpw: wp.weather()) {
                    switch (wpw) {
                        case WeatherPhenomena::Weather::SPRAY:      // mimic spray only _very_ close to ground
                            if (w.pos.alt_m() - fieldAlt_m > SPRAY_HEIGHT_M)
                                break;
                            [[fallthrough]];                        // otherwise treat the same as drizzle
                        case WeatherPhenomena::Weather::DRIZZLE:
                            w.rain_percent      = RAIN_DRIZZLE;
                            w.runway_friction   = RAIN_DRIZZLE_FRIC;
                            break;
                        case WeatherPhenomena::Weather::RAIN:
                        case WeatherPhenomena::Weather::SNOW:
                        case WeatherPhenomena::Weather::SNOW_GRAINS:
                        case WeatherPhenomena::Weather::ICE_CRYSTALS:
                        case WeatherPhenomena::Weather::ICE_PELLETS:
                        case WeatherPhenomena::Weather::HAIL:
                        case WeatherPhenomena::Weather::SMALL_HAIL:
                        case WeatherPhenomena::Weather::UNDETERMINED:
                            if (wp.qualifier() == WeatherPhenomena::Qualifier::LIGHT) {
                                w.rain_percent      = RAIN_LIGHT;
                                w.runway_friction   = RAIN_LIGHT_FRIC;
                            }
                            else if (wp.qualifier() == WeatherPhenomena::Qualifier::HEAVY) {
                                w.rain_percent      = RAIN_HEAVY;
                                w.runway_friction   = RAIN_HEAVY_FRIC;
                            }
                            else {
                                w.rain_percent      = RAIN_MODERATE;
                                w.runway_friction   = RAIN_MODERATE_FRIC;
                            }
                            // Snowy/icy rwy friction
                            switch (wpw) {
                                case WeatherPhenomena::Weather::SNOW:
                                case WeatherPhenomena::Weather::SNOW_GRAINS:
                                    w.runway_friction = (w.runway_friction-1)/2 + 7;    // convert from wet/puddly [1..6] to snowy [7..9]
                                    break;
                                case WeatherPhenomena::Weather::ICE_CRYSTALS:
                                case WeatherPhenomena::Weather::ICE_PELLETS:
                                    w.runway_friction = (w.runway_friction-1)/2 + 10;   // convert from wet/puddly [1..6] to icy [10..12]
                                    break;
                                default:                // just to silence compiler warnings
                                    break;
                            }
                            break;
                            
                        // Limit visibility in case of MIST/FOG
                        case WeatherPhenomena::Weather::MIST:
                            if (w.visibility_reported_sm > MIST_MAX_VISIBILITY_SM)
                                w.visibility_reported_sm = MIST_MAX_VISIBILITY_SM;
                            break;
                        case WeatherPhenomena::Weather::FOG:
                            if (w.visibility_reported_sm > FOG_MAX_VISIBILITY_SM)
                                w.visibility_reported_sm = FOG_MAX_VISIBILITY_SM;
                            break;
                            
                        // everything else we don't process
                        default:
                            break;
                    }
                }
            }
        }
        return false;
    }
    
    /// Pressure group, for QNH and SLP
    bool visitPressureGroup(const PressureGroup& pg,
                            ReportPart, const std::string&) override
    {
        switch (pg.type()) {
            case PressureGroup::Type::OBSERVED_QNH:
                w.qnh_base_elevation = fieldAlt_m;
                w.qnh_pas = pg.atmosphericPressure().toUnit(Pressure::Unit::HECTOPASCAL).value_or(NAN) * 100.0f;
                break;
            case PressureGroup::Type::OBSERVED_SLP:
                w.sealevel_pressure_pas = pg.atmosphericPressure().toUnit(Pressure::Unit::HECTOPASCAL).value_or(NAN) * 100.0f;
                break;
            default:
                break;
        }
        return false;
    }


    /// Temperatur group for air and surface temp
    bool visitTemperatureGroup(const TemperatureGroup& tg,
                               ReportPart, const std::string&) override
    {
        if (tg.type() == TemperatureGroup::Type::TEMPERATURE_AND_DEW_POINT) {
            // Temperatur: Fill the same temp all the way up to field altitude, which wouldn't be quite right...but under us is ground anyway
            if (tg.airTemperature().temperature().has_value())
                w.FillUp(w.temperature_altitude_msl_m, w.temperatures_aloft_deg_c,
                         fieldAlt_m,
                         tg.airTemperature().toUnit(Temperature::Unit::C).value_or(NAN),
                         true);
            // Dew Point: Fill the same temp all the way up to field altitude, which wouldn't be quite right...but under us is ground anyway
            if (tg.dewPoint().temperature().has_value())
                w.FillUp(w.temperature_altitude_msl_m, w.dewpoint_deg_c,
                         fieldAlt_m,
                         tg.dewPoint().toUnit(Temperature::Unit::C).value_or(NAN),
                         true);
        }
        return false;
    }
    
    /// Wind group
    bool visitWindGroup(const WindGroup& wg,
                        ReportPart, const std::string&) override
    {
        switch (wg.type()) {
                // process wind information
            case WindGroup::Type::SURFACE_WIND:
            case WindGroup::Type::SURFACE_WIND_CALM:
            case WindGroup::Type::VARIABLE_WIND_SECTOR:
            case WindGroup::Type::SURFACE_WIND_WITH_VARIABLE_SECTOR:
            case WindGroup::Type::WIND_SHEAR:
            {
                // up to which altitude will we set wind values?
                const float alt_m = fieldAlt_m + dataRefs.GetWeatherMaxMetarHeight_m();
                // Standard wind speed (can be 0 for calm winds) and direction
                const float speed = wg.windSpeed().toUnit(Speed::Unit::METERS_PER_SECOND).value_or(0.0f);
                const float dir   = wg.direction().degrees().value_or(0.0f);
                w.FillUp(w.wind_altitude_msl_m, w.wind_speed_msc,           // set wind speed up to preferred METAR height AGL
                         alt_m, speed, true);
                w.FillUp(w.wind_altitude_msl_m, w.wind_direction_degt,      // set wind direction up to preferred METAR height AGL
                         alt_m, dir, false);                                // no interpolation...that can go wrong with headings
                
                // Variable wind direction -> transform into +/- degrees as expected by XP
                const float begDir = wg.varSectorBegin().degrees().value_or(NAN);
                const float endDir = wg.varSectorEnd().degrees().value_or(NAN);
                const float halfDiff = !std::isnan(begDir) && !std::isnan(endDir) ?
                                       float(HeadingDiff(begDir, endDir) / 2.0) :
                                       0.0f;
                w.FillUp(w.wind_altitude_msl_m, w.shear_direction_degt,     // set wind shear up to preferred METAR height AGL
                         alt_m, halfDiff, false);

                // Gust speed if given
                float gust = wg.gustSpeed().toUnit(Speed::Unit::METERS_PER_SECOND).value_or(NAN);
                if (std::isnan(gust)) gust = 0.0f;                          // if not given set 'gain from gust' to zeri
                else                  gust -= speed;                        // if given, subtract normal wind speed, we need 'gain from gust'
                w.FillUp(w.wind_altitude_msl_m, w.shear_speed_msc,          // set wind shear gain up to preferred METAR height AGL
                         alt_m, gust, true);          

                break;
            }
                
                // don't process
            case WindGroup::Type::WIND_SHEAR_IN_LOWER_LAYERS:
            case WindGroup::Type::WIND_SHIFT:
            case WindGroup::Type::WIND_SHIFT_FROPA:
            case WindGroup::Type::PEAK_WIND:
            case WindGroup::Type::WSCONDS:
            case WindGroup::Type::WND_MISG:
                break;
        }
        return false;
    }
    
    /// Rwy State Group
    bool visitRunwayStateGroup(const RunwayStateGroup& rsg,
                               ReportPart, const std::string&) override
    {
        // XP provides just one `runway_friction` dataRef, not one per runway,
        // so we just process the first runway state group, assuming that
        // the states of different runways on the same airport won't be much different anyway.
        if (!bRwyFrictionDefined && rsg.deposits() != RunwayStateGroup::Deposits::NOT_REPORTED) {
            bRwyFrictionDefined = true;
            const float depth = rsg.depositDepth().toUnit(Precipitation::Unit::MM).value_or(0.0f);
            int extend = 0;
            switch (rsg.contaminationExtent()) {
                case RunwayStateGroup::Extent::FROM_26_TO_50_PERCENT:
                    extend = 1;
                    break;
                case RunwayStateGroup::Extent::MORE_THAN_51_PERCENT:
                    extend = 2;
                    break;
                default:
                    extend = 0;
            }

            switch (rsg.deposits()) {
                case RunwayStateGroup::Deposits::NOT_REPORTED:
                case RunwayStateGroup::Deposits::CLEAR_AND_DRY:
                    w.runway_friction = 0;
                    break;
                case RunwayStateGroup::Deposits::DAMP:
                    w.runway_friction = 1;
                    break;
                case RunwayStateGroup::Deposits::WET_AND_WATER_PATCHES:
                    // depth decides between wet (<2mm) and puddly (>=2mm)
                    w.runway_friction = (depth < 2 ? 1 : 4) + extend;
                    break;
                case RunwayStateGroup::Deposits::ICE:
                case RunwayStateGroup::Deposits::RIME_AND_FROST_COVERED:
                case RunwayStateGroup::Deposits::FROZEN_RUTS_OR_RIDGES:
                    w.runway_friction = 10 + extend;            // icy
                    break;
                case RunwayStateGroup::Deposits::DRY_SNOW:
                case RunwayStateGroup::Deposits::WET_SNOW:
                case RunwayStateGroup::Deposits::SLUSH:
                    w.runway_friction =  7 + extend;            // snowy
                    break;
                case RunwayStateGroup::Deposits::COMPACTED_OR_ROLLED_SNOW:
                    w.runway_friction = 13 + extend;            // snowy/icy
                    break;
            }
        }
        return false;
    }
    
    /// After finishing the processing, perform some cleanup
    void PostProcessing ()
    {
        // Runway friction
        if (!bRwyFrictionDefined) {
            if (w.runway_friction < 0)                      // don't yet have a runway friction?
                // Rain causes wet status [0..7]
                w.runway_friction = (int)std::lround(w.rain_percent * 6.f);
            // Rwy Friction: Consider freezing if there is something's on the rwy that is not yet ice
            const float t = w.GetInterpolated(w.temperature_altitude_msl_m,
                                              w.temperatures_aloft_deg_c,
                                              fieldAlt_m);
            if (t <= TEMP_RWY_ICED &&
                0 < w.runway_friction && w.runway_friction < 10)
            {
                // From water
                if (1 <= w.runway_friction && w.runway_friction <= 6)
                    w.runway_friction = (w.runway_friction-1)/2 + 10;   // convert from wet/puddly [1..6] to icy [10..12]
                else
                    w.runway_friction += 3;                             // convert from snowy [7..9] to icy [10..12]
            }
        }

        // Cleanup up cloud layers: Anything beyond iCloud, that is lower than the last METAR layer, is to be removed
        if (iCloud > 0) {
            const float highestMetarBase = w.cloud_base_msl_m[iCloud-1];
            for (std::size_t i = iCloud; i < w.cloud_type.size(); ++i) {
                if (w.cloud_base_msl_m[i] <= highestMetarBase)
                    RemoveClouds(i);
            }
        }
        
        // Thunderstorms require a cumulo-nimbus somewhere
        if (bThunderstorms > 0) {
            // a cumulo-nimbus anywhere?
            size_t iCB = SIZE_MAX;
            for (size_t i = 0; iCB == SIZE_MAX && i < w.cloud_type.size(); ++i)
                if (w.cloud_type[i] >= 2.5)
                    iCB = i;

            // if no CB yet, Need to turn one cloud layer to cmulu-numbus
            for (size_t i = 0; iCB == SIZE_MAX && i < w.cloud_type.size(); ++i) {
                // search for coverage of 25%, 50%, 75% depending on thunderstorm intensity
                if (w.cloud_coverage_percent[i] > float(bThunderstorms)/4.0f) {
                    // set to 2.5, 2.75, 3.0 depending on thunderstorm intensity
                    w.cloud_type[i] = 2.25f + float(bThunderstorms)*0.25f;
                    iCB = i;
                }
            }
            // if still no CB yet, turn first cloud layer to cmulu-numbus
            for (size_t i = 0; iCB == SIZE_MAX && i < w.cloud_type.size(); ++i) {
                // search for coverage of at least 10%
                if (w.cloud_coverage_percent[i] >= 0.1f) {
                    // set to 2.5, 2.75, 3.0 depending on thunderstorm intensity
                    w.cloud_type[i] = 2.25f + float(bThunderstorms)*0.25f;
                    iCB = i;
                }
            }
            
            // Add turbulence until under the top of the CB cloud
            w.FillUpMin(w.wind_altitude_msl_m, w.turbulence,
                        w.cloud_tops_msl_m[iCB],
                        float(bThunderstorms)*2.5f,     // 2.5, 5.0, 7.5 depending on TS intensity
                        true);
        }
    }
};

/// Parse the METAR into the global objects, avoids re-parsing the same METAR over again
const ParseResult& ParseMETAR (const std::string& raw)
{
    static std::mutex mutexMetar;               // Mutex that safeguards this procedure
    static ParseResult metarResult;             // Global object of last METAR parse result, helps avoiding parsing the same METAR twice
    static std::string metarRaw;                // The METAR that got parsed globally

    // Safeguard against multi-thread execution
    std::lock_guard<std::mutex> lck(mutexMetar);
    
    // samesame or different?
    if (metarRaw != raw) {
        metarResult = Parser::parse(metarRaw = raw);
        if (metarResult.reportMetadata.error != ReportError::NONE) {
            LOG_MSG(logWARN, "Parsing METAR failed with error %d\n%s",
                    int(metarResult.reportMetadata.error),
                    metarRaw.c_str());
        } else {
            LOG_MSG(logDEBUG, "Parsing METAR found %lu groups",
                    (unsigned long)metarResult.groups.size());
        }
    }
    return metarResult;
}

// Parse `metar` and fill weather from there as far as possible
void LTWeather::IncorporateMETAR()
{
    // --- Parse ---
    const ParseResult& r = ParseMETAR(metar);
    if (r.reportMetadata.error != ReportError::NONE)
        return;

    // Log before applying METAR
    if (dataRefs.ShallLogWeather())
        Log("Weather before applying METAR:");

    // --- Process by 'visiting' all groups ---
    LTWeatherVisitor v(*this);
    for (const GroupInfo& gi : r.groups)
        if (v.visit(gi))                    // returns if to stop processing
            break;
    v.PostProcessing();
}


//
// MARK: Weather Network Request handling
//

/// The request URL, parameters are in this order: radius, longitude, latitude
const char* WEATHER_URL="https://aviationweather.gov/cgi-bin/data/dataserver.php?requestType=retrieve&dataSource=metars&format=xml&hoursBeforeNow=2&mostRecent=true&boundingBox=%.2f,%.2f,%.2f,%.2f&fields=raw_text,station_id,latitude,longitude,altim_in_hg";

/// Weather search radius (increment) to use if the initial weather request came back empty
constexpr float ADD_WEATHER_RADIUS_NM = 100.0f;
/// How often to add up ADD_WEATHER_RADIUS_NM before giving up?
constexpr long  MAX_WEATHER_RADIUS_FACTOR = 5;

/// suppress further error message as we had enough already?
bool gbSuppressWeatherErrMsg = false;

// Error messages
#define ERR_WEATHER_ERROR       "Weather request returned with error: %s"
#define INFO_NO_NEAR_WEATHER    "Found no nearby weather in a %.fnm radius"
#define ERR_NO_WEATHER          "Found no weather in a %.fnm radius, giving up"
#define INFO_FOUND_WEATHER_AGAIN "Successfully updated weather again from %s"

/// return the value between two xml tags
std::string GetXMLValue (const std::string& _r, const std::string& _tag,
                         std::string::size_type& pos)
{
    // find the tag
    std::string::size_type p = _r.find(_tag, pos);
    if (p == std::string::npos)         // didn't find it
        return "";
    
    // find the beginning of the _next_ tag (we don't validate any further)
    const std::string::size_type startPos = p + _tag.size();
    pos = _r.find('<', startPos);       // where the end tag begins
    if (pos != std::string::npos)
        return _r.substr(startPos, pos-startPos);
    else {
        pos = 0;                        // we overwrite pos with npos...reset to buffer's beginning for next search
        return "";
    }
}

/// @brief Process the response from aviationweather.com
/// @details Response is in XML format. (JSON is not available.)
///          We aren't doing a full XML parse here but rely on the
///          fairly static structure:
///          We straight away search for:
///            `<error>` Indicates just that and stops interpretation.\n
///            `<station_id>`, `<raw_text>`, `<latitude>`, `<longitude>`,
///            and`<altim_in_hg>` are the values we are interested in.
bool WeatherProcessResponse (const std::string& _r)
{
    float lat = NAN;
    float lon = NAN;
    float hPa = NAN;
    std::string stationId;
    std::string METAR;
    
    // Any error?
    std::string::size_type pos = 0;
    std::string val = GetXMLValue(_r, "<error>", pos);
    if (!val.empty()) {
        LOG_MSG(logERR, ERR_WEATHER_ERROR, val.c_str());
        return false;
    }
    
    // find the pressure
    val = GetXMLValue(_r, "<altim_in_hg>", pos);
    if (!val.empty()) {
        hPa = std::stof(val) * (float)HPA_per_INCH;
        
        // We fetch the other fields in order of appearance, but need to start once again from the beginning of the buffer
        pos = 0;
        // Try fetching METAR and station_id
        METAR = GetXMLValue(_r, "<raw_text>", pos);
        stationId = GetXMLValue(_r, "<station_id>", pos);
        
        // If we've got a METAR we better take QNH from there as that is a local observation
        const float QNH = WeatherQNHfromMETAR(METAR);
        if (!std::isnan(QNH))
            hPa = QNH;

        // then let's see if we also find the weather station's location
        val = GetXMLValue(_r, "<latitude>", pos);
        if (!val.empty())
            lat = std::stof(val);
        val = GetXMLValue(_r, "<longitude>", pos);
        if (!val.empty())
            lon = std::stof(val);
        
        // tell ourselves what we found
        dataRefs.SetWeather(hPa, lat, lon, stationId, METAR);

        // found again weather after we had started to suppress messages?
        if (gbSuppressWeatherErrMsg) {
            // say hooray and report again
            LOG_MSG(logINFO, INFO_FOUND_WEATHER_AGAIN, stationId.c_str());
            gbSuppressWeatherErrMsg = false;
        }

        return true;
    }

    // didn't find weather!
    return false;
}

/// CURL callback just adding up data
size_t WeatherFetchCB(char *ptr, size_t, size_t nmemb, void* userdata)
{
    // copy buffer to our std::string
    std::string& readBuf = *reinterpret_cast<std::string*>(userdata);
    readBuf.append(ptr, nmemb);
    
    // all consumed
    return nmemb;
}

// check on X-Plane.org what version's available there
// This function would block. Idea is to call it in a thread like with std::async
bool WeatherFetch (float _lat, float _lon, float _radius_nm)
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_Weather", LC_ALL_MASK);

    bool bRet = false;
    try {
        char curl_errtxt[CURL_ERROR_SIZE];
        char url[512];
        std::string readBuf;
        
        // initialize the CURL handle
        CURL *pCurl = curl_easy_init();
        if (!pCurl) {
            LOG_MSG(logERR,ERR_CURL_EASY_INIT);
            return false;
        }

        // Loop in case we need to re-do a request with larger radius
        bool bRepeat = false;
        do {
            bRepeat = false;

            // put together the URL, convert nautical to statute miles
            const boundingBoxTy box (positionTy(_lat, _lon), _radius_nm * M_per_NM);
            const positionTy minPos = box.sw();
            const positionTy maxPos = box.ne();
            snprintf(url, sizeof(url), WEATHER_URL,
                     minPos.lat(), minPos.lon(),
                     maxPos.lat(), maxPos.lon());

            // prepare the handle with the right options
            readBuf.reserve(CURL_MAX_WRITE_SIZE);
            curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, dataRefs.GetNetwTimeoutMax());
            curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, curl_errtxt);
            curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, WeatherFetchCB);
            curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &readBuf);
            curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
            curl_easy_setopt(pCurl, CURLOPT_URL, url);

            // perform the HTTP get request
            CURLcode cc = CURLE_OK;
            if ((cc = curl_easy_perform(pCurl)) != CURLE_OK)
            {
                // problem with querying revocation list?
                if (LTOnlineChannel::IsRevocationError(curl_errtxt)) {
                    // try not to query revoke list
                    curl_easy_setopt(pCurl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
                    LOG_MSG(logWARN, ERR_CURL_DISABLE_REV_QU, LT_DOWNLOAD_CH);
                    // and just give it another try
                    cc = curl_easy_perform(pCurl);
                }

                // if (still) error, then log error
                if (cc != CURLE_OK)
                    LOG_MSG(logERR, ERR_CURL_PERFORM, "Weather download", cc, curl_errtxt);
            }

            if (cc == CURLE_OK)
            {
                // CURL was OK, now check HTTP response code
                long httpResponse = 0;
                curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);

                // not HTTP_OK?
                if (httpResponse != HTTP_OK) {
                    LOG_MSG(logERR, ERR_CURL_PERFORM, "Weather download", (int)httpResponse, ERR_HTTP_NOT_OK);
                }
                else {
                    // Success: Process data
                    bRet = WeatherProcessResponse(readBuf);
                    // Not found weather yet?
                    if (!bRet) {
                        // How often did we apply ADD_WEATHER_RADIUS_NM already?
                        const long nRadiusFactor = std::lround(_radius_nm/ADD_WEATHER_RADIUS_NM);
                        if (nRadiusFactor < MAX_WEATHER_RADIUS_FACTOR) {
                            if (!gbSuppressWeatherErrMsg)
                                LOG_MSG(logINFO, INFO_NO_NEAR_WEATHER, _radius_nm);
                            _radius_nm = (nRadiusFactor+1) * ADD_WEATHER_RADIUS_NM;
                            bRepeat = true;
                        } else if (!gbSuppressWeatherErrMsg) {
                            LOG_MSG(logERR, ERR_NO_WEATHER, _radius_nm);
                            gbSuppressWeatherErrMsg = true;
                        }
                    }
                }
            }
        } while (bRepeat);
        
        // cleanup CURL handle
        curl_easy_cleanup(pCurl);
    }
    catch (const std::exception& e) {
        LOG_MSG(logERR, "Fetching weather failed with exception %s", e.what());
    }
    catch (...) {
        LOG_MSG(logERR, "Fetching weather failed with exception");
    }
    
    // done
    return bRet;
}


//
// MARK: Global functions
//

static bool bWeatherCanSet = false;             ///< Would it be possible for LiveTraffic to control weather? (XP12 onwards)
static bool bWeatherControlling = false;        ///< Is LiveTraffic in control of weather?
static int weatherOrigSource = -1;              ///< Original value of `sim/weather/region/weather_source` before LiveTraffic took over
static int weatherOrigChangeMode = -1;          ///< Original value of `sim/weather/region/change_mode` before LiveTraffic took over

static std::recursive_mutex mtxWeather;         ///< manages access to weather storage
static LTWeather nextWeather;                   ///< next weather to set
static bool bSetWeather = false;                ///< is there a next weather to set?
static bool bResetWeather = false;              ///< Shall weather be reset, ie. handed back to XP?
static LTWeather setWeather;                    ///< the weather we set last time
static std::string gEmptyString;                ///< an empty string we can refer to if we need to return an empty reference

// Initialize Weather module, dataRefs
bool WeatherInit ()
{
    bWeatherCanSet = WeatherInitDataRefs();
    if (!bWeatherCanSet) {
        LOG_MSG(logWARN, "Could not find all Weather dataRefs, cannot set X-Plane's weather (X-Plane < v12?)");
    }
#if DEBUG
    // TODO: Undo: in debug version now activate weather logging
    else {
        dataRefs.SetDebugLogWeather(true);
    }
#endif
    return bWeatherCanSet;
}

// Shutdown Weather module
void WeatherStop ()
{
    WeatherReset();
}

// Can we set weather? (X-Plane 12 forward only)
bool WeatherCanSet ()
{
    return bWeatherCanSet;
}

// Are we controlling weather?
bool WeatherInControl ()
{
    return bWeatherControlling;
}

// Is X-Plane set to use real weather?
bool WeatherIsXPRealWeather ()
{
    if (!WeatherCanSet()) return false;
    return wdr_change_mode.get() == 7;
}


// Thread-safely store weather information to be set in X-Plane in the main thread later
void WeatherSet (const LTWeather& w)
{
    if (!WeatherCanSet()) {
        SHOW_MSG(logDEBUG, "Requested to set weather, but cannot due to missing dataRefs");
        return;
    }
    
    // Access to weather storage, copy weather info
    std::lock_guard<std::recursive_mutex> mtx (mtxWeather);
    nextWeather = w;
    bSetWeather = true;
}
    
// Thread-safely store weather information to be set in X-Plane in the main thread later
void WeatherSet (const std::string& metar, const std::string& metarIcao)
{
    if (!WeatherCanSet()) {
        SHOW_MSG(logDEBUG, "Requested to set weather, but cannot due to missing dataRefs");
        return;
    }
    
    // Access to weather storage, copy weather info
    std::lock_guard<std::recursive_mutex> mtx (mtxWeather);
    if (nextWeather.metar != metar) {               // makes only sense in case something has changed
        nextWeather = LTWeather();                  // reset all, reads `atmosphere_alt_levels_m` already
        nextWeather.wind_altitude_msl_m =           // set all altitude levels to the same
        nextWeather.temperature_altitude_msl_m = nextWeather.atmosphere_alt_levels_m;
        // TODO: Prepare more data, like preset arrays with zero
        nextWeather.metar = metar;                  // just store METAR
        nextWeather.metarFieldIcao = metarIcao;
        nextWeather.posMetarField = positionTy();
        bSetWeather = true;
    }
}
    
// Actually update X-Plane's weather if there is anything to do (called from main thread)
void WeatherUpdate ()
{
    // Quick exit if we can't or shan't
    if (!WeatherCanSet() || !dataRefs.IsXPThread()) return;
    
    // If the ask is to reset weather
    if (bResetWeather) {
        WeatherReset();
        return;
    }
    
    // TODO: Switch between METAR and XP real weather
    
    // If there's nothing to
    if (!bSetWeather) {
        if (WeatherInControl())                     // but in princuple we are in control
            wdr_qnh_pas.set(setWeather.qnh_pas);    // then re-set QNH as X-Plane likes to override that itself from time to time
        return;
    }

    // Access to weather storage guarded by a lock
    std::lock_guard<std::recursive_mutex> mtx (mtxWeather);
    bSetWeather = false;                                // reset flag right away so we don't try again in case of early exits (errors)
    
    // If there is a METAR, then let's process that now
    if (!nextWeather.metar.empty() && !nextWeather.metarFieldIcao.empty()) {
        // determine METAR location
        nextWeather.posMetarField = GetAirportLoc(nextWeather.metarFieldIcao);
        // Not too far away?
        if (nextWeather.posMetarField.hasPosAlt() &&
            float(nextWeather.posMetarField.distRoughSqr(dataRefs.GetViewPos())) < sqr(dataRefs.GetWeatherMaxMetarDist_m()))
        {
            nextWeather.IncorporateMETAR();
            // Remember the METAR we used
            setWeather.metar            = nextWeather.metar;
            setWeather.metarFieldIcao   = nextWeather.metarFieldIcao;
            setWeather.posMetarField    = nextWeather.posMetarField;
        }
        else {
            // Remember that we did _not_ use a METAR to define weather
            setWeather.metar.clear();
            setWeather.metarFieldIcao.clear();
            setWeather.posMetarField = positionTy();
        }
    }
        
    // Set weather with immediate effect if first time, or if position changed dramatically
    nextWeather.update_immediately |= !WeatherInControl() ||
                                      !setWeather.pos.hasPosAlt() ||
                                      setWeather.pos.dist(dataRefs.GetViewPos()) > WEATHER_MAX_DIST_M;
    if (nextWeather.update_immediately) {
        if (!WeatherInControl()) {
            // Taking control of weather
            weatherOrigSource       = wdr_weather_source.get();
            weatherOrigChangeMode   = wdr_change_mode.get();
            if (dataRefs.ShallLogWeather()) {
                SHOW_MSG(logDEBUG, "Weather originally %s (source = %d, change mode = %d)",
                         WeatherGetSource().c_str(),
                         weatherOrigSource, weatherOrigChangeMode);
                LTWeather().Get("Weather just prior to LiveTraffic taking over:");
            }
            SHOW_MSG(logINFO, "LiveTraffic takes over controlling X-Plane's weather");
            bWeatherControlling     = true;
        } else {
            SHOW_MSG(logINFO, "LiveTraffic is re-setting X-Plane's weather");
        }
    }

    // actually set the weather in X-Plane
    nextWeather.Set();
    // get all values from X-Plane right away, after XP's processing
    setWeather.Get();
    // if weather's position is given remember that
    if (nextWeather.pos.hasPosAlt())
        setWeather.pos = nextWeather.pos;
}

// Reset weather settings to what they were before X-Plane took over
void WeatherReset ()
{
    // If not called from main thread just set a flag and wait for main thread
    if (!dataRefs.IsXPThread()) {
        bResetWeather = true;
        return;
    }
    
    if (weatherOrigSource >= 0)     wdr_weather_source.set(weatherOrigSource);
    if (weatherOrigChangeMode >= 0) wdr_change_mode.set(weatherOrigChangeMode);
    
    if (bWeatherControlling) {
        bWeatherControlling = false;
        SHOW_MSG(logINFO, "LiveTraffic no longer controls X-Plane's weather, reset to previous settings");
        if (dataRefs.ShallLogWeather()) {
            LOG_MSG(logDEBUG, "Weather reset to %s (source = %d, change mode = %d)",
                    WeatherGetSource().c_str(),
                    weatherOrigSource, weatherOrigChangeMode);
        }
    }
    
    weatherOrigSource = -1;
    weatherOrigChangeMode = -1;
    setWeather.pos = setWeather.posMetarField = positionTy();
    setWeather.metar.clear();
    bResetWeather = bSetWeather = false;
}

// Log current weather
void WeatherLogCurrent (const std::string& msg)
{
    LTWeather().Get(msg);
}

// Current METAR in use for weather generation
const std::string& WeatherGetMETAR ()
{
    if (WeatherInControl())
        return setWeather.metar;                // if in control then return the METAR of the weather we did set
    else
        return gEmptyString;                    // otherwise (a reference to) an empty string
}

// Return a human readable string on the weather source, is "LiveTraffic" if WeatherInControl()
std::string WeatherGetSource ()
{
    // Preset closest to current conditions
    static std::array<const char*,10> WEATHER_PRESETS = {
        "Clear", "VFR Few", "VFR Scattered", "VFR Broken", "VFR Marginal", "IFR Non-precision", "IFR Precision", "Convective", "Large-cell Storms", "Unknown"
    };
    int preset = wdr_weather_preset.get();
    if (preset < 0 || preset > 8) preset = 9;           // 'Unknown'

    // Weather Source
    static std::array<const char*,5> WEATHER_SOURCES = {
        "X-Plane Preset", "X-Plane Real Weather", "Controlpad", "Plugin", "Unknown"
    };
    int source = wdr_weather_source.get();
    if (source < 0 || source > 3) source = 4;

    // Are we in control? Say so!
    const WeatherCtrlTy wc = dataRefs.GetWeatherControl();
    if (WeatherInControl() && wc > WC_NONE) {
        std::string s;
        if (wc == WC_REAL_TRAFFIC)
            s = "LiveTraffic using RealTraffic weather data, ";
        else {
            char t[100];
            snprintf(t, sizeof(t), "LiveTraffic using METAR up to %dft, ", dataRefs.GetWeatherMaxMetarHeight_ft());
            s += t;
        }
        return s + WEATHER_PRESETS[size_t(preset)];
    }
    else
        return std::string(WEATHER_SOURCES[size_t(source)]) + ", " + WEATHER_PRESETS[size_t(preset)];
}

// Extract QNH or SLP from METAR, NAN if not found any info, which is rather unlikely
float WeatherQNHfromMETAR (const std::string& metar)
{
    // --- Parse ---
    const ParseResult& r = ParseMETAR(metar);
    if (r.reportMetadata.error == ReportError::NONE) {
        // Find the pressure group
        for (const GroupInfo& gi: r.groups)
            if (const PressureGroup *pPg = std::get_if<PressureGroup>(&gi.group))
                return pPg->atmosphericPressure().toUnit(Pressure::Unit::HECTOPASCAL).value_or(NAN);
    }
    return NAN;
}


/// Is currently an async operation running to fetch METAR?
static std::future<bool> futWeather;

// Asynchronously, fetch fresh weather information
bool WeatherFetchUpdate (const positionTy& pos, float radius_nm)
{
    // does only make sense in a certain latitude range
    // (During XP startup irregular values >80 show up)
    if (pos.lat() >= 80.0)
        return false;
    
    // a request still underway?
    if (futWeather.valid() &&
        futWeather.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            // then stop here
            return false;

    // start another thread with the weather request
    futWeather = std::async(std::launch::async,
                            WeatherFetch, (float)pos.lat(), (float)pos.lon(), radius_nm);
    return true;
}
