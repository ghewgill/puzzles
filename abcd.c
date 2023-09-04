/*
 * abcd.c: Implementation for ABCD Puzzles.
 * (C) 2011 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * More information about the puzzle type:
 * http://wiki.logic-masters.de/index.php?title=ABCD_Puzzle/en
 * http://www.janko.at/Raetsel/AbcKombi/index.htm
 *
 * Objective of the game: Place one letter in each square. The numbers indicate
 * the amount of letters in each row and column.
 * Identical letters may not touch each other.
 */

/*
 * TODO:
 *
 * - Get large puzzles to have a lower fail ratio.
 *   I haven't currently been able to produce a valid 10x10n4 puzzle,
 *   and a 9x9n4 puzzle can take _tens of thousands_ of attempts.
 *   + Force a 0 on a row/column for a letter, and a high number on
 *     a column/row?
 *   + Or maybe introduce immutable letters, after a certain amount
 *     of attempts...
 *
 * - Solver techniques for diagonal mode?
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#ifdef STANDALONE_SOLVER
bool solver_verbose = false;
#endif

enum {
	COL_OUTERBG, COL_INNERBG,
	COL_GRID,
	COL_BORDERLETTER, COL_TEXT,
	COL_GUESS, COL_ERROR, COL_PENCIL,
	COL_HIGHLIGHT, COL_LOWLIGHT,
	NCOLOURS
};

struct game_params {
	int w, h, n;
	bool diag; /* disallow diagonal adjacent letters */
	bool removenums; /* incomplete clue set */
};

struct game_state {
	int w, h, n;
	bool diag;
	char *grid; /* size w*h */
	unsigned char *clues; /* remaining possibilities, size w*h*n */
	int *numbers; /* size n*(w+h) */
	bool completed, cheated;
};

#define PREFERRED_TILE_SIZE 36
#define FLASH_TIME 0.7F
#define FLASH_FRAME 0.1F

#define CUBOID(x,y,i) ( (i) + ((x)*n) + ((y)*n*w) )
#define HOR_CLUE(y,i) ( (i) + ((y)*n) )
#define VER_CLUE(x,i) ( HOR_CLUE( (x) +h, (i) ) )

#define EMPTY 127
#define NO_NUMBER -1

const struct game_params abcd_presets[] = {
	{4, 4, 4, false, false},
	{4, 4, 4, false, true},
	{5, 5, 4, false, false},
	{5, 5, 4, false, true},
	{6, 6, 4, false, false},
	{7, 7, 3, false, false},
	{7, 7, 4, false, false},
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
	if (i < 0 || i >= lenof(abcd_presets))
		return false;
		
	game_params *ret = snew(game_params);
	*ret = abcd_presets[i]; /* struct copy */
	*params = ret;
	
	char buf[80];
	sprintf(buf, "%dx%d, %d letters %s", ret->w, ret->h, ret->n,
			ret->diag ? "No diagonals" : ret->removenums ? "Hard" : "Easy");
	*name = dupstr(buf);
	
	return true;
}

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);
	*ret = abcd_presets[2]; /* struct copy */

	return ret;
}

static void free_params(game_params *params)
{
	sfree(params);
}

static game_params *dup_params(const game_params *params)
{
	game_params *ret = snew(game_params);
	*ret = *params; /* struct copy */
	return ret;
}

static void decode_params(game_params *ret, char const *string)
{
	/* Find width */
	ret->w = ret->h = atoi(string);
	while (*string && isdigit((unsigned char) *string)) ++string;
	
	/* Find height */
	if (*string == 'x')
	{
		++string;
		ret->h = atoi(string);
	}
	while (*string && isdigit((unsigned char) *string)) ++string;
		
	/* Find number of letters */
	if (*string == 'n')
	{
		++string;
		ret->n = atoi(string);
	}
	while (*string && isdigit((unsigned char) *string)) ++string;
	
	/* Find Diagonal flag */
	ret->diag = false;
	if (*string == 'D')
	{
		ret->diag = true;
		++string;
	}
	/* Find Remove clues flag */
	ret->removenums = false;
	if (*string == 'R')
	{
		ret->removenums = true;
		++string;
	}
}

static char *encode_params(const game_params *params, bool full)
{
	char data[256];
	sprintf(data, "%dx%dn%d", params->w, params->h, params->n);
	if(params->diag)
		sprintf(data + strlen(data), "D");
	if(full && params->removenums)
		sprintf(data + strlen(data), "R");
	return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
	config_item *ret;
	char buf[80];
	
	ret = snewn(6, config_item);
	
	ret[0].name = "Width";
	ret[0].type = C_STRING;
	sprintf(buf, "%d", params->w);
	ret[0].u.string.sval = dupstr(buf);
	
	ret[1].name = "Height";
	ret[1].type = C_STRING;
	sprintf(buf, "%d", params->h);
	ret[1].u.string.sval = dupstr(buf);
	
	ret[2].name = "Letters";
	ret[2].type = C_STRING;
	sprintf(buf, "%d", params->n);
	ret[2].u.string.sval = dupstr(buf);
	
	ret[3].name = "Remove clues";
	ret[3].type = C_BOOLEAN;
	ret[3].u.boolean.bval = params->removenums;
	
	ret[4].name = "Allow diagonal touching";
	ret[4].type = C_BOOLEAN;
	ret[4].u.boolean.bval = !params->diag;
	
	ret[5].name = NULL;
	ret[5].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = snew(game_params);
	
	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->n = atoi(cfg[2].u.string.sval);
	ret->removenums = cfg[3].u.boolean.bval;
	ret->diag = !cfg[4].u.boolean.bval;
	
	return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
	/* A width or height under 2 could possibly break the solver */
	if (params->w < 2) return "Width must be at least 2";
	if (params->h < 2) return "Height must be at least 2";
	
	/*
	* It is actually possible for puzzles with 2 letters to exist, but they're
	* not really interesting. There are also no puzzles with unique solutions
	* for an even x even grid with 2 letters. Puzzles with 1 letter and more 
	* than one cell don't exist.
	*/
	if (params->n < 3 && !params->diag) return "Letters must be at least 3";
	
	/*
	* Diagonal puzzles with 4 letters do exist, however using 4 letters will
	* almost certainly break the generator. It doesn't seem worth the effort
	* to make a special case for this configuration.
	* Anything under 4 letters can't avoid violating the no-neighbor rule.
	*/
	if (params->n < 5 && params->diag) return "Letters for Diagonal mode must be at least 5";
	
	/*
	* This limit is actually fairly arbitrary, but I'd rather avoid clashing
	* with hotkeys in the midend. It also fits nicely with the keypad.
	*/
	if (params->n > 9) return "Letters must be no more than 9";
	
	return NULL;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	int w = params->w;
	int h = params->h;
	int n = params->n;
	int l = w+h;
	
	int num;
	int i = 0;
	
	const char *p = desc;
	
	while(*p)
	{
		if(isdigit((unsigned char) *p))
		{
			/* Number clue */
			num = atoi(p);
			while (*p && isdigit((unsigned char) *p)) ++p;
			
			/* A clue which can't possibly fit should be blocked. */
			if((i < h*n && num > 1+(w/2)) || (i >= h*n && num > 1+(h/2)))
				return "Description contains invalid number clue.";
			
			++i;
		}
		else if (*p == '-')
		{
			/* Hidden number clue */
			++i;
			++p;
		}
		else if (*p == ',')
		{
			/* Commas can be skipped over */
			++p;
		}
		else
		{
			return "Invalid character in description.";
		}
	}
	
	if(i < l*n)
		return "Description contains not enough clues.";
	else if (i > l*n)
		return "Description contains too many clues.";
	
	return NULL;
}

