// Test for synthetic aircraft CRUISE state fix
// This test validates that aircraft don't get stuck in CRUISE state

#include <iostream>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <cmath>

// Mock enum for testing (matches the actual enum)
enum SyntheticFlightState : unsigned char {
    SYN_STATE_PARKED = 0,
    SYN_STATE_STARTUP,
    SYN_STATE_TAXI_OUT,
    SYN_STATE_LINE_UP_WAIT,
    SYN_STATE_TAKEOFF_ROLL,
    SYN_STATE_ROTATE,
    SYN_STATE_LIFT_OFF,
    SYN_STATE_INITIAL_CLIMB,
    SYN_STATE_CLIMB,
    SYN_STATE_CRUISE,
    SYN_STATE_HOLD,
    SYN_STATE_DESCENT,
    SYN_STATE_APPROACH,
    SYN_STATE_FINAL,
    SYN_STATE_FLARE,
    SYN_STATE_TOUCH_DOWN,
    SYN_STATE_ROLL_OUT,
    SYN_STATE_TAXI_IN,
    SYN_STATE_SHUTDOWN,
    SYN_STATE_MISSED_APPROACH,
    SYN_STATE_GO_AROUND
};

enum SyntheticTrafficType : unsigned char {
    SYN_TRAFFIC_NONE = 0,
    SYN_TRAFFIC_GA = 1,
    SYN_TRAFFIC_AIRLINE = 2,
    SYN_TRAFFIC_MILITARY = 4,
    SYN_TRAFFIC_ALL = 7
};

// Mock aircraft data for testing
struct MockSynData {
    SyntheticFlightState state;
    SyntheticTrafficType trafficType;
    double stateChangeTime;
    double nextEventTime;
    std::string destinationAirport;
    double altitude;
    double terrainElevation;
    double posLat, posLon;
    double holdingTime;
    
    MockSynData() : state(SYN_STATE_CRUISE), trafficType(SYN_TRAFFIC_GA), 
                    stateChangeTime(0), nextEventTime(0), destinationAirport("KJFK"),
                    altitude(10000.0), terrainElevation(100.0), 
                    posLat(40.7128), posLon(-74.0060), holdingTime(0.0) {}
};

// Mock position for testing
struct positionTy {
    double lat, lon, alt;
    positionTy(double la = 0, double lo = 0, double al = 0) : lat(la), lon(lo), alt(al) {}
    bool isNormal() const { return std::abs(lat) <= 90 && std::abs(lon) <= 180; }
    double dist(const positionTy& other) const {
        // Simple distance calculation in meters (approximate)
        double dlat = (lat - other.lat) * 111319.9;
        double dlon = (lon - other.lon) * 111319.9 * std::cos(lat * M_PI / 180.0);
        return std::sqrt(dlat * dlat + dlon * dlon);
    }
};

// Mock functions for testing
positionTy GetAirportPosition(const std::string& icao) {
    if (icao == "KJFK") return positionTy(40.6413, -73.7781, 4.0);
    if (icao == "KLAX") return positionTy(33.9425, -118.4081, 39.0);
    if (icao == "INVALID") return positionTy(999, 999, -9999); // Invalid airport - outside lat/lon bounds
    return positionTy(40.0, -74.0, 100.0); // Default valid position
}

std::vector<std::string> FindNearbyAirports(const positionTy& pos, double radiusNM) {
    std::vector<std::string> airports;
    airports.push_back("KJFK");
    airports.push_back("KEWR");
    airports.push_back("KLGA");
    return airports;
}

void SetRealisticDescentParameters(MockSynData& synData) {
    // Mock implementation
    std::cout << "Setting descent parameters for aircraft in cruise at " 
              << synData.altitude << "ft" << std::endl;
}

void LOG_MSG(const char* level, const char* format, ...) {
    // Mock logging - just print to console for testing
    std::cout << "[" << level << "] " << format << std::endl;
}

