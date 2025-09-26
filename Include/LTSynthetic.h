/// @file       LTSynthetic.h
/// @brief      Synthetic tracking data, e.g. for parked aircraft
/// @details    Defines SyntheticConnection:
///             - Scans mapFd (all available tracking data in LiveTraffic)
///               for parked aircraft and keeps a position copy
///             - For any parked aircraft no longer actively served by any other channel,
///               send the same position data regularly
/// @author     Birger Hoppe
/// @copyright  (c) 2024 Birger Hoppe
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

#ifndef LTSynthetic_h
#define LTSynthetic_h

#include "LTChannel.h"

//
// MARK: Synthetic Traffic Enums and Structs
//

/// Types of synthetic traffic to generate (must match SYN_TRAFFIC_MASK_* in DataRefs.h)
enum SyntheticTrafficType : unsigned char {
    SYN_TRAFFIC_NONE    = 0,    ///< No synthetic traffic
    SYN_TRAFFIC_GA      = 1,    ///< General Aviation traffic
    SYN_TRAFFIC_AIRLINE = 2,    ///< Commercial airline traffic
    SYN_TRAFFIC_MILITARY= 4,    ///< Military traffic
    SYN_TRAFFIC_ALL     = 7     ///< All traffic types
};

/// Synthetic flight states for AI behavior
enum SyntheticFlightState : unsigned char {
    SYN_STATE_PARKED = 0,       ///< Aircraft is parked
    SYN_STATE_STARTUP,          ///< Starting up engines
    SYN_STATE_TAXI_OUT,         ///< Taxiing to runway
    SYN_STATE_TAKEOFF,          ///< Taking off
    SYN_STATE_CLIMB,            ///< Climbing to cruise
    SYN_STATE_CRUISE,           ///< Cruising
    SYN_STATE_HOLD,             ///< In holding pattern
    SYN_STATE_DESCENT,          ///< Descending
    SYN_STATE_APPROACH,         ///< On approach
    SYN_STATE_LANDING,          ///< Landing
    SYN_STATE_TAXI_IN,          ///< Taxiing to gate
    SYN_STATE_SHUTDOWN          ///< Shutting down
};

/// Configuration for synthetic traffic generation
struct SyntheticTrafficConfig {
    bool enabled = false;                   ///< Enable synthetic traffic generation
    unsigned trafficTypes = SYN_TRAFFIC_GA; ///< Bitmask of traffic types to generate
    int maxAircraft = 200;                  ///< Maximum number of synthetic aircraft
    float density = 0.5f;                   ///< Traffic density (0.0 - 1.0)
    float gaRatio = 0.6f;                   ///< Ratio of GA traffic
    float airlineRatio = 0.3f;              ///< Ratio of airline traffic
    float militaryRatio = 0.1f;             ///< Ratio of military traffic
    bool enableTTS = false;                 ///< Enable TTS communications
    bool userAwareness = true;              ///< Aircraft react to user presence
    bool weatherOperations = true;          ///< Weather-based operations
    // Note: commRange removed - now using realistic communication degradation instead of hard range limit
};

/// Individual aircraft performance data based on realistic specifications
struct AircraftPerformance {
    std::string icaoType;           ///< ICAO aircraft type code
    double cruiseSpeedKts;          ///< Typical cruise speed in knots
    double maxSpeedKts;             ///< Maximum speed in knots
    double stallSpeedKts;           ///< Stall speed in knots (clean configuration)
    double serviceCeilingFt;        ///< Service ceiling in feet
    double climbRateFpm;            ///< Typical climb rate in feet per minute
    double descentRateFpm;          ///< Typical descent rate in feet per minute
    double maxAltFt;                ///< Maximum altitude in feet
    double approachSpeedKts;        ///< Typical approach speed in knots
    double taxiSpeedKts;            ///< Typical taxi speed in knots
    
