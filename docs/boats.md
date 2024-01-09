# Boats

![](https://raw.githubusercontent.com/x-sheep/puzzles-unreleased/master/docs/img/boats.png)

You're given a grid, and a list of boats which must be located inside that grid. Boats can be rotated. Two boats cannot be horizontally, vertically or diagonally adjacent.

The numbers on the side indicate the amount of cells inside that row or column which are occupied by a boat.

Some boat segments are given (corner pieces, centers, or single-length boats), along with their orientation.

At the bottom of the puzzle is a list of every boat that must be placed in the grid. When a boat is found, it is automatically crossed off of this list. Make sure a boat is surrounded by water on all sides, to indicate that it cannot possibly grow any further.

This puzzle is best known as *Battleships*.

More information: https://www.janko.at/Raetsel/Battleships/index.htm

## Controls

Left-click to place a boat segment in the grid. Unknown boat segments are represented by a small rectangle, and will automatically change into the correct shape when the surrounding cells are filled in.

Right-click to place water, to indicate that a boat cannot be placed here.

To play with a keyboard, use the arrow keys to move the cursor. Press Enter to place a boat segment, and press Space to place water.

## Boats parameters

These parameters are available from the ‘Custom…’ option on the ‘Type’ menu. 

<dl>
	<dt>Width, Height</dt>
	<dd>Size of the grid in squares.</dd>
	<dt>Fleet size</dt>
	<dd>The size of the largest possible boat.</dd>
	<dt>Fleet configuration</dt>
	<dd>Customize the fleet by entering a list of numbers. Each number indicates how many times a boat of a specific size appears. For example, the configuration <code>3,2,1</code> represents 3 boats of size 1, 2 boats of size 2, and 1 boat of size 3.</dd>
	<dt>Difficulty</dt>
	<dd>Determine the difficulty of the generated puzzle. Higher difficulties require more complex reasoning.</dd>
	<dt>Remove numbers</dt>
	<dd>When enabled, the difficulty is increased by hiding certain number clues.</dd>
</dl>

## Status

This puzzle is playable. The solver cannot currently handle some of the harder Battleship puzzles out there.
