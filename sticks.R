# -*- makefile -*-

STICKS_EXTRA = dsf

sticks : [X] GTK COMMON sticks STICKS_EXTRA sticks-icon|no-icon
sticks : [G] WINDOWS COMMON sticks STICKS_EXTRA sticks.res|noicon.res

stickssolver : [U] sticks[STANDALONE_SOLVER] STICKS_EXTRA STANDALONE
stickssolver : [C] sticks[STANDALONE_SOLVER] STICKS_EXTRA STANDALONE

ALL += sticks[COMBINED]

!begin am gtk
GAMES += sticks
!end

!begin >list.c
    A(sticks) \
!end

!begin >gamedesc.txt
sticks:sticks.exe:Sticks:Line-drawing puzzle:Fill in the grid with horizontal and vertical line segments.
!end