static game_state *blank_state(int w, int h, int n, bool diag)
{
	int l = w + h;
	
	game_state *ret = snew(game_state);
	
	ret->w = w;
	ret->h = h;
	ret->n = n;
	ret->diag = diag;
	
	ret->grid = snewn(w * h, char);
	ret->clues = snewn(w * h * n, unsigned char);
	ret->numbers = snewn(l * n, int);
	
	memset(ret->grid, EMPTY, w * h);
	memset(ret->clues, true, w*h*n);
	memset(ret->numbers, 0, l*n * sizeof(int));
	
	ret->completed = ret->cheated = false;
	
	return ret;
}

static key_label *game_request_keys(const game_params *params, int *nkeys)
{
	int i;
	int n = params->n;

	key_label *keys = snewn(n + 1, key_label);
	*nkeys = n + 1;

	for (i = 0; i < n; i++)
	{
		keys[i].button = 'A' + i;
		keys[i].label = NULL;
	}
	keys[n].button = '\b';
	keys[n].label = NULL;

	return keys;
}

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
	int w = params->w;
	int h = params->h;
	int n = params->n;
	bool diag = params->diag;
	
	game_state *state = blank_state(w, h, n, diag);
	
	/* Disable all clues for user interaction */
	memset(state->clues, false, w*h*n);
	
	const char *p = desc;
	int num;
	int i = 0;
	
	while(*p)
	{
		if(isdigit((unsigned char) *p))
		{
			num = atoi(p);
			state->numbers[i++] = num;
			while (*p && isdigit((unsigned char) *p)) ++p;
		}
		else if (*p == '-')
		{
			state->numbers[i++] = NO_NUMBER;
			++p;
		}
		else
			++p;
	}

	return state;
}

static game_state *dup_game(const game_state *state)
{
	int w = state->w;
	int h = state->h;
	int n = state->n;
	bool diag = state->diag;
	
	int l = w + h;
		
	game_state *ret = blank_state(w, h, n, diag);
	
	memcpy(ret->grid, state->grid, w * h);
	memcpy(ret->clues, state->clues, w * h * n);
	memcpy(ret->numbers, state->numbers, l * n * sizeof(int));
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;

	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->grid);
	sfree(state->clues);
	sfree(state->numbers);
	
	sfree(state);
}

static bool game_can_format_as_text_now(const game_params *params)
{
	/*
	* Puzzles with a width or height of 19 or more could contain a clue number
	* of 2 digits, which isn't supported by the format.
	*/
	
	return (params->w < 19 && params->h < 19);
}

static char *game_text_format(const game_state *state)
{
	char *ret;
	
	int w = state->w;
	int h = state->h;
	int n = state->n;
	
	int i,j;
	char c;
	
	int rw = (w+n)*2 + 1;
	int rh = h+n+2;
	int len = (rw * rh) + 1;
	
	/* Create return string, and fill it with spaces */
	ret = snewn(len, char);
	memset(ret, ' ', len);
	
	/* Place newlines */
	for(i = 0; i < rh; i++)
		ret[rw*(i+1)-1] = '\n';
	
	/* Place NUL */
	ret[len-1] = '\0';
	
	/* Place letters in topleft corner */
	for(i = 0; i < n; i++)
	{
		ret[rw*(n-1) + i*2] = 'A' + i; /* horizontal */
		ret[rw*i + (n-1)*2] = 'A' + i; /* vertical */
	}
	
	/* Place top clues */
	for(i = 0; i < w; i++)
		for(j = 0; j < n; j++)
		{
			if (state->numbers[VER_CLUE(i,j)] == NO_NUMBER)
				continue;
			
			ret[rw*j + n*2 + i*2] = '0' + state->numbers[VER_CLUE(i,j)];
		}
	
	/* Place left clues */
	for(i = 0; i < h; i++)
		for(j = 0; j < n; j++)
		{
			if (state->numbers[HOR_CLUE(i,j)] == NO_NUMBER)
				continue;
			
			ret[rw*(i+n+1) + j*2] = '0' + state->numbers[HOR_CLUE(i,j)];
		}
	
	/*
	* Place outline corners. We use a subtle visual cue here to differentiate
	* between puzzles where diagonal touching is allowed or disallowed.
	*/
	c = state->diag ? '*' : '+';
	ret[rw*n + n*2 - 1] = c; /* top left */
	ret[rw*(n+1) - 2] = c; /* top right */
	ret[rw*(n+h+1) + n*2 - 1] = c; /* bottom left */
	ret[rw*(n+h+2) - 2] = c; /* bottom right */
	
	/* Place horizontal borders */
	for (i = 0; i < (w*2)-1; i++)
	{
		ret[rw*n + n*2 + i] = '-'; /* top */
		ret[rw*(n+h+1) + n*2 + i] = '-'; /* bottom */
	}
	
	/* Place vertical borders */
	for (i = 0; i < h; i++)
	{
		ret[rw*(n+i+1) + n*2 - 1] = '|'; /* left */
		ret[rw*(n+i+2) - 2] = '|'; /* right */
	}
	
	/* Place letters */
	for (i = 0; i < h; i++)
		for(j = 0; j < w; j++)
		{
			c = state->grid[i*w+j];
			ret[rw*(n+i+1) + (n+j)*2] = (c != EMPTY ? 'A' + c : '.');
		}
	
	return ret;
}

static char *abcd_format_letters(const game_state *state, char solve)
{
	/*
	* Formats all entered letters to a single string.
	* Used for making the Solve move, as well as debugging purposes.
	*/
	
	char *ret, *p;
	int i;	
	int w = state->w;
	int h = state->h;
	
	ret = snewn(w*h * 2,char);
	p = ret;
	
	if(solve)
		*p++ = 'S';
	
	for(i = 0; i < w*h; i++)
	{
		*p++ = (state->grid[i] != EMPTY ? 'A' + state->grid[i] : '.');
	}
	*p++ = '\0';
	
	ret = sresize(ret, p - ret, char);
	
	return ret;
}

static void abcd_place_letter(game_state *state, int x, int y, char l, int *remaining)
{
	/*
	* Place one letter in the grid, rule the letter out for all adjacent squares,
	* and subtract the corresponding remaining values (if available)
	*/

	int w = state->w;
	int h = state->h;
	int n = state->n;
	bool diag = state->diag;
	int pos = y*w+x;
	char i;
	
	/* Place letter in grid */
	state->grid[pos] = l;
	
	/* Rule out all other letters in square */
	for (i = 0; i < n; i++)
	{
		if (i == l)
			continue;
		state->clues[CUBOID(x,y,i)] = false;
	}
	
	/* Remove possibility for adjacent squares */
	if (diag && x > 0 && y > 0)
		state->clues[CUBOID(x-1,y-1,l)] = false;
	if (diag && x < w-1 && y > 0)
		state->clues[CUBOID(x+1,y-1,l)] = false;
	if (diag && x > 0 && y < h-1)
		state->clues[CUBOID(x-1,y+1,l)] = false;
	if (diag && x < w-1 && y < h-1)
		state->clues[CUBOID(x+1,y+1,l)] = false;
	if (x > 0)
		state->clues[CUBOID(x-1,y,l)] = false;
	if (x < w-1)
		state->clues[CUBOID(x+1,y,l)] = false;
	if (y > 0)
		state->clues[CUBOID(x,y-1,l)] = false;
	if (y < h-1)
		state->clues[CUBOID(x,y+1,l)] = false;
	
	/* Update remaining array if entered */
	if (remaining)
	{
		if (remaining[HOR_CLUE(y,l)] != NO_NUMBER)
			remaining[HOR_CLUE(y,l)]--;
		if (remaining[VER_CLUE(x,l)] != NO_NUMBER)
			remaining[VER_CLUE(x,l)]--;
	}
}

