
CROSSING_EXTRA = dsf tree234

crossing : [X] GTK COMMON crossing CROSSING_EXTRA crossing-icon|no-icon
crossing : [G] WINDOWS COMMON crossing CROSSING_EXTRA crossing.res|noicon.res

crossingsolver : [U] crossing[STANDALONE_SOLVER] CROSSING_EXTRA STANDALONE
crossingsolver : [C] crossing[STANDALONE_SOLVER] CROSSING_EXTRA STANDALONE

ALL += crossing[COMBINED]

!begin am gtk
GAMES += crossing
!end

!begin >list.c
    A(crossing) \
!end

!begin >gamedesc.txt
crossing:crossing.exe:Crossing:Number crossword puzzle:Place each number from the list into the crossword.
!end
