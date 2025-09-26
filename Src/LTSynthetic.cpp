/// @file       LTSynthetic.cpp
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

// All includes are collected in one header
#include "LiveTraffic.h"

//
// MARK: SyntheticConnection
//

#define SYNTHETIC_NAME                "Synthetic"               ///< Human-readable Name of connection

// Position information per tracked plane
SyntheticConnection::mapSynDataTy SyntheticConnection::mapSynData;

// Configuration for synthetic traffic
SyntheticTrafficConfig SyntheticConnection::config;

// Constructor
SyntheticConnection::SyntheticConnection () :
LTFlightDataChannel(DR_CHANNEL_SYNTHETIC, SYNTHETIC_NAME, CHT_SYNTHETIC_DATA)
{}


// virtual thread main function
void SyntheticConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_Synthetic", LC_ALL_MASK);

    while ( shallRun() ) {
        // LiveTraffic Top Level Exception Handling
        try {
            // basis for determining when to be called next
            tNextWakeup = std::chrono::steady_clock::now();
            
            // where are we right now?
            const positionTy pos (dataRefs.GetViewPos());
            
            // If the camera position is valid we can request data around it
            if (pos.isNormal()) {
                // Next wakeup is "refresh interval" from _now_
                tNextWakeup += std::chrono::seconds(dataRefs.GetFdRefreshIntvl());
                
                // fetch data and process it
                if (FetchAllData(pos) && ProcessFetchedData())
                    // reduce error count if processed successfully
                    // as a chance to appear OK in the long run
                    DecErrCnt();
            }
            else {
                // Camera position is yet invalid, retry in a second
                tNextWakeup += std::chrono::seconds(1);
            }
            
            // sleep for FD_REFRESH_INTVL or if woken up for termination
            // by condition variable trigger
            {
                std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
                FDThreadSynchCV.wait_until(lk, tNextWakeup,
                                           [this]{return !shallRun();});
            }
            
        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            IncErrCnt();
        } catch (...) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown type)");
            IncErrCnt();
        }
    }
}


// Scan for relevant flight data and generate new synthetic traffic
bool SyntheticConnection::FetchAllData(const positionTy& centerPos)
{
    // Update configuration from DataRefs
    config.enabled = dataRefs.bSyntheticTrafficEnabled != 0;
    config.trafficTypes = static_cast<unsigned>(dataRefs.synTrafficTypes);
    config.maxAircraft = dataRefs.synMaxAircraft;
    config.density = static_cast<float>(dataRefs.synTrafficDensity) / 100.0f;
    config.gaRatio = static_cast<float>(dataRefs.synGARatio) / 100.0f;
    config.airlineRatio = static_cast<float>(dataRefs.synAirlineRatio) / 100.0f;
    config.militaryRatio = static_cast<float>(dataRefs.synMilitaryRatio) / 100.0f;
    config.enableTTS = dataRefs.bSynTTSEnabled != 0;
    config.userAwareness = dataRefs.bSynUserAwareness != 0;
    config.weatherOperations = dataRefs.bSynWeatherOperations != 0;
    // Note: commRange removed - now using realistic communication degradation instead of hard cutoff
    
    if (!config.enabled) {
        // Log once every 60 seconds when synthetic traffic is disabled
        static double lastLogTime = 0;
        double currentTime = std::time(nullptr);
        if (currentTime - lastLogTime > 60.0) {
            LOG_MSG(logDEBUG, "Synthetic traffic disabled (enable via dataRef livetraffic/cfg/synthetic/enabled)");
            lastLogTime = currentTime;
        }
        return true;  // Synthetic traffic disabled
    }
    
    LOG_MSG(logDEBUG, "Synthetic traffic enabled: %d aircraft, types=%u, density=%.1f%%", 
            config.maxAircraft, config.trafficTypes, config.density * 100.0f);
    
    // Generate new synthetic traffic if we have room
    if (mapSynData.size() < static_cast<size_t>(config.maxAircraft)) {
        GenerateTraffic(centerPos);
    }
    
    // --- Enhanced Parked Aircraft Management ---
    // Loop over all flight data and manage existing parked aircraft
    // - 'Parked' aircraft are kept and enhanced with AI behavior
    // - Not 'Parked' aircraft are removed from synthetic management
    
    // Lock to access mapFD
    std::lock_guard<std::mutex> lock (mapFdMutex);
    // Loop over all known flight data
    for (const auto& p: mapFd) {
        const LTFlightData::FDKeyTy& key = p.first;
        const LTFlightData& fd = p.second;
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        if (fd.IsValid() && fd.hasAc()) {
            const LTAircraft& ac = *fd.GetAircraft();
            auto synIt = mapSynData.find(key);
            
            // Check if this is one of our synthetic aircraft
            if (synIt != mapSynData.end()) {
                // Update position and state for our synthetic aircraft
                SynDataTy& synData = synIt->second;
                synData.pos = ac.GetPPos();
                
                // Update flight state based on aircraft phase
                flightPhaseE acPhase = ac.GetFlightPhase();
                if (acPhase == FPH_PARKED && synData.state != SYN_STATE_PARKED) {
                    HandleStateTransition(synData, SYN_STATE_PARKED, std::time(nullptr));
                }
            } else if (ac.GetFlightPhase() == FPH_PARKED) {
                // This is a newly parked aircraft, add it to our management
                SynDataTy& parkDat = mapSynData[key];
                const double prevHead = parkDat.pos.heading();
                parkDat.pos = ac.GetPPos();
                parkDat.pos.heading() = prevHead;
                parkDat.stat = fd.GetUnsafeStat();
                parkDat.state = SYN_STATE_PARKED;
                parkDat.trafficType = SYN_TRAFFIC_GA; // Default to GA for existing aircraft
                parkDat.stateChangeTime = std::time(nullptr);
                parkDat.nextEventTime = parkDat.stateChangeTime + 60; // Next event in 1 minute
                parkDat.isUserAware = false;
            }
                
            // Test if the aircraft came too close to any other parked aircraft on the ground
            if (ac.IsOnGrnd() && !ac.IsGroundVehicle()) {
                for (auto i = mapSynData.begin(); i != mapSynData.end(); ) {
                    // Only compare to other aircraft (not myself)
                    if (i->first == key) {
                        ++i;
                    } else {
                        const double dist = i->second.pos.dist(ac.GetPPos());
                        if (dist < GND_COLLISION_DIST)
                        {
                            LOG_MSG(logDEBUG, "%s came too close to synthetic %s, removing the synthetic aircraft",
                                    fd.keyDbg().c_str(), i->first.c_str());
                            // find the synthetic aircraft in the map of active aircraft and have it removed there
                            try {
                                LTFlightData& fdSynthetic = mapFd.at(i->first);
                                fdSynthetic.SetInvalid();
                            }
                            catch(...) {}
                            // Remove the synthetic aircraft here from SyntheticConnection
                            i = mapSynData.erase(i);
                        } else
                            ++i;
                    }
                }
            }
        }
    }
    return true;
}


