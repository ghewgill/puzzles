# -*- makefile -*-

ascent : [X] GTK COMMON ascent ascent-icon|no-icon
ascent : [G] WINDOWS COMMON ascent ascent.res|noicon.res

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