static int abcd_solver_runs(game_state *state, int *remaining, int horizontal, char c)
{
	/*
	* For each row and column, get the available runs of spaces where
	* a certain letter can be placed. These are used to determine the
	* maximum amount of letters which can be placed in this row/column.
	* We can use this information to confirm several letters.
	* 
	* Example:
	* #.###.##
	* (# is a square with letter A as a possibility)
	* These are three runs, with size 1, 3 and 2 respectively. The maximum
	* amount of A's which can be placed without violating the no-neighbour
	* rule is 4. If the amount of A's we needed to place was 4, we could
	* confirm these letters:
	* A.A.A...
	* For the run of size 2, we can't determine which position the A
	* should go.
	*/
	
	int w = state->w;
	int h = state->h;
	int n = state->n;
	
	int x = 0, y = 0;
	int a,b,amx,bmx,i,point;
	
	int action = false;
	int *rslen;
	int *rspos;
	
	amx = (horizontal ? h : w);
	bmx = (horizontal ? w : h);
	rslen = snewn(bmx, int);
	rspos = snewn(bmx, int);
	
	for (a = 0; a < amx; a++)
	{
		if (horizontal)
			y = a;
		else
			x = a;
		
		int req = (horizontal ? remaining[HOR_CLUE(y,c)] : remaining[VER_CLUE(x,c)]);
		
		if(req == NO_NUMBER || req == 0)
			continue;
		
		point = 0;
		memset(rslen, 0, bmx*sizeof(int));
		memset(rspos, 0, bmx*sizeof(int));
		
		/* Collect all runs and their starting positions */
		for (b = 0; b < bmx; b++)
		{
			if (horizontal)
				x = b;
			else
				y = b;
			
			if(state->clues[CUBOID(x,y,c)] && state->grid[y*w+x] == EMPTY)
			{
				if(rslen[point] == 0)
					rspos[point] = b;
				rslen[point]++;
			}
			else
			{
				if(rslen[point] != 0)
					point++;
			}
		}
		
		/* Make sure the point is on an index with no run */
		if(rslen[point] != 0)
			point++;

		/*
		* Get the maximum amount of letters. This is length/2 + 1 for odd lengths,
		* and length/2 for even lengths.
		*/
		int maxletters = 0;
		for(i = 0; i < point; i++)
			maxletters += (rslen[i] / 2) + (rslen[i] & 1);

		/*
		* If the maximum amount of letters is also the required amount,
		* we place letters on all runs with an odd length
		*/
		if (maxletters == req)
		{
			for (i = 0; i < point; i++)
				if (rslen[i] & 1)
				{
					action = true;
					for (b = rspos[i]; b <= rspos[i] + rslen[i]; b+=2)
					{
						x = (horizontal ? b : x);
						y = (horizontal ? y : b);
						
#ifdef STANDALONE_SOLVER
if(solver_verbose)
	printf("Solver: Run on %s %i confirms %c at %i,%i\n",
			horizontal ? "Row" : "Column",
			a, 'A'+c, x+1,y+1);
#endif
						
						abcd_place_letter(state, x, y, c, remaining);
					}
				}
		}
		
		/* TODO techniques involving diagonal adjacency */
	}
	
	sfree(rslen);
	sfree(rspos);
	
	return action;
}

static int abcd_validate_adjacency(game_state *state, int sx, int sy, int ex, int ey, int dx, int dy)
{
	/* Returns true if no adjacency error was found with the directional data. */
	
	int w = state->w;
	int x,y,px,py;
	
	for (x = sx; x < ex; x++)
		for (y = sy; y < ey; y++)
		{
			px = x+dx;
			py = y+dy;
			
			if (state->grid[y*w+x] != EMPTY && state->grid[y*w+x] == state->grid[py*w+px])
				return false;
		}
	
	return true;
}

static int abcd_validate_clues(game_state *state, int horizontal)
{
	/*
	* Returns 1 if a clue is not yet satisfied,
	* -1 if a clue is overcrowded, and 0 if all clues are satisfied.
	*/
	
	int w = state->w;
	int h = state->h;
	int n = state->n;
	
	int a,amx,b,bmx,i;
	int found, pos, clue, gridpos;
	int error = 0;
	
	amx = (horizontal ? h : w);
	bmx = (horizontal ? w : h);
	
	for (a = 0; a < amx; a++)
		for (i = 0; i < n; i++)
		{
			found = 0;
			pos = (horizontal ? HOR_CLUE(a,i) : VER_CLUE(a,i));
			clue = state->numbers[pos];
			
			if (clue == NO_NUMBER)
				continue;

			for(b = 0; b < bmx; b++)
			{
				gridpos = (horizontal ? a*w+b : b*w+a);
				if(state->grid[gridpos] == i)
					found++;
			}
			
			if (found < clue)
				error = (error == 0 ? 1 : error);
			else if (found > clue)
				error = -1;
		}
		
	return error;
}

static int abcd_validate_puzzle(game_state *state)
{
	int w = state->w;
	int h = state->h;
	bool diag = state->diag;
	
	/* Check for clue violations */
	int invalid;
	invalid = abcd_validate_clues(state,true);
	if(invalid == -1)
		return -1;
		
	if(invalid == 1)
	{
		int invalid2 = abcd_validate_clues(state,false);
		if(invalid2 == -1)
			return invalid2;
	}
	else
	{
		invalid = abcd_validate_clues(state,false);
		if(invalid == -1)
			return invalid;
	}
	
	/* Check for adjacency violations */
	if(!abcd_validate_adjacency(state, 0, 0, w-1, h, 1, 0))
		return -1;
	if(!abcd_validate_adjacency(state, 0, 0, w, h-1, 0, 1))
		return -1;
	if(diag && !abcd_validate_adjacency(state, 0, 0, w-1, h-1, 1, 1))
		return -1;
	if(diag && !abcd_validate_adjacency(state, 0, 1, w-1, h, 1, -1))
		return -1;
	
	/*
	* If no validators found a critical error,
	* but not all numbers are satisfied, return now
	*/
	if (invalid == 1)
		return invalid;
	
	/* Finally, make sure all squares are entered. */
	int x,y;
	for (x = 0; x < w; x++)
		for (y = 0; y < h; y++)
		{
			if(state->grid[y*w+x] == EMPTY)
				return 1;
		}
	
	/* Puzzle solved */
	return 0;
}

#define MULTIPLE 126