// Processes the available stored data and updates AI behavior
bool SyntheticConnection::ProcessFetchedData ()
{
    if (!config.enabled) {
        return true;  // Synthetic traffic disabled
    }
    
    // Timestamp with which we send the data
    const double tNow = (double)std::time(nullptr);
    // Camera pos
    const positionTy posCam = dataRefs.GetViewPos();
    // Squared search distance for distance comparison
    const double distSearchSqr = sqr(double(dataRefs.GetFdStdDistance_m()));
    
    // --- Enhanced Synthetic Aircraft Processing ---
    // For all stored aircraft
    // - Update AI behavior and state transitions
    // - Process TTS communications
    // - Handle weather-based operations
    // - Send dynamic data updates for LiveTraffic to process
    for (auto i = mapSynData.begin(); i != mapSynData.end();) {
        const LTFlightData::FDKeyTy& key = i->first;
        SynDataTy& synData = i->second;

        // Only process planes in search distance
        if (synData.pos.distRoughSqr(posCam) > distSearchSqr) {
            ++i;
            continue;
        }

        // Find the related flight data
        std::unique_lock<std::mutex> mapLock (mapFdMutex);
        LTFlightData& fd = mapFd[key];
        mapLock.unlock();
        
        // Update AI behavior
        UpdateAIBehavior(synData, tNow);
        
        // Update user awareness if enabled
        if (config.userAwareness) {
            UpdateUserAwareness(synData, posCam);
        }
        
        // Check weather impact
        if (config.weatherOperations) {
            CheckWeatherImpact(synData.pos, synData);
        }
        
        // Handle TTS communications
        if (config.enableTTS && synData.isUserAware && 
            (tNow - synData.lastCommTime) > 30.0) { // Every 30 seconds max
            std::string commMsg = GenerateCommMessage(synData, posCam);
            if (!commMsg.empty()) {
                ProcessTTSCommunication(synData, commMsg);
                synData.lastCommTime = tNow;
            }
        }
        
        // Haven't yet looked up startup position's heading for parked aircraft?
        if (synData.state == SYN_STATE_PARKED && std::isnan(synData.pos.heading())) {
            synData.pos.heading() = LTAptFindStartupLoc(synData.pos).heading();
            // Still have no heading? That means we don't have a startup position
            if (std::isnan(synData.pos.heading()))
            {
                i = mapSynData.erase(i);
                continue;
            }
        }
        
        // Calculate performance parameters
        CalculatePerformance(synData);
        
        // Send position for LiveTraffic's processing
        LTFlightData::FDDynamicData dyn;
        dyn.pChannel = this;
        dyn.spd = synData.targetSpeed;
        dyn.vsi = 0.0; // Will be calculated based on flight state
        dyn.gnd = (synData.state <= SYN_STATE_TAXI_OUT || synData.state >= SYN_STATE_TAXI_IN);
        dyn.heading = synData.pos.heading();
        
        // Set flight phase based on synthetic state
        switch (synData.state) {
            case SYN_STATE_PARKED:
                synData.pos.f.flightPhase = FPH_PARKED;
                dyn.vsi = 0.0;
                break;
            case SYN_STATE_TAXI_OUT:
            case SYN_STATE_TAXI_IN:
                synData.pos.f.flightPhase = FPH_TAXI;
                dyn.vsi = 0.0;
                break;
            case SYN_STATE_TAKEOFF:
                synData.pos.f.flightPhase = FPH_TAKE_OFF;
                dyn.vsi = 500.0; // 500 ft/min climb
                break;
            case SYN_STATE_CLIMB:
                synData.pos.f.flightPhase = FPH_CLIMB;
                dyn.vsi = 1500.0; // 1500 ft/min climb
                break;
            case SYN_STATE_CRUISE:
                synData.pos.f.flightPhase = FPH_CRUISE;
                dyn.vsi = 0.0;
                break;
            case SYN_STATE_HOLD:
                synData.pos.f.flightPhase = FPH_CRUISE;
                dyn.vsi = 0.0;
                break;
            case SYN_STATE_DESCENT:
                synData.pos.f.flightPhase = FPH_DESCEND;
                dyn.vsi = -1000.0; // 1000 ft/min descent
                break;
            case SYN_STATE_APPROACH:
                synData.pos.f.flightPhase = FPH_APPROACH;
                dyn.vsi = -500.0; // 500 ft/min descent
                break;
            case SYN_STATE_LANDING:
                synData.pos.f.flightPhase = FPH_LANDING;
                dyn.vsi = -200.0; // 200 ft/min descent
                break;
            default:
                synData.pos.f.flightPhase = FPH_UNKNOWN;
                dyn.vsi = 0.0;
                break;
        }
        
        synData.pos.f.specialPos = SPOS_STARTUP;
        synData.pos.f.bHeadFixed = true;

        // Update flight data
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        if (fd.key().empty()) {
            // Aircraft doesn't exist, create it
            fd.SetKey(key);
            fd.UpdateData(synData.stat, synData.pos.dist(dataRefs.GetViewPos()));
            // Remove buffering - use current time for synthetic aircraft
            dyn.ts = synData.pos.ts() = tNow;
            fd.AddDynData(dyn, 0, 0, &synData.pos);
            LOG_MSG(logDEBUG, "Created synthetic aircraft %s (%s)", key.c_str(), 
                    synData.trafficType == SYN_TRAFFIC_GA ? "GA" :
                    synData.trafficType == SYN_TRAFFIC_AIRLINE ? "Airline" : "Military");
        }

        // Add current data item - no buffering for synthetic traffic
        dyn.ts = synData.pos.ts() = tNow;
        fd.AddDynData(dyn, 0, 0, &synData.pos);
        
        // next aircraft
        ++i;
    }
    return true;
}

