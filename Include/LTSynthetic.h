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

/// Synthetic flight states for AI behavior - Enhanced to match detailed flight phases
enum SyntheticFlightState : unsigned char {
    SYN_STATE_PARKED = 0,       ///< Aircraft is parked
    SYN_STATE_STARTUP,          ///< Starting up engines
    SYN_STATE_TAXI_OUT,         ///< Taxiing to runway
    SYN_STATE_LINE_UP_WAIT,     ///< Lined up on runway, waiting for takeoff clearance
    SYN_STATE_TAKEOFF_ROLL,     ///< Takeoff roll on runway
    SYN_STATE_ROTATE,           ///< Rotating for liftoff
    SYN_STATE_LIFT_OFF,         ///< Just lifted off runway
    SYN_STATE_INITIAL_CLIMB,    ///< Initial climb phase (gear up)
    SYN_STATE_CLIMB,            ///< Regular climb phase
    SYN_STATE_CRUISE,           ///< Cruising
    SYN_STATE_HOLD,             ///< In holding pattern
    SYN_STATE_DESCENT,          ///< Descending
    SYN_STATE_APPROACH,         ///< On approach
    SYN_STATE_FINAL,            ///< Final approach
    SYN_STATE_FLARE,            ///< Flare for landing
    SYN_STATE_TOUCH_DOWN,       ///< Touchdown moment
    SYN_STATE_ROLL_OUT,         ///< Landing rollout
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
    bool dynamicDensity = false;            ///< Enable dynamic density based on X-Plane scenery
    float sceneryDensityMin = 0.1f;         ///< Minimum density in sparse scenery areas
    float sceneryDensityMax = 1.0f;         ///< Maximum density in dense scenery areas
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
        
        // TCAS (Traffic Collision Avoidance System) data
        double lastTCASCheck;                   ///< time of last TCAS scan
        bool tcasActive;                        ///< TCAS system active
        std::string tcasAdvisory;               ///< current TCAS advisory (RA/TA)
        double tcasAvoidanceHeading;            ///< heading to avoid traffic
        double tcasAvoidanceAltitude;           ///< altitude to avoid traffic
        bool inTCASAvoidance;                   ///< currently executing TCAS avoidance maneuver
        
        // Enhanced TCAS data
        std::string nearestTrafficCallsign;     ///< callsign of nearest conflicting traffic
        double tcasVerticalSpeed;               ///< vertical speed during TCAS maneuver (m/s)
        int tcasAdvisoryLevel;                  ///< 0=none, 1=traffic advisory, 2=resolution advisory
        double tcasManeuverStartTime;           ///< when TCAS maneuver started
        positionTy predictedPosition;           ///< predicted position for conflict detection
        double conflictSeverity;                ///< severity of conflict (0.0-1.0)
        
        // Seasonal and time-based traffic variations
        double seasonalFactor;                  ///< seasonal traffic adjustment (0.5-1.5)
        double timeFactor;                      ///< time-of-day traffic adjustment (0.3-1.8)
        std::string weatherConditions;          ///< current weather conditions affecting operations
        double weatherVisibility;               ///< visibility in meters for weather operations
        double weatherWindSpeed;                ///< wind speed in m/s
        double weatherWindDirection;            ///< wind direction in degrees
        
        // Enhanced navigation database integration
        std::vector<std::string> availableSIDs;      ///< available SID procedures for current airport
        std::vector<std::string> availableSTARs;     ///< available STAR procedures for current airport
        std::string assignedSID;                     ///< assigned SID procedure name
        std::string assignedSTAR;                    ///< assigned STAR procedure name
        bool usingRealNavData;                       ///< true if using real X-Plane nav data
        
        // Communication frequency management
        double currentComFreq;                  ///< current communication frequency (MHz)
        std::string currentAirport;             ///< nearest airport for frequency selection
        std::string destinationAirport;         ///< destination airport for approach/landing
        std::string currentFreqType;            ///< frequency type (tower, ground, approach, center)
        double lastFreqUpdate;                  ///< time of last frequency update
        
        // Enhanced ground operations  
        std::vector<positionTy> taxiRoute;      ///< planned taxi route waypoints
        size_t currentTaxiWaypoint;             ///< current taxi waypoint index
        std::string assignedGate;               ///< assigned gate or parking position
        bool groundCollisionAvoidance;          ///< ground collision avoidance active
        