static int abcd_solve_game(int *numbers, game_state *state)
{
	/*
	* Changes the *state to contain the number clues from *numbers and the found
	* solution. Returns an integer with an error code.
	* 0 is no error
	* 1 is multiple solutions
	* -1 is no solution
	*/
	
	int w = state->w;
	int h = state->h;
	int n = state->n;
	int l = w + h;
	
	int x,y;
	char c;
	
	int *remaining;
	int busy = true;
	int error = 0;

#ifdef STANDALONE_SOLVER
char *debug;
#endif
	
	/* Create a new game. Copy only the number clues and parameters */
	memcpy(state->numbers, numbers, l*n * sizeof(int));
	
	/*
	* Create an editable copy of the numbers, with the amount of
	* remaining letters per row/column.
	*/
	remaining = snewn(l * n, int);	
	memcpy(remaining, state->numbers, l*n * sizeof(int));
	
	/* Enter loop */
	while(busy && error == 0)
	{
		busy = false;
		
		/*
		* Check for all letters if the number is satisfied in each row/column.
		* Rule out this letter in all squares of this row/column,
		* then remove the number from the remaining array to speed up the rest
		* of the solving process.
		*/
		for (c = 0; c < n; c++)
		{
			/* Check rows */
			for (y = 0; y < h; y++)
				if(remaining[HOR_CLUE(y,c)] == 0)
				{
#ifdef STANDALONE_SOLVER
if(solver_verbose)
	printf("Solver: %c satisfied for Row %i \n", 'A'+c, y+1);
#endif
					busy = true;
					remaining[HOR_CLUE(y,c)] = NO_NUMBER;
					for (x = 0; x < w; x++)
						state->clues[CUBOID(x,y,c)] = false;
				}
			
			/* Check cols */
			for (x = 0; x < w; x++)
				if(remaining[VER_CLUE(x,c)] == 0)
				{
#ifdef STANDALONE_SOLVER
if(solver_verbose)
	printf("Solver: %c satisfied for Column %i \n", 'A'+c, x+1);
#endif
					busy = true;
					remaining[VER_CLUE(x,c)] = NO_NUMBER;
					for (y = 0; y < h; y++)
						state->clues[CUBOID(x,y,c)] = false;
				}
		}
		/* END CHECK */
		
		/* 
		* Check for single remaining possibility in one square
		*/
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++)
			{
				if (state->grid[y*w+x] != EMPTY)
					continue;
				
				/* Get the single possibility, or MULTIPLE if there's multiple */
				char let = EMPTY;
				for (c = 0; c < n; c++)
				{
					if (state->clues[CUBOID(x,y,c)])
						let = (let == EMPTY ? c : MULTIPLE);
				}
				
				/* There must be at least one remaining possibility */
				if (let == EMPTY)
					error = -1;
				else if (let != MULTIPLE)
				{
					/* One possibility found */
#ifdef STANDALONE_SOLVER
if(solver_verbose)
	printf("Solver: Single possibility %c on %i,%i\n", 'A'+let, x+1,y+1);
#endif
					busy = true;
					abcd_place_letter(state, x, y, let, remaining);
				}
			}
		/* END CHECK */
		
		/*
		* If something has been done at this point,
		* reuse the easier techniques before continuing
		*/
		if(busy)
			continue;
		
		/*
		* Try the runs techniques on all rows and columns for all letters
		*/
		for (c = 0; c < n; c++)
		{
			int action;
			action = abcd_solver_runs(state, remaining, true, c);
			busy = (action ? true : busy);
			action = abcd_solver_runs(state, remaining, false, c);
			busy = (action ? true : busy);
		}
		/* END CHECK */
		
	} /* while busy */
	
#ifdef STANDALONE_SOLVER
if(solver_verbose)
{
	debug = abcd_format_letters(state, false);
	printf("Solver letters: %s \n", abcd_format_letters(state, false));
	sfree(debug);
}
#endif
	
	/* Check if the puzzle has been solved. */
	if (error == 0)
		error = abcd_validate_puzzle(state);

#ifdef STANDALONE_SOLVER
if(solver_verbose)
{
	printf("Solver result: %s \n\n",
	error == 0 ? "Success" : error == 1 ? "No solution found" : "Error"
	);
}
#endif
	
	sfree(remaining);
	return error;
}

#undef MULTIPLE

static char *solve_game(const game_state *state, const game_state *currstate,
			const char *aux, const char **error)
{
	if(aux)
		return dupstr(aux);
	
	game_state *solved = blank_state(state->w, state->h, state->n, state->diag);
	int err = abcd_solve_game(state->numbers, solved);
	char *ret = abcd_format_letters(solved, true);
	free_game(solved);
	
	if(err == -1)
	{
		*error = "No solution exists for this puzzle.";
		sfree(ret);
		return NULL;
	}
	else if (err == 1)
	{
		*error = "Solver could not find a unique solution.";
		sfree(ret);
		return NULL;
	}
	
	return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	int w = params->w;
	int h = params->h;
	int n = params->n;
	int l = w+h;
	bool diag = params->diag;
	int attempts = 0;
	
	bool valid_puzzle = false;

#ifdef STANDALONE_SOLVER
char *debug;
#endif
	
	game_state *state = NULL;
	game_state *solved = NULL;
	
	char *ret, *p, *point;
	char letters[9];
	
	int x, y, i;
	
	while(!valid_puzzle)
	{
	
	attempts++;
	
	if(state) free_game(state);
	state = blank_state(w,h,n,diag);
	
	/*
	* The generation method used here is the simplest one: Make a random grid
	* with letters, and see if it's a solvable puzzle. This is adequate if at
	* least one size parameter is odd, but can take thousands of attempts if both
	* the width and height are even, and the puzzle is large.
	* (see TODO at the top of this file)
	*/
	
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			point = letters;
			
			/* Get all possibilities */
			for(i = 0; i < n; i++)
			{
				if(state->clues[CUBOID(x,y,i)])
				{
					*point++ = i;
				}
			}
			assert(point - letters);
			
			/* Place a random letter */
			int rl = random_upto(rs, point - letters);
			char let = letters[rl];

			abcd_place_letter(state, x, y, let, NULL);
		}
	}

#ifdef STANDALONE_SOLVER
if(solver_verbose)
{
	debug = abcd_format_letters(state, false);
	printf("Letters: %s\n", debug);
	sfree(debug);
}
#endif
	
	/* Create clues */
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			char let = state->grid[y*w+x];
			
			/* Add one to the corresponding horizontal and vertical clues */
			state->numbers[HOR_CLUE(y,let)]++;
			state->numbers[VER_CLUE(x,let)]++;
		}
	}
	
	/* Check if the puzzle can be solved */
	solved = blank_state(w,h,n,diag);
	int error = abcd_solve_game(state->numbers, solved);
	free_game(solved);
	if (error == 0)
	{
		/* Puzzle is valid */
		valid_puzzle = true;
	}
	
	} /* while !valid_puzzle */

#ifdef STANDALONE_SOLVER
printf("Valid puzzle generated after %i attempt(s) \n", attempts);
#endif
	
	if(params->removenums)
	{
		/* Create an array with a randomized order of each clue */
		int *indices = snewn(l*n, int);
		for(i = 0; i < l*n; i++) indices[i] = i;
		shuffle(indices, l*n, sizeof(*indices), rs);
		
		for (i = 0; i < l*n; i++)
		{
			int clue = state->numbers[indices[i]];
			state->numbers[indices[i]] = NO_NUMBER;
			
			/* Check if it's still solvable */
			solved = blank_state(w,h,n,diag);
			int error = abcd_solve_game(state->numbers, solved);
			free_game(solved);
			
			/* Not solvable anymore, put the clue back */
			if (error != 0)
				state->numbers[indices[i]] = clue;
		}
		sfree(indices);
	}
	
	/* We have a valid puzzle. Create game description */
	
	ret = snewn(l*n*4 + 1,char);
	p = ret;
	
	for (i = 0; i < l*n; i++)
	{
		if (state->numbers[i] != NO_NUMBER)
			p += sprintf(p, "%d", state->numbers[i]);
		else
			*p++ = '-';
		
		*p++ = ',';
	}
	
	/* Save aux data */
	*aux = abcd_format_letters(state, true);
	
	*p++ = '\0';

#ifdef STANDALONE_SOLVER
debug = game_text_format(state);
printf("%s", debug);
sfree(debug);
#endif
		
	free_game(state);
	
	ret = sresize(ret, p - ret, char);

	return ret;
}

struct game_ui {
	/* Cursor position */
	int hx, hy;
	/* Cursor type, enter or mark */
	int hpencil;
	/* Currently showing cursor */
	int hshow;
	/* Use highlight as cursor, so it doesn't disappear after entering something */
	int hcursor;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ret = snew(game_ui);
	
