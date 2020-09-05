#!/usr/bin/python3

"""
Restores original files in folders processed by CSL2XSB.py
by looking for *.orig files and replacing them
back to its original name.

Also, it clears away new copies created by XPMP2,
i.e. files ending on .xpmp2.obj
"""

import sys
from pathlib import Path, PurePath

""" === Recursively replaces .orig files === """

def ReplOrig(path: Path) -> int:
    numSubDir = 0
    numLocal = 0
    try:
        # for each file in that path
        for f in path.iterdir():
            # recursively dive into subdirectories
            if f.is_dir():
                numSubDir += ReplOrig(f)
            # process files
            elif f.is_file():
                if f.suffix == '.orig':             # restore .orig
                    f.replace(f.with_suffix(''))
                    numLocal += 1
                elif '.xpmp2.obj' in f.name:        # remove a XPMP2-created copy
                    print ('Removing '+str(f))
                    f.unlink()
                    numLocal += 1

        print (str(path)+': Restored '+str(numLocal)+' files')
        return numLocal+numSubDir

    except IOError as e:
        parser.error('Replacing originals failed for ' + e.filename + ':\n'+ e.strerror +'\n')



""" === MAIN === """

# path is on the command line or just the current path
if len(sys.argv)>1:
    basePath = Path(sys.argv[1])
    if not basePath.exists() or not basePath.is_dir():
        print('Base bath "' + str(basePath) + '" does not exist or is no directory.')
        exit(1)

else:
    basePath = Path.cwd()
    print ('Do you want to restore .orig files in the current directory "'+str(basePath)+'" and recursively deeper?')
    while True:
        UserWantsIt = input ('Answer "y" or "n": ')
        if UserWantsIt.upper() == 'N':
            print ('You answered "N", so we exit without doing anything.')
            exit(1)
        if UserWantsIt.upper() == 'Y':
            break

# do it
numFiles = ReplOrig(basePath)
print ('Restored '+str(numFiles)+' files to their original versions.')
