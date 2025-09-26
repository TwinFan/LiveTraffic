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

// Windows SAPI includes for Text-to-Speech
#if IBM
#include <sapi.h>
#include <sphelper.h>
#pragma comment(lib, "sapi.lib")
#endif

//
// MARK: Windows SAPI TTS Manager
//

#if IBM
/// Windows SAPI Text-to-Speech Manager
class TTSManager {
private:
    ISpVoice* pVoice;
    bool initialized;
    
    // Different voice settings for different aircraft types
    struct VoiceSettings {
        long rate;      // Speech rate (-10 to 10)
        long volume;    // Volume (0 to 100)  
        long pitch;     // Pitch offset (-10 to 10)
    };
    
    static const VoiceSettings gaVoice;      // General Aviation
    static const VoiceSettings airlineVoice; // Commercial airline
    static const VoiceSettings militaryVoice; // Military
    
public:
    TTSManager() : pVoice(nullptr), initialized(false) {}
    ~TTSManager() { Cleanup(); }
    
    bool Initialize() {
        if (initialized) return true;
        
        HRESULT hr = CoInitialize(nullptr);
        if (FAILED(hr)) {
            LOG_MSG(logERR, "TTS: Failed to initialize COM");
            return false;
        }
        
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&pVoice);
        if (FAILED(hr)) {
            LOG_MSG(logERR, "TTS: Failed to create SAPI voice instance");
            CoUninitialize();
            return false;
        }
        
        initialized = true;
        LOG_MSG(logDEBUG, "TTS: SAPI initialized successfully");
        return true;
    }
    
    void Cleanup() {
        if (pVoice) {
            pVoice->Release();
            pVoice = nullptr;
        }
        if (initialized) {
            CoUninitialize();
            initialized = false;
        }
    }
    
    void Speak(const std::string& text, SyntheticTrafficType trafficType, double distance) {
        if (!initialized || !pVoice || text.empty()) return;
        
        // Select voice settings based on aircraft type
        const VoiceSettings* settings = &gaVoice;
        switch (trafficType) {
            case SYN_TRAFFIC_AIRLINE:
                settings = &airlineVoice;
                break;
            case SYN_TRAFFIC_MILITARY:
                settings = &militaryVoice;
                break;
            default:
                settings = &gaVoice;
                break;
        }
        
        // Apply distance-based volume reduction (simulate radio range)
        long adjustedVolume = settings->volume;
        if (distance > 5.0) {
            // Reduce volume for distant aircraft (beyond 5 NM)
            double volumeReduction = std::min(0.8, (distance - 5.0) / 20.0);
            adjustedVolume = (long)(settings->volume * (1.0 - volumeReduction));
        }
        
        // Apply voice settings
        pVoice->SetRate(settings->rate);
        pVoice->SetVolume(adjustedVolume);
        
        // Add radio effect prefix for realism
        std::string radioText = text;
        if (distance > 10.0) {
            radioText = "[Static] " + radioText + " [Static]";
        } else if (distance > 5.0) {
            radioText = "[Weak Signal] " + radioText;
        }
        
        // Convert to wide string for SAPI
        std::wstring wideText(radioText.begin(), radioText.end());
        
        // Speak asynchronously to avoid blocking the main thread
        HRESULT hr = pVoice->Speak(wideText.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
        if (FAILED(hr)) {
            LOG_MSG(logWARN, "TTS: Failed to speak text: %s", text.c_str());
        }
    }
    
    static TTSManager& GetInstance() {
        static TTSManager instance;
        return instance;
    }
};

// Voice settings for different aircraft types
const TTSManager::VoiceSettings TTSManager::gaVoice = { -2, 70, 0 };        // Slower, softer for GA
const TTSManager::VoiceSettings TTSManager::airlineVoice = { 0, 85, -1 };   // Normal, professional tone
const TTSManager::VoiceSettings TTSManager::militaryVoice = { 1, 90, -2 };  // Faster, authoritative

#endif // IBM

//
// MARK: SyntheticConnection
//

#define SYNTHETIC_NAME                "Synthetic"               ///< Human-readable Name of connection

// Position information per tracked plane
SyntheticConnection::mapSynDataTy SyntheticConnection::mapSynData;

// Configuration for synthetic traffic
SyntheticTrafficConfig SyntheticConnection::config;

// Aircraft performance database
std::map<std::string, AircraftPerformance> SyntheticConnection::aircraftPerfDB;