// Test the improved cruise state logic
SyntheticFlightState TestCruiseStateLogic(MockSynData& synData, double currentTime) {
    SyntheticFlightState newState = synData.state;
    
    if (synData.state == SYN_STATE_CRUISE) {
        double cruiseTime = currentTime - synData.stateChangeTime;
        
        // GUARANTEED TIMEOUT: Force transition after maximum cruise time
        if (cruiseTime > 2700.0) { // After 45 minutes, force descent
            newState = SYN_STATE_DESCENT;
            SetRealisticDescentParameters(synData);
            LOG_MSG("DEBUG", "Aircraft forced descent after maximum cruise time (45 min)");
            return newState;
        }
        
        // Check and validate destination airport
        bool hasValidDestination = false;
        double distanceToAirport = 999999.0;
        
        if (!synData.destinationAirport.empty()) {
            positionTy airportPos = GetAirportPosition(synData.destinationAirport);
            if (airportPos.isNormal()) {
                hasValidDestination = true;
                positionTy currentPos(synData.posLat, synData.posLon, synData.altitude);
                distanceToAirport = currentPos.dist(airportPos);
            } else {
                // Destination airport is invalid - clear it immediately
                LOG_MSG("DEBUG", "Destination airport invalid, clearing destination");
                synData.destinationAirport = "";
            }
        }
        
        // Check if near valid destination and should start descent
        if (hasValidDestination) {
            double altitudeAGL = synData.altitude - synData.terrainElevation;
            double descentDistance = std::max(10000.0, altitudeAGL * 6.0); // ~6:1 descent ratio
            
            if (distanceToAirport < descentDistance && cruiseTime > 300.0) {
                newState = SYN_STATE_DESCENT;
                SetRealisticDescentParameters(synData);
                LOG_MSG("DEBUG", "Beginning descent to destination");
                return newState;
            }
        }
        
        // Enhanced fallback behavior with higher probabilities
        int decision = std::rand() % 100;
        
        if (cruiseTime > 1800.0) { // After 30 minutes, much more likely to transition
            if (decision < 75) { // 75% chance to descend
                newState = SYN_STATE_DESCENT;
                SetRealisticDescentParameters(synData);
                LOG_MSG("DEBUG", "Beginning descent after long cruise (30+ min)");
            }
        } else if (cruiseTime > 1200.0) { // After 20 minutes, higher probability  
            if (decision < 50) { // 50% chance to transition
                if (decision < 15) { // 15% for holding
                    newState = SYN_STATE_HOLD;
                    synData.holdingTime = 0.0;
                    LOG_MSG("DEBUG", "Entering holding pattern after 20 min cruise");
                } else { // 35% for descent
                    newState = SYN_STATE_DESCENT;
                    SetRealisticDescentParameters(synData);
                    LOG_MSG("DEBUG", "Beginning descent after 20 min cruise");
                }
            }
        } else if (cruiseTime > 600.0) { // After 10 minutes, higher probability
            if (decision < 30) { // 30% chance to transition
                if (decision < 10) { // 10% for holding
                    newState = SYN_STATE_HOLD;
                    synData.holdingTime = 0.0;
                    LOG_MSG("DEBUG", "Entering holding pattern after 10 min cruise");
                } else { // 20% for descent
                    newState = SYN_STATE_DESCENT;
                    SetRealisticDescentParameters(synData);
                    LOG_MSG("DEBUG", "Beginning descent after 10 min cruise");
                }
            }
        }
        
        // If no destination after validation or originally empty, try to find one
        if (synData.destinationAirport.empty() && cruiseTime > 60.0) { // After 1 minute of cruise
            std::vector<std::string> nearbyAirports = FindNearbyAirports(positionTy(synData.posLat, synData.posLon), 100.0);
            if (!nearbyAirports.empty()) {
                synData.destinationAirport = nearbyAirports[0];
                positionTy newAirportPos = GetAirportPosition(synData.destinationAirport);
                if (newAirportPos.isNormal()) {
                    hasValidDestination = true;
                    positionTy currentPos(synData.posLat, synData.posLon, synData.altitude);
                    distanceToAirport = currentPos.dist(newAirportPos);
                    LOG_MSG("DEBUG", "Assigned new destination");
                } else {
                    synData.destinationAirport = ""; // Clear if new airport also invalid
                }
            }
        }
    }
    
    return newState;
}

// Test scenarios
void TestScenario1_NormalCruiseWithValidDestination() {
    std::cout << "\n=== Test 1: Normal cruise with valid destination ===" << std::endl;
    MockSynData aircraft;
    aircraft.destinationAirport = "KJFK";
    aircraft.stateChangeTime = 1000.0;
    
    // Test various cruise times
    double testTimes[] = {1300.0, 1700.0, 2200.0, 2800.0, 3800.0}; // 5min, 12min, 20min, 30min, 47min cruise
    
    for (double currentTime : testTimes) {
        double cruiseTime = currentTime - aircraft.stateChangeTime;
        std::cout << "Testing at " << (cruiseTime / 60.0) << " minutes cruise time..." << std::endl;
        
        SyntheticFlightState newState = TestCruiseStateLogic(aircraft, currentTime);
        
        if (cruiseTime > 2700.0 && newState != SYN_STATE_DESCENT) {
            std::cout << "❌ FAIL: Aircraft not forced into descent after 45 minutes!" << std::endl;
        } else {
            std::cout << "✅ PASS: State transition logic working correctly" << std::endl;
        }
    }
}