//
// MARK: Enhanced Synthetic Traffic Generation
//

// Generate new synthetic traffic
bool SyntheticConnection::GenerateTraffic(const positionTy& centerPos)
{
    if (mapSynData.size() >= static_cast<size_t>(config.maxAircraft)) {
        LOG_MSG(logDEBUG, "Synthetic traffic at maximum capacity: %zu/%d aircraft", 
                mapSynData.size(), config.maxAircraft);
        return false; // Already at maximum capacity
    }
    
    // Determine what type of traffic to generate based on configuration
    double rand = static_cast<double>(std::rand()) / RAND_MAX;
    
    LOG_MSG(logDEBUG, "Generating synthetic traffic (rand=%.3f, types=%u)", rand, config.trafficTypes);
    
    if ((config.trafficTypes & SYN_TRAFFIC_GA) && rand < config.gaRatio) {
        GenerateGATraffic(centerPos);
    } else if ((config.trafficTypes & SYN_TRAFFIC_AIRLINE) && rand < (config.gaRatio + config.airlineRatio)) {
        GenerateAirlineTraffic(centerPos);
    } else if ((config.trafficTypes & SYN_TRAFFIC_MILITARY) && rand < 1.0) {
        GenerateMilitaryTraffic(centerPos);
    } else {
        LOG_MSG(logDEBUG, "No synthetic traffic generated this cycle");
    }
    
    return true;
}

// Generate GA traffic patterns
void SyntheticConnection::GenerateGATraffic(const positionTy& centerPos)
{
    // Find nearby airports for GA operations
    auto airports = FindNearbyAirports(centerPos, 25.0); // 25nm radius
    
    if (airports.empty()) return;
    
    // Select random airport
    std::string airport = airports[std::rand() % airports.size()];
    
    // Generate unique numeric key for new aircraft (KEY_PRIVATE expects numeric values)
    unsigned long numericKey = (static_cast<unsigned long>(std::rand()) << 16) | (std::time(nullptr) & 0xFFFF);
    std::string key = std::to_string(numericKey);
    
    // Generate varied position around the user position instead of exactly at centerPos
    positionTy acPos = GenerateVariedPosition(centerPos, 2.0, 10.0); // 2-10nm from user
    acPos.alt_m() = 150.0 + (std::rand() % 1000); // 150-1150m AGL for GA
    
    CreateSyntheticAircraft(key, acPos, SYN_TRAFFIC_GA);
    
    LOG_MSG(logDEBUG, "Generated GA traffic: %s at %s (%.2f nm from user)", 
            key.c_str(), airport.c_str(), centerPos.dist(acPos) / 1852.0);
}

// Generate airline traffic
void SyntheticConnection::GenerateAirlineTraffic(const positionTy& centerPos)
{
    // Find nearby airports suitable for airline operations
    auto airports = FindNearbyAirports(centerPos, 50.0); // 50nm radius for airlines
    
    if (airports.empty()) return;
    
    // Generate unique numeric key for new aircraft (KEY_PRIVATE expects numeric values)
    unsigned long numericKey = (static_cast<unsigned long>(std::rand()) << 16) | (std::time(nullptr) & 0xFFFF);
    std::string key = std::to_string(numericKey);
    
    // Position for airline aircraft - spread them around at higher altitudes
    positionTy acPos = GenerateVariedPosition(centerPos, 10.0, 50.0); // 10-50nm from user
    acPos.alt_m() = 3000.0 + (std::rand() % 8000); // 3000-11000m for airlines
    
    CreateSyntheticAircraft(key, acPos, SYN_TRAFFIC_AIRLINE);
    
    LOG_MSG(logDEBUG, "Generated Airline traffic: %s (%.2f nm from user)", 
            key.c_str(), centerPos.dist(acPos) / 1852.0);
}

// Generate military traffic
void SyntheticConnection::GenerateMilitaryTraffic(const positionTy& centerPos)
{
    // Generate unique numeric key for military aircraft (KEY_PRIVATE expects numeric values)
    unsigned long numericKey = (static_cast<unsigned long>(std::rand()) << 16) | (std::time(nullptr) & 0xFFFF);
    std::string key = std::to_string(numericKey);
    
    // Military aircraft can operate from various locations and altitudes - spread them out more
    positionTy acPos = GenerateVariedPosition(centerPos, 20.0, 100.0); // 20-100nm from user
    acPos.alt_m() = 5000.0 + (std::rand() % 15000); // 5000-20000m for military
    
    CreateSyntheticAircraft(key, acPos, SYN_TRAFFIC_MILITARY);
    
    LOG_MSG(logDEBUG, "Generated Military traffic: %s (%.2f nm from user)", 
            key.c_str(), centerPos.dist(acPos) / 1852.0);
}

