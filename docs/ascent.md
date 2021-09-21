# Ascent

![](https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/ascent.png)

You're given a grid, with several numbers inside. Your objective is to place each number exactly once, so a path is formed from the lowest number (i.e. 1) to the highest number. Two numbers that are in sequence must be horizontally, vertically or diagonally adjacent.

The puzzle can be played on a rectangular or hexagonal grid. It's also possible to play on a rectangular grid while not allowing the path to move diagonally.

In the alternate game mode 'Edges', the grid is surrounded by numbers placed inside arrows. An arrow points to the row, column or diagonal where this number appears in the path.

This puzzle is invented by Gyora Benedek, and is known as *Hidato* (or the non-trademarked name *Hidoku*). Edges mode is an implementation of *1to25* invented by Jeff Widderich.

More information: http://www.janko.at/Raetsel/Hidoku/index.htm

## Controls

There are three ways to enter a number:

1. Click a number to highlight it, then click (or drag to) an adjacent cell to place the next number in the sequence. The arrow keys and Enter can be used to emulate mouse clicks.

2. Click an empty cell, then type a multi-digit number. To confirm a number, either press Enter, an arrow key, or click any cell.

3. In Edges mode, click and drag from an edge number, then release in an empty grid cell in the same row, column or diagonal.

To remove numbers, right-click or right-drag a number.

It's also possible to draw a path while the numbers inside the path are still unknown. Left-click and drag across cells to draw a line. Right-click or right-drag to clear the line going through a cell.

If a path has only a single number, the endpoints will display one or two smaller numbers, which represent the numbers which are valid for this cell.

## Ascent parameters

These parameters are available from the ‘Custom…’ option on the ‘Type’ menu. 

<dl>
	<dt>Width, Height</dt>
	<dd>Size of the grid in squares.</dd>
	<dt>Always show start and end points</dt>
	<dd>When enabled, the first and last number are always given. Disable this option for an added challenge.</dd>
	<dt>Symmetrical clues</dt>
	<dd>When enabled, all given numbers form a symmetric pattern. This usually leads to easier puzzles.</dd>
	<dt>Grid type</dt>
	<dd>Choose between 'Rectangle', 'Rectangle (no diagonals)', 'Hexagon', 'Honeycomb' and 'Edges' mode.</dd>
	<dt>Difficulty</dt>
	<dd>Determine the difficulty of the generated puzzle. Higher difficulties require more complex reasoning.</dd>
</dl>

## Status

This puzzle is fully implemented and playable.
