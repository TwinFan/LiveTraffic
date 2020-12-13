/// @file       LTAPI.h
/// @brief      LiveTraffic API
/// @details    API to access LiveTraffic's aircraft information.
///             Data transfer from LiveTraffic to your plugin is by dataRefs
///             in a fast, efficient way:
///             LiveTraffic copies data of several planes combined into
///             defined structures. LTAPI handles all that in the background
///             and provides you with an array of aircraft information with
///             numerical info like position, heading, speed and
///             textual info like type, registration, call sign, flight number.
/// @see        https://twinfan.github.io/LTAPI/
/// @author     Birger Hoppe
/// @copyright  (c) 2019-2020 Birger Hoppe
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

#ifndef LTAPI_h
#define LTAPI_h

#include <cstring>
#include <memory>
#include <string>
#include <list>
#include <map>
#include <chrono>

#include "XPLMDataAccess.h"
#include "XPLMGraphics.h"

class LTDataRef;

/// @brief Represents one aircraft as controlled by LiveTraffic.
///
/// You can derive subclasses from this class if you want to
/// add information specific to you app. Then you would need to
/// provide a callback function `fCreateAcObject` to LTAPIConnect so that _you_
/// create new aircraft objects when required by LTAPIConnect.
class LTAPIAircraft
{
private:
    /// @brief Unique key for this aircraft, usually ICAO transponder hex code
    /// But could also be any other truly unique id per aircraft (FLARM ID, tail number...)
    unsigned        keyNum = 0;
    /// Key converted to a hex string
    std::string     key;

public:
    
    /// @brief Flight phase, definition copied from LiveTraffic
    enum LTFlightPhase {
        FPH_UNKNOWN     = 0,            ///< used for initializations
        FPH_TAXI        = 10,           ///< Taxiing
        FPH_TAKE_OFF    = 20,           ///< Group of status for take-off:
        FPH_TO_ROLL,                    ///< Take-off roll
        FPH_ROTATE,                     ///< Rotating
        FPH_LIFT_OFF,                   ///< Lift-off, until "gear-up" height
        FPH_INITIAL_CLIMB,              ///< Initial climb, until "flaps-up" height
        FPH_CLIMB       = 30,           ///< Regular climbout
        FPH_CRUISE      = 40,           ///< Cruising, no altitude change
        FPH_DESCEND     = 50,           ///< Descend, more then 100ft/min descend
        FPH_APPROACH    = 60,           ///< Approach, below "flaps-down" height
        FPH_FINAL,                      ///< Final, below "gear-down" height
        FPH_LANDING     = 70,           ///< Group of status for landing:
        FPH_FLARE,                      ///< Flare, when reaching "flare	" height
        FPH_TOUCH_DOWN,                 ///< The one cycle when plane touches down, don't rely on catching it...it's really one cycle only
        FPH_ROLL_OUT,                   ///< Roll-out after touch-down until reaching taxi speed or stopping
        FPH_STOPPED_ON_RWY              ///< Stopped on runway because ran out of tracking data, plane will disappear soon
    };

    /// @brief Bulk data transfer structur for communication with LTAPI
    /// @note Structure needs to be in synch with LiveTraffic,
    ///       version differences are handled using a struct size "negotiation",
    ///       but order of fields must match.
    struct LTAPIBulkData {
    public:
        // identification
        uint64_t        keyNum          = 0;  ///< a/c id, usually transp hex code, or any other unique id (FLARM etc.)
        // position, attitude
        float           lat_f           = 0.0f; ///< deprecated: [°] latitude
        float           lon_f           = 0.0f; ///< deprecated: [°] longitude
        float           alt_ft_f        = 0.0f; ///< deprecated: [ft] altitude
        float           heading         = 0.0f; ///< [°] heading
        float           track           = 0.0f; ///< [°] track over ground
        float           roll            = 0.0f; ///< [°] roll:  positive right
        float           pitch           = 0.0f; ///< [°] pitch: positive up
        float           speed_kt        = 0.0f; ///< [kt] ground speed
        float           vsi_ft          = 0.0f; ///< [ft/minute] vertical speed, positive up
        float           terrainAlt_ft   = 0.0f; ///< [ft] terrain altitude beneath plane
        float           height_ft       = 0.0f; ///< [ft] height AGL
        // configuration
        float           flaps           = 0.0f; ///< flap position: 0.0 retracted, 1.0 fully extended
        float           gear            = 0.0f; ///< gear position: 0.0 retracted, 1.0 fully extended
        float           reversers       = 0.0f; ///< reversers position: 0.0 closed, 1.0 fully opened
        // simulation
        float           bearing         = 0.0f; ///< [°] to current camera position
        float           dist_nm         = 0.0f; ///< [nm] distance to current camera
        
