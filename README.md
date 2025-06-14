# LiveTraffic

[![Build all Platforms](https://github.com/TwinFan/LiveTraffic/actions/workflows/build.yml/badge.svg)](https://github.com/TwinFan/LiveTraffic/actions/workflows/build.yml)

LiveTraffic is a plugin for the flight simulator [X-Plane](https://www.x-plane.com) to show real life traffic, based on publicly available live flight data, as additional planes within X-Plane.

It came into being when I was looking at plugins simulating additional traffic in X-Plane and thinking at that time: Why do they try to simulate traffic and invent flight models when there is flight data available online which brings yet another piece of reality into the simulation? Why can't one just read the data and display the aircraft?

Well...turned out it's not *that* easy ;-)

[Please see GitBook](https://twinfan.gitbook.io/livetraffic/) for further documentation on Features, Limitations, Requirements, Installation, Configuration...

## Download
Available on [x-plane.org](https://forums.x-plane.org/index.php?/files/file/49749-livetraffic/).

## License
[MIT License](https://github.com/TwinFan/LiveTraffic/blob/master/LICENSE), essentially freeware.

For license information on included code and libraries see `docs/LICENSE_*.txt` files.

## Credits
LiveTraffic is based on a number of other great libraries and APIs, most notably:
- [X-Plane SDK](https://developer.x-plane.com/sdk/plugin-sdk-documents/) to integrate with X-Plane
- [XPMP2](https://github.com/TwinFan/XPMP2) for CSL model processing
- [CURL](https://curl.haxx.se/libcurl/) for network protocol support
- [FMOD](https://www.fmod.com/) Audio Engine: FMOD Core API by Firelight Technologies Pty Ltd.
- [parson](https://github.com/kgabis/parson) as JSON parser
- [metaf](https://github.com/nnaumenko/metaf) as METAR parser
- [libz/zlib](https://zlib.net) as compression library (used by CURL)
- [ImGui](https://github.com/ocornut/imgui) for user interfaces
- [ImgWindow](https://github.com/xsquawkbox/xsb_public) for integrating ImGui into X-Plane windows
- [portable-file-dialogs](https://github.com/samhocevar/portable-file-dialogs) for file dialogs used in settings
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

Please note that LiveTraffic includes XPMP2, parson, and metaf libs as a GitHub submodules.
To properly build, you need to also checkout the submodules,
e.g. on the command line by doing
```
git clone --recurse-submodules https://github.com/TwinFan/LiveTraffic
```

There are four options to build from sources:

Options            | Windows            | MacOS (universal)   | Linux
-------------------|--------------------|---------------------|-------------------
**Github Actions** | Visual Studio 2022 | XCode 13            | Focal
**Docker**         | Mingw64            | clang, SDK 12       | Focal and Bionic
**CMake**          | VS 2022 / `NMAKE`  | XCode 14 / `ninja`  | Focal and Bionic / `ninja`
**IDE**            | Visual Studio 2019 | XCode 14            | -

### Github Actions

LiveTraffic builds on Github, see
[`.github/workflows/build.yml`](https://github.com/TwinFan/LiveTraffic/blob/master/.github/workflows/build.yml).

### Docker Cross Compile Environment

Locally, LiveTraffic can be build for all platforms using the Docker cross compile environment
[`twinfan/focal-win-mac-lin-compile-env`](https://hub.docker.com/r/twinfan/focal-win-mac-lin-compile-env).
Tested on Mac as a host, should work the same way on Linux.

- Install [Docker Desktop](https://www.docker.com/products/docker-desktop) and start it.
- `cd docker`
- `make`

In the first run only, it will download the necessary Docker image.
The actual build takes only a few seconds. Results are written to `build-*/*_x64` folders.

For more background info also see [`docker/README.md`](https://github.com/TwinFan/LiveTraffic/blob/master/docker/README.md).

The `Makefile` also builds the `doc` target, ie. the Doxygen documentation.
That will only work on a Mac. Otherwise, you may want to remove `doc` from `all`.

### CMake

Given a proper local setup with a suitable compile, CMake, and Ninja installed,
you can just locally build the sources from the `CMakeList.txt` file,
e.g. like this:

```
mkdir build
cd build
cmake -G Ninja ..
ninja
```

This is precicely how the Mac and Linux builds are done in Github Actions.

### IDE

#### Mac OS / Xcode

MacOS is the primary development platform. Open `LiveTraffic.xcodeproject`
with Xcode. In the project's "Build Settings" you find one User-Defined setting at the very end,
that will require changing: `XPLANE11_ROOT` defines the root path to your X-Plane installation. 
The build process will install the plugin also into `$(XPLANE11_ROOT)/Resources/plugins/$(PROJECT)`,
so that it is right away available in your installation after build.

#### Windows / Visual Studio

- Install [Visual Studio](https://visualstudio.microsoft.com/vs/community/)
- Open the project folder, typically `LifeTraffic`, using File > Open > Folder...
- Visual Studio will use the provide CMake setup
- Build from within Visual Studio

Results are in `build-win`.

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
