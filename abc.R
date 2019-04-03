# -*- makefile -*-

#ABC_LATIN_EXTRA is for NEW_GAME_DESC_VERSION 2

ABC_LATIN_EXTRA = tree234 maxflow dsf
ABC_EXTRA = latin ABC_LATIN_EXTRA

abc : [X] GTK COMMON abc ABC_EXTRA abc-icon|no-icon

abc : [G] WINDOWS COMMON abc ABC_EXTRA abc.res|noicon.res

abcsolver :    [U] abc[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] ABC_LATIN_EXTRA STANDALONE
abcsolver :    [C] abc[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] ABC_LATIN_EXTRA STANDALONE

ALL += abc[COMBINED] ABC_EXTRA

!begin am gtk
GAMES += abc
!end

!begin >list.c
    A(abc) \
!end

!begin >gamedesc.txt
abc:abc.exe:ABC:Letter-placing puzzle:Place A, B or C (one per row, column) depending on letters on the board's edge.
!end
