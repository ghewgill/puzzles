# -*- makefile -*-

ASCENT_EXTRA = matching

ascent : [X] GTK COMMON ascent ASCENT_EXTRA ascent-icon|no-icon
ascent : [G] WINDOWS COMMON ascent ASCENT_EXTRA ascent.res|noicon.res
ascentsolver : [U] ascent[STANDALONE_SOLVER] ASCENT_EXTRA STANDALONE
ascentsolver : [C] ascent[STANDALONE_SOLVER] ASCENT_EXTRA STANDALONE

ALL += ascent[COMBINED]

!begin am gtk
GAMES += ascent
!end

!begin >list.c
    A(ascent) \
!end

!begin >gamedesc.txt
ascent:ascent.exe:Ascent:Path-finding puzzle:Place each number once to create a path.
!end