// Create synthetic aircraft with realistic parameters
bool SyntheticConnection::CreateSyntheticAircraft(const std::string& key, const positionTy& pos, 
                                                   SyntheticTrafficType trafficType)
{
    // Convert string key to FDKeyTy for synthetic aircraft
    LTFlightData::FDKeyTy fdKey(LTFlightData::KEY_PRIVATE, key, 10);  // base 10 for string keys
    
    SynDataTy& synData = mapSynData[fdKey];
    
    // Initialize position
    synData.pos = pos;
    synData.pos.heading() = static_cast<double>(std::rand() % 360); // Random heading
    
    // Set traffic type and initial state
    synData.trafficType = trafficType;
    synData.state = (pos.alt_m() < 100.0) ? SYN_STATE_PARKED : SYN_STATE_CRUISE;
    synData.stateChangeTime = std::time(nullptr);
    synData.nextEventTime = synData.stateChangeTime + (30 + (std::rand() % 120)); // 30-150 seconds
    
    // Generate static data
    synData.stat.call = GenerateCallSign(trafficType);
    synData.stat.flight = synData.stat.call; // Use call sign as flight number
    synData.stat.opIcao = "SYN"; // Synthetic operator
    synData.stat.op = "Synthetic Traffic";
    
    // Generate a realistic flight plan based on current position and traffic type
    positionTy destination = pos; // Default destination is current position
    
    // Create a realistic destination based on traffic type and current position
    switch (trafficType) {
        case SYN_TRAFFIC_GA:
            // GA flights typically within 200nm radius
            destination.lat() += (std::rand() % 200 - 100) / 100.0; // +/- 1 degree (~60nm)
            destination.lon() += (std::rand() % 200 - 100) / 100.0;
            destination.alt_m() = pos.alt_m() + (std::rand() % 1000); // Vary altitude
            break;
        case SYN_TRAFFIC_AIRLINE:
            // Airlines can travel much farther
            destination.lat() += (std::rand() % 1000 - 500) / 100.0; // +/- 5 degrees (~300nm)
            destination.lon() += (std::rand() % 1000 - 500) / 100.0;
            destination.alt_m() = 10000.0 + (std::rand() % 5000); // High altitude
            break;
        case SYN_TRAFFIC_MILITARY:
            // Military flights vary widely
            destination.lat() += (std::rand() % 2000 - 1000) / 100.0; // +/- 10 degrees (~600nm)
            destination.lon() += (std::rand() % 2000 - 1000) / 100.0;
            destination.alt_m() = 15000.0 + (std::rand() % 10000); // Very high altitude
            break;
    }
    
    // Generate flight plan using origin and destination
    synData.flightPlan = GenerateFlightPlan(pos, destination, trafficType);
    
    // Generate aircraft type using the flight plan information
    std::string acType = GenerateAircraftType(trafficType, synData.flightPlan);
    synData.stat.acTypeIcao = acType;
    synData.stat.mdl = acType;
    
    // Set performance parameters
    switch (trafficType) {
        case SYN_TRAFFIC_GA:
            synData.targetSpeed = 60.0; // 60 m/s (~120 kts) for GA
            synData.targetAltitude = pos.alt_m() + 500.0; // 500m above current
            break;
        case SYN_TRAFFIC_AIRLINE:
            synData.targetSpeed = 120.0; // 120 m/s (~240 kts) for airlines
            synData.targetAltitude = pos.alt_m() + 2000.0; // 2000m above current
            break;
        case SYN_TRAFFIC_MILITARY:
            synData.targetSpeed = 200.0; // 200 m/s (~400 kts) for military
            synData.targetAltitude = pos.alt_m() + 3000.0; // 3000m above current
            break;
        default:
            synData.targetSpeed = 80.0;
            synData.targetAltitude = pos.alt_m() + 1000.0;
            break;
    }
    
    // Initialize other parameters
    synData.holdingTime = 0.0;
    synData.isUserAware = false;
    synData.lastCommTime = 0.0;
    // Flight plan already generated above based on origin/destination
    
    return true;
}

// Update AI behavior for existing aircraft
void SyntheticConnection::UpdateAIBehavior(SynDataTy& synData, double currentTime)
{
    // Check if it's time for a state change
    if (currentTime >= synData.nextEventTime) {
        SyntheticFlightState newState = synData.state;
        
        // Simple state machine for AI behavior
        switch (synData.state) {
            case SYN_STATE_PARKED:
                if (std::rand() % 100 < 30) { // 30% chance to start up
                    newState = SYN_STATE_STARTUP;
                }
                break;
                
            case SYN_STATE_STARTUP:
                newState = SYN_STATE_TAXI_OUT;
                break;
                
            case SYN_STATE_TAXI_OUT:
                newState = SYN_STATE_TAKEOFF;
                break;
                
            case SYN_STATE_TAKEOFF:
                newState = SYN_STATE_CLIMB;
                break;
                
            case SYN_STATE_CLIMB:
                if (synData.pos.alt_m() >= synData.targetAltitude * 0.9) {
                    newState = SYN_STATE_CRUISE;
                }
                break;
                
            case SYN_STATE_CRUISE:
                // Randomly enter holding or start descent
                if (std::rand() % 100 < 20) { // 20% chance to hold
                    newState = SYN_STATE_HOLD;
                } else if (std::rand() % 100 < 10) { // 10% chance to descend
                    newState = SYN_STATE_DESCENT;
                }
                break;
                
            case SYN_STATE_HOLD:
                synData.holdingTime += currentTime - synData.stateChangeTime;
                if (synData.holdingTime > 300.0) { // Hold for max 5 minutes
                    newState = SYN_STATE_DESCENT;
                }
                break;
                
            case SYN_STATE_DESCENT:
                if (synData.pos.alt_m() <= 1000.0) { // Below 1000m
                    newState = SYN_STATE_APPROACH;
                }
                break;
                
            case SYN_STATE_APPROACH:
                newState = SYN_STATE_LANDING;
                break;
                
            case SYN_STATE_LANDING:
                newState = SYN_STATE_TAXI_IN;
                break;
                
            case SYN_STATE_TAXI_IN:
                newState = SYN_STATE_PARKED;
                break;
                
            case SYN_STATE_SHUTDOWN:
                // Aircraft lifecycle complete - could be removed
                break;
        }
        
        if (newState != synData.state) {
            HandleStateTransition(synData, newState, currentTime);
        }
    }
}

// Handle state transitions for AI aircraft
void SyntheticConnection::HandleStateTransition(SynDataTy& synData, SyntheticFlightState newState, double currentTime)
{
    LOG_MSG(logDEBUG, "Aircraft %s transitioning from state %d to %d", 
            synData.stat.call.c_str(), synData.state, newState);
    
    synData.state = newState;
    synData.stateChangeTime = currentTime;
    
    // Set next event time based on new state
    switch (newState) {
        case SYN_STATE_STARTUP:
            synData.nextEventTime = currentTime + (60 + std::rand() % 120); // 1-3 minutes
            break;
        case SYN_STATE_TAXI_OUT:
        case SYN_STATE_TAXI_IN:
            synData.nextEventTime = currentTime + (120 + std::rand() % 180); // 2-5 minutes
            break;
        case SYN_STATE_TAKEOFF:
            synData.nextEventTime = currentTime + (30 + std::rand() % 60); // 30-90 seconds
            break;
        case SYN_STATE_CLIMB:
            synData.nextEventTime = currentTime + (300 + std::rand() % 600); // 5-15 minutes
            break;
        case SYN_STATE_CRUISE:
            synData.nextEventTime = currentTime + (600 + std::rand() % 1800); // 10-40 minutes
            break;
        case SYN_STATE_HOLD:
            synData.nextEventTime = currentTime + (60 + std::rand() % 240); // 1-5 minutes
            break;
        case SYN_STATE_DESCENT:
            synData.nextEventTime = currentTime + (300 + std::rand() % 600); // 5-15 minutes
            break;
        case SYN_STATE_APPROACH:
        case SYN_STATE_LANDING:
            synData.nextEventTime = currentTime + (60 + std::rand() % 120); // 1-3 minutes
            break;
        default:
            synData.nextEventTime = currentTime + 300; // Default 5 minutes
            break;
    }
}

