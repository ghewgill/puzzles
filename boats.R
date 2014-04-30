# -*- makefile -*-

BOATS_EXTRA = dsf

boats : [X] GTK COMMON boats BOATS_EXTRA boats-icon|no-icon
boats : [G] WINDOWS COMMON boats BOATS_EXTRA boats.res|noicon.res

boatssolver : [U] boats[STANDALONE_SOLVER] BOATS_EXTRA STANDALONE
boatssolver : [C] boats[STANDALONE_SOLVER] BOATS_EXTRA STANDALONE

ALL += boats[COMBINED]

!begin am gtk
GAMES += boats
!end

!begin >list.c
    A(boats) \
!end

!begin >gamedesc.txt
boats:boats.exe:Boats:Boat-placing puzzle
!end
