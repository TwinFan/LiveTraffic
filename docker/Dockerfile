# Linux-only compile environment, suitable for compiling X-Plane plugins
FROM ubuntu:18.04

# set up package manager with latest mirrors and certificates
RUN sed -i -e 's/http:\/\/archive.ubuntu.com\/ubuntu\//mirror:\/\/mirrors.ubuntu.com\/mirrors.txt/' /etc/apt/sources.list
RUN apt-get update
RUN apt-get install -y --no-install-recommends ca-certificates

# Install Linux toolchain (GCC) including CMake
RUN apt-get install -y --no-install-recommends build-essential ninja-build sudo bash coreutils curl libcurl4-openssl-dev zlib1g-dev libpng-dev
RUN apt-get clean
RUN curl -sSL https://github.com/Kitware/CMake/releases/download/v3.19.2/cmake-3.19.2-Linux-x86_64.tar.gz | tar -xz -C /usr/local/ --strip-components=1

# Install dependency libraries under Linux.
RUN apt-get install -y --no-install-recommends freeglut3-dev libudev-dev libopenal-dev && apt-get clean

# Add essential users to the docker image
RUN echo "" | adduser --uid 1000 --disabled-password  --gecos "" xpbuild && adduser xpbuild sudo
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

VOLUME /src
USER xpbuild
ADD build.sh /usr/bin/build.sh

# Entrypoint is the build.sh script, which takes care of actual building
WORKDIR /Src
ENTRYPOINT ["build.sh"]