        // CSL model scanning timing
        double lastCSLScanTime;                 ///< last time CSL models were scanned
        
        SynDataTy() : currentWaypoint(0), lastTerrainCheck(0.0), terrainElevation(0.0), 
                     terrainProbe(nullptr), headingChangeRate(2.0), targetHeading(0.0),
                     lastTCASCheck(0.0), tcasActive(true), tcasAdvisory(""),
                     tcasAvoidanceHeading(0.0), tcasAvoidanceAltitude(0.0), inTCASAvoidance(false),
                     nearestTrafficCallsign(""), tcasVerticalSpeed(0.0), tcasAdvisoryLevel(0), 
                     tcasManeuverStartTime(0.0), conflictSeverity(0.0),
                     seasonalFactor(1.0), timeFactor(1.0), weatherConditions("CLEAR"), 
                     weatherVisibility(10000.0), weatherWindSpeed(0.0), weatherWindDirection(0.0),
                     assignedSID(""), assignedSTAR(""), usingRealNavData(false),
                     currentComFreq(121.5), currentAirport(""), destinationAirport(""), currentFreqType("unicom"), lastFreqUpdate(0.0),
                     currentTaxiWaypoint(0), assignedGate(""), groundCollisionAvoidance(false),
                     lastCSLScanTime(0.0) {}
        
        ~SynDataTy() {
            // Thread-safe cleanup of terrain probe
            if (terrainProbe) {
                try {
                    XPLMDestroyProbe(terrainProbe);
                } catch (...) {
                    // Ignore exceptions during cleanup to prevent crash
                }
                terrainProbe = nullptr;
            }
        }
        
        // Copy constructor - need to handle probe ownership
        SynDataTy(const SynDataTy& other) 
            : stat(other.stat), pos(other.pos), state(other.state), trafficType(other.trafficType),
              stateChangeTime(other.stateChangeTime), nextEventTime(other.nextEventTime),
              flightPlan(other.flightPlan), assignedRunway(other.assignedRunway),
              targetAltitude(other.targetAltitude), targetSpeed(other.targetSpeed),
              holdingTime(other.holdingTime), isUserAware(other.isUserAware),
              lastComm(other.lastComm), lastCommTime(other.lastCommTime),
              lastPosUpdateTime(other.lastPosUpdateTime), flightPath(other.flightPath),
              currentWaypoint(other.currentWaypoint), targetWaypoint(other.targetWaypoint),
              lastTerrainCheck(other.lastTerrainCheck), terrainElevation(other.terrainElevation),
              terrainProbe(nullptr), // Don't copy probe - create new one when needed
              headingChangeRate(other.headingChangeRate), targetHeading(other.targetHeading),
              lastTCASCheck(other.lastTCASCheck), tcasActive(other.tcasActive),
              tcasAdvisory(other.tcasAdvisory), tcasAvoidanceHeading(other.tcasAvoidanceHeading),
              tcasAvoidanceAltitude(other.tcasAvoidanceAltitude), inTCASAvoidance(other.inTCASAvoidance),
              nearestTrafficCallsign(other.nearestTrafficCallsign), tcasVerticalSpeed(other.tcasVerticalSpeed),
              tcasAdvisoryLevel(other.tcasAdvisoryLevel), tcasManeuverStartTime(other.tcasManeuverStartTime),
              predictedPosition(other.predictedPosition), conflictSeverity(other.conflictSeverity),
              seasonalFactor(other.seasonalFactor), timeFactor(other.timeFactor),
              weatherConditions(other.weatherConditions), weatherVisibility(other.weatherVisibility),
              weatherWindSpeed(other.weatherWindSpeed), weatherWindDirection(other.weatherWindDirection),
              availableSIDs(other.availableSIDs), availableSTARs(other.availableSTARs),
              assignedSID(other.assignedSID), assignedSTAR(other.assignedSTAR), usingRealNavData(other.usingRealNavData),
              currentComFreq(other.currentComFreq), currentAirport(other.currentAirport),
              destinationAirport(other.destinationAirport), currentFreqType(other.currentFreqType), lastFreqUpdate(other.lastFreqUpdate),
              taxiRoute(other.taxiRoute), currentTaxiWaypoint(other.currentTaxiWaypoint),
              assignedGate(other.assignedGate), groundCollisionAvoidance(other.groundCollisionAvoidance),
              lastCSLScanTime(other.lastCSLScanTime) {}
        
