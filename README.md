# LiveTraffic
LiveTraffic is a plugin for the flight simulator [X-Plane](https://www.x-plane.com) to show real life traffic, based on publicly available live flight data, as additional planes within X-Plane.

It came into being when I was looking at plugins simulating additional traffic in X-Plane and thinking at that time: Why do they try to simulate traffic and invent flight models when there is flight data available online which brings yet another piece of reality into the simulation? Why can't one just read the data and display the aircraft?

Well...turned out it's not *that* easy ;-)

[Please see GitBook](https://twinfan.gitbook.io/livetraffic/) for further documentation on Features, Limitations, Requirements, Installation, Configuration...

## Status
Currently in beta testing, available to selected testers on Mac and Windows.
Not yet available for download.
Not yet available on [x-plane.org](https://forums.x-plane.org/index.php?/files/).

## License
[MIT License](https://github.com/TwinFan/LiveTraffic/blob/master/LICENSE), essentially freeware.

## Credits
LiveTraffic is based on a number of other great libraries and APIs, most notably:
- [X-Plane APIs](https://developer.x-plane.com/sdk/plugin-sdk-documents/) to integrate with X-Plane
- [CURL](https://curl.haxx.se/libcurl/) for network protocol support
- [parson](https://github.com/kgabis/parson) as JSON parser
- [libxplanemp](https://github.com/kuroneko/libxplanemp), which is also the basis for XSquawkBox, extended to process the VERT_OFFSET flag (will do Pull Request soon) 
- [libpng](http://www.libpng.org/pub/png/libpng.html)
- [libz/zlib](https://zlib.net)

## Changes to build for Linux