	ret->hx = ret->hy = 0;
	ret->hshow = ret->hcursor = ret->hpencil = false;
	
	return ret;
}

static void free_ui(game_ui *ui)
{
	sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
	return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding, const game_state *state)
{
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
							const game_state *newstate)
{
	int w = newstate->w;
	/*
	* We prevent pencil-mode highlighting of a filled square, unless
	* we're using the cursor keys. So if the user has just filled in
	* a square which we had a pencil-mode highlight in (by Undo, or
	* by Redo, or by Solve), then we cancel the highlight.
	*/
	if (ui->hshow && ui->hpencil && !ui->hcursor &&
			newstate->grid[ui->hy * w + ui->hx] != EMPTY) {
		ui->hshow = false;
	}
	
	if (!oldstate->completed && newstate->completed)
		ui->hshow = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (ui->hshow && (button == CURSOR_SELECT))
        return ui->hpencil ? "Ink" : "Pencil";
    return "";
}

#define FD_CURSOR		1
#define FD_PENCIL		2
#define FD_ERROR		4
#define FD_ERRVERT	8
#define FD_ERRHORZ	16
#define FD_ERRDIAGA	32 /* topleft-bottomright */
#define FD_ERRDIAGB	64 /* bottomleft-topright */
#define FD_ERRMASK  124

#define TILE_SIZE (ds->tilesize)
#define OUTER_COORD(x) ( (x) * TILE_SIZE + (TILE_SIZE/4))
#define INNER_COORD(x) (OUTER_COORD((x) + n))

#define FROMCOORD(x) ( (((x) - (TILE_SIZE/4)) / TILE_SIZE) - n )

struct game_drawstate {
	int tilesize;
	bool diag;
	int w,h,n;
	char *grid;
	int *cluefs;
	int *oldcluefs;
	int *gridfs;
	int *oldgridfs;
	unsigned char *clues;
	bool initial;
	int flash;
};

static char *interpret_move(const game_state *state, game_ui *ui, const game_drawstate *ds,
				int ox, int oy, int button)
{
	int w = state->w;
	int h = state->h;
	int n = state->n;
	
	int gx = FROMCOORD(ox);
	int gy = FROMCOORD(oy);
	int hx = ui->hx;
	int hy = ui->hy;
	
	char buf[80];
	
	button &= ~MOD_MASK;
	
	/* Mouse click */
	if (gx >= 0 && gx < w && gy >= 0 && gy < h)
	{
		/* Select square for letter placement */
		if (button == LEFT_BUTTON)
		{
			/* Select */
			if(!ui->hshow || ui->hpencil || hx != gx || hy != gy)
			{
				ui->hx = gx;
				ui->hy = gy;
				ui->hpencil = false;
				ui->hshow = true;
			}
			/* Deselect */
			else
			{
				ui->hshow = false;
			}
			
			ui->hcursor = false;
			return MOVE_UI_UPDATE;
		}
		/* Select square for marking */
		else if (button == RIGHT_BUTTON)
		{
			/* Select */
			if(!ui->hshow || !ui->hpencil || hx != gx || hy != gy)
			{
				ui->hx = gx;
				ui->hy = gy;
				ui->hpencil = true;
				ui->hshow = true;
			}
			/* Deselect */
			else
			{
				ui->hshow = false;
			}
			
			/* Remove the cursor again if the clicked square has a confirmed letter */
			if(state->grid[gy*w+gx] != EMPTY)
				ui->hshow = false;
			
			ui->hcursor = false;
			return MOVE_UI_UPDATE;
		}
	}
	
	/* Keyboard move */
	if (IS_CURSOR_MOVE(button))
	{
		move_cursor(button, &ui->hx, &ui->hy, w, h, 0, NULL);
		ui->hshow = ui->hcursor = true;
		return MOVE_UI_UPDATE;
	}
	
	/* Keyboard change pencil cursor */
	if (ui->hshow && button == CURSOR_SELECT)
	{
		ui->hpencil = !ui->hpencil;
		ui->hcursor = true;
		return MOVE_UI_UPDATE;
	}
	
	/* Enter or remove letter */
	if(ui->hshow && (
			(button >= 'a' && button <= 'i' && button - 'a' < n) || 
			(button >= 'A' && button <= 'I' && button - 'A' < n) || 
			(button >= '1' && button <= '9' && button - '1' < n) || 
			button == CURSOR_SELECT2 || button == '\b' || button == '0'))
	{
		char c;
		if (button >= 'a' && button <= 'i')
			c = button - 'a';
		else if (button >= 'A' && button <= 'I')
			c = button - 'A';
		else if (button >= '1' && button <= '9')
			c = button - '1';
		else
			c = EMPTY;
		
		/* When in pencil mode, filled in squares cannot be changed */
		if (ui->hpencil && state->grid[hy*w+hx] != EMPTY)
			return MOVE_NO_EFFECT;
		
		/* TODO Prevent operations which do nothing */
		
		sprintf(buf, "%c%d,%d,%c",
				(char)(ui->hpencil ? 'P' : 'R'),
				hx, hy,
				(char)(c != EMPTY ? 'A' + c : '-')
		);
		
		/* When not in keyboard mode, hide cursor */
		if (!ui->hcursor && !ui->hpencil)
			ui->hshow = false;
		
		return dupstr(buf);
	}
	
	/* Fill the board with marks */
	if(button == 'M' || button == 'm')
	{
		int x, y, z;
		char found = false;
		
		for(y = 0; y < h; y++)
		for(x = 0; x < w; x++)
		{
			if(state->grid[y*w+x] != EMPTY)
				continue;
				
			for(z = 0; z < n; z++)
				if(!state->clues[CUBOID(x,y,z)])
					found = true;
		}
		
		if(found)
			return dupstr("M");
	}
	
	return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	int w = state->w;
	int h = state->h;
	int n = state->n;
	
	int x, y, z;
	char c, i;
	
	game_state *ret;
	
	if(move[0] == 'S')
	{
		ret = dup_game(state);
		const char *p;
		p = move+1;

		for (x = 0; x < w*h; x++) {
			
			if (!*p || *p - 'A' < 0 || *p - 'A' >= n) {
				free_game(ret);
				return NULL;
			}
			
			ret->grid[x] = *p - 'A';
			p++;
		}
		
		ret->completed = ret->cheated = true;
		return ret;
	}
	else if ((move[0] == 'P' || move[0] == 'R') &&
			sscanf(move+1, "%d,%d,%c", &x, &y, &c) == 3 &&
			x >= 0 && x < w && y >= 0 && y < h &&
			(c == '-' || (c-'A' >= 0 && c-'A' < n))
			)
	{
		ret = dup_game(state);
		
		if (c == '-')
		{
			ret->grid[y*w+x] = EMPTY;
			memset(ret->clues + CUBOID(x,y,0), false, n);
			return ret;
		}
		else
		{
			i = c-'A';
			
			/* Toggle pencil mark */
			if (move[0] == 'P')
				ret->clues[CUBOID(x,y,i)] = !ret->clues[CUBOID(x,y,i)];
			
			/* Enter letter */
			else
				ret->grid[y*w+x] = i;
			
			/* Check if the puzzle has been completed */
			if (!ret->completed && abcd_validate_puzzle(ret) == 0)
				ret->completed = true;
			
			return ret;
		}
	}
	else if (move[0] == 'M')
	{
		ret = dup_game(state);
		
		for(y = 0; y < h; y++)
		for(x = 0; x < w; x++)
		for(z = 0; z < n; z++)
			ret->clues[CUBOID(x,y,z)] = true;
		
		return ret;
	}
	
	return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
	int n = params->n;
	if(ui->hshow) {
		*x = INNER_COORD(ui->hx);
		*y = INNER_COORD(ui->hy);
		*w = *h = TILE_SIZE;
	}
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	int w = params->w;
	int h = params->h;
	int n = params->n;
	
	*x = (w+n) * tilesize + (tilesize*3/4);
	*y = (h+n) * tilesize + (tilesize*3/4);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  const game_params *params, int tilesize)
{
	ds->tilesize = tilesize;
	ds->initial = false;
}