        // Assignment operator - need to handle probe ownership
        SynDataTy& operator=(const SynDataTy& other) {
            if (this != &other) {
                // Clean up existing probe first
                if (terrainProbe) {
                    try {
                        XPLMDestroyProbe(terrainProbe);
                    } catch (...) {
                        // Ignore exceptions during cleanup
                    }
                    terrainProbe = nullptr;
                }
                
                // Copy all other members
                stat = other.stat;
                pos = other.pos;
                state = other.state;
                trafficType = other.trafficType;
                stateChangeTime = other.stateChangeTime;
                nextEventTime = other.nextEventTime;
                flightPlan = other.flightPlan;
                assignedRunway = other.assignedRunway;
                targetAltitude = other.targetAltitude;
                targetSpeed = other.targetSpeed;
                holdingTime = other.holdingTime;
                isUserAware = other.isUserAware;
                lastComm = other.lastComm;
                lastCommTime = other.lastCommTime;
                lastPosUpdateTime = other.lastPosUpdateTime;
                flightPath = other.flightPath;
                currentWaypoint = other.currentWaypoint;
                targetWaypoint = other.targetWaypoint;
                lastTerrainCheck = other.lastTerrainCheck;
                terrainElevation = other.terrainElevation;
                // Don't copy probe - create new one when needed
                headingChangeRate = other.headingChangeRate;
                targetHeading = other.targetHeading;
                lastTCASCheck = other.lastTCASCheck;
                tcasActive = other.tcasActive;
                tcasAdvisory = other.tcasAdvisory;
                tcasAvoidanceHeading = other.tcasAvoidanceHeading;
                tcasAvoidanceAltitude = other.tcasAvoidanceAltitude;
                inTCASAvoidance = other.inTCASAvoidance;
                nearestTrafficCallsign = other.nearestTrafficCallsign;
                tcasVerticalSpeed = other.tcasVerticalSpeed;
                tcasAdvisoryLevel = other.tcasAdvisoryLevel;
                tcasManeuverStartTime = other.tcasManeuverStartTime;
                predictedPosition = other.predictedPosition;
                conflictSeverity = other.conflictSeverity;
                seasonalFactor = other.seasonalFactor;
                timeFactor = other.timeFactor;
                weatherConditions = other.weatherConditions;
                weatherVisibility = other.weatherVisibility;
                weatherWindSpeed = other.weatherWindSpeed;
                weatherWindDirection = other.weatherWindDirection;
                availableSIDs = other.availableSIDs;
                availableSTARs = other.availableSTARs;
                assignedSID = other.assignedSID;
                assignedSTAR = other.assignedSTAR;
                usingRealNavData = other.usingRealNavData;
                currentComFreq = other.currentComFreq;
                currentAirport = other.currentAirport;
                destinationAirport = other.destinationAirport;
                currentFreqType = other.currentFreqType;
                lastFreqUpdate = other.lastFreqUpdate;
                taxiRoute = other.taxiRoute;
                currentTaxiWaypoint = other.currentTaxiWaypoint;
                assignedGate = other.assignedGate;
                groundCollisionAvoidance = other.groundCollisionAvoidance;
                lastCSLScanTime = other.lastCSLScanTime;
            }
            return *this;
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
    
    /// CSL Model database for enhanced aircraft selection
    struct CSLModelData {
        std::string modelName;      ///< Model name/ID
        std::string icaoType;       ///< ICAO aircraft type
        std::string airline;        ///< Airline code  
        std::string livery;         ///< Livery information
        SyntheticTrafficType category; ///< GA, Airline, or Military classification
    };
    std::vector<CSLModelData> availableCSLModels;   ///< Cache of available CSL models
    std::map<SyntheticTrafficType, std::vector<size_t>> cslModelsByType; ///< Models grouped by type
    double lastCSLScanTime;                         ///< Last time CSL models were scanned
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
    
    /// Generate realistic call sign based on traffic type and location (country-specific)
    std::string GenerateCallSign(SyntheticTrafficType trafficType, const positionTy& pos = positionTy());
    
    /// Generate aircraft type based on traffic type and operations
    std::string GenerateAircraftType(SyntheticTrafficType trafficType, const std::string& route = "", const std::string& country = "US");
    
    /// Get country code from position (lat/lon) for registration purposes
    std::string GetCountryFromPosition(const positionTy& pos);
    
    /// Generate country-specific aircraft registration
    std::string GenerateCountrySpecificRegistration(const std::string& countryCode, SyntheticTrafficType trafficType);
    
    /// Update communication frequencies based on aircraft position and airport proximity
    void UpdateCommunicationFrequencies(SynDataTy& synData, const positionTy& userPos);
    
    /// Enhanced weather integration methods
    void UpdateAdvancedWeatherOperations(SynDataTy& synData, double currentTime);
    void GetCurrentWeatherConditions(const positionTy& pos, std::string& conditions, double& visibility, double& windSpeed, double& windDirection);
    double CalculateWeatherImpactFactor(const std::string& weatherConditions, double visibility, double windSpeed);
    
    /// Seasonal and time-based traffic variations
    double CalculateSeasonalFactor(double currentTime);
    double CalculateTimeOfDayFactor(double currentTime);
    void ApplyTrafficVariations(SynDataTy& synData, double currentTime);
    
    /// Enhanced navigation database integration
    void QueryAvailableSIDSTARProcedures(SynDataTy& synData, const std::string& airport);
    std::vector<std::string> GetRealSIDProcedures(const std::string& airport, const std::string& runway);
    std::vector<std::string> GetRealSTARProcedures(const std::string& airport, const std::string& runway);
    void AssignRealNavProcedures(SynDataTy& synData);
    
    /// Extended country coverage for aircraft registrations
    std::string GetExtendedCountryFromPosition(const positionTy& pos);
    std::string GenerateExtendedCountryRegistration(const std::string& countryCode, SyntheticTrafficType trafficType);
    
    /// CSL Model scanning and selection
    void ScanAvailableCSLModels();
    void CreateFallbackCSLModels();
    SyntheticTrafficType CategorizeAircraftType(const std::string& icaoType);
    std::vector<std::string> GetAvailableCSLModels(SyntheticTrafficType trafficType);
    std::string SelectCSLModelForAircraft(SyntheticTrafficType trafficType, const std::string& route);
    
    /// Comprehensive country registrations (100+ countries)
    std::string GetComprehensiveCountryFromPosition(const positionTy& pos);
    std::string GenerateComprehensiveCountryRegistration(const std::string& countryCode, SyntheticTrafficType trafficType);
    
    /// Invalidate scenery density cache (called when scenery changes)
    static void InvalidateSceneryDensityCache();

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
    
    /// Find nearby military airports for military traffic generation
    std::vector<std::string> FindNearbyMilitaryAirports(const positionTy& centerPos, double radiusNM);
    
    /// Check if airport is likely a military airport based on naming patterns
    bool IsMilitaryAirport(const std::string& icao, const std::string& name);
    
    /// Get airport position by ICAO code
    positionTy GetAirportPosition(const std::string& icaoCode);
    
    /// Clear and refresh the airport cache from X-Plane navigation database
    static void RefreshAirportCache();
    
    /// Create synthetic aircraft with realistic parameters
    bool CreateSyntheticAircraft(const std::string& key, const positionTy& pos, 
                                  SyntheticTrafficType trafficType, const std::string& destinationAirport = "");
    
    /// Handle state transitions for AI aircraft
    void HandleStateTransition(SynDataTy& synData, SyntheticFlightState newState, double currentTime);
    
    /// Enhanced AI behavior helper functions
    std::string AssignRealisticRunway(const SynDataTy& synData);
    void SetRealisticCruiseAltitude(SynDataTy& synData);
    void SetRealisticDescentParameters(SynDataTy& synData);
    
    /// Enhanced navigation system functions
    void GenerateRealisticFlightPath(SynDataTy& synData);
    double ApplyDepartureNavigation(SynDataTy& synData, double bearing);
    double ApplyCruiseNavigation(SynDataTy& synData, double bearing);
    double ApplyArrivalNavigation(SynDataTy& synData, double bearing);
    double GetWaypointTolerance(SyntheticFlightState state, SyntheticTrafficType trafficType);
    double GetRealisticTurnRate(const SynDataTy& synData);
    
    /// Flight path generation functions
    void GenerateDeparturePath(SynDataTy& synData, const positionTy& currentPos);
    void GenerateCruisePath(SynDataTy& synData, const positionTy& currentPos);
    void GenerateArrivalPath(SynDataTy& synData, const positionTy& currentPos);
    void GenerateBasicPath(SynDataTy& synData, const positionTy& currentPos);
    void GenerateHoldingPattern(SynDataTy& synData, const positionTy& currentPos);
    
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
    
    /// Get required terrain clearance based on flight state and aircraft type
    double GetRequiredTerrainClearance(SyntheticFlightState state, SyntheticTrafficType trafficType);
    void SmoothHeadingChange(SynDataTy& synData, double targetHeading, double deltaTime);
    positionTy GetNextWaypoint(SynDataTy& synData);
    
    /// Process TTS communications
    void ProcessTTSCommunication(SynDataTy& synData, const std::string& message);
    
    /// Check if user is tuned to specific frequency
    bool IsUserTunedToFrequency(double frequency);
    
    /// Update user awareness behavior
    void UpdateUserAwareness(SynDataTy& synData, const positionTy& userPos);
    
    /// TCAS (Traffic Collision Avoidance System) functions
    void UpdateTCAS(const LTFlightData::FDKeyTy& key, SynDataTy& synData, double currentTime);
    bool CheckTrafficConflict(const SynDataTy& synData1, const SynDataTy& synData2);
    void GenerateTCASAdvisory(SynDataTy& synData, const positionTy& conflictPos);
    void ExecuteTCASManeuver(SynDataTy& synData, double currentTime);
    
    /// Enhanced TCAS functions
    positionTy PredictAircraftPosition(const SynDataTy& synData, double timeAhead);
    double CalculateClosestPointOfApproach(const SynDataTy& synData1, const SynDataTy& synData2);
    bool CheckPredictiveConflict(const SynDataTy& synData1, const SynDataTy& synData2, double lookAheadTime);
    void CoordinateTCASResponse(SynDataTy& synData1, SynDataTy& synData2);
    int DetermineOptimalTCASManeuver(const SynDataTy& ownAircraft, const SynDataTy& trafficAircraft);
    
    /// Enhanced ground operations
    void UpdateGroundOperations(SynDataTy& synData, double currentTime);
    void GenerateTaxiRoute(SynDataTy& synData, const positionTy& origin, const positionTy& destination);
    bool CheckGroundCollision(const SynDataTy& synData, const positionTy& nextPos);
    void UpdateTaxiMovement(SynDataTy& synData, double deltaTime);
    
    /// Helper functions for realistic communication degradation
    std::string ApplyLightStaticEffects(const std::string& message);
    std::string ApplyModerateStaticEffects(const std::string& message);
    std::string ApplyHeavyStaticEffects(const std::string& message);
    
    /// Helper functions for ICAO phraseology
    std::string FormatICAOAltitude(double altitudeMeters);
    std::string GetAircraftTypeForComms(const std::string& icaoType, SyntheticTrafficType trafficType);
    std::string FormatRunwayForComms(const std::string& runway);
    
    /// Generate varied position around a center point to prevent aircraft stacking
    positionTy GenerateVariedPosition(const positionTy& centerPos, double minDistanceNM, double maxDistanceNM);
    
    /// Generate comprehensive debug log for all synthetic aircraft
    void GenerateDebugLog();
    
    /// Calculate dynamic density based on X-Plane scenery complexity
    float CalculateSceneryBasedDensity(const positionTy& centerPos);
    
    /// Count objects and scenery complexity in area
    int CountSceneryObjects(const positionTy& centerPos, double radiusNM);
    
    /// Get effective traffic density (static or dynamic)
    float GetEffectiveDensity(const positionTy& centerPos);
    
    /// Generate realistic SID name based on runway and position
    std::string GenerateRealisticSIDName(const std::string& runway, const positionTy& pos);
    
    /// Generate realistic STAR name based on airport and position  
    std::string GenerateRealisticSTARName(const std::string& airport, const positionTy& pos);

protected:
};

#endif /* LTSynthetic_h */