        struct BulkBitsTy {
            LTFlightPhase phase : 8;        ///< flight phase, see LTAircraft::FlightPhase
            bool        onGnd : 1;          ///< Is plane on ground?
            // Lights:
            bool        taxi       : 1;     ///< taxi lights
            bool        land       : 1;     ///< landing lights
            bool        bcn        : 1;     ///< beacon light
            bool        strb       : 1;     ///< strobe light
            bool        nav        : 1;     ///< navigaton lights
            unsigned    filler1    : 2;     ///< unused, fills up to byte alignment
            // Misc
            int         multiIdx    : 8;    ///< multiplayer index if plane reported via sim/multiplayer/position dataRefs, 0 if not
            // Filler for 8-byte alignment
            unsigned    filler2     : 8;
            unsigned    filler3     : 32;
        } bits;                             ///< Flights phase, on-ground status, lights
        
        // V1.22 additions
        double          lat             = 0.0f; ///< [°] latitude
        double          lon             = 0.0f; ///< [°] longitude
        double          alt_ft          = 0.0f; ///< [ft] altitude

        
        /// Constructor initializes some data without defaults
        LTAPIBulkData()
        { memset(&bits, 0, sizeof(bits)); }
    };
    
    /// @brief Bulk text transfer structur for communication with LTAPI
    /// @note To avoid alignment issues with arrays we keep this struct 8-byte-aligned
    struct LTAPIBulkInfoTexts {
    public:
        // identification
        uint64_t        keyNum;             ///< a/c id, usually transp hex code, or any other unique id (FLARM etc.)
        char            registration[8];    ///< tail number like "D-AISD"
        // aircraft model/operator
        char            modelIcao[8];       ///< ICAO aircraft type like "A321"
        char            acClass[4];         ///< a/c class like "L2J"
        char            wtc[4];             ///< wake turbulence category like H,M,L/M,L
        char            opIcao[8];          ///< ICAO-code of operator like "DLH"
        char            man[40];            ///< human-readable manufacturer like "Airbus"
        char            model[40];          ///< human-readable a/c model like "A321-231"
        char            catDescr[40];       ///< human-readable category description
        char            op[40];             ///< human-readable operator like "Lufthansa"
        // flight data
        char            callSign[8];        ///< call sign like "DLH56C"
        char            squawk[8];          ///< squawk code (as text) like "1000"
        char            flightNumber[8];    ///< flight number like "LH1113"
        char            origin[8];          ///< origin airport (IATA or ICAO) like "MAD" or "LEMD"
        char            destination[8];     ///< destination airport (IATA or ICAO) like "FRA" or "EDDF"
        char            trackedBy[24];      ///< name of channel deliverying the underlying tracking data

        // V1.22 additions, in V2.40 extended from 24 to 40 chars
        char            cslModel[40];       ///< name of CSL model used for actual rendering of plane

        /// Constructor initializes all data with zeroes
        LTAPIBulkInfoTexts()
        { memset(this, 0, sizeof(*this)); }
    };
    
    /// Structure to return plane's lights status
    struct LTLights {
        bool beacon     : 1;                ///< beacon light
        bool strobe     : 1;                ///< strobe light
        bool nav        : 1;                ///< navigaton lights
        bool landing    : 1;                ///< landing lights
        bool taxi       : 1;                ///< taxi lights
        
        /// Type conversion constructor
        LTLights ( const LTAPIBulkData::BulkBitsTy b ) :
        beacon(b.bcn), strobe(b.strb), nav(b.nav), landing(b.land), taxi(b.taxi){}
    };
    
protected:
    LTAPIBulkData       bulk;               ///< numerical plane's data
    LTAPIBulkInfoTexts  info;               ///< textual plane's data
    
