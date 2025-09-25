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
    int commRange = 25;                     ///< Communications range in nautical miles
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
    
    /// Process TTS communications
    void ProcessTTSCommunication(const SynDataTy& synData, const std::string& message);
    
    /// Update user awareness behavior
    void UpdateUserAwareness(SynDataTy& synData, const positionTy& userPos);
};

#endif /* LTSynthetic_h */