static float *game_colours(frontend *fe, int *ncolours)
{
	float *ret = snewn(3 * NCOLOURS, float);

	game_mkhighlight(fe, ret, COL_INNERBG, COL_HIGHLIGHT, COL_LOWLIGHT);
	frontend_default_colour(fe, &ret[COL_OUTERBG * 3]);
	
	int i;
	for (i = 0; i < 3; i++) {
		ret[COL_TEXT * 3 + i] = 0.0F;
		ret[COL_GRID * 3 + i] = 0.5F;
	}
	
	ret[COL_BORDERLETTER * 3 + 0] = 0.0F;
	ret[COL_BORDERLETTER * 3 + 1] = 0.0F;
	ret[COL_BORDERLETTER * 3 + 2] = 0.6F * ret[COL_OUTERBG * 3 + 1];
	
	ret[COL_GUESS * 3 + 0] = 0.0F;
	ret[COL_GUESS * 3 + 1] = 0.6F * ret[COL_INNERBG * 3 + 1];
	ret[COL_GUESS * 3 + 2] = 0.0F;

	ret[COL_ERROR * 3 + 0] = 1.0F;
	ret[COL_ERROR * 3 + 1] = 0.0F;
	ret[COL_ERROR * 3 + 2] = 0.0F;

	ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_INNERBG * 3 + 0];
	ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_INNERBG * 3 + 1];
	ret[COL_PENCIL * 3 + 2] = ret[COL_INNERBG * 3 + 2];

	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	struct game_drawstate *ds = snew(struct game_drawstate);

	ds->tilesize = 0;
	ds->diag = state->diag;
	int w = ds->w = state->w;
	int h = ds->h = state->h;
	int n = ds->n = state->n;
	int l = w + h;
	
	ds->initial = false;
	ds->flash = -1;
	
	ds->cluefs = snewn(l*n, int);
	ds->gridfs = snewn(w*h, int);
	ds->grid = snewn(w*h, char);
	ds->oldcluefs = snewn(l*n, int);
	ds->oldgridfs = snewn(w*h, int);
	ds->clues = snewn(w * h * n, unsigned char);
	memset(ds->clues, false, w*h*n);
	memset(ds->cluefs, 0, l*n*sizeof(int));
	memset(ds->gridfs, 0, w*h*sizeof(int));
	memset(ds->grid, ~0, w*h*sizeof(char));
	memset(ds->oldcluefs, ~0, l*n*sizeof(int));
	memset(ds->oldgridfs, ~0, w*h*sizeof(int));

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->cluefs);
	sfree(ds->gridfs);
	sfree(ds->grid);
	sfree(ds->oldcluefs);
	sfree(ds->oldgridfs);
	sfree(ds->clues);
	sfree(ds);
}

static void abcd_draw_borderletters(drawing *dr, game_drawstate *ds, int n, int color)
{
	int i;
	char buf[2];
	buf[1] = '\0';
	
	for (i = 0; i < n; i++)
	{
		buf[0] = 'A' + i;
		/* horizontal */
		draw_text(dr, OUTER_COORD(i) + TILE_SIZE/2, OUTER_COORD(n-1) + TILE_SIZE/2,
				FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
				color, buf);
		
		if(i == n-1) continue; /* Don't draw the last letter twice */
			
		/* vertical */
		draw_text(dr, OUTER_COORD(n-1) + TILE_SIZE/2, OUTER_COORD(i) + TILE_SIZE/2,
				FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
				color, buf);
	}
}

static void abcd_count_clues(const game_state *state, int *cluefs, int horizontal)
{
	int w = state->w;
	int h = state->h;
	int n = state->n;
	
	int a,amx,b,bmx,i;
	int clue, pos;
	
	int found, empty, gridpos;
	
	amx = (horizontal ? h : w);
	bmx = (horizontal ? w : h);
	
	for(a = 0; a < amx; a++)
		for(i = 0; i < n; i++)
		{
			pos = (horizontal ? HOR_CLUE(a,i) : VER_CLUE(a,i));
			
			clue = state->numbers[pos];
			
			if (clue == NO_NUMBER) continue;
			
			/* 
			 * Check for errors. If the amount of letters of this type
			 * found is more than the actual number, or if there's not
			 * enough free space to use, we mark it as an error.
			 */
			found = 0;
			empty = 0;
			
			for(b = 0; b < bmx; b++)
			{
				gridpos = (horizontal ? a*w+b : b*w+a);
				if(state->grid[gridpos] == i)
					found++;
				else if (state->grid[gridpos] == EMPTY)
					empty++;
			}
			
			if ((found > clue || found+empty < clue) && !(cluefs[pos] & FD_ERROR))
				cluefs[pos] |= FD_ERROR ; 
			else if (found <= clue && found+empty >= clue && (cluefs[pos] & FD_ERROR))
				cluefs[pos] &= ~FD_ERROR ; 
		}
}

static void abcd_draw_clues(drawing *dr, game_drawstate *ds, const game_state *state, int print_color, int horizontal)
{
	/*
	* Draws either all horizontal clues or all vertical clues. Since the code
	* for those is mostly the same, this function is called twice with a different
	* direction.
	*/
	
	int w = state->w;
	int h = state->h;
	int n = state->n;
	
	int oo, oi, ox, oy;
	int a,amx,i;
	int clue, pos;
	char buf[80];
	
	amx = (horizontal ? h : w);
	
	for(a = 0; a < amx; a++)
		for(i = 0; i < n; i++)
		{
			pos = (horizontal ? HOR_CLUE(a,i) : VER_CLUE(a,i));
			
			clue = state->numbers[pos];
			
			oo = OUTER_COORD(i);
			oi = INNER_COORD(a);
			ox = (horizontal ? oo : oi);
			oy = (horizontal ? oi : oo);
			
			/*
			* If we're not making a print, we run the full drawing code with
			*  error highlighting and redrawing.
			*/
			if(print_color == -1 && ds->cluefs[pos] != ds->oldcluefs[pos])
			{
				if (clue != NO_NUMBER)
				{
					sprintf(buf, "%i", clue);
					
					draw_rect(dr, ox, oy, TILE_SIZE-1, TILE_SIZE-1, COL_OUTERBG);
					draw_text(dr, ox + TILE_SIZE/2, oy + TILE_SIZE/2,
							FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
							(ds->cluefs[pos] & FD_ERROR ? COL_ERROR : COL_TEXT), buf);
				}
				draw_update(dr, ox, oy, TILE_SIZE-1, TILE_SIZE-1);
				ds->oldcluefs[pos] = ds->cluefs[pos];
			}
			else if(print_color != -1 && clue != NO_NUMBER)
			{
				/* Draw only the number in the specified color */
				sprintf(buf, "%i", clue);

				draw_text(dr, ox + TILE_SIZE/2, oy + TILE_SIZE/2,
						FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
						print_color, buf);
			}
		}
}

