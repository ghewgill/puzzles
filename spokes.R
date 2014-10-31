# -*- makefile -*-

SPOKES_EXTRA = dsf

spokes : [X] GTK COMMON spokes SPOKES_EXTRA spokes-icon|no-icon
spokes : [G] WINDOWS COMMON spokes SPOKES_EXTRA spokes.res|noicon.res

ALL += spokes[COMBINED]

!begin am gtk
GAMES += spokes
!end

!begin >list.c
    A(spokes) \
!end

!begin >gamedesc.txt
spokes:spokes.exe:Spokes:Wheel-connecting puzzle
!end
