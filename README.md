# LiveTraffic

[![Build all Platforms](https://github.com/TwinFan/LiveTraffic/actions/workflows/build.yml/badge.svg)](https://github.com/TwinFan/LiveTraffic/actions/workflows/build.yml)

LiveTraffic is a plugin for the flight simulator [X-Plane](https://www.x-plane.com) to show real life traffic, based on publicly available live flight data, as additional planes within X-Plane.

It came into being when I was looking at plugins simulating additional traffic in X-Plane and thinking at that time: Why do they try to simulate traffic and invent flight models when there is flight data available online which brings yet another piece of reality into the simulation? Why can't one just read the data and display the aircraft?

Well...turned out it's not *that* easy ;-)

[Please see GitBook](https://twinfan.gitbook.io/livetraffic/) for further documentation on Features, Limitations, Requirements, Installation, Configuration...

## Status
Available on [x-plane.org](https://forums.x-plane.org/index.php?/files/file/49749-livetraffic/).

## License
[MIT License](https://github.com/TwinFan/LiveTraffic/blob/master/LICENSE), essentially freeware.

For license information on included code and libraries see `docs/LICENSE_*.txt` files.

## Credits
LiveTraffic is based on a number of other great libraries and APIs, most notably:
- [X-Plane SDK](https://developer.x-plane.com/sdk/plugin-sdk-documents/) to integrate with X-Plane
- [XPMP2](https://github.com/TwinFan/XPMP2) for CSL model processing
- [CURL](https://curl.haxx.se/libcurl/) for network protocol support
- [parson](https://github.com/kgabis/parson) as JSON parser
- [libz/zlib](https://zlib.net) as compression library (used by CURL)
- [ImGui](https://github.com/ocornut/imgui) for user interfaces
- [ImgWindow](https://github.com/xsquawkbox/xsb_public) for integrating ImGui into X-Plane windows
- [IconFontCppHeaders](https://github.com/juliettef/IconFontCppHeaders) for header files for the included icon font

Thanks go to
- Sparker for providing [imgui4xp](https://github.com/sparker256/imgui4xp)
  as a testbed for ImGui integration and for accepting my additions to it.
- Crbascott for compiling and providing the `model_typecode.txt` file.
- Dimitri van Heesch for [Doxygen](https://www.doxygen.nl/), with which more and more
  parts of LiveTraffic's (and all of XPMP2's) code documentation have been created. 
- [FontAwesome](https://fontawesome.com/icons?d=gallery&s=solid&m=free)
  for the icon font `fa-solid-900.ttf`

## Build

### Mac OS

This is the primary development platform and tested best. Open `LiveTraffic.xcodeproject`
with XCode. In the project's "Build Settings" you find one User-Defined setting at the very end,
that will require changing: `XPLANE11_ROOT` defines the root path to your X-Plane installation. 
The build process will install the plugin also into `$(XPLANE11_ROOT)/Resources/plugins/$(PROJECT)`,
so that it is right away available in your installation after build.

### Windows

Windows cannot build in the Docker environment as mingw's libraries aren't up to C++ 17 standards.
- Install Visual Studio](https://visualstudio.microsoft.com/vs/community/)
- Open the solution `LifeTraffic.sln`
- Build from within Visual Studi

Results are in `build/x64`.

### Linux (via Docker)

I don't have a Linux build or test environment. The project does provide a Docker environment,
though, with which I build the Linux binaries on a Mac OS machine. Theoretically,
this should also work on a Windows machine, but I never tried. Feedback welcome.

- Install [Docker Desktop](https://www.docker.com/products/docker-desktop) and start it.
- `cd` to the project's `docker` folder
- `make`

In the first run only, it will create the necessary Docker image based on Ubuntu 18.04,
which includes downloading lots of base images and packages and takes a couple
of minutes. This is required once only.

The actual build takes only a few seconds. Results are written to `build-lin`.

### Doxygen Documentation

Newer files come with Doxygen-style documentation. All file headers are updated already
so that the file listing in the resulting Doxygen documentation should be appropriate.
But many older, while having many explanatory comment, are yet missing
proper Doygen-style format.

To build the Doxygen documentation
- install [Doxgen](http://www.doxygen.nl/download.html)
- `cd` into the project's main directory
- run `doxygen docs/LiveTraffic.doxygen`, e.g. on a Mac run
`/Applications/Doxygen.app/Contents/Resources/doxygen docs/LiveTraffic.doxygen`

The resulting documentation is written to `docs/html`, open `docs/html/index.html`.
