# -*- makefile -*-

SEISMIC_EXTRA = dsf

seismic : [X] GTK COMMON seismic SEISMIC_EXTRA seismic-icon|no-icon
seismic : [G] WINDOWS COMMON seismic SEISMIC_EXTRA seismic.res|noicon.res

seismicsolver : [U] seismic[STANDALONE_SOLVER] SEISMIC_EXTRA STANDALONE
seismicsolver : [C] seismic[STANDALONE_SOLVER] SEISMIC_EXTRA STANDALONE

ALL += seismic[COMBINED]

!begin am gtk
GAMES += seismic
!end

!begin >list.c
    A(seismic) \
!end

!begin >gamedesc.txt
seismic:seismic.exe:Seismic:Number placement puzzle
!end
