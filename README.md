puzzles-unreleased
==================

This repo contains several unfinished contributions for [Simon Tatham's Portable Puzzle Collection](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/) I've written over the years.

## Contents

Click on a puzzle to read a detailed description.

<table>
<tr>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/abcd.md">ABCD</a></b><br/>Place letters according to the numbers. Identical letters cannot touch.</td>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/ascent.md">Ascent</a></b><br/>Place each number once to create a path.</td>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/boats.md">Boats</a></b><br/>Find the fleet in the grid.</td>
</tr>
<tr>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/abcd.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/abcd.png"></a></td>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/ascent.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/ascent.png"></a></td>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/boats.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/boats.png"></a></td>
</tr>
<tr>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/bricks.md">Bricks</a></b><br/>Shade several cells in the hexagonal grid while making sure each cell has another shaded cell below it.</td>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/clusters.md">Clusters</a></b><br/>Fill in the grid with red and blue clusters, with all dead ends given.</td>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/mathrax.md">Mathrax</a></b><br/>Place each number according to the arithmetic clues.</td>
</tr>
<tr>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/bricks.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/bricks.png"></a></td>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/clusters.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/clusters.png"></a></td>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/mathrax.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/mathrax.png"></a></td>
</tr>
<tr>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/rome.md">Rome</a></b><br/>Fill the grid with arrows leading to a goal.</td>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/salad.md">Salad</a></b><br/>Place each character once in every row and column. Some squares remain empty.</td>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/spokes.md">Spokes</a></b><br/>Connect all hubs using horizontal, vertical and diagonal lines.</td>
</tr>
<tr>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/rome.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/rome.png"></a></td>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/salad.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/salad.png"></a></td>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/spokes.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/spokes.png"></a></td>
</tr>
<tr>
<td align="center" width="236"><b><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/sticks.md">Sticks</a></b><br/>Fill in the grid with horizontal and vertical line segments.</td>
<td></td>
<td></td>
</tr>
<tr>
<td align="center" width="236"><a href="https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/sticks.md"><img src="https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/sticks.png"></a></td>
<td></td>
<td></td>
</tr>
</table>

### Abandoned puzzles

* [Seismic](https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/seismic.md): Place numbers in each area, keeping enough distance between equal numbers.
* [Crossing](https://github.com/x-sheep/puzzles-unreleased/blob/master/docs/crossing.md): Place each number from the list into the crossword.

## Building with CMake

* Get the source code for the SGT Portable Puzzle Collection from [the official site](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/).
* Copy this folder into the above source folder as a subdirectory. Alternatively, add this repository as a submodule of the main repository.
* In the main repository's `CMakeLists.txt`, go to the line with `add_subdirectory(unfinished)` and add the following line below it:
```cmake
add_subdirectory(puzzles-unreleased) # or whatever this folder is called
```
* Run CMake in the main folder.
* Optional: To add icons on Unix, copy all save files from the `savefiles` subfolder to the `icons` folder in the main repository. 

---

More information can be found in the original collection's `README`.

Any bugfixes/contributions/suggestions welcome!

## LICENCE

Copyright (c) 2011-2021 Lennard Sprong

Based on Simon Tatham's Portable Puzzle Collection. [MIT Licence](./LICENCE)
