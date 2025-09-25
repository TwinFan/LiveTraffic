# Synthetic Traffic Configuration

LiveTraffic now includes a comprehensive synthetic traffic generator that creates realistic AI aircraft with advanced behavior patterns. This feature provides offline traffic simulation with three types of aircraft: General Aviation (GA), Commercial Airlines, and Military aircraft.

## Features

### Traffic Types
- **General Aviation (GA)**: Small aircraft like Cessna 172, Piper Cherokee, Cirrus SR22
- **Commercial Airlines**: Large aircraft like Boeing 737, Airbus A320, Boeing 777
- **Military Aircraft**: Military aircraft like F-16, C-130, KC-135

### Advanced AI Behavior
- **12-State Flight State Machine**: From parked through taxi, takeoff, cruise, approach, to landing
- **Realistic Performance**: Speed and altitude appropriate for aircraft type
- **User Awareness**: Aircraft respond to user aircraft presence within 10nm
- **Weather Operations**: Aircraft behavior affected by weather conditions
- **Holding Patterns**: Aircraft can enter holds when needed

### Communication System
- **TTS Integration**: Text-to-speech radio communications (Windows TTS support)  
- **Realistic Radio Calls**: Context-appropriate communications based on flight phase
- **Realistic Signal Degradation**: Communication quality degrades naturally with distance and atmospheric conditions
- **Dynamic Range**: No hard cutoff - communications continue at greater distances with increasing static and dropouts

## Configuration

### DataRef Configuration
Synthetic traffic is controlled through LiveTraffic's dataRef system:

```
livetraffic/cfg/synthetic/enabled             - Enable/disable synthetic traffic (0/1)
livetraffic/cfg/synthetic/traffic_types       - Traffic type bitmask (1=GA, 2=Airline, 4=Military)
livetraffic/cfg/synthetic/max_aircraft        - Maximum number of aircraft (default: 20)
livetraffic/cfg/synthetic/traffic_density     - Traffic density percentage (0-100)
livetraffic/cfg/synthetic/ga_ratio           - GA traffic ratio percentage (default: 60)
livetraffic/cfg/synthetic/airline_ratio      - Airline traffic ratio percentage (default: 30)
livetraffic/cfg/synthetic/military_ratio     - Military traffic ratio percentage (default: 10)
livetraffic/cfg/synthetic/tts_enabled        - Enable TTS communications (0/1)
livetraffic/cfg/synthetic/user_awareness     - Enable user awareness (0/1)
livetraffic/cfg/synthetic/weather_operations - Enable weather-based operations (0/1)
```

### Settings UI Configuration
Access synthetic traffic settings through the LiveTraffic Settings window:
1. Open LiveTraffic menu → Settings
2. Navigate to "Synthetic Traffic" section
3. Enable "Enable Synthetic Traffic" checkbox
4. Configure traffic types and ratios
5. Adjust advanced features as desired

### Example Configuration
To enable all traffic types with balanced distribution:
- Enable Synthetic Traffic: ✓
- Maximum Aircraft: 30
- Traffic Density: 75%
- Traffic Types: GA ✓, Airlines ✓, Military ✓
- GA Ratio: 50%, Airline Ratio: 40%, Military Ratio: 10%
- TTS Communications: ✓
- User Awareness: ✓  
- Weather Operations: ✓

## Communication System Details

### Realistic Signal Propagation
The synthetic traffic communication system now implements realistic radio signal behavior:

- **Near Range (0-10nm)**: Crystal clear communications with no interference
- **Medium Range (10-25nm)**: Gradual signal degradation begins, occasional static
- **Long Range (25-50nm)**: Moderate signal degradation with word dropouts and static
- **Extended Range (50nm+)**: Heavy static, frequent dropouts, garbled transmissions

### Signal Degradation Effects
- **Light Static**: Occasional "[static]" indicators, rare word dropouts (5% chance)
- **Moderate Static**: More frequent static, 15% word dropout rate, partial word garbling
- **Heavy Static**: "[heavy static]" and "[breaking up]" indicators, 30% word dropout rate, severe garbling

### Atmospheric Conditions
Signal quality is also affected by simulated atmospheric conditions:
- Random atmospheric factor between 0.8-1.2 applied to all communications
- Represents real-world factors like weather, ionospheric conditions, and interference

## Aircraft Behavior States

### Parked → Startup → Taxi Out → Takeoff → Climb → Cruise
- Aircraft begin parked at gates or ramp positions
- Realistic startup procedures with engine start delays
- Taxi to assigned runway following ground routes
- Takeoff with appropriate performance parameters
- Climb to cruise altitude based on aircraft type

### Cruise → Hold → Descent → Approach → Landing → Taxi In
- Cruise at realistic altitudes and speeds
- May enter holding patterns if required
- Begin descent when approaching destination
- Follow approach procedures to runway
- Land and taxi to gate/parking position

### State Transition Timing
- **Startup**: 1-3 minutes
- **Taxi**: 2-5 minutes
- **Takeoff**: 30-90 seconds
- **Climb**: 5-15 minutes
- **Cruise**: 10-40 minutes (varies)
- **Hold**: 1-5 minutes maximum
- **Descent**: 5-15 minutes
- **Approach/Landing**: 1-3 minutes each

## Call Sign Generation

### GA Aircraft
- Format: N##### (e.g., N1234AB, N5678CD)
- US registration style with numbers and letters

### Airlines
- Format: AAA### (e.g., UAL123, AAL456, DAL789)
- Common airline codes: UAL, AAL, DAL, SWA, JBU, ASA

### Military
- Format: AAAA### (e.g., ARMY123, NAVY456, USAF789)
- Military branches: ARMY, NAVY, USAF, USCG

## Performance Parameters

### General Aviation
- **Speed**: ~120 knots
- **Altitude**: 500-3000 feet AGL typically
- **Operations**: Local flights, training, recreational

### Airlines
- **Speed**: ~240 knots (varies by phase)
- **Altitude**: Cruise altitudes 25,000-42,000 feet
- **Operations**: Scheduled routes, IFR procedures

### Military
- **Speed**: ~400 knots (varies widely)
- **Altitude**: Various operational altitudes
- **Operations**: Training, transport, special missions

## Integration with Live Traffic

- Synthetic aircraft automatically avoid conflicts with live tracking data
- When live aircraft get too close to synthetic aircraft, synthetic aircraft are removed
- Synthetic traffic operates in areas where live tracking data is sparse
- All synthetic aircraft integrate seamlessly with LiveTraffic's existing systems

## Technical Notes

- Synthetic aircraft use the same rendering and physics systems as live aircraft
- Performance impact is minimal due to efficient AI state management
- Weather integration uses LiveTraffic's existing weather system
- TTS system is designed for Windows SAPI integration (framework provided)
- Navigation database integration ready for X-Plane navdata access

## Future Enhancements

- Complete X-Plane navigation database integration for realistic SID/STAR procedures
- Enhanced airport-specific operations and runway assignments
- Seasonal and time-based traffic patterns
- Integration with additional weather sources
- Expanded military aircraft operations and procedures