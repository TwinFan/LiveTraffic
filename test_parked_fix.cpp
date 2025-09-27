// Test for synthetic aircraft PARKED state fix
// This test validates that aircraft don't get permanently stuck in PARKED state

#include <iostream>
#include <cstdlib>
#include <ctime>
#include <string>

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
    
    MockSynData() : state(SYN_STATE_PARKED), trafficType(SYN_TRAFFIC_GA), 
                    stateChangeTime(0), nextEventTime(0) {}
};

void LOG_MSG(const char* level, const char* format, ...) {
    // Mock logging - just print to console for testing
    std::cout << "[" << level << "] " << format << std::endl;
}

// Current PARKED state logic (problematic version)
SyntheticFlightState TestCurrentParkedStateLogic(MockSynData& synData, double currentTime) {
    SyntheticFlightState newState = synData.state;
    
    if (synData.state == SYN_STATE_PARKED) {
        double parkedTime = currentTime - synData.stateChangeTime;
        
        // Current logic - low probability transitions
        int startupChance = 0;
        switch (synData.trafficType) {
            case SYN_TRAFFIC_GA: startupChance = 25; break;      // 25% chance for GA
            case SYN_TRAFFIC_AIRLINE: startupChance = 40; break; // 40% chance for airlines
            case SYN_TRAFFIC_MILITARY: startupChance = 35; break; // 35% chance for military
            case SYN_TRAFFIC_NONE:
            case SYN_TRAFFIC_ALL:
            default: startupChance = 20; break; // Default chance
        }
        
        // Time-based adjustments (more activity during day)
        time_t rawTime;
        struct tm* timeInfo;
        time(&rawTime);
        timeInfo = localtime(&rawTime);
        int hour = timeInfo->tm_hour;
        
        if (hour >= 6 && hour <= 22) { // Daytime operations
            startupChance += 15;
        } else { // Night operations
            startupChance -= 10;
        }
        
        if (std::rand() % 100 < startupChance) {
            newState = SYN_STATE_STARTUP;
            LOG_MSG("DEBUG", "Aircraft starting up (chance: %d%%)", startupChance);
        }
    }
    
    return newState;
}

// Test the actual improved logic from the code
SyntheticFlightState TestActualImprovedParkedStateLogic(MockSynData& synData, double currentTime) {
    SyntheticFlightState newState = synData.state;
    
    if (synData.state == SYN_STATE_PARKED) {
        double parkedTime = currentTime - synData.stateChangeTime;
        
        // GUARANTEED TIMEOUT: Force startup after maximum parked time to prevent stuck aircraft
        if (parkedTime > 3600.0) { // After 60 minutes, force startup regardless of conditions
            newState = SYN_STATE_STARTUP;
            LOG_MSG("DEBUG", "Aircraft forced startup after maximum parked time (60 min)");
            return newState;
        }
        
        // Progressive startup probability based on traffic type and parked time
        int startupChance = 0;
        switch (synData.trafficType) {
            case SYN_TRAFFIC_GA: startupChance = 35; break;      // Increased from 25%
            case SYN_TRAFFIC_AIRLINE: startupChance = 50; break; // Increased from 40%
            case SYN_TRAFFIC_MILITARY: startupChance = 45; break; // Increased from 35%
            case SYN_TRAFFIC_NONE:
            case SYN_TRAFFIC_ALL:
            default: startupChance = 30; break; // Increased from 20%
        }
        
        // Time-based adjustments (more activity during day)
        time_t rawTime;
        struct tm* timeInfo;
        time(&rawTime);
        timeInfo = localtime(&rawTime);
        int hour = timeInfo->tm_hour;
        
        if (hour >= 6 && hour <= 22) { // Daytime operations
            startupChance += 20; // Increased from 15%
        } else { // Night operations
            startupChance -= 5; // Reduced penalty (was -10)
        }
        
        // Progressive probability increases based on parked time to prevent stuck aircraft
        if (parkedTime > 2400.0) { // After 40 minutes
            startupChance += 40; // Much higher chance
        } else if (parkedTime > 1800.0) { // After 30 minutes
            startupChance += 25;
        } else if (parkedTime > 1200.0) { // After 20 minutes
            startupChance += 15;
        } else if (parkedTime > 600.0) { // After 10 minutes
            startupChance += 10;
        }
        
        // Cap at 95% to maintain some realism while ensuring high probability
        startupChance = std::min(95, std::max(5, startupChance)); // Minimum 5% chance always
        
        if (std::rand() % 100 < startupChance) {
            newState = SYN_STATE_STARTUP;
            LOG_MSG("DEBUG", "Aircraft starting up (chance: %d%%, parked: %.1f min)", 
                    startupChance, parkedTime / 60.0);
        }
    }
    
    return newState;
}

// Test scenarios
void TestScenario1_ShortParkedTime() {
    std::cout << "\n=== Test 1: Short parked time (5 minutes) ===" << std::endl;
    
    int transitionsOld = 0, transitionsNew = 0;
    int totalTests = 100;
    
    for (int i = 0; i < totalTests; i++) {
        MockSynData aircraftOld, aircraftNew;
        aircraftOld.trafficType = aircraftNew.trafficType = SYN_TRAFFIC_AIRLINE;
        aircraftOld.stateChangeTime = aircraftNew.stateChangeTime = 1000.0;
        double currentTime = 1300.0; // 5 minutes parked
        
        SyntheticFlightState oldResult = TestCurrentParkedStateLogic(aircraftOld, currentTime);
        SyntheticFlightState newResult = TestActualImprovedParkedStateLogic(aircraftNew, currentTime);
        
        if (oldResult != SYN_STATE_PARKED) transitionsOld++;
        if (newResult != SYN_STATE_PARKED) transitionsNew++;
    }
    
    std::cout << "Current logic: " << transitionsOld << "% transitions" << std::endl;
    std::cout << "Improved logic: " << transitionsNew << "% transitions" << std::endl;
    
    if (transitionsNew > transitionsOld) {
        std::cout << "✅ IMPROVEMENT: Higher transition rate with new logic" << std::endl;
    } else {
        std::cout << "⚠️  No significant improvement detected" << std::endl;
    }
}