    /// update helper, gets reset before updates, set during updates, stays false if not updated
    bool            bUpdated = false;
    
public:
    LTAPIAircraft();
    virtual ~LTAPIAircraft();
    
    // Updates an aircraft. If our key is defined it first verifies that the
    // key matches with the one currently available in the dataRefs.
    // Returns false if not.
    // If our key is not defined it just accepts anything available.
    // Updates all fields, set bUpdated and returns true.
    /// @brief Updates the aircraft with fresh numerical values, called from LTAPIConnect::UpdateAcList()
    /// @param __bulk A structure with updated numeric aircraft data
    /// @param __inSize Number of bytes returned by LiveTraffic
    virtual bool updateAircraft(const LTAPIBulkData& __bulk, size_t __inSize);
    /// @brief Updates the aircraft with fresh textual information, called from LTAPIConnect::UpdateAcList()
    /// @param __info A structure with updated textual info
    /// @param __inSize Number of bytes returned by LiveTraffic
    virtual bool updateAircraft(const LTAPIBulkInfoTexts& __info, size_t __inSize);
    /// Helper in update loop to detected removed aircrafts
    bool isUpdated () const { return bUpdated; }
    /// Helper in update loop, resets `bUpdated` flag
    void resetUpdated ()    { bUpdated = false; }
    
    // data access
public:
    std::string     getKey()            const { return key; }                   ///< Unique key for this aircraft, usually ICAO transponder hex code
    // identification
    std::string     getRegistration()   const { return info.registration; }     ///< tail number like "D-AISD"
    // aircraft model/operator
    std::string     getModelIcao()      const { return info.modelIcao; }        ///< ICAO aircraft type like "A321"
    std::string     getAcClass()        const { return info.acClass; }          ///< a/c class like "L2J"
    std::string     getWtc()            const { return info.wtc; }              ///< wake turbulence category like H,M,L/M,L
    std::string     getOpIcao()         const { return info.opIcao; }           ///< ICAO-code of operator like "DLH"
    std::string     getMan()            const { return info.man; }              ///< human-readable manufacturer like "Airbus"
    std::string     getModel()          const { return info.model; }            ///< human-readable a/c model like "A321-231"
    std::string     getCatDescr()       const { return info.catDescr; }         ///< human-readable category description
    std::string     getOp()             const { return info.op; }               ///< human-readable operator like "Lufthansa"
    std::string     getCslModel()       const { return info.cslModel; }         ///< name of CSL model used for actual rendering of plane
    // flight data
    std::string     getCallSign()       const { return info.callSign; }         ///< call sign like "DLH56C"
    std::string     getSquawk()         const { return info.squawk; }           ///< squawk code (as text) like "1000"
    std::string     getFlightNumber()   const { return info.flightNumber; }     ///< flight number like "LH1113"
    std::string     getOrigin()         const { return info.origin; }           ///< origin airport (IATA or ICAO) like "MAD" or "LEMD"
    std::string     getDestination()    const { return info.destination; }      ///< destination airport (IATA or ICAO) like "FRA" or "EDDF"
    std::string     getTrackedBy()      const { return info.trackedBy; }        ///< name of channel deliverying the underlying tracking data
    // combined info
    std::string     getDescription()    const;                                  ///< some reasonable descriptive string formed from the above, like an identifier, type, form/to
    // position, attitude
    double          getLat()            const { return bulk.lat; }              ///< [°] latitude
    double          getLon()            const { return bulk.lon; }              ///< [°] longitude
    double          getAltFt()          const { return bulk.alt_ft; }           ///< [ft] altitude
    float           getHeading()        const { return bulk.heading; }          ///< [°] heading
    float           getTrack()          const { return bulk.track; }            ///< [°] track over ground
    float           getRoll()           const { return bulk.roll; }             ///< [°] roll: positive right
    float           getPitch()          const { return bulk.pitch; }            ///< [°] pitch: positive up
    float           getSpeedKn()        const { return bulk.speed_kt; }         ///< [kt] ground speed
    float           getVSIft()          const { return bulk.vsi_ft; }           ///< [ft/minute] vertical speed, positive up
    float           getTerrainFt()      const { return bulk.terrainAlt_ft; }    ///< [ft] terrain altitude beneath plane
    float           getHeightFt()       const { return bulk.height_ft; }        ///< [ft] height AGL
    bool            isOnGnd()           const { return bulk.bits.onGnd; }       ///< Is plane on ground?
    LTFlightPhase   getPhase()          const { return bulk.bits.phase; }       ///< flight phase
    std::string     getPhaseStr()       const;                                  ///< flight phase as string
    // configuration
    float           getFlaps()          const { return bulk.flaps; }            ///< flap position: 0.0 retracted, 1.0 fully extended
    float           getGear()           const { return bulk.gear; }             ///< gear position: 0.0 retracted, 1.0 fully extended
    float           getReversers()      const { return bulk.reversers; }        ///< reversers position: 0.0 closed, 1.0 fully opened
    LTLights        getLights()         const { return bulk.bits; }             ///< all plane's lights
    // simulation
    float           getBearing()        const { return bulk.bearing; }          ///< [°] to current camera position
    float           getDistNm()         const { return bulk.dist_nm; }          ///< [nm] distance to current camera
    int             getMultiIdx()       const { return bulk.bits.multiIdx; }    ///< multiplayer index if plane reported via sim/multiplayer/position dataRefs, 0 if not

