# -*- makefile -*-

MAP      = map dsf

map      : [X] GTK COMMON MAP

map      : [G] WINDOWS COMMON MAP

mapsolver :     [U] map[STANDALONE_SOLVER] dsf STANDALONE m.lib
mapsolver :     [C] map[STANDALONE_SOLVER] dsf STANDALONE

ALL += MAP

!begin gtk
GAMES += map
!end

!begin >list.c
    A(map) \
!end