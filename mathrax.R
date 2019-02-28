# -*- makefile -*-

MATHRAX_EXTRA = LATIN

mathrax : [X] GTK COMMON mathrax MATHRAX_EXTRA mathrax-icon|no-icon
mathrax : [G] WINDOWS COMMON mathrax MATHRAX_EXTRA mathrax.res|noicon.res

ALL += mathrax[COMBINED] MATHRAX_EXTRA

!begin am gtk
GAMES += mathrax
!end

!begin >list.c
    A(mathrax) \
!end

!begin >gamedesc.txt
mathrax:mathrax.exe:Mathrax:Latin square puzzle:Place each number according to the arithmetic clues.
!end