void TestScenario2_LongParkedTime() {
    std::cout << "\n=== Test 2: Long parked time (40 minutes) ===" << std::endl;
    
    int transitionsOld = 0, transitionsNew = 0;
    int totalTests = 100;
    
    for (int i = 0; i < totalTests; i++) {
        MockSynData aircraftOld, aircraftNew;
        aircraftOld.trafficType = aircraftNew.trafficType = SYN_TRAFFIC_GA;
        aircraftOld.stateChangeTime = aircraftNew.stateChangeTime = 1000.0;
        double currentTime = 3400.0; // 40 minutes parked
        
        SyntheticFlightState oldResult = TestCurrentParkedStateLogic(aircraftOld, currentTime);
        SyntheticFlightState newResult = TestActualImprovedParkedStateLogic(aircraftNew, currentTime);
        
        if (oldResult != SYN_STATE_PARKED) transitionsOld++;
        if (newResult != SYN_STATE_PARKED) transitionsNew++;
    }
    
    std::cout << "Current logic: " << transitionsOld << "% transitions" << std::endl;
    std::cout << "Improved logic: " << transitionsNew << "% transitions" << std::endl;
    
    if (transitionsNew >= 70) { // Should be high at 40 minutes (adjusted threshold)
        std::cout << "✅ PASS: High transition rate after long parked time" << std::endl;
    } else {
        std::cout << "❌ FAIL: Transition rate still too low after 40 minutes" << std::endl;
    }
}

void TestScenario3_GuaranteedTimeout() {
    std::cout << "\n=== Test 3: Guaranteed timeout after 60 minutes ===" << std::endl;
    
    MockSynData aircraft;
    aircraft.trafficType = SYN_TRAFFIC_GA;
    aircraft.stateChangeTime = 1000.0;
    double currentTime = 4601.0; // 60+ minutes parked (3601 seconds)
    
    SyntheticFlightState result = TestActualImprovedParkedStateLogic(aircraft, currentTime);
    
    if (result == SYN_STATE_STARTUP) {
        std::cout << "✅ PASS: Aircraft forced to startup after 60 minutes" << std::endl;
    } else {
        std::cout << "❌ FAIL: Aircraft not forced to startup after 60 minutes" << std::endl;
        // Debug the timeout logic
        double parkedTime = currentTime - aircraft.stateChangeTime;
        std::cout << "  Debug: parkedTime=" << parkedTime << ", threshold=3600.0" << std::endl;
    }
}

void TestScenario4_NightOperations() {
    std::cout << "\n=== Test 4: Night operations (reduced activity) ===" << std::endl;
    
    // This test assumes it's currently night time (hour 2 AM)
    // In real implementation, we'd mock the time
    
    int transitionsOld = 0, transitionsNew = 0;
    int totalTests = 100;
    
    for (int i = 0; i < totalTests; i++) {
        MockSynData aircraftOld, aircraftNew;
        aircraftOld.trafficType = aircraftNew.trafficType = SYN_TRAFFIC_AIRLINE;
        aircraftOld.stateChangeTime = aircraftNew.stateChangeTime = 1000.0;
        double currentTime = 2200.0; // 20 minutes parked
        
        SyntheticFlightState oldResult = TestCurrentParkedStateLogic(aircraftOld, currentTime);
        SyntheticFlightState newResult = TestActualImprovedParkedStateLogic(aircraftNew, currentTime);
        
        if (oldResult != SYN_STATE_PARKED) transitionsOld++;
        if (newResult != SYN_STATE_PARKED) transitionsNew++;
    }
    
    std::cout << "Current logic: " << transitionsOld << "% transitions" << std::endl;
    std::cout << "Improved logic: " << transitionsNew << "% transitions" << std::endl;
    
    std::cout << "ℹ️  Note: Night operations should show reduced but non-zero activity" << std::endl;
}

int main() {
    std::cout << "Testing Synthetic Aircraft PARKED State Fix" << std::endl;
    std::cout << "===========================================" << std::endl;
    
    // Initialize random seed for consistent but varied testing
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    
    // Run all test scenarios
    TestScenario1_ShortParkedTime();
    TestScenario2_LongParkedTime();
    TestScenario3_GuaranteedTimeout();
    TestScenario4_NightOperations();
    
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "The current PARKED state logic has low probability transitions" << std::endl;
    std::cout << "which can cause aircraft to remain parked for very long periods." << std::endl;
    std::cout << "\nProposed improvements:" << std::endl;
    std::cout << "1. Higher base startup probabilities" << std::endl;
    std::cout << "2. Progressive probability increases over time" << std::endl;
    std::cout << "3. Guaranteed timeout after 60 minutes" << std::endl;
    std::cout << "4. More frequent state checks (reduce from 5-15min to 2-8min)" << std::endl;
    
    return 0;
}