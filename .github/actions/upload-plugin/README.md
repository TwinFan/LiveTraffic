# upload-plugin

This is a custom GitHub action to upload a just built X-Plane plugin to Github's artifacts for later deployment or download.

## Parameters

Parameter|Requied|Default|Description
---------|-------|-------|------------
`pluginName`|yes||Plugin's name, used both as top-level folder name and as file name as required by X-Plane
`archFolder`|yes||Subfolder in which the executable is placed, is based on architecture like 'lin_x64'
`xplFileName`|yes||Path to the just built xpl file

## What it does

- Organizes the produced plugin in the correct folder structure for deployment
- and adds it to artifacts.