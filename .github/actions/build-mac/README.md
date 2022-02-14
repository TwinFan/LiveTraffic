# build-mac

This is a custom GitHub action to build an X-Plane plugin on and for MacOS based on a prepared CMake setup.

## Inputs

Parameter|Requied|Default|Description
---------|-------|-------|------------
`pluginName`|yes||Plugin's name, used both as top-level folder name and as file name as required by X-Plane
`archFolder`|yes|`mac_x64`|Subfolder in which the executable is placed, is based on architecture like 'mac_x64'

## What it does

- Installs Ninja
- Creates build folder `build-mac`
- There, runs `cmake`, then `ninja` to build

## Outputs

Output|Description
------|-----------
`xpl-file-name`|path to the produced xpl file