    AircraftPerformance(const std::string& type = "", double cruise = 120, double maxSpd = 150, 
                       double stall = 60, double ceiling = 15000, double climb = 800, double descent = 800,
                       double maxAlt = 18000, double approach = 80, double taxi = 15)
        : icaoType(type), cruiseSpeedKts(cruise), maxSpeedKts(maxSpd), stallSpeedKts(stall),
          serviceCeilingFt(ceiling), climbRateFpm(climb), descentRateFpm(descent), 
          maxAltFt(maxAlt), approachSpeedKts(approach), taxiSpeedKts(taxi) {}
};

//
// MARK: SyntheticConnection
//

/// Synthetic tracking data creation with advanced AI behavior
class SyntheticConnection : public LTFlightDataChannel
{
protected:
    /// Enhanced synthetic aircraft data with AI state
    struct SynDataTy {
        LTFlightData::FDStaticData stat;        ///< plane's static data
        positionTy pos;                         ///< plane's position
        SyntheticFlightState state;             ///< current flight state
        SyntheticTrafficType trafficType;       ///< type of traffic (GA, airline, military)
        double stateChangeTime;                 ///< when the state was last changed
        double nextEventTime;                   ///< when next event should occur
        std::string flightPlan;                 ///< generated flight plan
        std::string assignedRunway;             ///< assigned runway
        double targetAltitude;                  ///< target altitude
        double targetSpeed;                     ///< target speed
        double holdingTime;                     ///< time spent in holding (if applicable)
        bool isUserAware;                       ///< aircraft is aware of user presence
        std::string lastComm;                   ///< last communication message
        double lastCommTime;                    ///< time of last communication
        double lastPosUpdateTime;               ///< time of last position update
        
        // Navigation and terrain awareness
        std::vector<positionTy> flightPath;     ///< waypoints for navigation
        size_t currentWaypoint;                 ///< current waypoint index
        positionTy targetWaypoint;              ///< current target waypoint
        double lastTerrainCheck;                ///< time of last terrain check
        double terrainElevation;                ///< cached terrain elevation at current position
        XPLMProbeRef terrainProbe;             ///< terrain probe reference for this aircraft
        double headingChangeRate;               ///< smooth heading change rate (deg/sec)
        double targetHeading;                   ///< target heading for navigation
        
        SynDataTy() : currentWaypoint(0), lastTerrainCheck(0.0), terrainElevation(0.0), 
                     terrainProbe(nullptr), headingChangeRate(2.0), targetHeading(0.0) {}
        
        ~SynDataTy() {
            if (terrainProbe) {
                XPLMDestroyProbe(terrainProbe);
                terrainProbe = nullptr;
            }
        }
    };
    /// Stores enhanced data per tracked plane
    typedef std::map<LTFlightData::FDKeyTy, SynDataTy> mapSynDataTy;
    /// @brief Enhanced synthetic aircraft data
    /// @note Defined `static` to preserve information across restarts
    static mapSynDataTy mapSynData;
    
    /// Configuration for synthetic traffic
    static SyntheticTrafficConfig config;
    
    /// Weather-based flight delays
    std::map<std::string, double> weatherDelays;
    
    /// Navigation data cache for SID/STAR procedures  
    std::map<std::string, std::vector<positionTy>> sidStarCache;
public:
    /// Constructor
    SyntheticConnection ();
    /// No URL involved
    std::string GetURL (const positionTy&) override { return ""; }
    /// Scan for relevant flight data and generate new synthetic traffic
    bool FetchAllData(const positionTy&) override;
    /// Processes the available stored data and updates AI behavior
    bool ProcessFetchedData () override;
    
    /// Configuration access
    static const SyntheticTrafficConfig& GetConfig() { return config; }
    static void SetConfig(const SyntheticTrafficConfig& newConfig) { config = newConfig; }
    
    /// Generate new synthetic aircraft
    bool GenerateTraffic(const positionTy& centerPos);
    
    /// Update AI behavior for existing aircraft
    void UpdateAIBehavior(SynDataTy& synData, double currentTime);
    
    /// Generate flight plan for aircraft
    std::string GenerateFlightPlan(const positionTy& origin, const positionTy& destination, 
                                   SyntheticTrafficType trafficType);
    
