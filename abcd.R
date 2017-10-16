# -*- makefile -*-

abcd : [X] GTK COMMON abcd abcd-icon|no-icon
abcd : [G] WINDOWS COMMON abcd abcd.res|noicon.res

abcdsolver : [U] abcd[STANDALONE_SOLVER] STANDALONE
abcdsolver : [C] abcd[STANDALONE_SOLVER] STANDALONE

ALL += abcd[COMBINED]

!begin am gtk
GAMES += abcd
!end

!begin >list.c
    A(abcd) \
!end

!begin >gamedesc.txt
abcd:abcd.exe:ABCD:Letter placement puzzle:Place letters according to the numbers. Identical letters cannot touch.
!end
