# -*- makefile -*-

SALAD_EXTRA        = LATIN
SALAD_EXTRA_SOLVER = LATIN_SOLVER

salad  : [X] GTK COMMON salad SALAD_EXTRA salad-icon|no-icon
salad  : [G] WINDOWS COMMON salad SALAD_EXTRA salad.res|noicon.res

saladsolver : [U] salad[STANDALONE_SOLVER] SALAD_EXTRA_SOLVER STANDALONE
saladsolver : [C] salad[STANDALONE_SOLVER] SALAD_EXTRA_SOLVER STANDALONE

ALL += salad[COMBINED] SALAD_EXTRA

!begin am gtk
GAMES += salad
!end

!begin >list.c
    A(salad) \
!end

!begin >gamedesc.txt
salad:salad.exe:Salad:Pseudo-Latin square puzzle:Place each character once in every row and column. Some squares remain empty.
!end
