set(abcd_crop 420x420 192x192+108+108)
set(ascent_crop 420x420 210x210+75+15)
set(boats_crop 420x420 128x128+188+22)
set(crossing_crop 504x504 180x180+167+139)
set(rome_crop 420x420 180x180+90+30)
set(salad_crop 420x420 180x180+120+0)
set(seismic_crop 420x420 180x180+90+30)
set(spokes_crop 360x360 180x180+120+180)

puzzle(abcd
  DISPLAYNAME "ABCD"
  DESCRIPTION "Letter placement puzzle"
  OBJECTIVE "Place letters according to the numbers. Identical letters cannot touch.")
solver(abcd)

puzzle(ascent
  DISPLAYNAME "Ascent"
  DESCRIPTION "Path-finding puzzle"
  OBJECTIVE "Place each number once to create a path.")
solver(ascent ${CMAKE_SOURCE_DIR}/matching.c)

puzzle(boats
  DISPLAYNAME "Boats"
  DESCRIPTION "Boat-placing puzzle"
  OBJECTIVE "Find the fleet in the grid.")
solver(boats ${CMAKE_SOURCE_DIR}/dsf.c)

puzzle(bricks
  DISPLAYNAME "Bricks"
  DESCRIPTION "Hexagonal shading puzzle"
  OBJECTIVE "Shade several cells in the hexagonal grid while making sure each cell has another shaded cell below it.")
solver(bricks)

puzzle(clusters
  DISPLAYNAME "Clusters"
  DESCRIPTION "Red and blue grid puzzle"
  OBJECTIVE "Fill in the grid with red and blue clusters, with all dead ends given.")

puzzle(crossing
  DISPLAYNAME "Crossing"
  DESCRIPTION "Number crossword puzzle"
  OBJECTIVE "Place each number from the list into the crossword.")
solver(crossing ${CMAKE_SOURCE_DIR}/dsf.c)

puzzle(mathrax
  DISPLAYNAME "Mathrax"
  DESCRIPTION "Latin square puzzle"
  OBJECTIVE "Place each number according to the arithmetic clues.")

puzzle(rome
  DISPLAYNAME "Rome"
  DESCRIPTION "Arrow-placing puzzle"
  OBJECTIVE "Fill the grid with arrows leading to a goal.")

puzzle(salad
  DISPLAYNAME "Salad"
  DESCRIPTION "Pseudo-Latin square puzzle"
  OBJECTIVE "Place each character once in every row and column. Some squares remain empty.")
solver(salad ${CMAKE_SOURCE_DIR}/latin.c)

puzzle(seismic
  DISPLAYNAME "Seismic"
  DESCRIPTION "Number placement puzzle"
  OBJECTIVE "Place numbers in each area, keeping enough distance between equal numbers.")
solver(seismic ${CMAKE_SOURCE_DIR}/dsf.c)

puzzle(spokes
  DISPLAYNAME "Spokes"
  DESCRIPTION "Wheel-connecting puzzle"
  OBJECTIVE "Connect all hubs using horizontal, vertical and diagonal lines.")
solver(spokes ${CMAKE_SOURCE_DIR}/dsf.c)

puzzle(sticks
  DISPLAYNAME "Sticks"
  DESCRIPTION "Line-drawing puzzle"
  OBJECTIVE "Fill in the grid with horizontal and vertical line segments.")
solver(sticks ${CMAKE_SOURCE_DIR}/dsf.c)

export_variables_to_parent_scope()
