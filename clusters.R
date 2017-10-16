# -*- makefile -*-

clusters : [X] GTK COMMON clusters clusters-icon|no-icon
clusters : [G] WINDOWS COMMON clusters clusters.res|noicon.res

ALL += clusters[COMBINED]

!begin am gtk
GAMES += clusters
!end

!begin >list.c
    A(clusters) \
!end

!begin >gamedesc.txt
clusters:clusters.exe:Clusters:Red and blue grid puzzle:Fill in the grid with red and blue clusters, with all dead ends given.
!end
