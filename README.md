# LiveTraffic
LiveTraffic is (going to be) a plugin for the flight simulator [X-Plane](https://www.x-plane.com) to show real life traffic, based on publicly available live flight data, as additional planes within X-Plane.

## Channels
LiveTraffic can read from the following sources, called channels:
- [ADS-B Exchange](https://www.adsbexchange.com), both
    - [online live data](https://www.adsbexchange.com/data) and
    - historical files (see section 'Historical Data' [here](https://www.adsbexchange.com/data/), especially the requirements!)
- [OpenSky Network](https://opensky-network.org), using its [REST API](https://opensky-network.org/apidoc/index.html)
- [Flightradar24](https://www.flightradar24.com)

LiveTraffic can even read and combine several channels at the same time. Currently, however, results of combined data is often unsatisfactory: Aircrafts tend to jump around.

I have not yet fully confirmed that I may use these sources the way LiveTraffic currently does. Especially Flightradar24 is a bit doubtful. So changes to this list may happen.

## Status
Currently in final stages of development.
Not yet available for download.
Not yet available on [x-plane.org](https://forums.x-plane.org/index.php?/files/).

## License
[MIT License](https://github.com/TwinFan/LiveTraffic/blob/master/LICENSE), essentially freeware.

## More Information
For further information see the [LiveTraffic Page](https://twinfan.github.io/LiveTraffic/).