// Find nearby airports for traffic generation
std::vector<std::string> SyntheticConnection::FindNearbyAirports(const positionTy& centerPos, double radiusNM)
{
    std::vector<std::string> airports;
    
    // Use center position and radius to determine appropriate airports
    // Calculate distances and filter based on proximity and radius
    
    // Define a set of world airports with their approximate positions
    struct AirportData {
        std::string icao;
        double lat;
        double lon;
    };
    
    static const AirportData worldAirports[] = {
        {"KORD", 41.9786, -87.9048},  // Chicago O'Hare
        {"KLAX", 33.9425, -118.4081}, // Los Angeles  
        {"KJFK", 40.6398, -73.7789},  // JFK New York
        {"KBOS", 42.3643, -71.0052},  // Boston Logan
        {"KDEN", 39.8617, -104.6731}, // Denver
        {"KATL", 33.6367, -84.4281},  // Atlanta
        {"KDFW", 32.8968, -97.0380},  // Dallas/Fort Worth
        {"KIAH", 29.9844, -95.3414},  // Houston
        {"KPHX", 33.4343, -112.0116}, // Phoenix
        {"KSEA", 47.4502, -122.3088}, // Seattle
        {"KLAS", 36.0840, -115.1537}, // Las Vegas
        {"KMIA", 25.7959, -80.2870},  // Miami
        {"KSFO", 37.6213, -122.3790}, // San Francisco
        {"KBWI", 39.1754, -76.6683},  // Baltimore
        {"KDCA", 38.8521, -77.0377}   // Washington Reagan
    };
    
    const size_t numAirports = sizeof(worldAirports) / sizeof(worldAirports[0]);
    const double radiusM = radiusNM * 1852.0; // Convert nautical miles to meters
    
    // Find airports within the specified radius
    for (size_t i = 0; i < numAirports; i++) {
        const AirportData& airport = worldAirports[i];
        
        // Calculate distance from center position to airport
        positionTy airportPos;
        airportPos.lat() = airport.lat;
        airportPos.lon() = airport.lon;
        airportPos.alt_m() = 0.0; // Sea level for distance calculation
        
        double distanceM = centerPos.dist(airportPos);
        
        // Include airport if within radius
        if (distanceM <= radiusM) {
            airports.push_back(airport.icao);
        }
    }
    
    // If no airports found within radius, return closest few airports
    if (airports.empty()) {
        // Calculate distances and sort by proximity
        std::vector<std::pair<double, std::string>> airportDistances;
        
        for (size_t i = 0; i < numAirports; i++) {
            const AirportData& airport = worldAirports[i];
            positionTy airportPos;
            airportPos.lat() = airport.lat;
            airportPos.lon() = airport.lon;
            airportPos.alt_m() = 0.0;
            
            double distanceM = centerPos.dist(airportPos);
            airportDistances.push_back(std::make_pair(distanceM, airport.icao));
        }
        
        // Sort by distance
        std::sort(airportDistances.begin(), airportDistances.end());
        
        // Return up to 3 closest airports
        for (size_t i = 0; i < std::min(size_t(3), airportDistances.size()); i++) {
            airports.push_back(airportDistances[i].second);
        }
    }
    
    return airports;
}

// Generate realistic call sign based on traffic type
std::string SyntheticConnection::GenerateCallSign(SyntheticTrafficType trafficType)
{
    std::string callSign;
    
    switch (trafficType) {
        case SYN_TRAFFIC_GA: {
            // GA call signs like N12345, N987AB
            callSign = "N";
            callSign += std::to_string(1000 + (std::rand() % 9000)); // 1000-9999
            char letter1 = 'A' + (std::rand() % 26);
            char letter2 = 'A' + (std::rand() % 26);
            callSign += std::string(1, letter1) + std::string(1, letter2);
            break;
        }
        case SYN_TRAFFIC_AIRLINE: {
            // Airline call signs like UAL123, AAL456
            const char* airlines[] = {"UAL", "AAL", "DAL", "SWA", "JBU", "ASA"};
            callSign = airlines[std::rand() % 6];
            callSign += std::to_string(100 + (std::rand() % 900)); // 100-999
            break;
        }
        case SYN_TRAFFIC_MILITARY: {
            // Military call signs like ARMY123, NAVY456
            const char* military[] = {"ARMY", "NAVY", "USAF", "USCG"};
            callSign = military[std::rand() % 4];
            callSign += std::to_string(100 + (std::rand() % 900)); // 100-999
            break;
        }
        default:
            callSign = "SYN" + std::to_string(std::rand() % 1000);
            break;
    }
    
    return callSign;
}