    // calculated
    /// @brief `lat`/`lon`/`alt` converted to local coordinates
    /// @see https://developer.x-plane.com/sdk/XPLMGraphics/#XPLMWorldToLocal
    /// @param[out] x Local x coordinate
    /// @param[out] y Local y coordinate
    /// @param[out] z Local z coordinate
    void            getLocalCoord (double& x, double& y, double& z) const
    { XPLMWorldToLocal(bulk.lat,bulk.lon,bulk.alt_ft, &x,&y,&z); }

public:
    /// @brief Standard object creation callback.
    /// @return An empty LTAPIAircraft object.
    static LTAPIAircraft* CreateNewObject() { return new LTAPIAircraft(); }
};

//
// MapLTAPIAircraft
//

/// Smart pointer to an TLAPIAircraft object
typedef std::shared_ptr<LTAPIAircraft> SPtrLTAPIAircraft;

/// @brief Map of all aircrafts stored as smart pointers to LTAPIAircraft objects
///
/// This is what LTAPIConnect::UpdateAcList() returns: a map of all aircrafts.
/// They key into the map is the aircraft's key (most often the
/// ICAO transponder hex code).
///
/// The value is a smart pointer to an LTAPIAircraft object.
/// As we use smart pointers, object storage is deallocated as soon
/// as objects are removed from the map. Effectively, the map manages
/// storage.
typedef std::map<std::string,SPtrLTAPIAircraft> MapLTAPIAircraft;

/// @brief Simple list of smart pointers to LTAPIAircraft objects
///
/// This is used to return aircraft objects which got removed by LiveTraffic,
/// see LTAPIConnect::UpdateAcList()
typedef std::list<SPtrLTAPIAircraft> ListLTAPIAircraft;

/// @brief Connects to LiveTraffic's dataRefs and returns aircraft information.
///
/// Typically, exactly one instance of this class is used.
class LTAPIConnect
{
public:
    /// @brief Callback function type passed in to LTAPIConnect()
    /// @return New LTAPIAircraft object or derived class' object
    ///
    /// The callback is actually called by UpdateAcList().
    ///
    /// If you use a class derived from LTAPIAircraft, then you
    /// pass in a pointer to a callback function, which returns new empty
    /// objects of _your_ derived class whenever UpdateAcList() needs to create
    /// a new aircraft object.
    typedef LTAPIAircraft* fCreateAcObject();
    
    /// Number of seconds between two calls of the expensive type,
    /// which fetches all texts from LiveTraffic, which in fact don't change
    /// that often anyway
    std::chrono::seconds sPeriodExpsv = std::chrono::seconds(3);
    
protected:
    /// Number of aircraft to fetch in one bulk operation
    const int iBulkAc = 50;
    /// bulk data array for communication with LT
    std::unique_ptr<LTAPIAircraft::LTAPIBulkData[]> vBulkNum;
    /// bulk info text array for communication with LT
    std::unique_ptr<LTAPIAircraft::LTAPIBulkInfoTexts[]> vInfoTexts;

protected:
    /// Pointer to callback function returning new aircraft objects
    fCreateAcObject* pfCreateAcObject = nullptr;
    