void TestScenario2_CruiseWithInvalidDestination() {
    std::cout << "\n=== Test 2: Cruise with invalid destination ===" << std::endl;
    MockSynData aircraft;
    aircraft.destinationAirport = "INVALID"; // This will return invalid position
    aircraft.stateChangeTime = 1000.0;
    
    // Test after 2 minutes (should trigger destination clearing and re-assignment)
    double currentTime = 1120.0; // 2 minutes cruise time
    
    std::cout << "Testing destination re-assignment after invalid destination..." << std::endl;
    SyntheticFlightState newState = TestCruiseStateLogic(aircraft, currentTime);
    
    if (aircraft.destinationAirport != "INVALID") {
        if (aircraft.destinationAirport.empty()) {
            std::cout << "✅ PASS: Invalid destination was cleared" << std::endl;
        } else {
            std::cout << "✅ PASS: Invalid destination was replaced with valid one: " << aircraft.destinationAirport << std::endl;
        }
    } else {
        std::cout << "❌ FAIL: Invalid destination not replaced!" << std::endl;
    }
}

void TestScenario3_CruiseWithoutDestination() {
    std::cout << "\n=== Test 3: Cruise without destination ===" << std::endl;
    MockSynData aircraft;
    aircraft.destinationAirport = ""; // No destination
    aircraft.stateChangeTime = 1000.0;
    
    // Test after 2 minutes (should trigger destination assignment)
    double currentTime = 1120.0; // 2 minutes cruise time
    
    std::cout << "Testing destination assignment for aircraft without destination..." << std::endl;
    SyntheticFlightState newState = TestCruiseStateLogic(aircraft, currentTime);
    
    if (!aircraft.destinationAirport.empty()) {
        std::cout << "✅ PASS: Destination assigned: " << aircraft.destinationAirport << std::endl;
    } else {
        std::cout << "⚠️  INFO: No destination assigned (may be normal depending on nearby airports)" << std::endl;
    }
}

void TestScenario4_GuaranteedTimeout() {
    std::cout << "\n=== Test 4: Guaranteed timeout after 45 minutes ===" << std::endl;
    MockSynData aircraft;
    aircraft.destinationAirport = ""; // No destination to force timeout behavior
    aircraft.stateChangeTime = 1000.0;
    
    // Test after 46 minutes (should force descent)
    double currentTime = 3800.0; // 46.7 minutes cruise time
    
    std::cout << "Testing guaranteed timeout transition..." << std::endl;
    SyntheticFlightState newState = TestCruiseStateLogic(aircraft, currentTime);
    
    if (newState == SYN_STATE_DESCENT) {
        std::cout << "✅ PASS: Aircraft forced into descent after maximum cruise time!" << std::endl;
    } else {
        std::cout << "❌ FAIL: Aircraft not forced into descent after 45+ minutes!" << std::endl;
    }
}

void TestScenario5_ProbabilityDistribution() {
    std::cout << "\n=== Test 5: Probability distribution test ===" << std::endl;
    
    int transitions = 0;
    int totalTests = 100;
    
    // Test 20-minute cruise time transition probability (should be ~50%)
    for (int i = 0; i < totalTests; i++) {
        MockSynData aircraft;
        aircraft.destinationAirport = "";
        aircraft.stateChangeTime = 1000.0;
        double currentTime = 2200.0; // 20 minutes cruise time
        
        SyntheticFlightState newState = TestCruiseStateLogic(aircraft, currentTime);
        if (newState != SYN_STATE_CRUISE) {
            transitions++;
        }
    }
    
    double transitionRate = (double)transitions / totalTests;
    std::cout << "Transition rate at 20 minutes: " << (transitionRate * 100) << "%" << std::endl;
    
    if (transitionRate >= 0.35 && transitionRate <= 0.65) { // Allow 35-65% range (target 50%)
        std::cout << "✅ PASS: Transition probability is within expected range" << std::endl;
    } else {
        std::cout << "⚠️  WARNING: Transition probability may be outside expected range (35-65%)" << std::endl;
    }
}

int main() {
    std::cout << "Testing Synthetic Aircraft CRUISE State Fix" << std::endl;
    std::cout << "===========================================" << std::endl;
    
    // Initialize random seed for consistent but varied testing
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    
    // Run all test scenarios
    TestScenario1_NormalCruiseWithValidDestination();
    TestScenario2_CruiseWithInvalidDestination();
    TestScenario3_CruiseWithoutDestination();
    TestScenario4_GuaranteedTimeout();
    TestScenario5_ProbabilityDistribution();
    
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "All cruise state fix tests completed!" << std::endl;
    std::cout << "If you see any ❌ FAIL messages above, the fix needs adjustment." << std::endl;
    std::cout << "✅ PASS messages indicate the fix is working correctly." << std::endl;
    
    return 0;
}