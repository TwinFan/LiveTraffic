# sign-notarize

This is a custom GitHub action to sign and then notarize a MacOS X-Plane plugin.
It waits for Apple's answer to notarization, which can take a couple of minutes!

## Inputs

Parameter|Requied|Default|Description
---------|-------|-------|------------
`bundleId`|yes||Plugin's bundle id, something like 'Author.plugin.NameOfPlugin
`xplFileName`|yes||Path to just built xpl plugin
`certificate`|yes||Base64 encoded .p12 certificate file
`certPwd`|yes||Password of the .p12 certificate file
`notarizeUser`|yes||Username/EMail for notarization
`notarizeAppPwd`|yes||App-specific password for notarization

## What it does

All actions are performed by script `sign-notarize`, which
- Creates a temporary keychain to store the passed-in certificate
- Signs the file provided in `xplFileName`
- Zips the file and sends it to Apple's notarization service
- Waits for notarization to finish, which can take a few minutes