    /// THE map of aircrafts
    MapLTAPIAircraft mapAc;
    
    /// Last fetching of expensive data
    std::chrono::time_point<std::chrono::steady_clock> lastExpsvFetch;
    
public:
    /// @brief Constructor
    /// @param _pfCreateAcObject (Optional) Poitner to callback function,
    ///        which returns new aircraft objects, see typedef fCreateAcObject()
    /// @param numBulkAc Number of aircraft to fetch in one bulk operation
    LTAPIConnect(fCreateAcObject* _pfCreateAcObject = LTAPIAircraft::CreateNewObject,
                 int numBulkAc = 50);
    virtual ~LTAPIConnect();
    
    /// Is LiveTraffic available? (checks via XPLMFindPluginBySignature)
    static bool isLTAvail ();

    /// @brief LiveTraffic's version number
    /// @details Version number became available with v2.01 only. This is why 150 is returned in case
    ///          LiveTraffic is available, but not the dataRef to fetch the number from.
    /// @note    Calling this function from your XPluginStart or XPluginEnable is not guaranteed
    ///          to return proper results. Call from a flight loop callback,
    ///          e.g. create a one-time late-init flight loop callback function for this purpose.
    ///          LTAPIExample.cpp demonstrates this.\n
    ///          Depending on startup order, LiveTraffic might or might not have been started yet.
    ///          This note is basically true for all requests accessing LiveTraffic data.
    ///          It is noted here only because it is tempting to fetch the version number once only during startup.
    /// @return Version (like 201 for v2.01), or constant 150 if unknown, or 0 if LiveTraffic is unavailable
    static int getLTVerNr();

    /// @brief LiveTraffic's version date
    /// @details Version date became available with v2.01 only. This is why 20191231 is returned in case
    ///          LiveTraffic is available, but not the dataRef to fetch the date from.
    /// @return Version date (like 20200430 for 30-APR-2020), or constant 20191231 if unknown, or 0 if LiveTraffic is unavailable
    static int getLTVerDate();

    /// @brief Does LiveTraffic display aircrafts? (Is it activated?)
    ///
    /// This is the only function which checks again and again if LiveTraffic's
    /// dataRefs are available. Use this to verify if LiveTraffic is (now)
    /// available before calling any other function on LiveTraffic's dataRefs.
    static bool doesLTDisplayAc ();
    
    /// How many aircraft does LiveTraffic display right now?
    static int getLTNumAc ();
    
    /// @brief Does LiveTaffic control AI planes?
    /// @note If your plugin usually deals with AI/multiplayer planes,
    ///       then you don't need to check for AI/multiplayer planes _if_
    ///       doesLTControlAI() returns true: In this case the planes returned
    ///       in the AI/multiplayer dataRefs are just a subset selected by
    ///       LiveTraffic of what you get via UpdateAcList() anyway.
    ///       Avoid duplicates, just use LTAPI if doesLTControlAI() is `true`.
    static bool doesLTControlAI ();
    
    /// What is current simulated time in LiveTraffic (usually 'now' minus buffering period)?
    static time_t getLTSimTime ();

    /// What is current simulated time in LiveTraffic (usually 'now' minus buffering period)?
    static std::chrono::system_clock::time_point getLTSimTimePoint ();
    
    /// @brief Main function: updates map of aircrafts and returns reference to it.
    /// @param plistRemovedAc (Optional) If you want to know which a/c are
    ///        _removed_ during this call (because the disappeared from
    ///        LiveTraffic) then pass a ListLTAPIAircraft object:
    ///        LTAPI will transfer otherwise removed objects there and
    ///        management of them is then up to you.
    ///        LTAPI will only _emplace_back_ to the list, never remove anything.
    const MapLTAPIAircraft& UpdateAcList (ListLTAPIAircraft* plistRemovedAc = nullptr);
    
