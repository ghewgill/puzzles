# Salad

![](https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/salad.png)

You have a square grid; each square may contain a character from A (or 1) to a given maximum. Your objective is to put characters in some of the squares, so each character appears exactly once in each row and column.

The rules vary depending on the game mode:

* ABC End View mode: Letters on the edge show which letter appears first when 'looking' into the grid.
* Number Ball mode: Squares with a ball must contain a number. Squares with a cross must remain empty.

Number Ball was invented by Inaba Naoki. The original puzzles are available here: http://www.janko.at/Raetsel/Nanbaboru/index.htm

I don't know who first designed ABC End View.

## Controls

Salad uses a control scheme similar to Solo, but with the ability to mark empty squares.

Left-click to select a cell, then type a letter or number on your keyboard to enter it. Press Backspace or Space to clear a cell.

Press 'X' to mark a cell as empty, or press 'O' to mark a cell as "definitely not empty".

Right-click a cell, then type a letter or number to add a pencil mark. Pencil marks can be used for any purpose. The letter 'X' can also be used to indicate a cell that might be empty.

You can also use the arrow keys to move the selected cell around. Press Enter to toggle between entering letters/numbers and entering pencil marks.

Press the 'M' key to fill every empty cell with all possible pencil marks.

## Salad parameters

These parameters are available from the ‘Custom…’ option on the ‘Type’ menu. 

<dl>
	<dt>Game mode</dt>
	<dd>Switch between ABC End View and Number Ball mode.</dd>
	<dt>Size (s*s)</dt>
	<dd>Size of the grid in squares.</dd>
	<dt>Symbols</dt>
	<dd>The amount of different symbols that appear in each row.</dd>
	<dt>Difficulty</dt>
	<dd>Determine the difficulty of the generated puzzle.</dd>
</dl>

## Status

This puzzle is playable.

The system for pseudo-latin squares is currently fairly messy, and doesn't allow for more complex solver techniques. This puzzle would greatly benefit from upstream support for latin squares where a symbol (specifically, the empty square) can appear more than once per row. This would allow for more puzzle types in the future to reuse a great deal of code.

The Number Ball generator currently doesn't create puzzles that make good use of the concept, in my opinion.