    /// Find SID/STAR procedures using X-Plane navdata
    std::vector<positionTy> GetSIDSTAR(const std::string& airport, const std::string& runway, bool isSID);
    
    /// Generate SID procedures using actual navigation database  
    std::vector<positionTy> GenerateSIDFromNavData(const positionTy& airportPos, const std::string& airport, const std::string& runway);
    
    /// Generate STAR procedures using actual navigation database
    std::vector<positionTy> GenerateSTARFromNavData(const positionTy& airportPos, const std::string& airport, const std::string& runway);
    
    /// Generate TTS communication message
    std::string GenerateCommMessage(const SynDataTy& synData, const positionTy& userPos);
    
    /// Check weather impact on operations
    bool CheckWeatherImpact(const positionTy& pos, SynDataTy& synData);
    
    /// Generate realistic call sign based on traffic type
    std::string GenerateCallSign(SyntheticTrafficType trafficType);
    
    /// Generate aircraft type based on traffic type and operations
    std::string GenerateAircraftType(SyntheticTrafficType trafficType, const std::string& route = "");

protected:
    void Main () override;          ///< virtual thread main function
    
    /// Generate GA traffic patterns
    void GenerateGATraffic(const positionTy& centerPos);
    
    /// Generate airline traffic
    void GenerateAirlineTraffic(const positionTy& centerPos);
    
    /// Generate military traffic
    void GenerateMilitaryTraffic(const positionTy& centerPos);
    
    /// Find nearest airports for traffic generation
    std::vector<std::string> FindNearbyAirports(const positionTy& centerPos, double radiusNM);
    
    /// Create synthetic aircraft with realistic parameters
    bool CreateSyntheticAircraft(const std::string& key, const positionTy& pos, 
                                  SyntheticTrafficType trafficType);
    
    /// Handle state transitions for AI aircraft
    void HandleStateTransition(SynDataTy& synData, SyntheticFlightState newState, double currentTime);
    
    /// Calculate performance parameters based on aircraft type
    void CalculatePerformance(SynDataTy& synData);
    
    /// Get aircraft performance data for a specific ICAO type
    const AircraftPerformance* GetAircraftPerformance(const std::string& icaoType) const;
    
    /// Initialize aircraft performance database with realistic data
    static void InitializeAircraftPerformanceDB();
    
    /// Aircraft performance database
    static std::map<std::string, AircraftPerformance> aircraftPerfDB;
    
#ifdef DEBUG
    /// Validate aircraft performance database (debug only)
    static void ValidateAircraftPerformanceDB();
#endif
    
    /// Update aircraft position based on movement
    void UpdateAircraftPosition(SynDataTy& synData, double currentTime);
    
    /// Navigation and terrain awareness methods
    void UpdateNavigation(SynDataTy& synData, double currentTime);
    void UpdateTerrainAwareness(SynDataTy& synData);
    void GenerateFlightPath(SynDataTy& synData, const positionTy& origin, const positionTy& destination);
    bool IsTerrainSafe(const positionTy& position, double minClearance = 150.0);
    double GetTerrainElevation(const positionTy& position, XPLMProbeRef& probeRef);
    void SmoothHeadingChange(SynDataTy& synData, double targetHeading, double deltaTime);
    positionTy GetNextWaypoint(SynDataTy& synData);
    
    /// Process TTS communications
    void ProcessTTSCommunication(SynDataTy& synData, const std::string& message);
    
    /// Update user awareness behavior
    void UpdateUserAwareness(SynDataTy& synData, const positionTy& userPos);
    
    /// Helper functions for realistic communication degradation
    std::string ApplyLightStaticEffects(const std::string& message);
    std::string ApplyModerateStaticEffects(const std::string& message);
    std::string ApplyHeavyStaticEffects(const std::string& message);
    
    /// Generate varied position around a center point to prevent aircraft stacking
    positionTy GenerateVariedPosition(const positionTy& centerPos, double minDistanceNM, double maxDistanceNM);
};

#endif /* LTSynthetic_h */