    /// Returns the map of aircraft as it currently stands
    const MapLTAPIAircraft& getAcMap () const { return mapAc; }
    
    /// @brief Finds an aircraft for a given multiplayer slot
    /// @param multiIdx The multiplayer index to look for
    /// @return Pointer to aircraft in slot `multiIdx`, is empty if not found
    SPtrLTAPIAircraft getAcByMultIdx (int multiIdx) const;
    
protected:
    /// @brief fetch bulk data and create/update aircraft objects
    /// @param numAc Total number of aircraft to fetch
    /// @param DR The dataRef to use for fetching the actual data from LT
    /// @param[out] outSizeLT Returns LT's structure size
    /// @param vBulk Reference to allocated memory for data transfer
    /// @tparam T is the structure to fill, either LTAPIAircraft::LTAPIBulkData or LTAPIAircraft::LTAPIBulkInfoTexts
    /// @return Have aircraft objects been created?
    template <class T>
    bool DoBulkFetch (int numAc, LTDataRef& DR, int& outSizeLT,
                      std::unique_ptr<T[]> &vBulk);
    
};


/// @brief Represents a dataRef and covers late binding.
///
/// Late binding is important: We read another plugin's dataRefs. The other
/// plugin (here: LiveTraffic) needs to register the dataRefs first before
/// we can find them. So we would potentially fail if we search for them
/// during startup (like when declaring statically).
/// With this wrapper we still can do static declaration because the actual
/// call to XPLMFindDataRef happens only the first time we actually access it.

class LTDataRef {
protected:
    std::string     sDataRef;           ///< dataRef name, passed in via constructor
    XPLMDataRef     dataRef = NULL;     ///< dataRef identifier returned by X-Plane
    XPLMDataTypeID  dataTypes = xplmType_Unknown;   ///< supported data types
    bool            bValid = true;      ///< does this object have a valid binding to a dataRef already?
public:
    LTDataRef (std::string _sDataRef);  ///< Constructor, set the dataRef's name
    inline bool needsInit () const { return bValid && !dataRef; }
    /// @brief Found the dataRef _and_ it contains formats we can work with?
    bool    isValid ();
    /// Finds the dataRef (and would try again and again, no matter what bValid says)
    bool    FindDataRef ();

    // types
    /// Get types supported by the dataRef
    XPLMDataTypeID getDataRefTypes() const { return dataTypes; }
    /// Is `int` a supported dataRef type?
    bool    hasInt ()   const { return dataTypes & xplmType_Int; }
    /// Is `float` a supported dataRef type?
    bool    hasFloat () const { return dataTypes & xplmType_Float; }
    /// Defines which types to work with to become `valid`
    static constexpr XPLMDataTypeID usefulTypes =
            xplmType_Int | xplmType_Float | xplmType_Data;

    /// @brief Get dataRef's integer value.
    /// Silently returns 0 if dataRef doesn't exist.
    int     getInt();
    /// @brief Get dataRef's float value.
    /// Silently returns 0.0f if dataRef doesn't exist.
    float   getFloat();
    /// Gets dataRef's integer value and returns if it is not zero
    inline bool getBool() { return getInt() != 0; }
    /// Gets dataRef's binary data
    int     getData(void* pOut, int inOffset, int inMaxBytes);
    
    /// Writes an integer value to the dataRef
    void    set(int i);
    /// Writes a float vlue to the dataRef
    void    set(float f);

protected:
};

//
// Sizes for version compatibility comparison
//

/// Size of original bulk structure as per LiveTraffic v1.20
constexpr size_t LTAPIBulkData_v120 = 80;
/// Size of current bulk structure
constexpr size_t LTAPIBulkData_v122 = sizeof(LTAPIAircraft::LTAPIBulkData);

/// Size of original bulk info structure as per previous versions of LiveTraffic
constexpr size_t LTAPIBulkInfoTexts_v120 = 264;
constexpr size_t LTAPIBulkInfoTexts_v122 = 288;
/// Size of current bulk info structure
constexpr size_t LTAPIBulkInfoTexts_v240 = sizeof(LTAPIAircraft::LTAPIBulkInfoTexts);

#endif /* LTAPI_h */
