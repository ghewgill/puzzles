# Mathrax

![](https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/mathrax.png)

You have a square grid; each square may contain a digit from 1 to the size of the grid, and some squares have clues overlapping them. Your objective is to fill the grid with numbers so that no number appears more than once in a row or column, and all clues are satisfied.

The following clues can appear:

* An 'E' indicates that the four adjacent digits are even.
* An 'O' indicates that the four adjacent digits are odd.
* An '=' indicates that diagonally adjacent digits are equal.
* A number indicates the result of the given operation when applied to each pair of diagonally adjacent digits. (topleft * bottomright) = (topright * bottomleft)

The inventor of Mathrax is unknown. 

More information: https://www.janko.at/Raetsel/Mathrax/index.htm

## Controls

Mathrax uses the same control scheme as Solo.

Left-click to select a cell, then type a number on your keyboard to enter it. Press Backspace or Space to clear a cell.

Right-click a cell, then type a number to add a pencil mark. Pencil marks can be used for any purpose.

You can also use the arrow keys to move the selected cell around. Press Enter to toggle between entering number and entering pencil marks.

Press the 'M' key to fill every empty cell with all possible pencil marks.

## Mathrax parameters

These parameters are available from the ‘Custom…’ option on the ‘Type’ menu. 

<dl>
	<dt>Size (s*s)</dt>
	<dd>Size of the grid in squares.</dd>
	<dt>Difficulty</dt>
	<dd>Determine the difficulty of the generated puzzle. Higher difficulties require more complex reasoning.</dd>
	<dt>Addition clues</dt>
	<dd>Allows clues with the addition operation to appear.</dd>
	<dt>Subtraction clues</dt>
	<dd>Allows clues with the subtraction operation to appear. Note that clues with a difference of zero are covered by Equality clues instead.</dd>
	<dt>Multiplication clues</dt>
	<dd>Allows clues with the multiplication operation to appear.</dd>
	<dt>Division clues</dt>
	<dd>Allows clues with the division operation to appear. Note that clues with a ratio of one are covered by Equality clues instead.</dd>
	<dt>Equality clues</dt>
	<dd>Allows clues with equality signs to appear.</dd>
	<dt>Even/odd clues</dt>
	<dd>Allows Even clues and Odd clues to appear.</dd>
</dl>

## Status

This game is fully implemented and playable.

I haven't properly tested the Recursive difficulty level. It's possible that it works exactly the same as Hard mode.