static void abcd_draw_pencil(drawing *dr, game_drawstate *ds, const game_state *state, int x, int y)
{
	/*
	* Draw the entered clues for a square.
	* Mostly copied from unequal.c, which was copied from solo.c
	*/
	int w = state->w;
	int n = state->n;
	int ox = INNER_COORD(x), oy = INNER_COORD(y);
	int nhints, i, j, hw, hh, hmax, fontsz;
	char str[2];

	/* (can assume square has just been cleared) */

	/* Draw hints; steal ingenious algorithm (basically)
	* from solo.c:draw_number() */
	for (i = nhints = 0; i < n; i++) {
		if (state->clues[CUBOID(x, y, i)]) nhints++;
	}

	for (hw = 1; hw * hw < nhints; hw++);
	
	if (hw < 3) hw = 3;
		hh = (nhints + hw - 1) / hw;
	if (hh < 2) hh = 2;
		hmax = max(hw, hh);
		fontsz = TILE_SIZE/(hmax*(11-hmax)/8);

	for (i = j = 0; i < n; i++)
	{
		if (state->clues[CUBOID(x, y, i)])
		{
			int hx = j % hw, hy = j / hw;

			str[0] = 'A'+i;
			str[1] = '\0';
			draw_text(dr,
				ox + (4*hx+3) * TILE_SIZE / (4*hw+2),
				oy + (4*hy+3) * TILE_SIZE / (4*hh+2),
				FONT_VARIABLE, fontsz,
				ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, str);
			j++;
		}
	}
}

static void abcd_set_errors_adjacent(game_drawstate *ds, const game_state *state, int sx, int sy, int ex, int ey, int dx, int dy, int flag)
{
	/* 
	* Sets the entered error flag for all letters with an identical letter
	* in position x+dx,y+dy
	*/
	
	int w = state->w;
	int h = state->h;
	
	int x, y, px, py;
	
	/* First unset the flag everywhere on the grid */
	for (x = 0; x < w; x++)
		for (y = 0; y < h; y++)
			ds->gridfs[y*w+x] &= ~flag;
	
	for (x = sx; x < ex; x++)
		for (y = sy; y < ey; y++)
		{
			px = x+dx;
			py = y+dy;
			if(state->grid[y*w+x] != EMPTY && state->grid[y*w+x] == state->grid[py*w+px])
			{
				ds->gridfs[y*w+x] |= flag;
				ds->gridfs[py*w+px] |= flag;
			}
		}
}

static void game_redraw(drawing *dr, game_drawstate *ds, const game_state *oldstate,
			const game_state *state, int dir, const game_ui *ui,
			float animtime, float flashtime)
{
	int w = state->w;
	int h = state->h;
	int n = state->n;
	int x,y,i;
	int tx, ty, fs, bgcol;
	char buf[80];
	int flash = -1;
	bool dirty;
	
	if (!ds->initial)
	{
		int rx = (w+n) * TILE_SIZE + (TILE_SIZE*3/4);
		int ry = (h+n) * TILE_SIZE + (TILE_SIZE*3/4);
		
		/* Draw a rectangle covering the screen for background */
		draw_rect(dr, 0, 0, rx, ry, COL_OUTERBG);
		
		/* Draw the letters in the corner */
		abcd_draw_borderletters(dr, ds, n, COL_BORDERLETTER);
		draw_update(dr, 0, 0, rx, ry);
		
		ds->initial = true;
	}
	
	if(flashtime > 0)
		flash = (int)(flashtime / FLASH_FRAME) % 3;
	
	abcd_count_clues(state, ds->cluefs, true);
	abcd_count_clues(state, ds->cluefs, false);
	
	/* Draw clues */
	abcd_draw_clues(dr, ds, state, -1, true);
	abcd_draw_clues(dr, ds, state, -1, false);
	
	/* Set cursor flag */
	for(x = 0; x < w; x++)
		for(y = 0; y < h; y++)
		{
			ds->gridfs[y * w + x] &= ~(FD_CURSOR|FD_PENCIL);
			if (ui->hy == y && ui->hx == x && ui->hshow)
				ds->gridfs[y * w + x] |= ui->hpencil ? FD_PENCIL : FD_CURSOR;
		}
	
	abcd_set_errors_adjacent(ds, state, 0, 0, w-1, h, 1, 0, FD_ERRHORZ); /* horizontal */
	abcd_set_errors_adjacent(ds, state, 0, 0, w, h-1, 0, 1, FD_ERRVERT); /* vertical */
	if (state->diag)
	{
		abcd_set_errors_adjacent(ds, state, 0, 0, w-1, h-1, 1, 1, FD_ERRDIAGA); /* topleft-bottomright */
		abcd_set_errors_adjacent(ds, state, 0, 1, w-1, h, 1, -1, FD_ERRDIAGB); /* bottomleft-topright */
	}
	
	/* Draw tiles */
	for (x = 0; x < w; x++)
		for (y = 0; y < h; y++)
		{
			fs = ds->gridfs[y*w+x];
			dirty = false;
			
			if(flash != ds->flash || fs != ds->oldgridfs[y*w+x] || 
					state->grid[y*w+x] != ds->grid[y*w+x])
				dirty = true;
			
			for(i = 0; i < n && !dirty; i++)
			{
				if(state->clues[CUBOID(x,y,i)] != ds->clues[CUBOID(x,y,i)])
					dirty = true;
			}
			
			if(!dirty)
				continue;
			
			tx = INNER_COORD(x);
			ty = INNER_COORD(y);
			
			/*
			* Determine background color. A diagonal stripe animation is shown
			* when the puzzle has been solved.
			*/
			bgcol = (flashtime > 0 && (x+y) % 3 == flash ? COL_HIGHLIGHT :
					flashtime > 0 && (x+y+2) % 3 == flash ? COL_LOWLIGHT :
					flashtime == 0 && fs & FD_CURSOR ? COL_HIGHLIGHT :
					COL_INNERBG);
			
			/* Draw the tile background */
			draw_rect(dr, tx+1, ty, TILE_SIZE-1, TILE_SIZE-1, bgcol);
			
			/* Draw the pencil marker */
			if (flashtime == 0 && fs & FD_PENCIL)
			{
				int coords[6];
				coords[0] = tx;
				coords[1] = ty;
				coords[2] = tx+ TILE_SIZE/2;
				coords[3] = ty;
				coords[4] = tx;
				coords[5] = ty+ TILE_SIZE/2;
				draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
			}
			
			if (state->grid[y*w+x] != EMPTY)
			{
				/* Draw single entry */
				buf[0] = 'A' + state->grid[y*w+x];
				buf[1] = '\0';
							
				/*
				* TODO:
				* We simply color the letter red if it violates an adjacency rule.
				* This could be changed to the exclamation mark symbol which
				* appears in Map and Tents.
				*/
				
				draw_text(dr, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
						FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
						(fs & FD_ERRMASK ? COL_ERROR : COL_GUESS),
						buf);
			}
			else
			{
				/* Draw pencil marks (if available) */
				abcd_draw_pencil(dr, ds, state, x, y);
			}
			
			/* Draw the border */
			int coords[8];
			coords[0] = tx;
			coords[1] = ty - 1;
			coords[2] = tx + TILE_SIZE;
			coords[3] = ty - 1;
			coords[4] = tx + TILE_SIZE;
			coords[5] = ty + TILE_SIZE - 1;
			coords[6] = tx;
			coords[7] = ty + TILE_SIZE - 1;
			draw_polygon(dr, coords, 4, -1, COL_GRID);
			
			
			/* Draw a small cross to indicate diagonal mode is turned on */
			if(ds->diag && x > 0 && y > 0)
			{
				draw_line(dr, tx, ty-1, tx + (TILE_SIZE/6), 
					ty + (TILE_SIZE/6) - 1, COL_GRID);
			}
			
			if(ds->diag && x < w-1 && y > 0)
			{
				draw_line(dr, tx + TILE_SIZE, ty-1, 
					(tx + TILE_SIZE)-(TILE_SIZE/6), 
					ty + (TILE_SIZE/6) - 1, COL_GRID);
			}
			
			if(ds->diag && x > 0 && y < h-1)
			{
				draw_line(dr, tx, ty+TILE_SIZE-1, tx + (TILE_SIZE/6), 
					(ty + TILE_SIZE)-(TILE_SIZE/6) - 1, COL_GRID);
			}
			
			if(ds->diag && x < w-1 && y < h-1)
			{
				draw_line(dr, tx + TILE_SIZE, ty+TILE_SIZE-1, 
					(tx + TILE_SIZE)-(TILE_SIZE/6), 
					(ty + TILE_SIZE)-(TILE_SIZE/6) - 1, COL_GRID);
			}
			
			draw_update(dr, tx, ty, TILE_SIZE, TILE_SIZE);
			ds->oldgridfs[y*w+x] = fs;
			ds->grid[y*w+x] = state->grid[y*w+x];
			for(i = 0; i < n; i++)
			{
				ds->clues[CUBOID(x,y,i)] = state->clues[CUBOID(x,y,i)];
			}
		}
	
	ds->flash = flash;
}

