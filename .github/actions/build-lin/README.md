# build-lin

This is a custom GitHub action to build an X-Plane plugin on Linux based on a prepared CMake setup.

## Parameters

Parameter|Requied|Default|Description
---------|-------|-------|------------
`pluginName`|yes||Plugin's name, used both as top-level folder name and as file name as required by X-Plane
`archFolder`|yes|`lin_x64`|Subfolder in which the executable is placed, is based on architecture like 'lin_x64'

## What it does

- Installs Ninja and OpenGL libs
- Creates build folder `build-lin`
- There, runs `cmake`, then `ninja` to build

## Outputs

Output|Description
------|-----------
`xpl-file-name`|path to the produced xpl file