// Generate aircraft type based on traffic type
std::string SyntheticConnection::GenerateAircraftType(SyntheticTrafficType trafficType, const std::string& route)
{
    std::string acType;
    
    switch (trafficType) {
        case SYN_TRAFFIC_GA: {
            const char* gaTypes[] = {"C172", "C152", "PA28", "C182", "SR22", "BE36"};
            acType = gaTypes[std::rand() % 6];
            
            // For GA, route length might influence aircraft choice
            if (!route.empty()) {
                if (route.find("long") != std::string::npos || route.find("IFR") != std::string::npos) {
                    // Long distance GA flights prefer more capable aircraft
                    const char* longDistanceGA[] = {"SR22", "BE36", "C182"};
                    acType = longDistanceGA[std::rand() % 3];
                } else if (route.find("local") != std::string::npos || route.find("VFR") != std::string::npos) {
                    // Local flights can use basic trainers
                    const char* localGA[] = {"C172", "C152", "PA28"};
                    acType = localGA[std::rand() % 3];
                }
            }
            break;
        }
        case SYN_TRAFFIC_AIRLINE: {
            const char* airlineTypes[] = {"B737", "A320", "B777", "A330", "B787", "A350"};
            acType = airlineTypes[std::rand() % 6];
            
            // Route characteristics influence aircraft selection
            if (!route.empty()) {
                if (route.find("domestic") != std::string::npos || route.find("short") != std::string::npos) {
                    // Short haul domestic - prefer narrow body
                    const char* shortHaul[] = {"B737", "A320"};
                    acType = shortHaul[std::rand() % 2];
                } else if (route.find("international") != std::string::npos || route.find("long") != std::string::npos || 
                          route.find("FL350+") != std::string::npos) {
                    // Long haul international - prefer wide body
                    const char* longHaul[] = {"B777", "A330", "B787", "A350"};
                    acType = longHaul[std::rand() % 4];
                }
            }
            break;
        }
        case SYN_TRAFFIC_MILITARY: {
            const char* militaryTypes[] = {"F16", "F18", "C130", "KC135", "E3", "B2"};
            acType = militaryTypes[std::rand() % 6];
            
            // Route type affects military aircraft selection
            if (!route.empty()) {
                if (route.find("transport") != std::string::npos || route.find("strategic") != std::string::npos) {
                    // Transport/strategic missions - prefer cargo/tanker aircraft
                    const char* transport[] = {"C130", "KC135", "E3"};
                    acType = transport[std::rand() % 3];
                } else if (route.find("local ops") != std::string::npos || route.find("patrol") != std::string::npos) {
                    // Local operations - prefer fighters
                    const char* fighters[] = {"F16", "F18"};
                    acType = fighters[std::rand() % 2];
                } else if (route.find("FL400+") != std::string::npos) {
                    // High altitude - prefer strategic bombers or surveillance
                    const char* highAlt[] = {"B2", "E3"};
                    acType = highAlt[std::rand() % 2];
                }
            }
            break;
        }
        default:
            acType = "C172";
            break;
    }
    
    return acType;
}

// Calculate performance parameters based on aircraft type
void SyntheticConnection::CalculatePerformance(SynDataTy& synData)
{
    // Adjust speed and altitude based on flight state and aircraft type
    double baseSpeed = 60.0; // Default GA speed
    
    switch (synData.trafficType) {
        case SYN_TRAFFIC_GA:
            baseSpeed = 60.0; // ~120 kts
            break;
        case SYN_TRAFFIC_AIRLINE:
            baseSpeed = 120.0; // ~240 kts
            break;
        case SYN_TRAFFIC_MILITARY:
            baseSpeed = 200.0; // ~400 kts
            break;
    }
    
    // Adjust speed based on flight state
    switch (synData.state) {
        case SYN_STATE_TAXI_OUT:
        case SYN_STATE_TAXI_IN:
            synData.targetSpeed = 5.0; // Taxi speed ~10 kts
            break;
        case SYN_STATE_TAKEOFF:
            synData.targetSpeed = baseSpeed * 0.6; // Take-off speed
            break;
        case SYN_STATE_CLIMB:
            synData.targetSpeed = baseSpeed * 0.8; // Climb speed
            break;
        case SYN_STATE_CRUISE:
            synData.targetSpeed = baseSpeed; // Cruise speed
            break;
        case SYN_STATE_DESCENT:
        case SYN_STATE_APPROACH:
            synData.targetSpeed = baseSpeed * 0.7; // Approach speed
            break;
        case SYN_STATE_LANDING:
            synData.targetSpeed = baseSpeed * 0.5; // Landing speed
            break;
        default:
            synData.targetSpeed = 0.0; // Stationary
            break;
    }
}

// Generate TTS communication message
std::string SyntheticConnection::GenerateCommMessage(const SynDataTy& synData, const positionTy& userPos)
{
    if (!config.enableTTS) return "";
    
    std::string message;
    double distance = synData.pos.dist(userPos) / 1852.0; // Convert to nautical miles
    
    // Calculate communication reliability based on distance (realistic degradation)
    double commReliability = 1.0;
    if (distance > 10.0) {
        // Communication starts degrading after 10 nautical miles
        // Reliability drops exponentially with distance following realistic radio propagation
        commReliability = std::exp(-0.1 * (distance - 10.0));
    }
    
    // Random factor for atmospheric conditions and interference
    double atmosphericFactor = 0.8 + (std::rand() / static_cast<double>(RAND_MAX)) * 0.4; // 0.8 to 1.2
    commReliability *= atmosphericFactor;
    
    // Randomly determine if message gets through based on reliability
    double randomThreshold = std::rand() / static_cast<double>(RAND_MAX);
    if (randomThreshold > commReliability) return ""; // Message blocked/lost
    
    // Generate message based on flight state
    switch (synData.state) {
        case SYN_STATE_TAXI_OUT:
            message = synData.stat.call + " requesting taxi clearance";
            break;
        case SYN_STATE_TAKEOFF:
            message = synData.stat.call + " ready for departure";
            break;
        case SYN_STATE_CRUISE:
            if (std::rand() % 100 < 10) { // 10% chance
                message = synData.stat.call + " level at " + std::to_string(static_cast<int>(synData.pos.alt_m() * 3.28084 / 100)) + " hundred";
            }
            break;
        case SYN_STATE_APPROACH:
            message = synData.stat.call + " requesting approach clearance";
            break;
        case SYN_STATE_LANDING:
            message = synData.stat.call + " on final approach";
            break;
        default:
            break;
    }
    
    // Apply signal degradation effects based on distance and reliability
    if (!message.empty() && commReliability < 0.7) {
        // At poor signal strength, add realistic communication degradation
        if (commReliability < 0.3) {
            // Very poor signal - heavy static and garbling
            message = ApplyHeavyStaticEffects(message);
        } else if (commReliability < 0.5) {
            // Poor signal - moderate static and some dropouts  
            message = ApplyModerateStaticEffects(message);
        } else {
            // Weak signal - light static and occasional dropouts
            message = ApplyLightStaticEffects(message);
        }
    }
    
    return message;
}

// Process TTS communications (placeholder for Windows TTS integration)
void SyntheticConnection::ProcessTTSCommunication(const SynDataTy& synData, const std::string& message)
{
    if (!config.enableTTS || message.empty()) return;
    
    // Log the communication for now (actual TTS implementation would go here)
    LOG_MSG(logDEBUG, "TTS: %s", message.c_str());
    
    // TODO: Integrate with Windows TTS API
    // This would involve:
    // 1. Initialize Windows SAPI (Speech API)
    // 2. Create voice objects with different characteristics for different aircraft types
    // 3. Queue the message for speech synthesis
    // 4. Handle radio effects and audio positioning based on distance
}

