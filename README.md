# LiveTraffic
LiveTraffic is (going to be) a plugin for the flight simulator [X-Plane](https://www.x-plane.com) to show real life traffic, based on publicly available live flight data, as additional planes within X-Plane.

It came into being when I was looking at plugins simulating additional traffic in X-Plane and thinking at that time: Why do they try to simulate traffic and invent flight models when there is flight data available online which brings yet another piece of reality into the simulation? Why can't one just read the data and display the aircraft?

Well...turned out it's not *that* easy ;-)

I spent an awful lot of time dealing with the inaccuracies of the data sources, see section 'Limitations' below. There are only timestamps and positions, heading and speed is point-in-time info but not a reliable vector to the next position. There is no information on pitch or bank angle, or on gear or flaps positions. So I ended up inventing my own flight models to make aircrafts move more or less smoothly from one data point to the next, extend gear and flaps, pitch up an down, flare etc.

And as an airport is a naturally interesting hot spot of plane movements, but due to simple physics often only sparingly covered with ADS-B receivers and hence live data, there is even lots of code to simulate final approach, landing, and roll-out for planes for which data coverage ends on approach.

## Limitations
LiveTraffic relies mostly on available live flight data. This data
- can and will be **inaccurate**,
- never includes all planes in the air,
- often misses moving planes on the ground as ADS-B receiver coverage is more difficult the lower a plane flies
- contains by design only positions for every few seconds but no complete paths or vectors.

This inevitably leads to some aircrafts
- not sticking exactly to taxi routes on the ground,
- touching down before or after the beginning of the runway,
- hovering above it (Flightradar24 especially),
- missing the runway by a few meters (seems to be an Embraer specialty) or even landing off-site,
- appearing and disappearing at will,
- performing funny maneuvers like full circles or flying extremely slow.

There is a lot of processing to try to smooth all of these effects, but it only works so far.

Live aircrafts will **ignore your plane** and simply fly or drive through you if you are in the way. That can look funny if you are stopping short at the beginning of the active departure runway and a A380 performs its take-off roll through you... 

**TCAS display might or might not work.** TCAS display implementations in add-on planes vary and are beyond my control. LiveTraffic uses the same underlying library to display additional aircrafts as XSquawkBox does. If there are issues with XSquawkBox the same will apply to LiveTraffic.
I can get TCAS display to work in the standard X-Plane Cirrus TheJet, see sections 'TCAS' below.

If you can't live with these limitations don't try LiveTraffic.

## Features

### Live Flight Data Channels
LiveTraffic can read from the following sources, called channels:
- [ADS-B Exchange](https://www.adsbexchange.com), both
- [online live data](https://www.adsbexchange.com/data) and
- historical files (see section 'Historical Data' [here](https://www.adsbexchange.com/data/), especially the requirements!)
- [OpenSky Network](https://opensky-network.org), using its [REST API](https://opensky-network.org/apidoc/index.html)
- [Flightradar24](https://www.flightradar24.com)

LiveTraffic can even read and combine several channels at the same time. Currently, however, results of combined data is often unsatisfactory: Aircrafts tend to jump around.

I have not yet fully confirmed that I may use above sources the way LiveTraffic currently does. Especially Flightradar24 is a bit doubtful. So changes to this list may happen.

### TCAS
TCAS might or might not work, also see 'Limitations'.

In X-Plane 10 I can confirm that the TCAS display *does* work in the standard Cirrus TheJet after applying the following workaround:
1. Startup X-Plane with activated LiveTraffic plugin as usual.
1. Disable LiveTraffic in the Plugin Admin. This takes a while as X-Plane then configures the additional planes as AI planes.
1. Re-enable LiveTraffic again.

### Data Refs
LiveTraffic makes good use of [X-Plane's data ref feature](https://developer.x-plane.com/sdk/XPLMDataAccess/). Historically, this was just meant to ease my development life as I could use the [DataRefEditor plugin](https://developer.x-plane.com/tools/datarefeditor/) to change the configuration and read aircrafts' status. Thus, it became the backbone of the configuration object in LiveTraffic and remains available to the outside world.

- `livetraffic/ac` includes information on one aircraft like position, heading, speed, flight phase... The aircraft is selected by writing the ICAO hex transponder code, converted to decimal, to `livetraffic/ac/key` or by writing an index number (1 through the number of currently displayed aircrafts) to the same `livetraffic/ac/key`.
- `livetraffic/sim/date` and `livetraffic/sim/time` provide the current simulated date/time. LiveTraffic's date/time might differ from X-Plane's: You can always use live flight data even if you have set X-Plane to some other day of the year (then `livetraffic/sim` is current ime) or you can even *write* to these data refs and actively set X-Plane to the set date while using historical flight data for displaying traffic.
- `livetraffic/cfg` includes most of the configuration options.
- `livetraffic/channel` allows to enable/disable the various flight data channels.
- `livetraffic/dbg` provides debugging options like filter the entire network traffic for one specific aircraft, providing additional log output on the selected aircraft or debugging libxplanemp's model matching.

Nowadays, all configuration and channel enable/disable options are also available in a settings GUI accessible from the plugin's menu and saved into a configuration file. The settings GUI and the config file code essentially just read and write data refs. 

## Status
Currently in final stages of development.
Not yet available for download.
Not yet available on [x-plane.org](https://forums.x-plane.org/index.php?/files/).

## License
[MIT License](https://github.com/TwinFan/LiveTraffic/blob/master/LICENSE), essentially freeware.

## Requirements
- [X-Plane](https://www.x-plane.com), version 10 or 11
- Configure 'other aircrafts' ([X-Plane 10](https://www.x-plane.com/manuals/desktop/10/#changingthenumberofotheraircraft), [X-Plane 11](https://x-plane.com/manuals/desktop/#changingthenumberofotheraircraft))
- Operating system
- So far only **developed and tested on Mac**.
- I'm willing to try a **Windows** version in the near future. (Setting up this Githib repository is actually a step in this direction in order to synch developments on both platforms.)
- I don't have time and resources for a Linux environment; but help is appreciated once the Mac version is published. Most effort will likely be to set up and maintain config/build scripts. Mac and Windows versions are based on integrated XCode / Visual Studio build environments.
- CSL packages with aircraft models
- ["Bluebell" OBJ8 CSL packages](https://forums.x-plane.org/index.php?/files/file/37041-bluebell-obj8-csl-packages/) recommended including the *vertical offset files* (LiveTraffic includes a change to libxplanemp to process the VERT_OFFSET tag)

## Based on
LiveTraffic is based on a number of other great libraries and APIs, most notably:
- [X-Plane APIs](https://developer.x-plane.com/sdk/plugin-sdk-documents/) to integrate with X-Plane
- [CURL](https://curl.haxx.se/libcurl/) for network protocol support
- [parson](https://github.com/kgabis/parson) as JSON parser
- [libxplanemp](https://github.com/kuroneko/libxplanemp), which is also the basis for XSquawkBox, extended to process the VERT_OFFSET flag (will do Pull Request soon) 
- [libpng](http://www.libpng.org/pub/png/libpng.html)
- [libz/zlib](https://zlib.net)