// Constructor
SyntheticConnection::SyntheticConnection () :
LTFlightDataChannel(DR_CHANNEL_SYNTHETIC, SYNTHETIC_NAME, CHT_SYNTHETIC_DATA)
{
    // Initialize aircraft performance database on first construction
    InitializeAircraftPerformanceDB();
}


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
                parkDat.lastPosUpdateTime = std::time(nullptr);
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

        // Safety check: ensure synData has valid call sign
        if (synData.stat.call.empty()) {
            LOG_MSG(logWARN, "Removing synthetic aircraft with empty call sign");
            i = mapSynData.erase(i);
            continue;
        }

        // Only process planes in search distance
        if (synData.pos.distRoughSqr(posCam) > distSearchSqr) {
            ++i;
            continue;
        }

        // Safety check: ensure position is valid
        if (!synData.pos.isNormal()) {
            LOG_MSG(logWARN, "Removing synthetic aircraft %s with invalid position", synData.stat.call.c_str());
            i = mapSynData.erase(i);
            continue;
        }

        // Find the related flight data
        std::unique_lock<std::mutex> mapLock (mapFdMutex);
        LTFlightData& fd = mapFd[key];
        mapLock.unlock();
        
        // Update AI behavior with exception handling
        try {
            UpdateAIBehavior(synData, tNow);
        } catch (const std::exception& e) {
            LOG_MSG(logERR, "Exception in UpdateAIBehavior for %s: %s", synData.stat.call.c_str(), e.what());
            ++i;
            continue;
        } catch (...) {
            LOG_MSG(logERR, "Unknown exception in UpdateAIBehavior for %s", synData.stat.call.c_str());
            ++i;
            continue;
        }
        
        // Update user awareness if enabled
        if (config.userAwareness) {
            try {
                UpdateUserAwareness(synData, posCam);
            } catch (...) {
                LOG_MSG(logWARN, "Exception in UpdateUserAwareness for %s", synData.stat.call.c_str());
            }
        }
        
        // Update communication frequencies based on position and airport proximity
        try {
            UpdateCommunicationFrequencies(synData, posCam);
        } catch (...) {
            LOG_MSG(logWARN, "Exception in UpdateCommunicationFrequencies for %s", synData.stat.call.c_str());
        }
        
        // Handle enhanced ground operations
        if (synData.state == SYN_STATE_TAXI_OUT || synData.state == SYN_STATE_TAXI_IN) {
            try {
                UpdateGroundOperations(synData, tNow);
            } catch (...) {
                LOG_MSG(logWARN, "Exception in UpdateGroundOperations for %s", synData.stat.call.c_str());
            }
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
        
        // Update aircraft position based on movement
        UpdateAircraftPosition(synData, tNow);
        
        // Update TCAS (Traffic Collision Avoidance System)
        UpdateTCAS(key, synData, tNow);
        
        // Send position for LiveTraffic's processing
        LTFlightData::FDDynamicData dyn;
        dyn.pChannel = this;
        dyn.spd = synData.targetSpeed;
        dyn.vsi = 0.0; // Will be calculated based on flight state
        
        // Determine ground status based on flight state with terrain awareness
        bool isOnGround = false;
        switch (synData.state) {
            case SYN_STATE_PARKED:
            case SYN_STATE_STARTUP:
            case SYN_STATE_TAXI_OUT:
            case SYN_STATE_TAXI_IN:
            case SYN_STATE_SHUTDOWN:
                isOnGround = true;
                break;
            case SYN_STATE_TAKEOFF:
            case SYN_STATE_LINE_UP_WAIT:
            case SYN_STATE_LANDING:
                // For transition states, use terrain-based determination
                {
                    // Use per-aircraft probe instead of static probe to avoid race conditions
                    XPLMProbeRef probeToUse = synData.terrainProbe;
                    bool needsCleanup = false;
                    
                    // Create temporary probe if aircraft doesn't have one
                    if (!probeToUse) {
                        probeToUse = XPLMCreateProbe(xplm_ProbeY);
                        needsCleanup = true;
                    }
                    
                    if (probeToUse) {
                        try {
                            double terrainAlt = YProbe_at_m(synData.pos, probeToUse);
                            if (!std::isnan(terrainAlt)) {
                                // Use the same logic as TryDeriveGrndStatus: on ground if within FD_GND_AGL of terrain
                                isOnGround = (synData.pos.alt_m() < terrainAlt + FD_GND_AGL);
                            } else {
                                // Fallback: conservative approach for transition states
                                isOnGround = (synData.state == SYN_STATE_TAKEOFF) ? 
                                            (synData.pos.alt_m() < 100.0) : // Takeoff: assume ground until 100m MSL
                                            (synData.pos.alt_m() < 50.0);   // Landing: assume ground below 50m MSL
                            }
                        } catch (...) {
                            LOG_MSG(logWARN, "Exception during terrain probe for ground state determination");
                            // Fallback: conservative approach
                            isOnGround = (synData.state == SYN_STATE_TAKEOFF) ? 
                                        (synData.pos.alt_m() < 100.0) :
                                        (synData.pos.alt_m() < 50.0);
                        }
                        
                        // Clean up temporary probe
                        if (needsCleanup) {
                            try {
                                XPLMDestroyProbe(probeToUse);
                            } catch (...) {
                                // Ignore cleanup exceptions
                            }
                        }
                    } else {
                        LOG_MSG(logWARN, "Failed to create terrain probe for ground state determination");
                        // Fallback: conservative approach for transition states
                        isOnGround = (synData.state == SYN_STATE_TAKEOFF) ? 
                                    (synData.pos.alt_m() < 100.0) : // Takeoff: assume ground until 100m MSL
                                    (synData.pos.alt_m() < 50.0);   // Landing: assume ground below 50m MSL
                    }
                }
                break;
            default:
                // All other states (CLIMB, CRUISE, HOLD, DESCENT, APPROACH) are airborne
                isOnGround = false;
                break;
        }
        
        // Set both dynamic data and position ground flags consistently
        dyn.gnd = isOnGround;
        synData.pos.f.onGrnd = isOnGround ? GND_ON : GND_OFF;
        
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
            case SYN_STATE_LINE_UP_WAIT:
                synData.pos.f.flightPhase = FPH_TAXI; // On ground, waiting
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
    
    // Set terrain-safe altitude for GA aircraft
    XPLMProbeRef tempProbe = nullptr;
    double terrainElev = GetTerrainElevation(acPos, tempProbe);
    if (tempProbe) XPLMDestroyProbe(tempProbe);
    
    double baseAltitude = 150.0 + (std::rand() % 1000); // 150-1150m AGL for GA  
    double requiredClearance = GetRequiredTerrainClearance(SYN_STATE_CRUISE, SYN_TRAFFIC_GA);
    acPos.alt_m() = std::max(baseAltitude, terrainElev + requiredClearance);
    
    LOG_MSG(logDEBUG, "GA aircraft altitude: terrain=%.0fm, required=%.0fm, final=%.0fm", 
            terrainElev, terrainElev + requiredClearance, acPos.alt_m());
    
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
    
    // Set terrain-safe altitude for airline aircraft
    XPLMProbeRef tempProbe = nullptr;
    double terrainElev = GetTerrainElevation(acPos, tempProbe);
    if (tempProbe) XPLMDestroyProbe(tempProbe);
    
    double baseAltitude = 3000.0 + (std::rand() % 8000); // 3000-11000m for airlines
    double requiredClearance = GetRequiredTerrainClearance(SYN_STATE_CRUISE, SYN_TRAFFIC_AIRLINE);
    acPos.alt_m() = std::max(baseAltitude, terrainElev + requiredClearance);
    
    LOG_MSG(logDEBUG, "Airline aircraft altitude: terrain=%.0fm, required=%.0fm, final=%.0fm", 
            terrainElev, terrainElev + requiredClearance, acPos.alt_m());
    
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
    
    // Set terrain-safe altitude for military aircraft
    XPLMProbeRef tempProbe = nullptr;
    double terrainElev = GetTerrainElevation(acPos, tempProbe);
    if (tempProbe) XPLMDestroyProbe(tempProbe);
    
    double baseAltitude = 5000.0 + (std::rand() % 15000); // 5000-20000m for military
    double requiredClearance = GetRequiredTerrainClearance(SYN_STATE_CRUISE, SYN_TRAFFIC_MILITARY);
    acPos.alt_m() = std::max(baseAltitude, terrainElev + requiredClearance);
    
    LOG_MSG(logDEBUG, "Military aircraft altitude: terrain=%.0fm, required=%.0fm, final=%.0fm", 
            terrainElev, terrainElev + requiredClearance, acPos.alt_m());
    
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
    
    // Set traffic type and initial state based on altitude
    synData.trafficType = trafficType;
    bool initiallyOnGround = (pos.alt_m() < 100.0);
    synData.state = initiallyOnGround ? SYN_STATE_PARKED : SYN_STATE_CRUISE;
    synData.stateChangeTime = std::time(nullptr);
    synData.nextEventTime = synData.stateChangeTime + (30 + (std::rand() % 120)); // 30-150 seconds
    
    // Set initial ground status in position data
    synData.pos.f.onGrnd = initiallyOnGround ? GND_ON : GND_OFF;
    
    // Generate static data with country-specific registration
    synData.stat.call = GenerateCallSign(trafficType, pos);
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
    
    // Set initial performance parameters using aircraft-specific data
    const AircraftPerformance* perfData = GetAircraftPerformance(acType);
    if (perfData) {
        // Use aircraft-specific performance for initial setup
        synData.targetSpeed = perfData->cruiseSpeedKts * 0.514444; // Convert kts to m/s
        
        // Set realistic target altitude based on aircraft type
        double serviceCeilingM = perfData->serviceCeilingFt * 0.3048; // Convert ft to m
        double currentAltM = pos.alt_m();
        
        // Target altitude is a fraction of service ceiling, but not too low
        double minTargetAlt = currentAltM + 500.0; // At least 500m above current
        double maxTargetAlt = serviceCeilingM * 0.8; // 80% of service ceiling
        synData.targetAltitude = std::max(minTargetAlt, std::min(maxTargetAlt, currentAltM + serviceCeilingM * 0.3));
        
        LOG_MSG(logDEBUG, "Set initial performance for %s: speed=%0.1f kts, target alt=%0.0f ft", 
                acType.c_str(), synData.targetSpeed / 0.514444, synData.targetAltitude / 0.3048);
    } else {
        // Fallback to generic performance by traffic type
        switch (trafficType) {
            case SYN_TRAFFIC_GA:
                synData.targetSpeed = 120.0 * 0.514444; // 120 kts to m/s
                synData.targetAltitude = pos.alt_m() + 1500.0; // 1500m above current
                break;
            case SYN_TRAFFIC_AIRLINE:
                synData.targetSpeed = 460.0 * 0.514444; // 460 kts to m/s
                synData.targetAltitude = pos.alt_m() + 10000.0; // 10km above current (cruise altitude)
                break;
            case SYN_TRAFFIC_MILITARY:
                synData.targetSpeed = 500.0 * 0.514444; // 500 kts to m/s
                synData.targetAltitude = pos.alt_m() + 15000.0; // 15km above current
                break;
            default:
                synData.targetSpeed = 150.0 * 0.514444; // 150 kts to m/s
                synData.targetAltitude = pos.alt_m() + 3000.0; // 3km above current
                break;
        }
        LOG_MSG(logDEBUG, "Set generic performance for %s (traffic type %d)", acType.c_str(), trafficType);
    }
    
    // Initialize other parameters
    synData.holdingTime = 0.0;
    synData.isUserAware = false;
    synData.lastComm = "";
    synData.lastCommTime = 0.0;
    synData.lastPosUpdateTime = std::time(nullptr);
    // Flight plan already generated above based on origin/destination
    
    // Initialize runway assignment (fix for crash)
    synData.assignedRunway = ""; // Initialize to empty to prevent access violation
    
    // Initialize navigation and terrain awareness
    synData.flightPath.clear();
    synData.currentWaypoint = 0;
    synData.targetWaypoint = synData.pos;
    synData.lastTerrainCheck = 0.0;
    synData.terrainElevation = 0.0;
    synData.terrainProbe = nullptr; // Will be created when first needed
    synData.headingChangeRate = 2.0; // Default turn rate 2 deg/sec
    synData.targetHeading = synData.pos.heading();
    
    // Pre-populate terrain elevation to avoid initial probe issues
    try {
        XPLMProbeRef tempProbe = XPLMCreateProbe(xplm_ProbeY);
        if (tempProbe) {
            synData.terrainElevation = GetTerrainElevation(synData.pos, tempProbe);
            // Store the probe for future use
            synData.terrainProbe = tempProbe;
            LOG_MSG(logDEBUG, "Initialized terrain probe for aircraft %s at elevation %.0fm", 
                    synData.stat.call.c_str(), synData.terrainElevation);
        } else {
            LOG_MSG(logWARN, "Failed to create initial terrain probe for aircraft %s", synData.stat.call.c_str());
            synData.terrainElevation = 500.0; // Conservative estimate
        }
    } catch (...) {
        LOG_MSG(logERR, "Exception creating terrain probe for aircraft %s", synData.stat.call.c_str());
        synData.terrainElevation = 500.0; // Conservative estimate
    }
    
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
                // Assign a runway for departure if not already assigned
                if (synData.assignedRunway.empty()) {
                    // Generate a realistic runway designation
                    std::string runways[] = {"09L", "09R", "27L", "27R", "01", "19", "36", "18", "06", "24"};
                    synData.assignedRunway = runways[std::rand() % 10];
                }
                break;
                
            case SYN_STATE_TAXI_OUT:
                newState = SYN_STATE_LINE_UP_WAIT;
                break;
                
            case SYN_STATE_LINE_UP_WAIT:
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
        case SYN_STATE_LINE_UP_WAIT:
            synData.nextEventTime = currentTime + (30 + std::rand() % 90); // 30-120 seconds wait
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

// Generate realistic call sign based on traffic type and location (country-specific)
std::string SyntheticConnection::GenerateCallSign(SyntheticTrafficType trafficType, const positionTy& pos)
{
    std::string callSign;
    
    // Get country code from position for registration purposes
    std::string country = (std::abs(pos.lat()) > 0.001 || std::abs(pos.lon()) > 0.001) ? GetCountryFromPosition(pos) : "US";
    
    switch (trafficType) {
        case SYN_TRAFFIC_GA: {
            // Generate country-specific GA registration
            callSign = GenerateCountrySpecificRegistration(country, trafficType);
            break;
        }
        case SYN_TRAFFIC_AIRLINE: {
            // Airline call signs like UAL123, AAL456 - typically use airline codes regardless of country
            const char* airlines[] = {"UAL", "AAL", "DAL", "SWA", "JBU", "ASA", "BAW", "AFR", "DLH", "ACA", "QFA"};
            callSign = airlines[std::rand() % 11];
            callSign += std::to_string(100 + (std::rand() % 900)); // 100-999
            break;
        }
        case SYN_TRAFFIC_MILITARY: {
            // Military call signs vary by country
            if (country == "US") {
                const char* military[] = {"ARMY", "NAVY", "USAF", "USCG"};
                callSign = military[std::rand() % 4];
            } else if (country == "CA") {
                callSign = "RCAF";
            } else if (country == "GB") {
                callSign = "ROYAL";
            } else if (country == "DE") {
                callSign = "GAF";
            } else if (country == "FR") {
                callSign = "COTAM";
            } else {
                callSign = "MIL"; // Generic military
            }
            callSign += std::to_string(100 + (std::rand() % 900)); // 100-999
            break;
        }
        default:
            callSign = "SYN" + std::to_string(std::rand() % 1000);
            break;
    }
    
    return callSign;
}

// Get country code from position (lat/lon) for registration purposes  
std::string SyntheticConnection::GetCountryFromPosition(const positionTy& pos)
{
    double lat = pos.lat();
    double lon = pos.lon();
    
    // Simplified country detection based on geographic regions
    // In a real implementation, this would use a proper geographic database
    
    // North America
    if (lat >= 24.0 && lat <= 83.0 && lon >= -170.0 && lon <= -30.0) {
        if (lat >= 49.0 && lon >= -140.0) {
            return "CA"; // Canada (rough approximation)
        }
        if (lat >= 14.0 && lat <= 33.0 && lon >= -118.0 && lon <= -86.0) {
            return "MX"; // Mexico (rough approximation)
        }
        return "US"; // United States
    }
    
    // Europe
    if (lat >= 35.0 && lat <= 72.0 && lon >= -25.0 && lon <= 45.0) {
        if (lat >= 49.0 && lat <= 62.0 && lon >= -8.0 && lon <= 2.0) {
            return "GB"; // United Kingdom
        }
        if (lat >= 47.0 && lat <= 55.5 && lon >= 5.5 && lon <= 15.0) {
            return "DE"; // Germany
        }
        if (lat >= 42.0 && lat <= 51.5 && lon >= -5.0 && lon <= 9.5) {
            return "FR"; // France
        }
        return "EU"; // Generic Europe
    }
    
    // Australia
    if (lat >= -44.0 && lat <= -10.0 && lon >= 112.0 && lon <= 154.0) {
        return "AU";
    }
    
    // Asia (simplified)
    if (lat >= 1.0 && lat <= 50.0 && lon >= 73.0 && lon <= 150.0) {
        if (lat >= 30.0 && lat <= 46.0 && lon >= 123.0 && lon <= 132.0) {
            return "JA"; // Japan
        }
        return "AS"; // Generic Asia
    }
    
    // Default to US for unrecognized regions
    return "US";
}

// Generate country-specific aircraft registration
std::string SyntheticConnection::GenerateCountrySpecificRegistration(const std::string& countryCode, SyntheticTrafficType trafficType)
{
    std::string registration;
    
    if (countryCode == "US") {
        // US: N-numbers (N12345, N987AB)
        registration = "N";
        registration += std::to_string(1000 + (std::rand() % 9000)); // 1000-9999
        if (std::rand() % 2 == 0) { // 50% chance to add letters
            char letter1 = 'A' + (std::rand() % 26);
            char letter2 = 'A' + (std::rand() % 26);
            registration += std::string(1, letter1) + std::string(1, letter2);
        }
    } else if (countryCode == "CA") {
        // Canada: C-numbers (C-FABC, C-GDEF)
        registration = "C-";
        char letter1 = (std::rand() % 2 == 0) ? 'F' : 'G'; // Canadian prefix letters
        registration += letter1;
        for (int i = 0; i < 3; i++) {
            registration += static_cast<char>('A' + (std::rand() % 26));
        }
    } else if (countryCode == "GB") {
        // UK: G-numbers (G-ABCD)
        registration = "G-";
        for (int i = 0; i < 4; i++) {
            registration += static_cast<char>('A' + (std::rand() % 26));
        }
    } else if (countryCode == "DE") {
        // Germany: D-numbers (D-ABCD)
        registration = "D-";
        for (int i = 0; i < 4; i++) {
            registration += static_cast<char>('A' + (std::rand() % 26));
        }
    } else if (countryCode == "FR") {
        // France: F-numbers (F-GABC)
        registration = "F-G";
        for (int i = 0; i < 3; i++) {
            registration += static_cast<char>('A' + (std::rand() % 26));
        }
    } else if (countryCode == "AU") {
        // Australia: VH-numbers (VH-ABC)
        registration = "VH-";
        for (int i = 0; i < 3; i++) {
            registration += static_cast<char>('A' + (std::rand() % 26));
        }
    } else if (countryCode == "JA") {
        // Japan: JA-numbers (JA123A)
        registration = "JA";
        registration += std::to_string(100 + (std::rand() % 900));
        registration += static_cast<char>('A' + (std::rand() % 26));
    } else {
        // Default to US-style for unknown countries
        registration = "N";
        registration += std::to_string(1000 + (std::rand() % 9000));
        char letter1 = 'A' + (std::rand() % 26);
        char letter2 = 'A' + (std::rand() % 26);
        registration += std::string(1, letter1) + std::string(1, letter2);
    }
    
    return registration;
}

// Generate aircraft type based on traffic type
std::string SyntheticConnection::GenerateAircraftType(SyntheticTrafficType trafficType, const std::string& route)
{
    std::string acType;
    
    switch (trafficType) {
        case SYN_TRAFFIC_GA: {
            // Weighted selection based on real-world GA aircraft popularity
            // Weights reflect actual fleet numbers and training aircraft usage
            struct GASelection {
                const char* type;
                int weight;
            };
            
            GASelection gaTypes[] = {
                {"C172", 40},   // Most popular trainer and rental aircraft
                {"PA28", 20},   // Popular Cherokee/Warrior family
                {"C182", 15},   // High-performance single
                {"C152", 12},   // Popular older trainer
                {"SR22", 8},    // Modern high-performance (expensive, less common)
                {"BE36", 5}     // Bonanza (premium GA)
            };
            
            // Route-based aircraft selection refinement
            if (!route.empty()) {
                if (route.find("long") != std::string::npos || route.find("IFR") != std::string::npos) {
                    // Long distance GA flights prefer more capable aircraft
                    GASelection longDistanceGA[] = {
                        {"SR22", 40},
                        {"C182", 35}, 
                        {"BE36", 25}
                    };
                    int totalWeight = 0;
                    for (const auto& sel : longDistanceGA) totalWeight += sel.weight;
                    int randVal = std::rand() % totalWeight;
                    int cumWeight = 0;
                    for (const auto& sel : longDistanceGA) {
                        cumWeight += sel.weight;
                        if (randVal < cumWeight) {
                            acType = sel.type;
                            break;
                        }
                    }
                } else if (route.find("local") != std::string::npos || route.find("VFR") != std::string::npos) {
                    // Local flights heavily favor basic trainers
                    GASelection localGA[] = {
                        {"C172", 50},
                        {"PA28", 30},
                        {"C152", 20}
                    };
                    int totalWeight = 0;
                    for (const auto& sel : localGA) totalWeight += sel.weight;
                    int randVal = std::rand() % totalWeight;
                    int cumWeight = 0;
                    for (const auto& sel : localGA) {
                        cumWeight += sel.weight;
                        if (randVal < cumWeight) {
                            acType = sel.type;
                            break;
                        }
                    }
                } else {
                    // General selection with realistic weights
                    int totalWeight = 0;
                    for (const auto& sel : gaTypes) totalWeight += sel.weight;
                    int randVal = std::rand() % totalWeight;
                    int cumWeight = 0;
                    for (const auto& sel : gaTypes) {
                        cumWeight += sel.weight;
                        if (randVal < cumWeight) {
                            acType = sel.type;
                            break;
                        }
                    }
                }
            } else {
                // Default weighted selection
                int totalWeight = 0;
                for (const auto& sel : gaTypes) totalWeight += sel.weight;
                int randVal = std::rand() % totalWeight;
                int cumWeight = 0;
                for (const auto& sel : gaTypes) {
                    cumWeight += sel.weight;
                    if (randVal < cumWeight) {
                        acType = sel.type;
                        break;
                    }
                }
            }
            break;
        }
        case SYN_TRAFFIC_AIRLINE: {
            // Weighted selection based on real-world airline fleet sizes
            struct AirlineSelection {
                const char* type;
                int weight;
            };
            
            AirlineSelection airlineTypes[] = {
                {"B737", 35},   // Most popular narrow body worldwide
                {"A320", 35},   // Equally popular narrow body
                {"B777", 10},   // Popular wide body
                {"A330", 8},    // Wide body
                {"B787", 7},    // Modern wide body
                {"A350", 5}     // Newer wide body
            };
            
            // Route characteristics influence aircraft selection
            if (!route.empty()) {
                if (route.find("domestic") != std::string::npos || route.find("short") != std::string::npos) {
                    // Short haul domestic strongly favors narrow body
                    AirlineSelection shortHaul[] = {
                        {"B737", 50},
                        {"A320", 50}
                    };
                    acType = shortHaul[std::rand() % 2].type;
                } else if (route.find("international") != std::string::npos || route.find("long") != std::string::npos || 
                          route.find("FL350+") != std::string::npos) {
                    // Long haul international prefers wide body
                    AirlineSelection longHaul[] = {
                        {"B777", 30},
                        {"A330", 25},
                        {"B787", 25},
                        {"A350", 20}
                    };
                    int totalWeight = 0;
                    for (const auto& sel : longHaul) totalWeight += sel.weight;
                    int randVal = std::rand() % totalWeight;
                    int cumWeight = 0;
                    for (const auto& sel : longHaul) {
                        cumWeight += sel.weight;
                        if (randVal < cumWeight) {
                            acType = sel.type;
                            break;
                        }
                    }
                } else {
                    // Mixed selection with realistic weights
                    int totalWeight = 0;
                    for (const auto& sel : airlineTypes) totalWeight += sel.weight;
                    int randVal = std::rand() % totalWeight;
                    int cumWeight = 0;
                    for (const auto& sel : airlineTypes) {
                        cumWeight += sel.weight;
                        if (randVal < cumWeight) {
                            acType = sel.type;
                            break;
                        }
                    }
                }
            } else {
                // Default weighted selection
                int totalWeight = 0;
                for (const auto& sel : airlineTypes) totalWeight += sel.weight;
                int randVal = std::rand() % totalWeight;
                int cumWeight = 0;
                for (const auto& sel : airlineTypes) {
                    cumWeight += sel.weight;
                    if (randVal < cumWeight) {
                        acType = sel.type;
                        break;
                    }
                }
            }
            break;
        }
        case SYN_TRAFFIC_MILITARY: {
            // Military aircraft selection based on mission type
            struct MilitarySelection {
                const char* type;
                int weight;
            };
            
            // Route type affects military aircraft selection
            if (!route.empty()) {
                if (route.find("transport") != std::string::npos || route.find("strategic") != std::string::npos) {
                    // Transport/strategic missions - prefer cargo/tanker aircraft
                    MilitarySelection transport[] = {
                        {"C130", 60},   // Workhorse tactical transport
                        {"KC135", 25},  // Strategic tanker
                        {"E3", 15}      // AWACS surveillance
                    };
                    int totalWeight = 0;
                    for (const auto& sel : transport) totalWeight += sel.weight;
                    int randVal = std::rand() % totalWeight;
                    int cumWeight = 0;
                    for (const auto& sel : transport) {
                        cumWeight += sel.weight;
                        if (randVal < cumWeight) {
                            acType = sel.type;
                            break;
                        }
                    }
                } else if (route.find("local ops") != std::string::npos || route.find("patrol") != std::string::npos) {
                    // Local operations - prefer fighters
                    MilitarySelection fighters[] = {
                        {"F16", 60},    // Most common NATO fighter
                        {"F18", 40}     // US Navy/Marine fighter
                    };
                    acType = (std::rand() % 100 < 60) ? "F16" : "F18";
                } else if (route.find("FL400+") != std::string::npos) {
                    // High altitude - prefer strategic bombers or surveillance
                    MilitarySelection highAlt[] = {
                        {"E3", 70},     // AWACS can fly high
                        {"B2", 30}      // Strategic bomber
                    };
                    acType = (std::rand() % 100 < 70) ? "E3" : "B2";
                } else {
                    // General military mix
                    MilitarySelection militaryTypes[] = {
                        {"F16", 25},
                        {"F18", 20},
                        {"C130", 30},
                        {"KC135", 15},
                        {"E3", 8},
                        {"B2", 2}       // Very rare
                    };
                    int totalWeight = 0;
                    for (const auto& sel : militaryTypes) totalWeight += sel.weight;
                    int randVal = std::rand() % totalWeight;
                    int cumWeight = 0;
                    for (const auto& sel : militaryTypes) {
                        cumWeight += sel.weight;
                        if (randVal < cumWeight) {
                            acType = sel.type;
                            break;
                        }
                    }
                }
            } else {
                // Default military selection
                MilitarySelection militaryTypes[] = {
                    {"F16", 30},
                    {"F18", 25},
                    {"C130", 25},
                    {"KC135", 12},
                    {"E3", 6},
                    {"B2", 2}
                };
                int totalWeight = 0;
                for (const auto& sel : militaryTypes) totalWeight += sel.weight;
                int randVal = std::rand() % totalWeight;
                int cumWeight = 0;
                for (const auto& sel : militaryTypes) {
                    cumWeight += sel.weight;
                    if (randVal < cumWeight) {
                        acType = sel.type;
                        break;
                    }
                }
            }
            break;
        }
        default:
            acType = "C172"; // Safe default
            break;
    }
    
    return acType;
}

// Calculate performance parameters based on aircraft type
void SyntheticConnection::CalculatePerformance(SynDataTy& synData)
{
    // Get aircraft-specific performance data
    const AircraftPerformance* perfData = GetAircraftPerformance(synData.stat.acTypeIcao);
    
    // Default values if no specific performance data is found (fallback to traffic type)
    double cruiseSpeedKts = 120;
    double approachSpeedKts = 80;
    double taxiSpeedKts = 15;
    double stallSpeedKts = 60;
    
    if (perfData) {
        // Use aircraft-specific performance data
        cruiseSpeedKts = perfData->cruiseSpeedKts;
        approachSpeedKts = perfData->approachSpeedKts;
        taxiSpeedKts = perfData->taxiSpeedKts;
        stallSpeedKts = perfData->stallSpeedKts;
        
        LOG_MSG(logDEBUG, "Using performance data for %s: cruise=%0.0f kts, approach=%0.0f kts", 
                synData.stat.acTypeIcao.c_str(), cruiseSpeedKts, approachSpeedKts);
    } else {
        // Fallback to generic performance by traffic type
        switch (synData.trafficType) {
            case SYN_TRAFFIC_GA:
                cruiseSpeedKts = 120;
                approachSpeedKts = 70;
                taxiSpeedKts = 12;
                stallSpeedKts = 50;
                break;
            case SYN_TRAFFIC_AIRLINE:
                cruiseSpeedKts = 460;
                approachSpeedKts = 150;
                taxiSpeedKts = 25;
                stallSpeedKts = 130;
                break;
            case SYN_TRAFFIC_MILITARY:
                cruiseSpeedKts = 500;
                approachSpeedKts = 200;
                taxiSpeedKts = 40;
                stallSpeedKts = 180;
                break;
        }
        LOG_MSG(logDEBUG, "Using generic performance for %s (traffic type %d)", 
                synData.stat.acTypeIcao.c_str(), synData.trafficType);
    }
    
    // Convert knots to m/s for internal calculations (1 knot = 0.514444 m/s)
    const double KTS_TO_MS = 0.514444;
    
    // Calculate target speed based on flight state with aircraft-specific values
    switch (synData.state) {
        case SYN_STATE_PARKED:
        case SYN_STATE_STARTUP:
        case SYN_STATE_SHUTDOWN:
            synData.targetSpeed = 0.0; // Stationary
            break;
            
        case SYN_STATE_TAXI_OUT:
        case SYN_STATE_TAXI_IN:
            synData.targetSpeed = taxiSpeedKts * KTS_TO_MS; // Aircraft-specific taxi speed
            break;
            
        case SYN_STATE_LINE_UP_WAIT:
            // Stationary on runway, waiting for clearance
            synData.targetSpeed = 0.0;
            break;
            
        case SYN_STATE_TAKEOFF:
            // Takeoff speed is typically 1.2 * stall speed
            synData.targetSpeed = (stallSpeedKts * 1.2) * KTS_TO_MS;
            break;
            
        case SYN_STATE_CLIMB:
            // Climb speed is typically between takeoff and cruise speed
            synData.targetSpeed = (cruiseSpeedKts * 0.85) * KTS_TO_MS;
            break;
            
        case SYN_STATE_CRUISE:
            // Use aircraft's cruise speed
            synData.targetSpeed = cruiseSpeedKts * KTS_TO_MS;
            break;
            
        case SYN_STATE_HOLD:
            // Holding speed is typically slower than cruise
            synData.targetSpeed = (cruiseSpeedKts * 0.75) * KTS_TO_MS;
            break;
            
        case SYN_STATE_DESCENT:
            // Descent speed similar to cruise but may be reduced
            synData.targetSpeed = (cruiseSpeedKts * 0.9) * KTS_TO_MS;
            break;
            
        case SYN_STATE_APPROACH:
            // Use aircraft-specific approach speed
            synData.targetSpeed = approachSpeedKts * KTS_TO_MS;
            break;
            
        case SYN_STATE_LANDING:
            // Landing speed is typically approach speed minus 10-20 kts
            synData.targetSpeed = (approachSpeedKts * 0.85) * KTS_TO_MS;
            break;
            
        default:
            synData.targetSpeed = 0.0; // Stationary
            break;
    }
}

// Update aircraft position based on movement
void SyntheticConnection::UpdateAircraftPosition(SynDataTy& synData, double currentTime)
{
    // Calculate time delta since last position update
    double deltaTime = currentTime - synData.lastPosUpdateTime;
    
    // Don't update if delta is too small (less than 0.1 seconds)
    if (deltaTime < 0.1) {
        return;
    }
    
    // Save the old position for reference
    positionTy oldPos = synData.pos;
    
    // Only update position if aircraft should be moving
    bool shouldMove = false;
    double altitudeChangeRate = 0.0; // meters per second
    
    switch (synData.state) {
        case SYN_STATE_PARKED:
        case SYN_STATE_STARTUP:
        case SYN_STATE_SHUTDOWN:
            // Stationary states - no movement
            shouldMove = false;
            break;
            
        case SYN_STATE_TAXI_OUT:
        case SYN_STATE_TAXI_IN:
            // Ground movement - horizontal only, no altitude change
            shouldMove = (synData.targetSpeed > 0.0);
            altitudeChangeRate = 0.0;
            break;
            
        case SYN_STATE_LINE_UP_WAIT:
            // Stationary on runway, no movement
            shouldMove = false;
            altitudeChangeRate = 0.0;
            break;
            
        case SYN_STATE_TAKEOFF:
            // Taking off - horizontal movement plus altitude gain
            shouldMove = true;
            {
                const AircraftPerformance* perfData = GetAircraftPerformance(synData.stat.acTypeIcao);
                double climbRateFpm = perfData ? perfData->climbRateFpm * 0.5 : 500.0; // Half climb rate for takeoff
                altitudeChangeRate = climbRateFpm / 60.0 * 0.3048; // ft/min to m/s
            }
            break;
            
        case SYN_STATE_CLIMB:
            // Climbing - horizontal movement plus significant altitude gain
            shouldMove = true;
            {
                const AircraftPerformance* perfData = GetAircraftPerformance(synData.stat.acTypeIcao);
                double climbRateFpm = perfData ? perfData->climbRateFpm : 1500.0;
                altitudeChangeRate = climbRateFpm / 60.0 * 0.3048; // ft/min to m/s
            }
            break;
            
        case SYN_STATE_CRUISE:
        case SYN_STATE_HOLD:
            // Level flight - horizontal movement only
            shouldMove = true;
            altitudeChangeRate = 0.0;
            break;
            
        case SYN_STATE_DESCENT:
            // Descending - horizontal movement plus altitude loss
            shouldMove = true;
            {
                const AircraftPerformance* perfData = GetAircraftPerformance(synData.stat.acTypeIcao);
                double descentRateFpm = perfData ? perfData->descentRateFpm : 1000.0;
                altitudeChangeRate = -descentRateFpm / 60.0 * 0.3048; // ft/min to m/s (negative for descent)
            }
            break;
            
        case SYN_STATE_APPROACH:
            // On approach - horizontal movement plus moderate altitude loss
            shouldMove = true;
            {
                const AircraftPerformance* perfData = GetAircraftPerformance(synData.stat.acTypeIcao);
                double descentRateFpm = perfData ? perfData->descentRateFpm * 0.5 : 500.0; // Half descent rate for approach
                altitudeChangeRate = -descentRateFpm / 60.0 * 0.3048; // ft/min to m/s (negative for descent)
            }
            break;
            
        case SYN_STATE_LANDING:
            // Landing - horizontal movement plus gentle altitude loss
            shouldMove = true;
            altitudeChangeRate = -200.0 / 60.0 * 0.3048; // Gentle descent rate in m/s
            break;
    }
    
    if (shouldMove && synData.targetSpeed > 0.0) {
        // Calculate distance traveled in this time interval
        double distanceM = synData.targetSpeed * deltaTime; // speed is in m/s
        
        // Convert heading from degrees to radians
        double headingRad = synData.pos.heading() * PI / 180.0;
        
        // Calculate new latitude and longitude using flat earth approximation
        // (good enough for short distances)
        const double METERS_PER_DEGREE_LAT = 111320.0;
        const double METERS_PER_DEGREE_LON = 111320.0 * cos(synData.pos.lat() * PI / 180.0);
        
        // Calculate position changes
        double deltaLat = (distanceM * cos(headingRad)) / METERS_PER_DEGREE_LAT;
        double deltaLon = (distanceM * sin(headingRad)) / METERS_PER_DEGREE_LON;
        
        // Update position
        synData.pos.lat() += deltaLat;
        synData.pos.lon() += deltaLon;
        
        // Update altitude based on vertical speed
        if (altitudeChangeRate != 0.0) {
            double newAltitude = synData.pos.alt_m() + (altitudeChangeRate * deltaTime);
            
            // Apply aircraft performance constraints
            const AircraftPerformance* perfData = GetAircraftPerformance(synData.stat.acTypeIcao);
            if (perfData) {
                double maxAltM = perfData->maxAltFt * 0.3048; // Convert ft to m
                double serviceCeilingM = perfData->serviceCeilingFt * 0.3048;
                
                // Don't exceed aircraft's maximum altitude
                newAltitude = std::min(newAltitude, maxAltM);
                
                // Reduce climb rate significantly above service ceiling
                if (newAltitude > serviceCeilingM && altitudeChangeRate > 0.0) {
                    newAltitude = std::min(newAltitude, serviceCeilingM + 300.0); // Allow 300m above service ceiling
                }
            }
            
            // Apply altitude constraints based on flight state
            switch (synData.state) {
                case SYN_STATE_TAKEOFF:
                    // Don't climb too high during takeoff
                    newAltitude = std::min(newAltitude, oldPos.alt_m() + 300.0); // Max 300m gain
                    break;
                    
                case SYN_STATE_CLIMB:
                    // Don't exceed target altitude
                    if (newAltitude >= synData.targetAltitude) {
                        newAltitude = synData.targetAltitude;
                        // Consider transitioning to cruise if we've reached target altitude
                        if (std::abs(newAltitude - synData.targetAltitude) < 50.0) {
                            // Close enough to target altitude, could transition to cruise
                            // This will be handled by the AI behavior update in the next cycle
                        }
                    }
                    break;
                    
                case SYN_STATE_DESCENT:
                case SYN_STATE_APPROACH:
                case SYN_STATE_LANDING:
                    // Enhanced terrain avoidance for descent phases
                    {
                        // Get proper clearance requirements for this phase and aircraft type
                        double requiredClearance = GetRequiredTerrainClearance(synData.state, synData.trafficType);
                        double minSafeAltitude = synData.terrainElevation + requiredClearance;
                        
                        // Apply terrain avoidance with extra safety margin
                        newAltitude = std::max(newAltitude, minSafeAltitude);
                        
                        // Special handling for approach and landing near airports
                        if (synData.state == SYN_STATE_APPROACH || synData.state == SYN_STATE_LANDING) {
                            // Allow controlled descent to airports, but maintain minimum safety
                            double absoluteMinimum = synData.terrainElevation + 30.0;
                            newAltitude = std::max(newAltitude, absoluteMinimum);
                        }
                    }
                    break;
            }
            
            synData.pos.alt_m() = newAltitude;
        }
        
        // Update navigation and terrain awareness
        UpdateNavigation(synData, currentTime);
        UpdateTerrainAwareness(synData);
        
        // Log significant movements for debugging
        double movedDistance = synData.pos.dist(oldPos);
        if (movedDistance > 100.0) { // Log if moved more than 100 meters
            LOG_MSG(logDEBUG, "Aircraft %s moved %.0fm in %.1fs (speed=%.1f m/s, state=%d)", 
                    synData.stat.call.c_str(), movedDistance, deltaTime, synData.targetSpeed, synData.state);
        }
    }
    
    // Update the timestamp
    synData.lastPosUpdateTime = currentTime;
}

// Helper function to format altitude according to ICAO standards
std::string SyntheticConnection::FormatICAOAltitude(double altitudeMeters)
{
    // Convert meters to feet
    int altFeet = static_cast<int>(altitudeMeters * 3.28084);
    
    if (altFeet >= 18000) {
        // Above 18,000 feet, use flight levels
        int flightLevel = (altFeet + 50) / 100; // Round to nearest hundred
        return "flight level " + std::to_string(flightLevel);
    } else if (altFeet >= 1000) {
        // Between 1,000 and 18,000 feet, use thousands and hundreds
        int thousands = altFeet / 1000;
        int hundreds = (altFeet % 1000) / 100;
        if (hundreds == 0) {
            return std::to_string(thousands) + " thousand feet";
        } else {
            return std::to_string(thousands) + " thousand " + std::to_string(hundreds) + " hundred feet";
        }
    } else {
        // Below 1,000 feet, just use feet
        int roundedFeet = ((altFeet + 25) / 50) * 50; // Round to nearest 50 feet
        return std::to_string(roundedFeet) + " feet";
    }
}

// Helper function to get aircraft type for communications
std::string SyntheticConnection::GetAircraftTypeForComms(const std::string& icaoType, SyntheticTrafficType trafficType)
{
    // For initial contact, include aircraft type
    if (!icaoType.empty() && icaoType != "ZZZZ") {
        return icaoType;
    }
    
    // Fallback to generic type based on traffic category
    switch (trafficType) {
        case SYN_TRAFFIC_GA:
            return "light aircraft";
        case SYN_TRAFFIC_AIRLINE:
            return "heavy";
        case SYN_TRAFFIC_MILITARY:
            return "military aircraft";
        default:
            return "aircraft";
    }
}

// Helper function to format runway for communications
std::string SyntheticConnection::FormatRunwayForComms(const std::string& runway)
{
    if (runway.empty()) return "";
    
    std::string formatted = "runway ";
    
    // Handle runway numbers (e.g., "09L" becomes "zero niner left")
    for (char c : runway) {
        if (c >= '0' && c <= '9') {
            if (c == '0') formatted += "zero ";
            else if (c == '9') formatted += "niner ";
            else formatted += std::string(1, c) + " ";
        } else if (c == 'L') {
            formatted += "left";
        } else if (c == 'R') {
            formatted += "right";
        } else if (c == 'C') {
            formatted += "center";
        }
    }
    
    return formatted;
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
    
    // Generate message based on flight state using proper ICAO phraseology
    std::string aircraftType = GetAircraftTypeForComms(synData.stat.acTypeIcao, synData.trafficType);
    std::string runway = FormatRunwayForComms(synData.assignedRunway);
    
    switch (synData.state) {
        case SYN_STATE_STARTUP:
            if (std::rand() % 100 < 5) { // 5% chance for startup message
                message = synData.stat.call + " ground, " + aircraftType + " at gate, request start up";
            }
            break;
            
        case SYN_STATE_TAXI_OUT:
            // Proper taxi request with aircraft type and destination
            message = synData.stat.call + " ground, " + aircraftType + " at gate, request taxi to " + 
                     (runway.empty() ? "active runway" : runway) + " for departure";
            break;
            
        case SYN_STATE_LINE_UP_WAIT:
            // Line up and wait communications
            if (!runway.empty()) {
                int variation = std::rand() % 2;
                if (variation == 0) {
                    message = synData.stat.call + " tower, " + aircraftType + " lined up and waiting " + runway;
                } else {
                    message = synData.stat.call + " tower, in position and holding " + runway;
                }
            } else {
                message = synData.stat.call + " tower, " + aircraftType + " lined up and waiting";
            }
            break;
            
        case SYN_STATE_TAKEOFF:
            // Proper departure request with runway and aircraft type - add variations
            if (!runway.empty()) {
                int variation = std::rand() % 3;
                switch (variation) {
                    case 0:
                        message = synData.stat.call + " tower, " + aircraftType + " holding short " + 
                                 runway + ", ready for departure";
                        break;
                    case 1:
                        message = synData.stat.call + " tower, " + aircraftType + " ready for takeoff " + runway;
                        break;
                    case 2:
                        message = synData.stat.call + " tower, ready for immediate departure " + runway;
                        break;
                }
            } else {
                message = synData.stat.call + " tower, " + aircraftType + " ready for departure";
            }
            break;
            
        case SYN_STATE_CLIMB:
            if (std::rand() % 100 < 8) { // 8% chance for climb report
                std::string altitude = FormatICAOAltitude(synData.pos.alt_m());
                message = synData.stat.call + " departure, passing " + altitude + " for " + 
                         FormatICAOAltitude(synData.targetAltitude);
            }
            break;
            
        case SYN_STATE_CRUISE:
            if (std::rand() % 100 < 10) { // 10% chance for level report
                std::string altitude = FormatICAOAltitude(synData.pos.alt_m());
                message = synData.stat.call + " center, level " + altitude;
            }
            break;
            
        case SYN_STATE_HOLD:
            if (std::rand() % 100 < 20) { // 20% chance for hold report
                std::string altitude = FormatICAOAltitude(synData.pos.alt_m());
                message = synData.stat.call + " center, entering hold at " + altitude + ", expect further clearance";
            }
            break;
            
        case SYN_STATE_DESCENT:
            if (std::rand() % 100 < 12) { // 12% chance for descent report
                std::string currentAlt = FormatICAOAltitude(synData.pos.alt_m());
                std::string targetAlt = FormatICAOAltitude(synData.targetAltitude);
                message = synData.stat.call + " center, leaving " + currentAlt + " for " + targetAlt;
            }
            break;
            
        case SYN_STATE_APPROACH:
        {
            // Proper approach request with aircraft type and intentions - add variations
            int variation = std::rand() % 3;
            switch (variation) {
                case 0:
                    message = synData.stat.call + " approach, " + aircraftType + " requesting vectors to " + 
                             (runway.empty() ? "ILS approach" : "ILS " + runway);
                    break;
                case 1:
                    message = synData.stat.call + " approach, " + aircraftType + " requesting " + 
                             (runway.empty() ? "approach clearance" : runway + " approach");
                    break;
                case 2:
                    message = synData.stat.call + " approach, with information alpha, requesting vectors " +
                             (runway.empty() ? "for approach" : "ILS " + runway);
                    break;
            }
            break;
        }
            
        case SYN_STATE_LANDING:
            // Proper final approach call
            if (!runway.empty()) {
                message = synData.stat.call + " tower, " + aircraftType + " established ILS " + runway;
            } else {
                message = synData.stat.call + " tower, established on final approach";
            }
            break;
            
        case SYN_STATE_TAXI_IN:
            if (std::rand() % 100 < 15) { // 15% chance for taxi-in message
                message = synData.stat.call + " ground, " + aircraftType + " clear of " + 
                         (runway.empty() ? "runway" : runway) + ", taxi to gate";
            }
            break;
            
        case SYN_STATE_SHUTDOWN:
            if (std::rand() % 100 < 3) { // 3% chance for shutdown message
                message = synData.stat.call + " ground, " + aircraftType + " parking complete, shutting down";
            }
            break;
            
        default:
            break;
    }
    
    // Add TCAS advisory communications if active
    if (synData.inTCASAvoidance && !synData.tcasAdvisory.empty() && std::rand() % 100 < 30) {
        // 30% chance to report TCAS advisory
        message = synData.stat.call + " " + synData.tcasAdvisory.substr(0, synData.tcasAdvisory.find(" - ")) + 
                 ", responding to traffic advisory";
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

// Process TTS communications with Windows SAPI integration
void SyntheticConnection::ProcessTTSCommunication(SynDataTy& synData, const std::string& message)
{
    if (!config.enableTTS || message.empty()) return;
    
    // Store the last communication message
    synData.lastComm = message;
    
    LOG_MSG(logDEBUG, "TTS: %s", message.c_str());
    
#if IBM
    // Windows SAPI TTS integration
    TTSManager& tts = TTSManager::GetInstance();
    
    // Initialize TTS if not already done
    if (!tts.Initialize()) {
        LOG_MSG(logWARN, "TTS: Failed to initialize SAPI, falling back to logging only");
        return;
    }
    
    // Calculate distance to user for audio effects
    const positionTy userPos = dataRefs.GetViewPos();
    double distance = synData.pos.dist(userPos) / 1852.0; // Convert to nautical miles
    
    // Use SAPI to speak the message with appropriate voice characteristics
    tts.Speak(message, synData.trafficType, distance);
    
#else
    // Non-Windows platforms: log only (could implement other TTS engines here)
    LOG_MSG(logINFO, "TTS not implemented on this platform: %s", message.c_str());
#endif
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

// Find SID/STAR procedures using X-Plane navdata with actual XPLMNavigation functions
std::vector<positionTy> SyntheticConnection::GetSIDSTAR(const std::string& airport, const std::string& runway, bool isSID)
{
    std::vector<positionTy> procedure;
    
    // Check cache first
    std::string cacheKey = airport + "_" + runway + (isSID ? "_SID" : "_STAR");
    auto cacheIt = sidStarCache.find(cacheKey);
    if (cacheIt != sidStarCache.end()) {
        return cacheIt->second;
    }
    
    LOG_MSG(logDEBUG, "Looking up %s for airport %s runway %s using XPLMNavigation", 
            isSID ? "SID" : "STAR", airport.c_str(), runway.c_str());
    
    // Find the airport using XPLMNavigation
    XPLMNavRef airportRef = XPLMFindNavAid(nullptr, airport.c_str(), nullptr, nullptr, nullptr, xplm_Nav_Airport);
    
    if (airportRef == XPLM_NAV_NOT_FOUND) {
        LOG_MSG(logWARN, "Airport %s not found in navigation database", airport.c_str());
        sidStarCache[cacheKey] = procedure; // Cache empty result
        return procedure;
    }
    
    // Get airport information
    float airportLat, airportLon, airportElevation;
    int frequency;
    float heading;
    char airportID[32];
    char airportName[256];
    char reg;
    
    XPLMGetNavAidInfo(airportRef, nullptr, &airportLat, &airportLon, &airportElevation, 
                      &frequency, &heading, airportID, airportName, &reg);
    
    positionTy airportPos;
    airportPos.lat() = airportLat;
    airportPos.lon() = airportLon;  
    airportPos.alt_m() = airportElevation * 0.3048; // Convert feet to meters
    
    LOG_MSG(logDEBUG, "Found airport %s at %0.4f,%0.4f elevation %0.1f ft", 
            airportID, airportLat, airportLon, airportElevation);
    
    if (isSID) {
        // Generate SID (Standard Instrument Departure) using real navaid data
        procedure = GenerateSIDFromNavData(airportPos, airport, runway);
    } else {
        // Generate STAR (Standard Terminal Arrival Route) using real navaid data  
        procedure = GenerateSTARFromNavData(airportPos, airport, runway);
    }
    
    // Cache the result
    sidStarCache[cacheKey] = procedure;
    
    LOG_MSG(logDEBUG, "Generated %s for %s runway %s with %d waypoints", 
            isSID ? "SID" : "STAR", airport.c_str(), runway.c_str(), (int)procedure.size());
    
    return procedure;
}

// Generate SID procedures using actual navigation database
std::vector<positionTy> SyntheticConnection::GenerateSIDFromNavData(const positionTy& airportPos, 
                                                                    const std::string& airport, 
                                                                    const std::string& runway)
{
    std::vector<positionTy> sidProcedure;
    
    // Find nearby navigation aids for SID construction
    float searchLat = static_cast<float>(airportPos.lat());
    float searchLon = static_cast<float>(airportPos.lon());
    
    // Look for VORs, NDBs, and fixes within 50nm for SID waypoints
    const double searchRadiusNM = 50.0;
    const double searchRadiusM = searchRadiusNM * 1852.0;
    
    std::vector<XPLMNavRef> nearbyNavaids;
    
    // Search for different types of navigation aids
    XPLMNavType searchTypes[] = {xplm_Nav_VOR, xplm_Nav_NDB, xplm_Nav_Fix};
    
    for (XPLMNavType navType : searchTypes) {
        // Find navaids of this type near the airport
        XPLMNavRef navRef = XPLMFindNavAid(nullptr, nullptr, &searchLat, &searchLon, nullptr, navType);
        
        while (navRef != XPLM_NAV_NOT_FOUND) {
            float navLat, navLon, navElevation;
            char navID[32];
            
            XPLMGetNavAidInfo(navRef, nullptr, &navLat, &navLon, &navElevation, 
                              nullptr, nullptr, navID, nullptr, nullptr);
            
            // Calculate distance from airport
            positionTy navPos;
            navPos.lat() = navLat;
            navPos.lon() = navLon;
            navPos.alt_m() = navElevation * 0.3048;
            
            double distance = airportPos.dist(navPos);
            
            if (distance <= searchRadiusM && distance > 1000.0) { // Not too close, not too far
                nearbyNavaids.push_back(navRef);
                
                // Limit the number of navaids we consider
                if (nearbyNavaids.size() >= 10) break;
            }
            
            // This is a simplified search - in reality, we'd need to iterate through all navaids
            // For now, just take the first suitable one we find
            break;
        }
    }
    
    // Build SID procedure from suitable navaids
    if (!nearbyNavaids.empty()) {
        // Sort navaids by distance and bearing for logical SID construction
        std::sort(nearbyNavaids.begin(), nearbyNavaids.end(), 
                  [&airportPos](XPLMNavRef a, XPLMNavRef b) {
                      float aLat, aLon, bLat, bLon;
                      XPLMGetNavAidInfo(a, nullptr, &aLat, &aLon, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
                      XPLMGetNavAidInfo(b, nullptr, &bLat, &bLon, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
                      
                      positionTy aPos(aLat, aLon, 0);
                      positionTy bPos(bLat, bLon, 0);
                      
                      return airportPos.dist(aPos) < airportPos.dist(bPos);
                  });
        
        // Create SID waypoints using the nearest suitable navaids
        for (size_t i = 0; i < std::min(nearbyNavaids.size(), (size_t)5); i++) {
            float navLat, navLon, navElevation;
            char navID[32];
            
            XPLMGetNavAidInfo(nearbyNavaids[i], nullptr, &navLat, &navLon, &navElevation, 
                              nullptr, nullptr, navID, nullptr, nullptr);
            
            positionTy waypoint;
            waypoint.lat() = navLat;
            waypoint.lon() = navLon;
            waypoint.alt_m() = airportPos.alt_m() + (i + 1) * 500.0; // Climbing departure
            
            sidProcedure.push_back(waypoint);
            
            LOG_MSG(logDEBUG, "SID waypoint %zu: %s at %0.4f,%0.4f", 
                    i + 1, navID, navLat, navLon);
        }
    }
    
    // If no suitable navaids found, generate a basic geometric SID
    if (sidProcedure.empty()) {
        LOG_MSG(logDEBUG, "No suitable navaids found for SID, generating basic geometric procedure");
        
        // Create a basic straight-out departure
        for (int i = 1; i <= 3; i++) {
            positionTy waypoint;
            double distance = i * 5000.0; // 5km intervals
            double bearing = 360.0; // Due north default
            
            // Try to use runway heading if available (simplified)
            if (!runway.empty() && runway.length() >= 2) {
                try {
                    int runwayNum = std::stoi(runway.substr(0, 2));
                    bearing = runwayNum * 10.0; // Convert runway number to heading
                } catch (...) {
                    // Keep default bearing
                }
            }
            
            double lat_offset = (distance * cos(bearing * PI / 180.0)) / 111320.0;
            double lon_offset = (distance * sin(bearing * PI / 180.0)) / (111320.0 * cos(airportPos.lat() * PI / 180.0));
            
            waypoint.lat() = airportPos.lat() + lat_offset;
            waypoint.lon() = airportPos.lon() + lon_offset;
            waypoint.alt_m() = airportPos.alt_m() + i * 500.0;
            
            sidProcedure.push_back(waypoint);
        }
    }
    
    return sidProcedure;
}

// Generate STAR procedures using actual navigation database
std::vector<positionTy> SyntheticConnection::GenerateSTARFromNavData(const positionTy& airportPos, 
                                                                      const std::string& airport, 
                                                                      const std::string& runway)
{
    std::vector<positionTy> starProcedure;
    
    // Similar to SID generation, but create an arrival procedure
    float searchLat = static_cast<float>(airportPos.lat());
    float searchLon = static_cast<float>(airportPos.lon());
    
    const double searchRadiusNM = 100.0; // Larger radius for STAR
    const double searchRadiusM = searchRadiusNM * 1852.0;
    
    std::vector<XPLMNavRef> nearbyNavaids;
    
    // Search for navigation aids suitable for STAR
    XPLMNavType searchTypes[] = {xplm_Nav_VOR, xplm_Nav_Fix, xplm_Nav_ILS, xplm_Nav_Localizer};
    
    for (XPLMNavType navType : searchTypes) {
        XPLMNavRef navRef = XPLMFindNavAid(nullptr, nullptr, &searchLat, &searchLon, nullptr, navType);
        
        while (navRef != XPLM_NAV_NOT_FOUND) {
            float navLat, navLon, navElevation;
            char navID[32];
            
            XPLMGetNavAidInfo(navRef, nullptr, &navLat, &navLon, &navElevation, 
                              nullptr, nullptr, navID, nullptr, nullptr);
            
            positionTy navPos;
            navPos.lat() = navLat;
            navPos.lon() = navLon;
            navPos.alt_m() = navElevation * 0.3048;
            
            double distance = airportPos.dist(navPos);
            
            if (distance > 10000.0 && distance <= searchRadiusM) { // Suitable for STAR approach
                nearbyNavaids.push_back(navRef);
                
                if (nearbyNavaids.size() >= 8) break;
            }
            
            break; // Simplified search
        }
    }
    
    // Build STAR procedure - approach from outside to airport
    if (!nearbyNavaids.empty()) {
        // Sort by distance (furthest first for arrival)
        std::sort(nearbyNavaids.begin(), nearbyNavaids.end(), 
                  [&airportPos](XPLMNavRef a, XPLMNavRef b) {
                      float aLat, aLon, bLat, bLon;
                      XPLMGetNavAidInfo(a, nullptr, &aLat, &aLon, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
                      XPLMGetNavAidInfo(b, nullptr, &bLat, &bLon, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
                      
                      positionTy aPos(aLat, aLon, 0);
                      positionTy bPos(bLat, bLon, 0);
                      
                      return airportPos.dist(aPos) > airportPos.dist(bPos); // Furthest first
                  });
        
        // Create descending approach waypoints
        for (size_t i = 0; i < std::min(nearbyNavaids.size(), (size_t)4); i++) {
            float navLat, navLon, navElevation;
            char navID[32];
            
            XPLMGetNavAidInfo(nearbyNavaids[i], nullptr, &navLat, &navLon, &navElevation, 
                              nullptr, nullptr, navID, nullptr, nullptr);
            
            positionTy waypoint;
            waypoint.lat() = navLat;
            waypoint.lon() = navLon;
            waypoint.alt_m() = airportPos.alt_m() + (4 - i) * 1000.0; // Descending approach
            
            starProcedure.push_back(waypoint);
            
            LOG_MSG(logDEBUG, "STAR waypoint %zu: %s at %0.4f,%0.4f", 
                    i + 1, navID, navLat, navLon);
        }
    }
    
    // Generate basic geometric STAR if no navaids found
    if (starProcedure.empty()) {
        LOG_MSG(logDEBUG, "No suitable navaids found for STAR, generating basic geometric procedure");
        
        // Create a basic straight-in arrival
        for (int i = 4; i >= 1; i--) {
            positionTy waypoint;
            double distance = i * 8000.0; // 8km intervals, approaching
            double bearing = 180.0; // Default approach bearing
            
            // Try to use opposite runway heading for approach
            if (!runway.empty() && runway.length() >= 2) {
                try {
                    int runwayNum = std::stoi(runway.substr(0, 2));
                    bearing = (runwayNum + 18) * 10.0; // Opposite direction
                    if (bearing >= 360.0) bearing -= 360.0;
                } catch (...) {
                    // Keep default bearing
                }
            }
            
            double lat_offset = (distance * cos(bearing * PI / 180.0)) / 111320.0;
            double lon_offset = (distance * sin(bearing * PI / 180.0)) / (111320.0 * cos(airportPos.lat() * PI / 180.0));
            
            waypoint.lat() = airportPos.lat() + lat_offset;
            waypoint.lon() = airportPos.lon() + lon_offset;
            waypoint.alt_m() = airportPos.alt_m() + i * 600.0;
            
            starProcedure.push_back(waypoint);
        }
    }
    
    return starProcedure;
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
    // Basic validation of input parameters
    if (minDistanceNM < 0.0 || maxDistanceNM < 0.0 || minDistanceNM > maxDistanceNM) {
        LOG_MSG(logWARN, "Invalid distance parameters for GenerateVariedPosition: min=%.1f, max=%.1f", minDistanceNM, maxDistanceNM);
        return centerPos; // Return original position as fallback
    }
    
    // Validate center position
    if (!centerPos.isNormal()) {
        LOG_MSG(logWARN, "Invalid center position for GenerateVariedPosition");
        return centerPos;
    }
    
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
    LOG_MSG(logDEBUG, "Could not find optimal separation after %d attempts, using fallback position", maxAttempts);
    
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

// Initialize aircraft performance database with realistic performance data
// Based on typical specifications from flight manuals and published sources
void SyntheticConnection::InitializeAircraftPerformanceDB()
{
    if (!aircraftPerfDB.empty()) return; // Already initialized
    
    // General Aviation Aircraft
    // Cessna 172 Skyhawk - Popular training aircraft
    aircraftPerfDB["C172"] = AircraftPerformance("C172", 122, 140, 47, 14000, 645, 500, 16000, 65, 12);
    
    // Cessna 152 - Training aircraft
    aircraftPerfDB["C152"] = AircraftPerformance("C152", 107, 127, 43, 14700, 715, 480, 16000, 60, 10);
    
    // Piper PA-28 Cherokee/Warrior
    aircraftPerfDB["PA28"] = AircraftPerformance("PA28", 125, 140, 55, 14300, 640, 500, 16000, 70, 12);
    
    // Cessna 182 Skylane - High performance single
    aircraftPerfDB["C182"] = AircraftPerformance("C182", 145, 175, 56, 18100, 924, 600, 20000, 75, 15);
    
    // Cirrus SR22 - Modern high performance single
    aircraftPerfDB["SR22"] = AircraftPerformance("SR22", 183, 213, 81, 17500, 1200, 700, 19000, 90, 15);
    
    // Beechcraft Bonanza A36
    aircraftPerfDB["BE36"] = AircraftPerformance("BE36", 176, 200, 59, 18500, 1030, 650, 20000, 85, 15);
    
    // Commercial/Airline Aircraft
    // Boeing 737-800 - Popular narrow body
    aircraftPerfDB["B737"] = AircraftPerformance("B737", 453, 544, 132, 41000, 2500, 2000, 41000, 145, 25);
    
    // Airbus A320 - Popular narrow body
    aircraftPerfDB["A320"] = AircraftPerformance("A320", 447, 537, 118, 39800, 2220, 1800, 41000, 138, 25);
    
    // Boeing 777-300ER - Wide body long haul
    aircraftPerfDB["B777"] = AircraftPerformance("B777", 490, 590, 160, 43100, 2900, 2500, 43100, 170, 30);
    
    // Airbus A330-300 - Wide body
    aircraftPerfDB["A330"] = AircraftPerformance("A330", 470, 570, 145, 42650, 2500, 2200, 42650, 160, 30);
    
    // Boeing 787-9 Dreamliner
    aircraftPerfDB["B787"] = AircraftPerformance("B787", 488, 587, 138, 43000, 3000, 2300, 43000, 155, 30);
    
    // Airbus A350-900
    aircraftPerfDB["A350"] = AircraftPerformance("A350", 488, 587, 140, 42000, 3100, 2400, 43000, 160, 30);
    
    // Military Aircraft
    // F-16 Fighting Falcon
    aircraftPerfDB["F16"] = AircraftPerformance("F16", 515, 1500, 200, 50000, 50000, 15000, 60000, 250, 50);
    
    // F/A-18 Hornet
    aircraftPerfDB["F18"] = AircraftPerformance("F18", 570, 1190, 230, 50000, 45000, 12000, 55000, 280, 50);
    
    // C-130 Hercules Transport
    aircraftPerfDB["C130"] = AircraftPerformance("C130", 336, 417, 115, 28000, 1830, 1200, 33000, 130, 35);
    
    // KC-135 Stratotanker
    aircraftPerfDB["KC135"] = AircraftPerformance("KC135", 460, 585, 160, 50000, 2000, 1800, 50000, 180, 35);
    
    // E-3 AWACS
    aircraftPerfDB["E3"] = AircraftPerformance("E3", 360, 530, 150, 42000, 2300, 1500, 42000, 170, 30);
    
    // B-2 Spirit Stealth Bomber
    aircraftPerfDB["B2"] = AircraftPerformance("B2", 475, 630, 180, 50000, 6000, 3000, 50000, 200, 40);
    
    LOG_MSG(logDEBUG, "Initialized aircraft performance database with %zu aircraft types", aircraftPerfDB.size());
}

// Get aircraft performance data for a specific ICAO type
const AircraftPerformance* SyntheticConnection::GetAircraftPerformance(const std::string& icaoType) const
{
    auto it = aircraftPerfDB.find(icaoType);
    return (it != aircraftPerfDB.end()) ? &(it->second) : nullptr;
}

// Test function to validate aircraft performance database (for debugging)
#ifdef DEBUG
void SyntheticConnection::ValidateAircraftPerformanceDB()
{
    LOG_MSG(logDEBUG, "Validating aircraft performance database...");
    
    for (const auto& pair : aircraftPerfDB) {
        const std::string& type = pair.first;
        const AircraftPerformance& perf = pair.second;
        
        // Basic validation checks
        bool isValid = true;
        std::string errors;
        
        if (perf.cruiseSpeedKts <= perf.stallSpeedKts) {
            errors += "cruise speed <= stall speed; ";
            isValid = false;
        }
        
        if (perf.approachSpeedKts <= perf.stallSpeedKts) {
            errors += "approach speed <= stall speed; ";
            isValid = false;
        }
        
        if (perf.maxSpeedKts < perf.cruiseSpeedKts) {
            errors += "max speed < cruise speed; ";
            isValid = false;
        }
        
        if (perf.serviceCeilingFt <= 0 || perf.maxAltFt <= 0) {
            errors += "invalid altitude limits; ";
            isValid = false;
        }
        
        if (perf.climbRateFpm <= 0 || perf.descentRateFpm <= 0) {
            errors += "invalid climb/descent rates; ";
            isValid = false;
        }
        
        if (isValid) {
            LOG_MSG(logDEBUG, "%s: VALID - Cruise=%0.0f kts, Service ceiling=%0.0f ft, Climb=%0.0f fpm", 
                    type.c_str(), perf.cruiseSpeedKts, perf.serviceCeilingFt, perf.climbRateFpm);
        } else {
            LOG_MSG(logERR, "%s: INVALID - %s", type.c_str(), errors.c_str());
        }
    }
    
    LOG_MSG(logDEBUG, "Aircraft performance database validation complete. %zu aircraft types loaded.", 
            aircraftPerfDB.size());
}
#endif

// Update navigation system for smooth, realistic flight paths
void SyntheticConnection::UpdateNavigation(SynDataTy& synData, double currentTime)
{
    // Skip navigation updates for ground operations
    if (synData.state == SYN_STATE_PARKED || synData.state == SYN_STATE_STARTUP ||
        synData.state == SYN_STATE_TAXI_OUT || synData.state == SYN_STATE_TAXI_IN ||
        synData.state == SYN_STATE_SHUTDOWN) {
        return;
    }
    
    // If no flight path exists, generate one
    if (synData.flightPath.empty()) {
        // Generate a basic flight path for this aircraft
        positionTy destination = synData.pos;
        destination.lat() += (std::rand() % 200 - 100) / 1000.0; // ±0.1 degrees
        destination.lon() += (std::rand() % 200 - 100) / 1000.0;
        destination.alt_m() = synData.targetAltitude;
        
        GenerateFlightPath(synData, synData.pos, destination);
    }
    
    // Get current target waypoint
    if (synData.currentWaypoint < synData.flightPath.size()) {
        synData.targetWaypoint = synData.flightPath[synData.currentWaypoint];
        
        // Calculate bearing to target waypoint
        double bearing = synData.pos.angle(synData.targetWaypoint);
        synData.targetHeading = bearing;
        
        // Check if we've reached the current waypoint
        double distanceToWaypoint = synData.pos.dist(synData.targetWaypoint);
        if (distanceToWaypoint < 500.0) { // Within 500m of waypoint
            synData.currentWaypoint++;
            
            if (synData.currentWaypoint >= synData.flightPath.size()) {
                // Reached end of flight path, generate new one if still in cruise
                if (synData.state == SYN_STATE_CRUISE) {
                    positionTy newDestination = synData.pos;
                    newDestination.lat() += (std::rand() % 200 - 100) / 1000.0;
                    newDestination.lon() += (std::rand() % 200 - 100) / 1000.0;
                    newDestination.alt_m() = synData.targetAltitude;
                    
                    GenerateFlightPath(synData, synData.pos, newDestination);
                    synData.currentWaypoint = 0;
                }
            }
        }
    }
    
    // Smooth heading changes to avoid sharp turns
    double deltaTime = currentTime - synData.lastPosUpdateTime;
    if (deltaTime > 0.0) {
        SmoothHeadingChange(synData, synData.targetHeading, deltaTime);
    }
}

// Update terrain awareness to maintain safe separation from ground
void SyntheticConnection::UpdateTerrainAwareness(SynDataTy& synData)
{
    // Update terrain elevation more frequently in mountainous areas
    double currentTime = std::time(nullptr);
    bool needsTerrainUpdate = (currentTime - synData.lastTerrainCheck > 2.0); // Check every 2 seconds
    
    // Also check if position has changed significantly
    static std::map<std::string, positionTy> lastProbePos;
    const std::string key = synData.stat.call;
    auto lastPosIt = lastProbePos.find(key);
    
    if (lastPosIt != lastProbePos.end()) {
        double distFromLastProbe = synData.pos.dist(lastPosIt->second);
        if (distFromLastProbe > 1000.0) { // 1km threshold
            needsTerrainUpdate = true;
        }
    } else {
        needsTerrainUpdate = true; // First time
    }
    
    if (needsTerrainUpdate) {
        // Create temporary probe for safety if main probe is null or invalid
        XPLMProbeRef safeProbe = synData.terrainProbe;
        bool usingTempProbe = false;
        
        // Safety check: if terrain probe is null, create temporary one
        if (!safeProbe) {
            safeProbe = XPLMCreateProbe(xplm_ProbeY);
            usingTempProbe = true;
            LOG_MSG(logDEBUG, "Created temporary terrain probe for aircraft %s", synData.stat.call.c_str());
        }
        
        if (safeProbe) {
            synData.terrainElevation = GetTerrainElevation(synData.pos, safeProbe);
            synData.lastTerrainCheck = currentTime;
            lastProbePos[key] = synData.pos;
            
            // If we created the probe for the aircraft's permanent use, store it
            if (usingTempProbe && !synData.terrainProbe) {
                synData.terrainProbe = safeProbe;
                usingTempProbe = false; // Don't destroy it below
            }
            
            // Also probe ahead on flight path for proactive terrain avoidance
            positionTy aheadPos = synData.pos;
            double headingRad = synData.pos.heading() * PI / 180.0;
            double lookAheadDistance = synData.targetSpeed * 60.0; // 1 minute ahead at current speed
            lookAheadDistance = std::min(lookAheadDistance, 10000.0); // Max 10km ahead
            
            // Calculate position 1 minute ahead
            const double METERS_PER_DEGREE_LAT = 111320.0;
            const double METERS_PER_DEGREE_LON = 111320.0 * cos(aheadPos.lat() * PI / 180.0);
            
            double deltaLat = (lookAheadDistance * cos(headingRad)) / METERS_PER_DEGREE_LAT;
            double deltaLon = (lookAheadDistance * sin(headingRad)) / METERS_PER_DEGREE_LON;
            
            aheadPos.lat() += deltaLat;
            aheadPos.lon() += deltaLon;
            
            double aheadTerrainElev = GetTerrainElevation(aheadPos, safeProbe);
            
            // If terrain ahead is significantly higher, start climbing early
            double terrainRise = aheadTerrainElev - synData.terrainElevation;
            if (terrainRise > 100.0) { // Terrain rises more than 100m ahead
                if (synData.pos.alt_m() < aheadTerrainElev + 300.0) {
                    synData.targetAltitude = std::max(synData.targetAltitude, aheadTerrainElev + 500.0);
                    LOG_MSG(logINFO, "Aircraft %s: Terrain rising ahead (%.0fm), climbing to %0.0f ft", 
                            synData.stat.call.c_str(), aheadTerrainElev, synData.targetAltitude / 0.3048);
                }
            }
            
            // Clean up temporary probe if we used one
            if (usingTempProbe) {
                XPLMDestroyProbe(safeProbe);
            }
        } else {
            LOG_MSG(logERR, "Failed to create terrain probe for aircraft %s", synData.stat.call.c_str());
            // Use cached terrain elevation or conservative estimate
            if (synData.terrainElevation <= 0.0) {
                synData.terrainElevation = 500.0; // Conservative estimate
            }
        }
    }
    
    // Enhanced terrain safety checks based on flight phase
    double requiredClearance = GetRequiredTerrainClearance(synData.state, synData.trafficType);
    
    // Check current position safety
    if (!IsTerrainSafe(synData.pos, requiredClearance)) {
        // Terrain conflict - immediate emergency climb
        double emergencyAltitude = synData.terrainElevation + requiredClearance + 150.0; // Extra safety
        
        if (synData.state != SYN_STATE_LANDING) { // Don't emergency climb during landing
            synData.targetAltitude = std::max(synData.targetAltitude, emergencyAltitude);
            
            // Force immediate climb if critically low
            if (synData.pos.alt_m() < synData.terrainElevation + (requiredClearance * 0.5)) {
                synData.pos.alt_m() = synData.terrainElevation + requiredClearance;
                LOG_MSG(logWARN, "Aircraft %s: EMERGENCY TERRAIN AVOIDANCE - Immediate altitude correction to %0.0f ft", 
                        synData.stat.call.c_str(), synData.pos.alt_m() / 0.3048);
            } else {
                LOG_MSG(logINFO, "Aircraft %s: Terrain conflict, climbing to %0.0f ft (clearance: %.0fm)", 
                        synData.stat.call.c_str(), synData.targetAltitude / 0.3048, requiredClearance);
            }
        }
    }
}

// Generate a realistic flight path between two points
void SyntheticConnection::GenerateFlightPath(SynDataTy& synData, const positionTy& origin, const positionTy& destination)
{
    synData.flightPath.clear();
    synData.currentWaypoint = 0;
    
    // Simple flight path generation - can be enhanced with real navdata
    double distance = origin.dist(destination);
    int numWaypoints = std::max(2, (int)(distance / 10000.0)); // One waypoint every 10km
    
    for (int i = 1; i <= numWaypoints; i++) {
        double ratio = (double)i / (double)numWaypoints;
        
        positionTy waypoint;
        waypoint.lat() = origin.lat() + (destination.lat() - origin.lat()) * ratio;
        waypoint.lon() = origin.lon() + (destination.lon() - origin.lon()) * ratio;
        waypoint.alt_m() = origin.alt_m() + (destination.alt_m() - origin.alt_m()) * ratio;
        
        // Add some variation to make the path less linear
        if (i > 1 && i < numWaypoints) {
            double variation = 0.01; // ±0.01 degrees
            waypoint.lat() += (std::rand() % 200 - 100) / 10000.0 * variation;
            waypoint.lon() += (std::rand() % 200 - 100) / 10000.0 * variation;
        }
        
        // Enhanced terrain-safe waypoint generation
        double terrainElev = GetTerrainElevation(waypoint, synData.terrainProbe);
        double requiredClearance = GetRequiredTerrainClearance(SYN_STATE_CRUISE, synData.trafficType);
        
        // Ensure waypoint altitude is safe with proper clearance
        double minSafeAltitude = terrainElev + requiredClearance;
        waypoint.alt_m() = std::max(waypoint.alt_m(), minSafeAltitude);
        
        // For mountainous terrain, add extra vertical separation between waypoints
        if (i > 0 && !synData.flightPath.empty()) {
            // Bounds check: ensure we don't access invalid indices
            size_t prevIndex = synData.flightPath.size() - 1;
            if (prevIndex < synData.flightPath.size()) {
                double altitudeDiff = std::abs(waypoint.alt_m() - synData.flightPath[prevIndex].alt_m());
                if (altitudeDiff > 1000.0) { // More than 1000m altitude difference
                    // Add intermediate waypoint to smooth the climb/descent
                    positionTy intermediateWp;
                    intermediateWp.lat() = (waypoint.lat() + synData.flightPath[prevIndex].lat()) / 2.0;
                    intermediateWp.lon() = (waypoint.lon() + synData.flightPath[prevIndex].lon()) / 2.0;
                    intermediateWp.alt_m() = (waypoint.alt_m() + synData.flightPath[prevIndex].alt_m()) / 2.0;
                    
                    // Ensure intermediate waypoint is also terrain safe
                    double intermediateTerrainElev = GetTerrainElevation(intermediateWp, synData.terrainProbe);
                    intermediateWp.alt_m() = std::max(intermediateWp.alt_m(), intermediateTerrainElev + requiredClearance);
                    
                    synData.flightPath.push_back(intermediateWp);
                }
            }
        }
        
        synData.flightPath.push_back(waypoint);
    }
    
    LOG_MSG(logDEBUG, "Generated flight path for %s with %d waypoints", 
            synData.stat.call.c_str(), (int)synData.flightPath.size());
}

// Check if position is safe from terrain
bool SyntheticConnection::IsTerrainSafe(const positionTy& position, double minClearance)
{
    // Safety check for position validity
    if (!position.isNormal()) {
        LOG_MSG(logWARN, "Invalid position for terrain safety check");
        return false; // Conservative approach - assume unsafe
    }
    
    // Use terrain probe to get actual elevation with error handling
    XPLMProbeRef tempProbe = nullptr;
    double terrainElevation = 0.0;
    
    try {
        terrainElevation = GetTerrainElevation(position, tempProbe);
    } catch (...) {
        LOG_MSG(logERR, "Exception during terrain safety check at %.6f,%.6f", position.lat(), position.lon());
        // Assume conservative terrain elevation
        terrainElevation = 1000.0;
    }
    
    // Clean up probe safely
    if (tempProbe) {
        try {
            XPLMDestroyProbe(tempProbe);
        } catch (...) {
            // Ignore cleanup exceptions
        }
    }
    
    bool isSafe = (position.alt_m() >= (terrainElevation + minClearance));
    
    if (!isSafe) {
        LOG_MSG(logWARN, "Terrain safety check failed: altitude %.0fm, terrain %.0fm, required clearance %.0fm",
                position.alt_m(), terrainElevation, minClearance);
    }
    
    return isSafe;
}

// Get terrain elevation at a specific position
double SyntheticConnection::GetTerrainElevation(const positionTy& position, XPLMProbeRef& probeRef)
{
    // Safety check: ensure we have valid coordinates
    if (!position.isNormal()) {
        LOG_MSG(logWARN, "Invalid position for terrain probe: %.6f,%.6f", position.lat(), position.lon());
        return 0.0; // Assume sea level for invalid coordinates
    }
    
    // Use X-Plane's terrain probing system with error handling
    double elevation = NAN;
    try {
        elevation = YProbe_at_m(position, probeRef);
    } catch (...) {
        LOG_MSG(logERR, "Exception during terrain probing at %.6f,%.6f", position.lat(), position.lon());
        elevation = NAN;
    }
    
    // If probing fails, use a conservative estimate based on nearby areas
    if (std::isnan(elevation)) {
        // Try probing slightly offset positions to get a better estimate
        std::vector<positionTy> offsetPositions = {
            positionTy(position.lat() + 0.001, position.lon(), 0.0),          // North
            positionTy(position.lat() - 0.001, position.lon(), 0.0),          // South  
            positionTy(position.lat(), position.lon() + 0.001, 0.0),          // East
            positionTy(position.lat(), position.lon() - 0.001, 0.0)           // West
        };
        
        double maxElevation = 0.0;
        bool foundValidElevation = false;
        
        for (const auto& offsetPos : offsetPositions) {
            try {
                double offsetElev = YProbe_at_m(offsetPos, probeRef);
                if (!std::isnan(offsetElev)) {
                    maxElevation = std::max(maxElevation, offsetElev);
                    foundValidElevation = true;
                }
            } catch (...) {
                // Ignore exceptions for offset probes
                continue;
            }
        }
        
        if (foundValidElevation) {
            elevation = maxElevation + 200.0; // Add safety margin for uncertainty
            LOG_MSG(logWARN, "Terrain probe failed at %.6f,%.6f, using conservative estimate: %.0fm", 
                    position.lat(), position.lon(), elevation);
        } else {
            // Last resort: use a very conservative mountain altitude estimate
            // In mountainous regions, assume significant elevation
            elevation = 1000.0; // 1000m conservative estimate for unknown terrain
            LOG_MSG(logERR, "All terrain probes failed at %.6f,%.6f, using emergency estimate: %.0fm", 
                    position.lat(), position.lon(), elevation);
        }
    }
    
    return elevation;
}

// Get required terrain clearance based on flight state and aircraft type
double SyntheticConnection::GetRequiredTerrainClearance(SyntheticFlightState state, SyntheticTrafficType trafficType)
{
    double baseClearance = 300.0; // Base clearance in meters
    
    // Adjust clearance based on aircraft type
    switch (trafficType) {
        case SYN_TRAFFIC_GA:
            baseClearance = 250.0; // GA can fly lower
            break;
        case SYN_TRAFFIC_AIRLINE:
            baseClearance = 400.0; // Airlines need more clearance
            break;
        case SYN_TRAFFIC_MILITARY:
            baseClearance = 200.0; // Military can fly lower (but still safe)
            break;
        default:
            baseClearance = 300.0;
            break;
    }
    
    // Adjust clearance based on flight phase
    switch (state) {
        case SYN_STATE_PARKED:
        case SYN_STATE_STARTUP:
        case SYN_STATE_SHUTDOWN:
            return 0.0; // On ground
            
        case SYN_STATE_TAXI_OUT:
        case SYN_STATE_TAXI_IN:
            return 10.0; // Minimal clearance for taxiing
            
        case SYN_STATE_LINE_UP_WAIT:
            return 10.0; // Minimal clearance on runway
            
        case SYN_STATE_TAKEOFF:
            return std::max(50.0, baseClearance * 0.3); // Lower during takeoff
            
        case SYN_STATE_CLIMB:
            return baseClearance * 1.2; // Extra clearance while climbing
            
        case SYN_STATE_CRUISE:
            return baseClearance * 1.5; // Maximum clearance for cruise
            
        case SYN_STATE_HOLD:
            return baseClearance * 1.3; // Extra clearance for holding patterns
            
        case SYN_STATE_DESCENT:
            return baseClearance * 1.1; // Slightly more clearance during descent
            
        case SYN_STATE_APPROACH:
            return std::max(150.0, baseClearance * 0.6); // Reduced for approach but still safe
            
        case SYN_STATE_LANDING:
            return 30.0; // Minimal safe clearance for landing
            
        default:
            return baseClearance;
    }
}

// Smooth heading changes to avoid sharp turns
void SyntheticConnection::SmoothHeadingChange(SynDataTy& synData, double targetHeading, double deltaTime)
{
    double currentHeading = synData.pos.heading();
    
    // Calculate the shortest angular distance
    double headingDiff = targetHeading - currentHeading;
    while (headingDiff > 180.0) headingDiff -= 360.0;
    while (headingDiff < -180.0) headingDiff += 360.0;
    
    // Limit turn rate based on aircraft type and speed
    const AircraftPerformance* perfData = GetAircraftPerformance(synData.stat.acTypeIcao);
    double maxTurnRate = synData.headingChangeRate; // Default 2 deg/sec
    
    if (perfData) {
        // Faster aircraft turn slower, GA aircraft turn faster
        if (synData.trafficType == SYN_TRAFFIC_GA) {
            maxTurnRate = 4.0; // GA can turn faster
        } else if (synData.trafficType == SYN_TRAFFIC_AIRLINE) {
            maxTurnRate = 1.5; // Airlines turn more slowly
        } else if (synData.trafficType == SYN_TRAFFIC_MILITARY) {
            maxTurnRate = 8.0; // Military can turn very fast
        }
    }
    
    // Calculate maximum heading change for this time step
    double maxChange = maxTurnRate * deltaTime;
    
    // Apply smooth heading change
    double headingChange = std::min(std::abs(headingDiff), maxChange);
    if (headingDiff < 0.0) headingChange = -headingChange;
    
    double newHeading = currentHeading + headingChange;
    
    // Normalize to 0-360 range
    while (newHeading < 0.0) newHeading += 360.0;
    while (newHeading >= 360.0) newHeading -= 360.0;
    
    synData.pos.heading() = newHeading;
}

// Get the next waypoint in the flight path
positionTy SyntheticConnection::GetNextWaypoint(SynDataTy& synData)
{
    if (synData.currentWaypoint < synData.flightPath.size()) {
        return synData.flightPath[synData.currentWaypoint];
    }
    
    // If no waypoints, return current position
    return synData.pos;
}

// TCAS (Traffic Collision Avoidance System) implementation with enhanced predictive capability
void SyntheticConnection::UpdateTCAS(const LTFlightData::FDKeyTy& key, SynDataTy& synData, double currentTime)
{
    // Only active for airborne aircraft
    if (!synData.tcasActive || synData.pos.f.onGrnd == GND_ON) {
        return;
    }
    
    // Check TCAS every 1 second for better responsiveness
    if (currentTime - synData.lastTCASCheck < 1.0) {
        return;
    }
    synData.lastTCASCheck = currentTime;
    
    // Update predicted position for this aircraft
    synData.predictedPosition = PredictAircraftPosition(synData, 30.0); // 30 seconds ahead
    
    // Scan for traffic conflicts with enhanced detection
    bool trafficAdvisoryDetected = false;
    bool resolutionAdvisoryDetected = false;
    positionTy conflictPosition;
    double highestThreatLevel = 0.0;
    std::string threatCallsign;
    
    for (const auto& otherAircraft : mapSynData) {
        if (otherAircraft.first == key) continue; // Skip self
        
        const SynDataTy& otherSynData = otherAircraft.second;
        
        // Only check airborne aircraft
        if (otherSynData.pos.f.onGrnd == GND_ON) continue;
        
        // Check immediate conflict (Resolution Advisory range)
        if (CheckTrafficConflict(synData, otherSynData)) {
            resolutionAdvisoryDetected = true;
            conflictPosition = otherSynData.pos;
            threatCallsign = otherSynData.stat.call;
            highestThreatLevel = 1.0;
            break; // Immediate conflict takes priority
        }
        
        // Check predictive conflict (Traffic Advisory range)
        if (CheckPredictiveConflict(synData, otherSynData, 40.0)) { // 40 second look-ahead
            double cpa = CalculateClosestPointOfApproach(synData, otherSynData);
            if (cpa > highestThreatLevel) {
                trafficAdvisoryDetected = true;
                conflictPosition = otherSynData.pos;
                threatCallsign = otherSynData.stat.call;
                highestThreatLevel = cpa;
            }
        }
    }
    
    // Update conflict severity
    synData.conflictSeverity = highestThreatLevel;
    synData.nearestTrafficCallsign = threatCallsign;
    
    if (resolutionAdvisoryDetected) {
        // Escalate to Resolution Advisory if not already
        if (synData.tcasAdvisoryLevel < 2) {
            synData.tcasAdvisoryLevel = 2;
            synData.tcasManeuverStartTime = currentTime;
            GenerateTCASAdvisory(synData, conflictPosition);
        }
        ExecuteTCASManeuver(synData, currentTime);
    } else if (trafficAdvisoryDetected) {
        // Issue Traffic Advisory if not already in RA
        if (synData.tcasAdvisoryLevel == 0) {
            synData.tcasAdvisoryLevel = 1;
            synData.tcasAdvisory = "TRAFFIC ADVISORY - TRAFFIC, TRAFFIC";
            LOG_MSG(logINFO, "TCAS %s: Traffic Advisory for traffic %s", 
                    synData.stat.call.c_str(), threatCallsign.c_str());
        }
    } else if (synData.inTCASAvoidance || synData.tcasAdvisoryLevel > 0) {
        // Clear of conflict, resume normal operations
        synData.inTCASAvoidance = false;
        synData.tcasAdvisory = "";
        synData.tcasAdvisoryLevel = 0;
        synData.nearestTrafficCallsign = "";
        synData.conflictSeverity = 0.0;
        LOG_MSG(logINFO, "TCAS %s: Clear of conflict, resuming normal operations", synData.stat.call.c_str());
    }
}

// Enhanced check for traffic conflicts with improved separation standards
bool SyntheticConnection::CheckTrafficConflict(const SynDataTy& synData1, const SynDataTy& synData2)
{
    // Calculate horizontal separation
    double horizontalSeparation = synData1.pos.dist(synData2.pos) / 1852.0; // Convert to nautical miles
    
    // Calculate vertical separation
    double verticalSeparation = std::abs(synData1.pos.alt_m() - synData2.pos.alt_m()) / 0.3048; // Convert to feet
    
    // Enhanced TCAS conflict thresholds based on altitude and aircraft type
    double minHorizontalSep = 3.0; // Base 3 nautical miles
    double minVerticalSep = 700.0; // Base 700 feet
    
    // Adjust thresholds based on altitude (closer spacing allowed at lower altitudes)
    double altitude1 = synData1.pos.alt_m() * 3.28084; // Convert to feet
    double altitude2 = synData2.pos.alt_m() * 3.28084;
    double avgAltitude = (altitude1 + altitude2) / 2.0;
    
    if (avgAltitude < 10000.0) {
        // Below 10,000 feet - reduced separation
        minHorizontalSep = 2.5;
        minVerticalSep = 500.0;
    } else if (avgAltitude > 40000.0) {
        // Above 40,000 feet - increased separation
        minHorizontalSep = 4.0;
        minVerticalSep = 1000.0;
    }
    
    // Adjust for aircraft types (larger aircraft need more separation)
    if (synData1.trafficType == SYN_TRAFFIC_AIRLINE || synData2.trafficType == SYN_TRAFFIC_AIRLINE) {
        minHorizontalSep *= 1.2; // 20% more separation for airlines
        minVerticalSep *= 1.1; // 10% more vertical separation
    }
    
    // Check if aircraft are too close
    bool horizontalConflict = horizontalSeparation < minHorizontalSep;
    bool verticalConflict = verticalSeparation < minVerticalSep;
    
    // Conflict exists if both horizontal and vertical separation are insufficient
    return horizontalConflict && verticalConflict;
}

// Enhanced TCAS advisory generation with coordinated responses
void SyntheticConnection::GenerateTCASAdvisory(SynDataTy& synData, const positionTy& conflictPos)
{
    // Determine optimal maneuver based on relative positions and aircraft capabilities
    double altitudeDiff = conflictPos.alt_m() - synData.pos.alt_m();
    double bearingToTraffic = synData.pos.angle(conflictPos);
    double currentHeading = synData.pos.heading();
    
    // Calculate optimal maneuver type
    int maneuverType = DetermineOptimalTCASManeuver(synData, {});
    
    if (std::abs(altitudeDiff) < 200.0 && maneuverType != 3) {
        // Level flight conflict - prefer turning maneuver
        double headingDiff = bearingToTraffic - currentHeading;
        while (headingDiff < -180.0) headingDiff += 360.0;
        while (headingDiff > 180.0) headingDiff -= 360.0;
        
        // Choose turn direction based on traffic bearing and airspace considerations
        if (headingDiff > 0) {
            // Traffic is to the right, turn left
            synData.tcasAvoidanceHeading = currentHeading - 30.0;
            synData.tcasAdvisory = "RESOLUTION ADVISORY - TURN LEFT, TURN LEFT";
        } else {
            // Traffic is to the left, turn right
            synData.tcasAvoidanceHeading = currentHeading + 30.0;
            synData.tcasAdvisory = "RESOLUTION ADVISORY - TURN RIGHT, TURN RIGHT";
        }
        
        // Normalize heading
        while (synData.tcasAvoidanceHeading < 0.0) synData.tcasAvoidanceHeading += 360.0;
        while (synData.tcasAvoidanceHeading >= 360.0) synData.tcasAvoidanceHeading -= 360.0;
        
    } else if (altitudeDiff > 0 || maneuverType == 1) {
        // Traffic above or optimal maneuver is descend - descend advisory
        synData.tcasAvoidanceAltitude = synData.pos.alt_m() - 500.0; // Descend 1640 ft
        synData.tcasVerticalSpeed = -8.0; // 1600 ft/min descent rate
        synData.tcasAdvisory = "RESOLUTION ADVISORY - DESCEND, DESCEND";
    } else {
        // Traffic below or optimal maneuver is climb - climb advisory  
        synData.tcasAvoidanceAltitude = synData.pos.alt_m() + 500.0; // Climb 1640 ft
        synData.tcasVerticalSpeed = 8.0; // 1600 ft/min climb rate
        synData.tcasAdvisory = "RESOLUTION ADVISORY - CLIMB, CLIMB";
    }
    
    synData.inTCASAvoidance = true;
    synData.tcasAdvisoryLevel = 2; // Resolution Advisory
    LOG_MSG(logWARN, "TCAS %s: %s (conflict severity: %.2f)", 
            synData.stat.call.c_str(), synData.tcasAdvisory.c_str(), synData.conflictSeverity);
}

// Execute enhanced TCAS avoidance maneuver with improved logic
void SyntheticConnection::ExecuteTCASManeuver(SynDataTy& synData, double currentTime)
{
    if (!synData.inTCASAvoidance) return;
    
    // Calculate maneuver duration (typically 20-30 seconds for TCAS maneuvers)
    double maneuverDuration = currentTime - synData.tcasManeuverStartTime;
    
    // Apply heading change if required
    if (std::abs(synData.tcasAvoidanceHeading) > 0.001) {
        SmoothHeadingChange(synData, synData.tcasAvoidanceHeading, 2.0); // 2 second interval
    }
    
    // Apply altitude change with vertical speed if required  
    if (std::abs(synData.tcasAvoidanceAltitude) > 0.001) {
        synData.targetAltitude = synData.tcasAvoidanceAltitude;
        
        // Ensure we don't go below terrain
        double requiredClearance = GetRequiredTerrainClearance(synData.state, synData.trafficType);
        double minSafeAltitude = synData.terrainElevation + requiredClearance;
        synData.targetAltitude = std::max(synData.targetAltitude, minSafeAltitude);
        
        // Check if we've reached the target altitude or maneuver time limit
        double altitudeDiff = std::abs(synData.pos.alt_m() - synData.targetAltitude);
        if (altitudeDiff < 50.0 || maneuverDuration > 30.0) { // Within 160 feet or 30 seconds elapsed
            // Maneuver complete, level off
            synData.tcasVerticalSpeed = 0.0;
            LOG_MSG(logDEBUG, "TCAS %s: Maneuver complete, leveling off at %.0f ft", 
                    synData.stat.call.c_str(), synData.pos.alt_m() * 3.28084);
        }
    }
    
    // Check for maneuver timeout (maximum 60 seconds)
    if (maneuverDuration > 60.0) {
        synData.inTCASAvoidance = false;
        synData.tcasAdvisoryLevel = 0;
        synData.tcasVerticalSpeed = 0.0;
        LOG_MSG(logINFO, "TCAS %s: Maneuver timeout, resuming normal operations", synData.stat.call.c_str());
    }
}

// Update communication frequencies based on aircraft position and airport proximity
void SyntheticConnection::UpdateCommunicationFrequencies(SynDataTy& synData, const positionTy& userPos)
{
    double currentTime = std::time(nullptr);
    
    // Update frequency every 30 seconds or when changing flight states
    if (currentTime - synData.lastFreqUpdate < 30.0 && std::abs(synData.currentComFreq - 121.5) > 0.001) {
        return;
    }
    
    synData.lastFreqUpdate = currentTime;
    
    // Find nearest airport for frequency determination
    std::vector<std::string> nearbyAirports = FindNearbyAirports(synData.pos, 25.0); // 25nm radius
    
    std::string nearestAirport;
    double minDistance = 999999.0;
    
    // Find the closest airport
    for (const std::string& airportCode : nearbyAirports) {
        // Get airport position (simplified - would use actual airport database)
        positionTy airportPos = synData.pos; // Placeholder
        double distance = synData.pos.dist(airportPos) / 1852.0; // Convert to NM
        
        if (distance < minDistance) {
            minDistance = distance;
            nearestAirport = airportCode;
        }
    }
    
    // Determine appropriate frequency based on flight state and position
    double newFreq = 121.5; // Default UNICOM
    std::string freqType = "unicom";
    
    switch (synData.state) {
        case SYN_STATE_PARKED:
        case SYN_STATE_STARTUP:
            if (minDistance < 5.0) {
                newFreq = 121.9; // Ground frequency
                freqType = "ground";
            }
            break;
            
        case SYN_STATE_TAXI_OUT:
        case SYN_STATE_TAXI_IN:
            if (minDistance < 3.0) {
                newFreq = 121.9; // Ground frequency
                freqType = "ground";
            } else {
                newFreq = 121.5; // UNICOM
                freqType = "unicom";
            }
            break;
            
        case SYN_STATE_LINE_UP_WAIT:
        case SYN_STATE_TAKEOFF:
        case SYN_STATE_LANDING:
            if (minDistance < 5.0) {
                newFreq = 118.1; // Tower frequency
                freqType = "tower";
            }
            break;
            
        case SYN_STATE_APPROACH:
            if (minDistance < 15.0) {
                newFreq = 119.1; // Approach frequency  
                freqType = "approach";
            } else {
                newFreq = 120.4; // Center frequency
                freqType = "center";
            }
            break;
            
        case SYN_STATE_CLIMB:
            if (synData.pos.alt_m() > 3000.0) { // Above 10,000 feet
                newFreq = 120.4; // Center frequency
                freqType = "center";
            } else {
                newFreq = 119.1; // Approach/departure frequency
                freqType = "departure";
            }
            break;
            
        case SYN_STATE_CRUISE:
        case SYN_STATE_HOLD:
        case SYN_STATE_DESCENT:
            newFreq = 120.4; // Center frequency
            freqType = "center";
            break;
            
        default:
            newFreq = 121.5; // UNICOM
            freqType = "unicom";
            break;
    }
    
    // Add some realistic frequency variation
    newFreq += (std::rand() % 10 - 5) * 0.025; // +/- 0.125 MHz variation
    
    // Update frequency if it changed significantly
    if (std::abs(synData.currentComFreq - newFreq) > 0.1) {
        synData.currentComFreq = newFreq;
        synData.currentFreqType = freqType;
        synData.currentAirport = nearestAirport;
        
        LOG_MSG(logDEBUG, "Aircraft %s switched to %s frequency %.3f MHz (airport: %s, distance: %.1f nm)",
                synData.stat.call.c_str(), freqType.c_str(), newFreq, 
                nearestAirport.c_str(), minDistance);
    }
}

// Enhanced ground operations
void SyntheticConnection::UpdateGroundOperations(SynDataTy& synData, double currentTime)
{
    // Generate taxi route if needed
    if (synData.taxiRoute.empty() && 
        (synData.state == SYN_STATE_TAXI_OUT || synData.state == SYN_STATE_TAXI_IN)) {
        
        positionTy origin = synData.pos;
        positionTy destination = synData.pos;
        
        if (synData.state == SYN_STATE_TAXI_OUT) {
            // Taxi to runway - find nearest runway
            destination.lat() += (std::rand() % 20 - 10) / 10000.0; // Small offset for runway
            destination.lon() += (std::rand() % 20 - 10) / 10000.0;
        } else {
            // Taxi to gate - find nearest gate/parking
            if (synData.assignedGate.empty()) {
                synData.assignedGate = "Gate " + std::to_string(1 + (std::rand() % 50));
            }
            destination.lat() -= (std::rand() % 30 - 15) / 10000.0; // Small offset for gate
            destination.lon() -= (std::rand() % 30 - 15) / 10000.0;
        }
        
        GenerateTaxiRoute(synData, origin, destination);
    }
    
    // Update taxi movement
    if (!synData.taxiRoute.empty()) {
        UpdateTaxiMovement(synData, currentTime - synData.lastPosUpdateTime);
    }
    
    // Ground collision avoidance
    if (synData.groundCollisionAvoidance) {
        positionTy nextPos = synData.pos;
        // Calculate next position based on current movement
        double deltaTime = 1.0; // 1 second ahead
        double speed = synData.targetSpeed; // m/s
        double heading = synData.pos.heading();
        
        nextPos.lat() += (speed * deltaTime * std::cos(heading * PI / 180.0)) / 111320.0;
        nextPos.lon() += (speed * deltaTime * std::sin(heading * PI / 180.0)) / (111320.0 * std::cos(nextPos.lat() * PI / 180.0));
        
        if (CheckGroundCollision(synData, nextPos)) {
            // Stop if collision detected
            synData.targetSpeed = 0.0;
            LOG_MSG(logDEBUG, "Ground collision avoidance: %s stopping", synData.stat.call.c_str());
        }
    }
}

// Generate taxi route waypoints
void SyntheticConnection::GenerateTaxiRoute(SynDataTy& synData, const positionTy& origin, const positionTy& destination)
{
    synData.taxiRoute.clear();
    synData.currentTaxiWaypoint = 0;
    
    // Simple taxi route generation - in reality would use airport taxi diagram
    positionTy waypoint1 = origin;
    positionTy waypoint2 = destination;
    
    // Add intermediate waypoint(s) for realistic taxi path
    positionTy intermediate;
    intermediate.lat() = (origin.lat() + destination.lat()) / 2.0;
    intermediate.lon() = (origin.lon() + destination.lon()) / 2.0;
    intermediate.alt_m() = origin.alt_m();
    intermediate.heading() = origin.angle(destination);
    
    synData.taxiRoute.push_back(waypoint1);
    synData.taxiRoute.push_back(intermediate);
    synData.taxiRoute.push_back(waypoint2);
    
    LOG_MSG(logDEBUG, "Generated taxi route for %s with %zu waypoints", 
            synData.stat.call.c_str(), synData.taxiRoute.size());
}

// Check for potential ground collisions with other aircraft
bool SyntheticConnection::CheckGroundCollision(const SynDataTy& synData, const positionTy& nextPos)
{
    // Check against all other synthetic aircraft on ground
    for (const auto& pair : mapSynData) {
        const SynDataTy& otherAc = pair.second;
        
        // Skip self and aircraft not on ground
        if (pair.second.stat.call == synData.stat.call || 
            otherAc.pos.f.onGrnd != GND_ON) {
            continue;
        }
        
        // Check distance
        double distance = nextPos.dist(otherAc.pos);
        
        // Ground separation minimum based on aircraft type
        double minSeparation = 50.0; // 50 meters default
        if (synData.trafficType == SYN_TRAFFIC_AIRLINE || otherAc.trafficType == SYN_TRAFFIC_AIRLINE) {
            minSeparation = 100.0; // 100 meters for airlines
        }
        
        if (distance < minSeparation) {
            LOG_MSG(logDEBUG, "Ground collision risk: %s too close to %s (%.1f m)", 
                    synData.stat.call.c_str(), otherAc.stat.call.c_str(), distance);
            return true;
        }
    }
    
    return false;
}

// Update taxi movement along planned route
void SyntheticConnection::UpdateTaxiMovement(SynDataTy& synData, double deltaTime)
{
    if (synData.taxiRoute.empty() || synData.currentTaxiWaypoint >= synData.taxiRoute.size()) {
        return;
    }
    
    positionTy& currentWaypoint = synData.taxiRoute[synData.currentTaxiWaypoint];
    double distanceToWaypoint = synData.pos.dist(currentWaypoint);
    
    // Check if we've reached the current waypoint (within 10 meters)
    if (distanceToWaypoint < 10.0) {
        synData.currentTaxiWaypoint++;
        
        if (synData.currentTaxiWaypoint >= synData.taxiRoute.size()) {
            // Reached destination
            LOG_MSG(logDEBUG, "Aircraft %s completed taxi route", synData.stat.call.c_str());
            return;
        }
        
        // Move to next waypoint
        currentWaypoint = synData.taxiRoute[synData.currentTaxiWaypoint];
        distanceToWaypoint = synData.pos.dist(currentWaypoint);
    }
    
    // Update heading towards current waypoint
    double targetHeading = synData.pos.angle(currentWaypoint);
    SmoothHeadingChange(synData, targetHeading, deltaTime);
    
    // Adjust speed based on proximity to waypoint and other factors
    double targetSpeed = synData.targetSpeed;
    
    // Slow down when approaching waypoints
    if (distanceToWaypoint < 50.0) {
        targetSpeed *= 0.5; // Half speed when close to waypoint
    }
    
    // Further reduce speed in congested areas (simplified check)
    if (synData.groundCollisionAvoidance) {
        targetSpeed *= 0.7;
    }
    
    // Update target speed with taxi-specific limitations
    const AircraftPerformance* perfData = GetAircraftPerformance(synData.stat.acTypeIcao);
    double maxTaxiSpeed = perfData ? perfData->taxiSpeedKts * 0.514444 : 15.0 * 0.514444; // Convert to m/s
    synData.targetSpeed = std::min(targetSpeed, maxTaxiSpeed);
}

// Enhanced TCAS functions for predictive conflict detection and resolution

// Predict aircraft position at a future time based on current velocity and flight state
positionTy SyntheticConnection::PredictAircraftPosition(const SynDataTy& synData, double timeAhead)
{
    positionTy predictedPos = synData.pos;
    
    // Calculate current velocity components
    double groundSpeed = synData.targetSpeed; // m/s
    double heading = synData.pos.heading();
    double verticalSpeed = synData.tcasVerticalSpeed; // m/s
    
    // Predict horizontal movement
    double deltaLat = (groundSpeed * timeAhead * std::cos(heading * PI / 180.0)) / 111320.0; // degrees
    double deltaLon = (groundSpeed * timeAhead * std::sin(heading * PI / 180.0)) / 
                     (111320.0 * std::cos(predictedPos.lat() * PI / 180.0)); // degrees
    
    predictedPos.lat() += deltaLat;
    predictedPos.lon() += deltaLon;
    
    // Predict vertical movement
    predictedPos.alt_m() += verticalSpeed * timeAhead;
    
    // Ensure predicted altitude doesn't go below terrain
    double requiredClearance = GetRequiredTerrainClearance(synData.state, synData.trafficType);
    double minSafeAltitude = synData.terrainElevation + requiredClearance;
    predictedPos.alt_m() = std::max(predictedPos.alt_m(), minSafeAltitude);
    
    return predictedPos;
}

// Calculate closest point of approach between two aircraft
double SyntheticConnection::CalculateClosestPointOfApproach(const SynDataTy& synData1, const SynDataTy& synData2)
{
    // Get current positions and velocities
    positionTy pos1 = synData1.pos;
    positionTy pos2 = synData2.pos;
    
    // Calculate velocity vectors
    double speed1 = synData1.targetSpeed;
    double speed2 = synData2.targetSpeed;
    double heading1 = pos1.heading();
    double heading2 = pos2.heading();
    
    // Convert to velocity components (m/s)
    double vx1 = speed1 * std::sin(heading1 * PI / 180.0);
    double vy1 = speed1 * std::cos(heading1 * PI / 180.0);
    double vx2 = speed2 * std::sin(heading2 * PI / 180.0);
    double vy2 = speed2 * std::cos(heading2 * PI / 180.0);
    
    // Calculate relative position and velocity
    double dx = (pos2.lon() - pos1.lon()) * 111320.0 * std::cos(pos1.lat() * PI / 180.0);
    double dy = (pos2.lat() - pos1.lat()) * 111320.0;
    double dvx = vx2 - vx1;
    double dvy = vy2 - vy1;
    
    // Calculate time to closest approach
    double relativeSpeed = dvx * dvx + dvy * dvy;
    if (relativeSpeed < 0.001) {
        // Aircraft moving in parallel, return current separation
        return std::sqrt(dx * dx + dy * dy);
    }
    
    double timeToClosest = -(dx * dvx + dy * dvy) / relativeSpeed;
    timeToClosest = std::max(0.0, timeToClosest); // Don't predict past
    
    // Calculate closest approach distance
    double closestDx = dx + dvx * timeToClosest;
    double closestDy = dy + dvy * timeToClosest;
    double closestDistance = std::sqrt(closestDx * closestDx + closestDy * closestDy);
    
    // Include vertical separation in the calculation
    double vz1 = synData1.tcasVerticalSpeed;
    double vz2 = synData2.tcasVerticalSpeed;
    double dz = synData2.pos.alt_m() - synData1.pos.alt_m();
    double dvz = vz2 - vz1;
    double closestDz = dz + dvz * timeToClosest;
    
    // Return 3D separation distance
    return std::sqrt(closestDistance * closestDistance + closestDz * closestDz);
}

// Check for predictive conflicts using look-ahead time
bool SyntheticConnection::CheckPredictiveConflict(const SynDataTy& synData1, const SynDataTy& synData2, double lookAheadTime)
{
    // Predict positions at multiple time intervals
    const int numSteps = 10;
    double timeStep = lookAheadTime / numSteps;
    
    for (int i = 1; i <= numSteps; i++) {
        double checkTime = timeStep * i;
        positionTy pos1 = PredictAircraftPosition(synData1, checkTime);
        positionTy pos2 = PredictAircraftPosition(synData2, checkTime);
        
        // Create temporary SynData objects for conflict check
        SynDataTy tempData1 = synData1;
        SynDataTy tempData2 = synData2;
        tempData1.pos = pos1;
        tempData2.pos = pos2;
        
        // Check if conflict would occur at this time
        if (CheckTrafficConflict(tempData1, tempData2)) {
            return true;
        }
    }
    
    return false;
}

// Determine optimal TCAS maneuver based on flight conditions and aircraft performance
int SyntheticConnection::DetermineOptimalTCASManeuver(const SynDataTy& ownAircraft, const SynDataTy& trafficAircraft)
{
    // Maneuver types: 0=turn, 1=descend, 2=climb, 3=maintain
    
    double ownAltitude = ownAircraft.pos.alt_m() * 3.28084; // Convert to feet
    
    // Consider aircraft capabilities and current flight state
    const AircraftPerformance* perfData = GetAircraftPerformance(ownAircraft.stat.acTypeIcao);
    
    // GA aircraft prefer turning at lower altitudes
    if (ownAircraft.trafficType == SYN_TRAFFIC_GA && ownAltitude < 10000.0) {
        return 0; // Turn maneuver
    }
    
    // Airlines prefer vertical maneuvers at high altitudes  
    if (ownAircraft.trafficType == SYN_TRAFFIC_AIRLINE && ownAltitude > 20000.0) {
        // Check if near service ceiling
        if (perfData && ownAltitude > (perfData->serviceCeilingFt * 0.9)) {
            return 1; // Descend (near ceiling)
        }
        return 2; // Climb (normal operations)
    }
    
    // Military aircraft have better climb performance
    if (ownAircraft.trafficType == SYN_TRAFFIC_MILITARY) {
        return 2; // Climb maneuver
    }
    
    // Consider current flight state
    switch (ownAircraft.state) {
        case SYN_STATE_CLIMB:
            return 2; // Continue climbing
        case SYN_STATE_DESCENT:
        case SYN_STATE_APPROACH:
            return 1; // Continue descending
        case SYN_STATE_CRUISE:
            // At cruise, prefer maneuver that maintains cruise efficiency
            return (ownAltitude < 25000.0) ? 2 : 1; // Climb if low, descend if high
        default:
            return 0; // Default to turn
    }
}

// Coordinate TCAS responses between two aircraft to avoid complementary maneuvers
void SyntheticConnection::CoordinateTCASResponse(SynDataTy& synData1, SynDataTy& synData2)
{
    // This is a simplified coordination algorithm
    // In real TCAS, this would involve data link communication between aircraft
    
    // Determine which aircraft should climb and which should descend
    double alt1 = synData1.pos.alt_m();
    double alt2 = synData2.pos.alt_m();
    
    if (alt1 > alt2) {
        // Higher aircraft climbs, lower aircraft descends
        synData1.tcasAvoidanceAltitude = alt1 + 500.0;
        synData1.tcasVerticalSpeed = 8.0;
        synData1.tcasAdvisory = "RESOLUTION ADVISORY - CLIMB, CLIMB";
        
        synData2.tcasAvoidanceAltitude = alt2 - 500.0;
        synData2.tcasVerticalSpeed = -8.0;
        synData2.tcasAdvisory = "RESOLUTION ADVISORY - DESCEND, DESCEND";
    } else {
        // Lower aircraft climbs, higher aircraft descends
        synData1.tcasAvoidanceAltitude = alt1 + 500.0;
        synData1.tcasVerticalSpeed = 8.0;
        synData1.tcasAdvisory = "RESOLUTION ADVISORY - CLIMB, CLIMB";
        
        synData2.tcasAvoidanceAltitude = alt2 - 500.0;
        synData2.tcasVerticalSpeed = -8.0;
        synData2.tcasAdvisory = "RESOLUTION ADVISORY - DESCEND, DESCEND";
    }
    
    synData1.inTCASAvoidance = true;
    synData2.inTCASAvoidance = true;
    synData1.tcasAdvisoryLevel = 2;
    synData2.tcasAdvisoryLevel = 2;
    
    LOG_MSG(logINFO, "TCAS Coordination: %s and %s executing coordinated maneuvers", 
            synData1.stat.call.c_str(), synData2.stat.call.c_str());
}
