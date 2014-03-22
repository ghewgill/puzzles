# -*- makefile -*-

SALAD_EXTRA = latin tree234 maxflow

salad  : [X] GTK COMMON salad SALAD_EXTRA salad-icon|no-icon
salad  : [G] WINDOWS COMMON salad SALAD_EXTRA salad.res|noicon.res

saladsolver : [U] salad[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] tree234 maxflow STANDALONE
saladsolver : [C] salad[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] tree234 maxflow STANDALONE

ALL += salad[COMBINED] SALAD_EXTRA

!begin am gtk
GAMES += salad
!end

!begin >list.c
    A(salad) \
!end

!begin >gamedesc.txt
salad:salad.exe:Salad:Pseudo-Latin square puzzle
!end
