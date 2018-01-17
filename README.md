puzzles-unreleased
==================

This repo contains several unfinished contributions for [Simon Tatham's Portable Puzzle Collection](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/) I've written over the years.

## Contents

Each puzzle source file contains a TODO list.

### Playable and fully commented

* ABCD (based on [ABC Combi](http://www.janko.at/Raetsel/AbcKombi/index.htm)): Place letters in the grid according to the numbers in the border. Identical letters cannot touch.
* Boats (based on [Battleship](http://www.janko.at/Raetsel/Battleships/index.htm)): Place the entire fleet into the grid according to the numbers on the side.
* [Rome](http://www.janko.at/Raetsel/Nikoli/Roma.htm): Place arrows in the grid, so all arrows lead to a goal.

### Playable

* Ascent (based on [Hidoku](http://www.janko.at/Raetsel/Hidoku/index.htm)): Create a path of numbers, using each number exactly once.
* [Clusters](http://www.inabapuzzle.com/honkaku/kura.html): Fill in the grid with red and blue clusters. Tiles with a dot are adjacent to exactly 1 other tile of the same color. All other tiles are adjacent to at least 2 tiles of the same color.
* Salad: Place each character once in every row and column. Some squares remain empty. Contains two modes.
   * [ABC End View](http://www.janko.at/Raetsel/AbcEndView/index.htm) mode: Letters on the side show which letter appears first when 'looking' into the grid.
   * [Number Ball](http://www.janko.at/Raetsel/Nanbaboru/index.htm) mode: Squares with a ball must contain a number.
* [Spokes](http://puzzlepicnic.com/genre?id=12): Connect all hubs using horizontal, vertical and diagonal lines.
   
### Abandoned

* Seismic (based on [Hakyuu](http://www.janko.at/Raetsel/Hakyuu/index.htm)): Place numbers in each area, keeping enough distance between equal numbers. Contains two modes.
   * Seismic: Two equal numbers N in the same row or column must have at least N spaces between them.
   * Tectonic: Two equal numbers cannot be horizontally, vertically or diagonally adjacent. 
* Crossing (based on [Nansuke](http://www.nikoli.co.jp/en/puzzles/number_skeleton.html)): Place each number from the list into the crossword.

## Building

Get the source code for the SGT Portable Puzzle Collection from [the official site](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/). Copy these source files into the folder, run `mkfiles.pl`, then use one of the generated makefiles.

More information can be found in the original collection's `README`.

Any bugfixes/contributions/suggestions welcome!

## LICENCE

Copyright (c) 2011-2018 Lennard Sprong

Based on Simon Tatham's Portable Puzzle Collection. [MIT Licence](./LICENCE)
