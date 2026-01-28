# Seismic

![](https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/seismic.png)

You're given a grid that has been divided into areas. Fill each empty cell with a number so each area of size N contains one instance of each number between 1 and N. Depending on the game mode, the following rule is added:

* Seismic: Two equal numbers N in the same row or column must have at least N spaces between them.
* Tectonic: Two equal numbers cannot be horizontally, vertically or diagonally adjacent.

Seismic mode is an implementation of *Hakyuu*, a puzzle invented by [Nikoli](https://www.nikoli.co.jp/). It's also known as *Ripple Effect*. More information: http://www.janko.at/Raetsel/Hakyuu/index.htm

I don't know who first invented Tectonic.

## Controls

Seismic uses the same control scheme as Solo, but the interface automatically enforces the maximum number on each area, so it's not possible to enter numbers that are out of range.

Left-click to select a cell, then type a number on your keyboard to enter it. Press Backspace or Space to clear a cell.

Right-click a cell, then type a number to add a pencil mark. Pencil marks can be used for any purpose.

You can also use the arrow keys to move the selected cell around. Press Enter to toggle between entering number and entering pencil marks.

Press the 'M' key to fill every empty cell with all possible pencil marks.

## Seismic parameters

These parameters are available from the ‘Custom…’ option on the ‘Type’ menu. 

<dl>
	<dt>Width, Height</dt>
	<dd>Size of the grid in squares.</dd>
	<dt>Difficulty</dt>
	<dd>Determine the difficulty of the generated puzzle. Higher difficulties require more complex reasoning.</dd>
	<dt>Game mode</dt>
	<dd>Switch between Seismic and Tectonic mode.</dd>
</dl>

## Status

This puzzle is playable on lower sizes, but has a near-zero chance of generating sizes higher than 7x7. The generator step that creates randomly filled regions needs to be completely replaced with a different approach.
