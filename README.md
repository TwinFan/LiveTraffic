# LiveTraffic
LiveTraffic is a plugin for the flight simulator [X-Plane](https://www.x-plane.com) to show real life traffic, based on publicly available live flight data, as additional planes within X-Plane.

It came into being when I was looking at plugins simulating additional traffic in X-Plane and thinking at that time: Why do they try to simulate traffic and invent flight models when there is flight data available online which brings yet another piece of reality into the simulation? Why can't one just read the data and display the aircraft?

Well...turned out it's not *that* easy ;-)

[Please see GitBook](https://twinfan.gitbook.io/livetraffic/) for further documentation on Features, Limitations, Requirements, Installation, Configuration...

## Status
Available on [x-plane.org](https://forums.x-plane.org/index.php?/files/file/49749-livetraffic/).

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

## To use cmake
- CD to repo folder where you see .git
- CD to Src
- mkdir build
- CD build
- cmake ..
- make VERBOSE=1
  
## To use docker
- CD to repo folder where you see .git
- (cd docker; make)

## To use Xcode (macOS only)
This repository contains all the Xcode project files needed to clone into Xcode, and build with practically no configuration required:
- Go to Xcode's splash screen (aka `Welcome to X-Code` window) and click `Clone an Existing Project` on the bottom left.
- Paste this repository's URL into the box (e.g., "`https://github.com/TwinFan/livetraffic`") and click `Clone`.
- On the next screen, choose a folder for the project wisely:
- this project depends on `libxplanemp` which you likely will also want to build from sources;
- you should ensure this folder is ***parallel*** to the `libxplanemp` project directory, i.e., so they share a common root;
- for more explanation, see below: **"Resolving required dependencies"**.
- ... dependencies ...
- ... pick scheme ...
- ... build ...
- ... see output ...

## Resolving required dependencies (libpng, libxplanemp, etc.)
... this section needs to have answers for all platforms and build environments ...
