// Minimal test to check our synthetic states enum
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
    SYN_STATE_SHUTDOWN
};

void test_switch(SyntheticFlightState state) {
    switch (state) {
        case SYN_STATE_PARKED:
        case SYN_STATE_STARTUP:
            break;
        case SYN_STATE_TAXI_OUT:
            break;
        case SYN_STATE_TAKEOFF_ROLL:
        case SYN_STATE_ROTATE:
        case SYN_STATE_LIFT_OFF:
            break;
        default:
            break;
    }
}

int main() { return 0; }
