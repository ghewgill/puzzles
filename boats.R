# -*- makefile -*-

boats : [X] GTK COMMON boats boats-icon|no-icon
boats : [G] WINDOWS COMMON boats boats.res|noicon.res

boatssolver : [U] boats[STANDALONE_SOLVER] STANDALONE
boatssolver : [C] boats[STANDALONE_SOLVER] STANDALONE

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
