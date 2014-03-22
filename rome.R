# -*- makefile -*-

ROME_EXTRA = dsf

rome : [X] GTK COMMON rome ROME_EXTRA rome-icon|no-icon
rome : [G] WINDOWS COMMON rome ROME_EXTRA rome.res|noicon.res

#romesolver : [U] rome[STANDALONE_SOLVER] STANDALONE
#romesolver : [C] rome[STANDALONE_SOLVER] STANDALONE

ALL += rome[COMBINED]

!begin am gtk
GAMES += rome
!end

!begin >list.c
    A(rome) \
!end

!begin >gamedesc.txt
rome:rome.exe:Rome:Arrow-placing puzzle
!end