// Update user awareness behavior
void SyntheticConnection::UpdateUserAwareness(SynDataTy& synData, const positionTy& userPos)
{
    double distance = synData.pos.dist(userPos) / 1852.0; // Distance in nautical miles
    
    // Aircraft becomes user-aware within 10nm
    if (distance < 10.0 && !synData.isUserAware) {
        synData.isUserAware = true;
        LOG_MSG(logDEBUG, "Aircraft %s is now user-aware (distance: %.1fnm)", 
                synData.stat.call.c_str(), distance);
    } else if (distance > 15.0 && synData.isUserAware) {
        synData.isUserAware = false;
        LOG_MSG(logDEBUG, "Aircraft %s is no longer user-aware (distance: %.1fnm)", 
                synData.stat.call.c_str(), distance);
    }
    
    // Modify behavior if user-aware
    if (synData.isUserAware) {
        // Aircraft might change course slightly to avoid user
        // Or acknowledge user presence through radio calls
        // This is a simplified implementation
        if (distance < 2.0 && std::rand() % 100 < 5) { // 5% chance when very close
            // Generate traffic advisory message
            std::string advisory = synData.stat.call + " has traffic in sight";
            ProcessTTSCommunication(synData, advisory);
        }
    }
}

// Check weather impact on operations
bool SyntheticConnection::CheckWeatherImpact(const positionTy& pos, SynDataTy& synData)
{
    if (!config.weatherOperations) return false;
    
    // Use position to determine weather effects based on altitude and location
    double altitudeFt = pos.alt_m() * 3.28084; // Convert to feet
    double latitude = pos.lat();
    
    // Weather impact varies by altitude and geographic location
    double weatherFactor = 1.0;
    
    // High altitude operations (above FL300) - less weather impact
    if (altitudeFt > 30000.0) {
        weatherFactor = 0.3; // 30% chance of weather impact
    }
    // Medium altitude (FL100-FL300) - moderate weather impact  
    else if (altitudeFt > 10000.0) {
        weatherFactor = 0.6; // 60% chance of weather impact
    }
    // Low altitude (below FL100) - highest weather impact
    else {
        weatherFactor = 1.0; // 100% baseline chance
    }
    
    // Geographic factors - higher latitudes typically have more weather
    double latitudeFactor = 1.0;
    if (std::abs(latitude) > 60.0) {
        latitudeFactor = 1.5; // Polar regions - more weather
    } else if (std::abs(latitude) > 40.0) {
        latitudeFactor = 1.2; // Temperate regions - moderate increase
    } else if (std::abs(latitude) < 20.0) {
        latitudeFactor = 1.3; // Tropical regions - thunderstorms and convection
    }
    
    // Seasonal variation (simplified - would use actual date/time in real implementation)
    double seasonalFactor = 0.8 + (std::rand() / static_cast<double>(RAND_MAX)) * 0.4; // 0.8 to 1.2
    
    // Combined weather probability
    double finalWeatherChance = weatherFactor * latitudeFactor * seasonalFactor * 0.5; // Base 0.5% chance
    
    // Check if weather impact occurs
    if ((std::rand() % 1000) < static_cast<int>(finalWeatherChance * 10)) {
        double delay = 60.0 + (std::rand() % 300); // 1-6 minute delay
        
        // Weather type based on altitude and position
        std::string weatherType;
        if (altitudeFt < 5000.0) {
            weatherType = "fog/low visibility";
        } else if (altitudeFt < 15000.0) {
            weatherType = "turbulence/icing";
        } else {
            weatherType = "high altitude winds";
        }
        
        synData.nextEventTime += delay;
        
        // Generate position-based weather key for caching
        std::string weatherKey = std::to_string(static_cast<int>(pos.lat() * 10)) + "_" + 
                                std::to_string(static_cast<int>(pos.lon() * 10));
        weatherDelays[weatherKey] = std::time(nullptr) + delay;
        
        LOG_MSG(logDEBUG, "Weather delay applied to %s at %.1f,%.1f,%.0fft: %s, %.0f seconds", 
                synData.stat.call.c_str(), pos.lat(), pos.lon(), altitudeFt, 
                weatherType.c_str(), delay);
        return true;
    }
    
    return false;
}

// Generate flight plan for aircraft (simplified implementation)
std::string SyntheticConnection::GenerateFlightPlan(const positionTy& origin, const positionTy& destination, 
                                                     SyntheticTrafficType trafficType)
{
    std::string flightPlan;
    
    // Calculate distance between origin and destination
    double distanceNM = origin.dist(destination) / 1852.0; // Convert to nautical miles
    
    // Calculate approximate bearing from origin to destination
    double deltaLon = destination.lon() - origin.lon();
    double deltaLat = destination.lat() - origin.lat();
    double bearing = std::atan2(deltaLon, deltaLat) * 180.0 / PI;
    if (bearing < 0) bearing += 360.0;
    
    switch (trafficType) {
        case SYN_TRAFFIC_GA:
            if (distanceNM < 50.0) {
                // Short distance VFR flight
                flightPlan = "VFR direct, " + std::to_string(static_cast<int>(distanceNM)) + "nm";
            } else if (distanceNM < 200.0) {
                // Medium distance VFR with waypoints
                flightPlan = "VFR via waypoints, " + std::to_string(static_cast<int>(distanceNM)) + "nm, hdg " + std::to_string(static_cast<int>(bearing));
            } else {
                // Long distance - likely IFR for GA
                flightPlan = "IFR airways, " + std::to_string(static_cast<int>(distanceNM)) + "nm";
            }
            break;
            
        case SYN_TRAFFIC_AIRLINE:
            if (distanceNM < 100.0) {
                // Short haul domestic
                flightPlan = "IFR direct routing, " + std::to_string(static_cast<int>(distanceNM)) + "nm domestic";
            } else if (distanceNM < 500.0) {
                // Medium haul with standard airways
                flightPlan = "IFR via J-airways, " + std::to_string(static_cast<int>(distanceNM)) + "nm";
            } else {
                // Long haul with optimized routing
                flightPlan = "IFR optimized routing, " + std::to_string(static_cast<int>(distanceNM)) + "nm, FL350+";
            }
            break;
            
        case SYN_TRAFFIC_MILITARY:
            if (distanceNM < 200.0) {
                // Local military operations
                flightPlan = "Military local ops, " + std::to_string(static_cast<int>(distanceNM)) + "nm";
            } else {
                // Military transport or deployment
                flightPlan = "Military strategic routing, " + std::to_string(static_cast<int>(distanceNM)) + "nm, FL400+";
            }
            break;
            
        default:
            flightPlan = "Unknown routing, " + std::to_string(static_cast<int>(distanceNM)) + "nm";
            break;
    }
    
    return flightPlan;
}