static float game_anim_length(const game_state *oldstate, const game_state *newstate,
				  int dir, game_ui *ui)
{
	return 0.0F;
}

static float game_flash_length(const game_state *oldstate, const game_state *newstate,
				   int dir, game_ui *ui)
{
	if (!oldstate->completed && newstate->completed &&
			!oldstate->cheated && !newstate->cheated)
		return FLASH_TIME;
	return 0.0F;
}

static int game_status(const game_state *state)
{
	return state->completed ? +1 : 0;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
	return true;
}

static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
	int pw, ph;

	/* Using 9mm squares */
	game_compute_size(params, 900, ui, &pw, &ph);
	*x = pw / 100.0F;
	*y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int tilesize)
{
	int ink = print_mono_colour(dr, 0);
	int w = state->w;
	int h = state->h;
	int n = state->n;
	int x,y;
	char buf[2];
	
	/* The drawing functions are dependent on a game_drawstate */
	game_drawstate *ds = game_new_drawstate(dr, state);
	ds->diag = state->diag;
	ds->tilesize = tilesize;
	
	/* Draw letters at top left */
	abcd_draw_borderletters(dr, ds, n, ink);
	
	/* Draw clues */
	abcd_draw_clues(dr, ds, state, ink, true);
	abcd_draw_clues(dr, ds, state, ink, false);
	
	/* Draw tiles */
	for (x = 0; x < w; x++)
		for (y = 0; y < h; y++)
		{
			int tx = INNER_COORD(x);
			int ty = INNER_COORD(y);
			
			if (state->grid[y*w+x] != EMPTY)
			{
				/* Draw single entry */
				buf[0] = 'A' + state->grid[y*w+x];
				buf[1] = '\0';
				
				draw_text(dr, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
						FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
						ink, buf);
			}
			
			/* Draw the border */
			int coords[8];
			coords[0] = tx;
			coords[1] = ty - 1;
			coords[2] = tx + TILE_SIZE;
			coords[3] = ty - 1;
			coords[4] = tx + TILE_SIZE;
			coords[5] = ty + TILE_SIZE - 1;
			coords[6] = tx;
			coords[7] = ty + TILE_SIZE - 1;
			draw_polygon(dr, coords, 4, -1, ink);
			
			/* Draw a small cross to indicate if diagonal mode is on */
			if(ds->diag && x > 0 && y > 0)
			{
				draw_line(dr, tx - (TILE_SIZE/6), ty - (TILE_SIZE/6) - 1, tx + (TILE_SIZE/6), ty + (TILE_SIZE/6) - 1, ink);
				draw_line(dr, tx - (TILE_SIZE/6), ty + (TILE_SIZE/6) - 1, tx + (TILE_SIZE/6), ty - (TILE_SIZE/6) - 1, ink);
			}
		}
}

#ifdef COMBINED
#define thegame abcd
#endif

const struct game thegame = {
	"ABCD", NULL, NULL,
	default_params,
	game_fetch_preset, NULL,
	decode_params,
	encode_params,
	free_params,
	dup_params,
	true, game_configure, custom_params,
	validate_params,
	new_game_desc,
	validate_desc,
	new_game,
	dup_game,
	free_game,
	true, solve_game,
	true, game_can_format_as_text_now, game_text_format,
    NULL, NULL, /* get_prefs, set_prefs */
	new_ui,
	free_ui,
	encode_ui,
	decode_ui,
	game_request_keys,
	game_changed_state,
	current_key_label,
	interpret_move,
	execute_move,
	PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
	game_colours,
	game_new_drawstate,
	game_free_drawstate,
	game_redraw,
	game_anim_length,
	game_flash_length,
	game_get_cursor_location,
	game_status,
	true, false, game_print_size, game_print,
	false, /* wants_statusbar */
	false, game_timing_state,
	REQUIRE_RBUTTON | REQUIRE_NUMPAD, /* flags */
};

/* ***************** *
 * Standalone solver *
 * ***************** */

#ifdef STANDALONE_SOLVER
#include <time.h>
#include <stdarg.h>

/* Most of the standalone solver code was copied from unequal.c and singles.c */

const char *quis;

static void usage_exit(const char *msg)
{
	if (msg)
		fprintf(stderr, "%s: %s\n", quis, msg);
	fprintf(stderr, "Usage: %s [-v] [--seed SEED] <params> | [game_id [game_id ...]]\n", quis);
	exit(1);
}

int main(int argc, char *argv[])
{
	random_state *rs;
	time_t seed = time(NULL);
	
	game_params *params = NULL;
	
	char *id = NULL, *desc = NULL;
	const char *err;
	
	quis = argv[0];
	
	while (--argc > 0)
	{
		char *p = *++argv;
		if (!strcmp(p, "--seed"))
		{
			if (argc == 0)
				usage_exit("--seed needs an argument");
			seed = (time_t)atoi(*++argv);
			argc--;
		}
		else if(!strcmp(p, "-v"))
			solver_verbose = true;
		else if (*p == '-')
			usage_exit("unrecognised option");
		else
			id = p;
	}
	
	if(id)
	{
		desc = strchr(id, ':');
		if (desc) *desc++ = '\0';

		params = default_params();
		decode_params(params, id);
		err = validate_params(params, true);
		if (err)
		{
			fprintf(stderr, "Parameters are invalid\n");
			fprintf(stderr, "%s: %s", argv[0], err);
			exit(1);
		}
	}
	
	if (!desc)
	{
		rs = random_new((void*)&seed, sizeof(time_t));
		if(!params) params = default_params();
		char *desc_gen, *aux;
		printf("Generating puzzle with parameters %s\n", encode_params(params, true));
		desc_gen = new_game_desc(params, rs, &aux, false);
		printf("Game ID: %s",desc_gen);
	}
	else
	{
		err = validate_desc(params, desc);
		if (err)
		{
			fprintf(stderr, "Description is invalid\n");
			fprintf(stderr, "%s", err);
			exit(1);
		}
		
		game_state *input = new_game(NULL, params, desc);
		
		game_state *solved = blank_state(params->w, params->h, params->n, params->diag);
		int errcode = abcd_solve_game(input->numbers, solved);
		if (errcode == 0)
		{
			char *fmt = game_text_format(solved);
			printf("%s", fmt);
			sfree(fmt);
		}
		
		free_game(input);
		free_game(solved);
	}
	
	return 0;
}
#endif