// Find SID/STAR procedures using X-Plane navdata (placeholder)
std::vector<positionTy> SyntheticConnection::GetSIDSTAR(const std::string& airport, const std::string& runway, bool isSID)
{
    std::vector<positionTy> procedure;
    
    // Check cache first
    std::string cacheKey = airport + "_" + runway + (isSID ? "_SID" : "_STAR");
    auto cacheIt = sidStarCache.find(cacheKey);
    if (cacheIt != sidStarCache.end()) {
        return cacheIt->second;
    }
    
    // This would use X-Plane SDK navigation functions in a real implementation
    // For now, just return an empty procedure
    // TODO: Implement actual SID/STAR lookup using XPLMNavigation functions
    
    // Cache the result
    sidStarCache[cacheKey] = procedure;
    
    return procedure;
}

// Helper functions for communication degradation effects

/// Apply light static effects for weak signal communications
std::string SyntheticConnection::ApplyLightStaticEffects(const std::string& message) {
    std::string degraded = message;
    
    // Randomly drop some words (5% chance per word)
    std::string result;
    std::istringstream iss(degraded);
    std::string word;
    bool first = true;
    
    while (iss >> word) {
        if (std::rand() % 100 < 5) continue; // 5% chance to drop word
        
        if (!first) result += " ";
        result += word;
        first = false;
    }
    
    // Occasionally add "[static]" to indicate interference
    if (std::rand() % 100 < 15) {
        result += " [static]";
    }
    
    return result;
}

/// Apply moderate static effects for poor signal communications  
std::string SyntheticConnection::ApplyModerateStaticEffects(const std::string& message) {
    std::string degraded = message;
    
    // Drop more words (15% chance per word)
    std::string result;
    std::istringstream iss(degraded);
    std::string word;
    bool first = true;
    
    while (iss >> word) {
        if (std::rand() % 100 < 15) continue; // 15% chance to drop word
        
        if (!first) result += " ";
        
        // Occasionally garble words (10% chance)
        if (std::rand() % 100 < 10 && word.length() > 3) {
            word = word.substr(0, word.length() / 2) + "...";
        }
        
        result += word;
        first = false;
    }
    
    // Add static indicators more frequently
    if (std::rand() % 100 < 40) {
        result += " [static]";
    }
    
    return result;
}

/// Apply heavy static effects for very poor signal communications
std::string SyntheticConnection::ApplyHeavyStaticEffects(const std::string& message) {
    std::string degraded = message;
    
    // Drop many words (30% chance per word) 
    std::string result;
    std::istringstream iss(degraded);
    std::string word;
    bool first = true;
    
    while (iss >> word) {
        if (std::rand() % 100 < 30) continue; // 30% chance to drop word
        
        if (!first) result += " ";
        
        // Frequently garble words (25% chance)
        if (std::rand() % 100 < 25 && word.length() > 2) {
            word = word.substr(0, 1) + "..." + (word.length() > 3 ? word.substr(word.length()-1) : "");
        }
        
        result += word;
        first = false;
    }
    
    // Heavy static interference
    if (std::rand() % 100 < 70) {
        result = "[heavy static] " + result + " [breaking up]";
    }
    
    return result;
}

// Generate varied position around a center point to prevent aircraft stacking
positionTy SyntheticConnection::GenerateVariedPosition(const positionTy& centerPos, double minDistanceNM, double maxDistanceNM)
{
    const int maxAttempts = 10;
    const double minSeparationNM = 1.0; // Minimum 1nm separation between aircraft
    
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        // Generate random distance within range
        double distance = minDistanceNM + (static_cast<double>(std::rand()) / RAND_MAX) * (maxDistanceNM - minDistanceNM);
        distance *= 1852.0; // Convert nautical miles to meters
        
        // Generate random bearing
        double bearing = static_cast<double>(std::rand()) / RAND_MAX * 2.0 * PI;
        
        // Calculate new position using bearing and distance
        positionTy newPos = centerPos;
        
        // Simple flat earth approximation for positioning (good enough for local distances)
        double lat_offset = (distance * cos(bearing)) / 111320.0; // 111320 m per degree latitude
        double lon_offset = (distance * sin(bearing)) / (111320.0 * cos(centerPos.lat() * PI / 180.0));
        
        newPos.lat() += lat_offset;
        newPos.lon() += lon_offset;
        newPos.alt_m() = centerPos.alt_m(); // Keep same base altitude, will be modified by caller
        
        // Check if this position is far enough from existing synthetic aircraft
        bool positionOK = true;
        for (const auto& synAircraft : mapSynData) {
            double dist = synAircraft.second.pos.dist(newPos) / 1852.0; // Distance in nautical miles
            if (dist < minSeparationNM) {
                positionOK = false;
                break;
            }
        }
        
        if (positionOK) {
            return newPos;
        }
    }
    
    // If we couldn't find a suitable position after maxAttempts, just return the last generated one
    // This prevents infinite loops if the area is too crowded
    double distance = minDistanceNM + (static_cast<double>(std::rand()) / RAND_MAX) * (maxDistanceNM - minDistanceNM);
    distance *= 1852.0;
    double bearing = static_cast<double>(std::rand()) / RAND_MAX * 2.0 * PI;
    
    positionTy newPos = centerPos;
    double lat_offset = (distance * cos(bearing)) / 111320.0;
    double lon_offset = (distance * sin(bearing)) / (111320.0 * cos(centerPos.lat() * PI / 180.0));
    
    newPos.lat() += lat_offset;
    newPos.lon() += lon_offset;
    newPos.alt_m() = centerPos.alt_m();
    
    return newPos;
}
