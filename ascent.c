/*
 * ascent.c: Implementation of Hidoku puzzles.
 * (C) 2015 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective: Place each number from 1 to n once.
 * Consecutive numbers must be orthogonally or diagonally adjacent.
 *
 * This puzzle type was invented by Gyora Benedek.
 * Edges mode is an implementation of 1to25 invented by Jeff Widderich.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "matching.h"

#ifdef STANDALONE_SOLVER
int solver_verbose = false;

#define solver_printf if(!solver_verbose) {} else printf

#else
#define solver_printf(...)
#endif

enum {
	COL_MIDLIGHT,
	COL_LOWLIGHT,
	COL_HIGHLIGHT,
	COL_BORDER,
	COL_LINE,
	COL_IMMUTABLE,
	COL_ERROR,
	COL_CURSOR,
	COL_ARROW,
	NCOLOURS
};

typedef int number;
typedef int cell;
typedef unsigned char bitmap;

#define NUMBER_EMPTY  ((number) -1 )
#define NUMBER_WALL   ((number) -2 )
#define NUMBER_BOUND  ((number) -3 )
#define IS_OBSTACLE(i) (i <= -2)
#define NUMBER_EDGE(n) (-10 - (n))
#define IS_NUMBER_EDGE(i) (i <= -10)

/* Draw-only numbers */
#define NUMBER_MOVE   ((number) -4 )
#define NUMBER_CLEAR  ((number) -5 )
#define NUMBER_FLAG_MOVE   ((number) 0x4000 )
#define NUMBER_FLAG_MASK   NUMBER_FLAG_MOVE

#define CELL_NONE     ((cell)   -1 )
#define CELL_MULTIPLE ((cell)   -2 )

#define MAXIMUM_DIRS 8
#define FLAG_ENDPOINT (1<<MAXIMUM_DIRS)
#define FLAG_COMPLETE (1<<(MAXIMUM_DIRS+1))
#define FLAG_ERROR    (1<<(MAXIMUM_DIRS+2))
#define FLAG_USER     (1<<(MAXIMUM_DIRS+3))

#define BITMAP_SIZE(i) ( ((i)+7) / 8 )
#define GET_BIT(bmp, i) ( bmp[(i)/8] & 1<<((i)%8) )
#define SET_BIT(bmp, i) bmp[(i)/8] |= 1<<((i)%8)
#define CLR_BIT(bmp, i) bmp[(i)/8] &= ~(1<<((i)%8))

struct game_params {
	/* User-friendly width and height */
#ifndef PORTRAIT_SCREEN
	int w, h;
#else
	int h, w;
#endif

	/* Difficulty and grid type */
	int diff, mode;
	/* Should the start and end point be removed? */
	bool removeends;
	/* Should all given numbers be in a rotationally symmetric pattern? */
	bool symmetrical;
};

#define DIFFLIST(A)                             \
	A(EASY,Easy, e)                             \
	A(NORMAL,Normal, n)                         \
	A(TRICKY,Tricky, t)                         \
	A(HARD,Hard, h)                             \

#define MODELIST(A)                             \
	A(ORTHOGONAL,Rectangle (No diagonals), O)   \
	A(RECT,Rectangle, R)                        \
	A(HEXAGON,Hexagon, H)                       \
	A(HONEYCOMB,Honeycomb, C)                   \
	A(EDGES,Edges, E)                           \

#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title

#define DIFFENUM(upper,title,lower) DIFF_ ## upper,
enum { DIFFLIST(DIFFENUM) DIFFCOUNT };
static char const *const ascent_diffnames[] = { DIFFLIST(TITLE) };
static char const ascent_diffchars[] = DIFFLIST(ENCODE);

#define MODEENUM(upper,title,lower) MODE_ ## upper,
enum { MODELIST(MODEENUM) MODECOUNT };
static char const ascent_modechars[] = MODELIST(ENCODE);

/*
 * Hexagonal grids are drawn as rectangular grids, with each row having a
 * horizontal offset of 1/2 tile relative to the row above it.
 */
#define IS_HEXAGONAL(mode) ((mode) == MODE_HEXAGON || (mode) == MODE_HONEYCOMB)

typedef struct {
	int dx, dy;
} ascent_step;
typedef struct {
	int dircount;
	/* dirs[n] must be the inverse of dirs[dircount-(n+1)] */
	ascent_step dirs[MAXIMUM_DIRS];
} ascent_movement;

const static ascent_movement movement_orthogonal = {
	4, {
		     {0,-1},
		{-1, 0}, {1, 0},
		     {0, 1},
	}
};

const static ascent_movement movement_full = {
	8, {
		{-1,-1}, {0,-1}, {1,-1},
		{-1, 0},         {1, 0},
		{-1, 1}, {0, 1}, {1, 1},
	}
};

/*
* Hexagonal grids are implemented as normal square grids, but disallowing
* movement in the top-left and bottom-right directions.
*/
const static ascent_movement movement_hex = {
	6, {
		    {0, -1}, {1,-1},
		{-1, 0},         {1, 0},
		    {-1, 1}, {0, 1},
	}
};

const static ascent_movement *ascent_movement_for_mode(int mode)
{
	if(mode == MODE_ORTHOGONAL)
		return &movement_orthogonal;
	if(IS_HEXAGONAL(mode))
		return &movement_hex;
	return &movement_full;
}

const static struct game_params ascent_presets[] = {
	{ 7,  6, DIFF_EASY, MODE_RECT, false, false },
	{ 7,  6, DIFF_NORMAL, MODE_RECT, false, false },
	{ 7,  6, DIFF_TRICKY, MODE_RECT, false, false },
	{ 7,  6, DIFF_HARD, MODE_RECT, false, false },
	{ 10, 8, DIFF_EASY, MODE_RECT, false, false },
	{ 10, 8, DIFF_NORMAL, MODE_RECT, false, false },
	{ 10, 8, DIFF_TRICKY, MODE_RECT, false, false },
	{ 10, 8, DIFF_HARD, MODE_RECT, false, false },
	{ 5, 5, DIFF_NORMAL, MODE_EDGES, true, false },
	{ 5, 5, DIFF_TRICKY, MODE_EDGES, true, false },
	{ 5, 5, DIFF_HARD, MODE_EDGES, true, false },
};

const static struct game_params ascent_honeycomb_presets[] = {
	{ 7,  6, DIFF_NORMAL, MODE_HONEYCOMB, false, false },
	{ 7,  6, DIFF_TRICKY, MODE_HONEYCOMB, false, false },
	{ 7,  6, DIFF_HARD, MODE_HONEYCOMB, false, false },
	{ 10, 8, DIFF_NORMAL, MODE_HONEYCOMB, false, false },
	{ 10, 8, DIFF_TRICKY, MODE_HONEYCOMB, false, false },
	{ 10, 8, DIFF_HARD, MODE_HONEYCOMB, false, false },
};

const static struct game_params ascent_hexagonal_presets[] = {
	{ 7, 7, DIFF_NORMAL, MODE_HEXAGON, false, false },
	{ 7, 7, DIFF_TRICKY, MODE_HEXAGON, false, false },
	{ 7, 7, DIFF_HARD, MODE_HEXAGON, false, false },
	{ 9, 9, DIFF_NORMAL, MODE_HEXAGON, false, false },
	{ 9, 9, DIFF_TRICKY, MODE_HEXAGON, false, false },
	{ 9, 9, DIFF_HARD, MODE_HEXAGON, false, false },
};

#define DEFAULT_PRESET 0

struct game_state {
	/*
	 * Physical width and height. Grid types may increase the size to 
	 * make room for extra padding.
	 */
	int w, h, mode;
	
	number *grid;
	bitmap *immutable;
	int *path;

	number last;
	
	bool completed, cheated;
};

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);

	*ret = ascent_presets[DEFAULT_PRESET];        /* structure copy */
	
	return ret;
}

static game_params *dup_params(const game_params *params)
{
	game_params *ret = snew(game_params);
	*ret = *params;               /* structure copy */
	return ret;
}

static struct preset_menu *game_preset_menu(void)
{
	int i;
	game_params *params;
	char buf[80];
	struct preset_menu *menu, *honey, *hex;
	menu = preset_menu_new();

	for (i = 0; i < lenof(ascent_presets); i++)
	{
		params = dup_params(&ascent_presets[i]);
		sprintf(buf, "%dx%d %s%s", params->w, params->h, 
			params->mode == MODE_EDGES ? "Edges " : "", ascent_diffnames[params->diff]);
		preset_menu_add_preset(menu, dupstr(buf), params);
	}

	honey = preset_menu_add_submenu(menu, dupstr("Honeycomb"));

	for (i = 0; i < lenof(ascent_honeycomb_presets); i++)
	{
		params = dup_params(&ascent_honeycomb_presets[i]);
		sprintf(buf, "%dx%d Honeycomb %s", params->w, params->h, ascent_diffnames[params->diff]);
		preset_menu_add_preset(honey, dupstr(buf), params);
	}

	hex = preset_menu_add_submenu(menu, dupstr("Hexagon"));

	for (i = 0; i < lenof(ascent_hexagonal_presets); i++)
	{
		params = dup_params(&ascent_hexagonal_presets[i]);
		sprintf(buf, "Size %d Hexagon %s", params->w, ascent_diffnames[params->diff]);
		preset_menu_add_preset(hex, dupstr(buf), params);
	}

	return menu;
}

static void free_params(game_params *params)
{
	sfree(params);
}

static void decode_params(game_params *params, char const *string)
{
	params->w = params->h = atoi(string);
	while (*string && isdigit((unsigned char) *string)) ++string;
	if (*string == 'x') {
		string++;
		params->h = atoi(string);
		while (*string && isdigit((unsigned char)*string)) string++;
	}
	if (*string == 'm') {
		int i;
		string++;
		params->mode = MODECOUNT + 1;   /* ...which is invalid */
		if (*string) {
			for (i = 0; i < MODECOUNT; i++) {
				if (*string == ascent_modechars[i])
					params->mode = i;
			}
			string++;
		}
	}
	if(*string == 'E')
	{
		params->removeends = true;
		string++;
	}
	if (*string == 'd') {
		int i;
		string++;
		params->diff = DIFFCOUNT + 1;   /* ...which is invalid */
		if (*string) {
			for (i = 0; i < DIFFCOUNT; i++) {
				if (*string == ascent_diffchars[i])
					params->diff = i;
			}
			string++;
		}
	}
	else if (params->mode == MODE_EDGES)
		params->diff = max(params->diff, DIFF_NORMAL);

	if (*string == 'S')
	{
		params->symmetrical = true;
		string++;
	}
	else
		params->symmetrical = false;
}

static char *encode_params(const game_params *params, bool full)
{
	char buf[256];
	char *p = buf;
	p += sprintf(p, "%dx%dm%c", params->w, params->h, ascent_modechars[params->mode]);
	if(full && params->removeends)
		*p++ = 'E';
	if (full)
	{
		p += sprintf(p, "d%c", ascent_diffchars[params->diff]);
		if (params->symmetrical && params->mode != MODE_EDGES)
			p += sprintf(p, "S");
	}

	*p++ = '\0';
	
	return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
	config_item *ret;
	char buf[80];
	
	ret = snewn(7, config_item);
	
	ret[0].name = "Width";
	ret[0].type = C_STRING;
	sprintf(buf, "%d", params->w);
	ret[0].u.string.sval = dupstr(buf);
	
	ret[1].name = "Height";
	ret[1].type = C_STRING;
	sprintf(buf, "%d", params->h);
	ret[1].u.string.sval = dupstr(buf);
	
	ret[2].name = "Always show start and end points";
	ret[2].type = C_BOOLEAN;
	ret[2].u.boolean.bval = !params->removeends;
	
	ret[3].name = "Symmetrical clues";
	ret[3].type = C_BOOLEAN;
	ret[3].u.boolean.bval = params->symmetrical;

	ret[4].name = "Grid type";
	ret[4].type = C_CHOICES;
	ret[4].u.choices.choicenames = MODELIST(CONFIG);
	ret[4].u.choices.selected = params->mode;

	ret[5].name = "Difficulty";
	ret[5].type = C_CHOICES;
	ret[5].u.choices.choicenames = DIFFLIST(CONFIG);
	ret[5].u.choices.selected = params->diff;
	
	ret[6].name = NULL;
	ret[6].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = snew(game_params);
	
	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->removeends = !cfg[2].u.boolean.bval;
	ret->symmetrical = cfg[3].u.boolean.bval;
	ret->mode = cfg[4].u.choices.selected;
	ret->diff = cfg[5].u.choices.selected;
	
	return ret;
}


static const char *validate_params(const game_params *params, bool full)
{
	int w = params->w;
	int h = params->h;
	
	if(w*h >= 1000) return "Puzzle is too large";
	
	if(w < 2) return "Width must be at least 2";
	if(h < 2) return "Height must be at least 2";
	
	if(w > 50) return "Width must be no more than 50";
	if(h > 50) return "Height must be no more than 50";

	if (params->mode == MODE_HEXAGON && (h & 1) == 0)
		return "Height must be an odd number";
	if (params->mode == MODE_HEXAGON && w <= h / 2)
		return "Width is too low for hexagon grid";
	if (params->mode == MODE_EDGES && w == 2 && h == 2)
		return "Grid for Edges mode must be bigger than 2x2";
	if (full && params->mode == MODE_EDGES && params->diff < DIFF_NORMAL)
		return "Difficulty level for Edges mode must be at least Normal";
	if (full && params->symmetrical && params->mode == MODE_EDGES)
		return "Symmetrical clues must be disabled for Edges mode";
	
	return NULL;
}

/* ******************** *
 * Validation and Tools *
 * ******************** */

static bool is_near(cell a, cell b, int w, int mode)
{
	int dx = (a % w) - (b % w);
	int dy = (a / w) - (b / w);

	if(mode == MODE_ORTHOGONAL)
		return (abs(dx) + abs(dy)) == 1;

	if (IS_HEXAGONAL(mode) && dx == dy)
		return false;

	return (abs(dx) | abs(dy)) == 1;
}

static bool is_edge_valid(cell edge, cell i, int w, int h)
{
	/* Rows */
	if ((edge / w) > 0 && (edge / w) < h - 1)
		return i / w == (edge / w);

	/* Columns */
	if ((edge % w) > 0 && (edge % w) < w - 1)
		return i % w == (edge % w);

	/* Diagonals */
	return abs((i % w) - edge % w) == abs((i / w) - edge / w);
}

static bool check_completion(number *grid, int w, int h, int mode)
{
	int x = -1, y = -1, x2 = -1, y2 = -1, i;
	bool found;
	number n, last = (w*h) - 1;
	const ascent_movement *movement = ascent_movement_for_mode(mode);

	/* Check for empty squares, and locate path start */
	for(i = 0; i < w*h; i++)
	{
		if(grid[i] == NUMBER_EMPTY) return false;
		if(grid[i] == 0)
		{
			x = i%w;
			y = i/w;
		}
		if(IS_OBSTACLE(grid[i]))
			last--;
	}
	if(x == -1)
		return false;
	
	/* Keep selecting the next number in line */
	while(grid[y*w+x] != last)
	{
		for(i = 0; i < movement->dircount; i++)
		{
			x2 = x + movement->dirs[i].dx;
			y2 = y + movement->dirs[i].dy;
			if(y2 < 0 || y2 >= h || x2 < 0 || x2 >= w)
				continue;
			
			if(grid[y2*w+x2] == grid[y*w+x] + 1)
				break;
		}
		
		/* No neighbour found */
		if(i == movement->dircount) return false;
		x = x2;
		y = y2;
	}

	for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	{
		i = y*w + x;
		if (!IS_NUMBER_EDGE(grid[i]))
			continue;
		n = NUMBER_EDGE(grid[i]);

		found = false;
		for (y2 = 0; y2 < h; y2++)
		for (x2 = 0; x2 < w; x2++)
		{
			if (is_edge_valid(i, y2*w+x2, w, h) && grid[y2*w + x2] == n)
				found = true;
		}

		if (!found) return false;
	}
	
	return true;
}

static number ascent_follow_path(const game_state *state, cell i, cell prev, int *length)
{
	/*
	 * Follow the path in a certain direction, and return the first number 
	 * found, or NUMBER_EMPTY if the path is a dead end. 
	 * 
	 * If a cell contains more than two path segments, there is a risk of
	 * being trapped in an endless loop. The function ascent_clean_path 
	 * can be used to ensure no more than two path segments meet in any cell.
	 */
	int w = state->w;
	const ascent_movement *movement = ascent_movement_for_mode(state->mode);
	cell i2 = i;
	cell start = prev;
	int dir;
	int len = 0;

	while(state->grid[i] == NUMBER_EMPTY && i != start)
	{
		for(dir = 0; dir < movement->dircount; dir++)
		{
			if(!(state->path[i] & (1<<dir))) continue;

			i2 = (movement->dirs[dir].dy * w) + movement->dirs[dir].dx + i;
			if(i2 != prev) break;
		}

		if (dir == movement->dircount)
		{
			if (length) *length = len;
			return NUMBER_EMPTY;
		}

		prev = i;
		i = i2;
		++len;
		if (start == CELL_NONE)
			start = prev;
	}

	if (length) *length = len;

	return state->grid[i];
}

static void update_path_hints(number *prevhints, number *nexthints, const game_state *state)
{
	int s = state->w * state->h;
	int i, len = 0;
	number other, hint;

	for (i = 0; i < s; i++)
	{
		prevhints[i] = NUMBER_EMPTY;
		nexthints[i] = NUMBER_EMPTY;
	}
	if (!state->path) return;
	
	for (i = 0; i < s; i++)
	{
		if (!state->path[i] || state->grid[i] != NUMBER_EMPTY || state->path[i] & FLAG_COMPLETE)
			continue;

		other = ascent_follow_path(state, i, CELL_NONE, &len);
		if (other >= 0)
		{
			hint = other - len;
			prevhints[i] = hint >= 0 ? hint : NUMBER_WALL;
			hint = other + len;
			nexthints[i] = hint <= state->last ? hint : NUMBER_WALL;
		}
	}
}

/*
 * Path generator by Steffen Bauer
 * 
 * Employing the algorithm described at:
 * http://clisby.net/projects/hamiltonian_path/
 */

static void reverse_path(int i1, int i2, cell *path)
{
	int i;
	int ilim = (i2-i1+1)/2;
	int temp;
	for (i=0; i<ilim; i++)
	{
		temp = path[i1+i];
		path[i1+i] = path[i2-i];
		path[i2-i] = temp;
	}
}

static int backbite_left(ascent_step step, int n, cell *path, int w, int h, const bitmap *walls)
{
	int neighx, neighy, neigh, i;
	neighx = (path[0] % w) + step.dx;
	neighy = (path[0] / w) + step.dy;

	if (neighx < 0 || neighx >= w || neighy < 0 || neighy >= h) return n;

	neigh = neighy*w + neighx;
	if (walls && GET_BIT(walls, neigh)) return n;

	for (i = 1; i < n; i++)
	{
		if (neigh == path[i])
		{
			reverse_path(0, i - 1, path);
			return n;
		}
	}

	reverse_path(0, n-1, path);
	path[n] = neigh;
	return n + 1;
}

static int backbite_right(ascent_step step, int n, cell *path, int w, int h, const bitmap *walls)
{
	int neighx, neighy, neigh, i;
	neighx = (path[n-1] % w) + step.dx;
	neighy = (path[n-1] / w) + step.dy;

	if (neighx < 0 || neighx >= w || neighy < 0 || neighy >= h) return n;

	neigh = neighy*w + neighx;
	if (walls && GET_BIT(walls, neigh)) return n;

	for (i = n - 2; i >= 0; i--)
	{
		if (neigh == path[i])
		{
			reverse_path(i+1, n-1, path);
			return n;
		}
	}

	path[n] = neigh;
	return n + 1;
}

static int backbite(ascent_step step, int n, cell *path, int w, int h, random_state *rs, const bitmap *walls)
{
	return (random_upto(rs, 2) ? backbite_left : backbite_right)(step, n, path, w, h, walls);
}

#define MAX_ATTEMPTS 1000
static number *generate_hamiltonian_path(int w, int h, random_state *rs, const game_params *params)
{
	cell *path = snewn(w*h, cell);
	bitmap *walls = NULL;
	int i, n = 1, nn, attempts = 0, wallcount = 0;
	number *ret = NULL;
	
	const ascent_movement *movement = ascent_movement_for_mode(params->mode);

	if (params->mode == MODE_HEXAGON)
	{
		int j1, j2, center = h/2;
		walls = snewn(BITMAP_SIZE(w*h), bitmap);
		memset(walls, 0, BITMAP_SIZE(w*h));

		for (j1 = 1; j1 <= center; j1++)
		for (j2 = 0; j2 < j1; j2++)
		{
			i = ((center - j1) * w) + j2;
			SET_BIT(walls, i);
			SET_BIT(walls, (w*h)-(i+1));
			wallcount += 2;
		}
	}
	if (params->mode == MODE_HONEYCOMB)
	{
		int x, y, extra;
		walls = snewn(BITMAP_SIZE(w*h), bitmap);
		memset(walls, 0, BITMAP_SIZE(w*h));
		for (y = 0; y < h; y++)
		{
			for (x = 0; x < y / 2; x++)
			{
				SET_BIT(walls, (y*w)+(w-x-1));
				wallcount++;
			}
			extra = (h | y) & 1 ? 0 : 1;
			for (x = 0; x + extra < (h-y) / 2; x++)
			{
				SET_BIT(walls, y*w + x);
				wallcount++;
			}
		}
	}
	if (params->mode == MODE_EDGES)
	{
		int i;
		walls = snewn(BITMAP_SIZE(w*h), bitmap);
		memset(walls, 0, BITMAP_SIZE(w*h));
		for (i = 0; i < w; i++)
		{
			SET_BIT(walls, i);
			SET_BIT(walls, i + (w*(h-1)));
			wallcount += 2;
		}
		for (i = 1; i < h-1; i++)
		{
			SET_BIT(walls, w*i);
			SET_BIT(walls, w*i + (w-1));
			wallcount += 2;
		}
	}

	/* Find a starting position */
	do
	{
		i = random_upto(rs, w*h);
	} while (walls && GET_BIT(walls, i));
	path[0] = i;

	/*
	* The backbite algorithm will randomly navigate the grid. If it fails to
	* make any progress for MAX_ATTEMPTS cycles in a row, abort.
	*/
	while (n + wallcount < w*h && attempts < MAX_ATTEMPTS)
	{
		ascent_step step = movement->dirs[random_upto(rs, movement->dircount)];
		nn = backbite(step, n, path, w, h, rs, walls);
		if (n == nn)
			attempts++;
		else
			attempts = 0;
		n = nn;
	}

	/* Build the grid of numbers if the algorithm succeeds. */
	if (n + wallcount == w*h)
	{
		ret = snewn(w*h, number);
		for (i = 0; i < w*h; i++)
			ret[i] = -2;
		for (i = 0; i < n; i++)
			ret[path[i]] = i;
	}

	sfree(path);
	sfree(walls);

	return ret;
}

static void update_positions(cell *positions, const number *grid, int s)
{
	cell i;
	number n;

	for(n = 0; n < s; n++) positions[n] = CELL_NONE;
	for(i = 0; i < s; i++)
	{
		n = grid[i];
		if(n < 0 || n >= s) continue;
		positions[n] = (positions[n] == CELL_NONE ? i : CELL_MULTIPLE);
	}
}

/* ****** *
 * Solver *
 * ****** */

struct solver_scratch {
	int w, h, mode;
	const ascent_movement *movement;

	/* The position of each number. */
	cell *positions;
	
	number *grid;
	
	/* The last number of the path. */
	number end;
	
	/* All possible numbers in each cell */
	bitmap *marks; /* GET_BIT i*s+n */
	
	/* The possible path segments for each cell */
	int *path;
	bool found_endpoints;

	/* Scratch space for solver_overlap */
	bitmap *overlap;
};

static struct solver_scratch *new_scratch(int w, int h, int mode, number last)
{
	int i, n = w*h;
	struct solver_scratch *ret = snew(struct solver_scratch);
	ret->w = w;
	ret->h = h;
	ret->mode = mode;
	ret->end = last;
	ret->positions = snewn(n, cell);
	ret->grid = snewn(n, number);
	ret->path = snewn(n, int);
	ret->found_endpoints = false;
	ret->movement = ascent_movement_for_mode(mode);
	for(i = 0; i < n; i++)
	{
		ret->positions[i] = CELL_NONE;
		ret->grid[i] = NUMBER_EMPTY;
	}
	
	ret->marks = snewn(BITMAP_SIZE(w*h*n), bitmap);
	memset(ret->marks, 0, BITMAP_SIZE(w*h*n));
	memset(ret->path, 0, n*sizeof(int));

	ret->overlap = snewn(BITMAP_SIZE(w*h*2), bitmap);

	return ret;
}

static void free_scratch(struct solver_scratch *scratch)
{
	sfree(scratch->positions);
	sfree(scratch->grid);
	sfree(scratch->marks);
	sfree(scratch->path);
	sfree(scratch->overlap);
	sfree(scratch);
}

static int solver_place(struct solver_scratch *scratch, cell pos, number num)
{
	int w = scratch->w, s = w*scratch->h;
	cell i; number n;
	
	/* Place the number and update the positions array */
	scratch->grid[pos] = num;
	scratch->positions[num] = (scratch->positions[num] == CELL_NONE ? pos : CELL_MULTIPLE);
	
	/* Rule out this number in all other cells */
	for(i = 0; i < s; i++)
	{
		if(i == pos) continue;
		CLR_BIT(scratch->marks, i*s+num);
	}
	
	/* Rule out all other numbers in this cell */
	for(n = 0; n < scratch->end; n++)
	{
		if(n == num) continue;
		CLR_BIT(scratch->marks, pos*s+n);
	}
	
	solver_printf("Placing %d at %d,%d\n", num+1, pos%w, pos/w);
	
	return 1;
}

static int solver_single_position(struct solver_scratch *scratch)
{
	/* Find numbers which have a single possible cell */
	
	int s = scratch->w*scratch->h;
	cell i, found; number n;
	int ret = 0;
	
	for(n = 0; n <= scratch->end; n++)
	{
		if(scratch->positions[n] != CELL_NONE) continue;
		found = CELL_NONE;
		for(i = 0; i < s; i++)
		{
			if(scratch->grid[i] != NUMBER_EMPTY) continue;
			if(!GET_BIT(scratch->marks, i*s+n)) continue;
			if(found == CELL_NONE)
				found = i;
			else
				found = CELL_MULTIPLE;
		}
		assert(found != CELL_NONE);
		if(found >= 0)
		{
			solver_printf("Single possibility for number %d\n", n+1);
			ret += solver_place(scratch, found, n);
		}
	}
	
	return ret;
}

static int solver_single_number(struct solver_scratch *scratch, bool simple)
{
	/* Find cells which have a single possible number */
	
	int w = scratch->w, s = w*scratch->h;
	cell i; number n, found;
	int ret = 0;
	
	for(i = 0; i < s; i++)
	{
		if(scratch->grid[i] != NUMBER_EMPTY) continue;
		found = NUMBER_EMPTY;
		for(n = 0; n <= scratch->end; n++)
		{
			if(!GET_BIT(scratch->marks, i*s+n)) continue;
			if(found == NUMBER_EMPTY)
				found = n;
			else
				found = NUMBER_WALL;
		}
		assert(found != NUMBER_EMPTY);
		if(found >= 0)
		{
			if(simple && 
				(found == 0 || scratch->positions[found-1] == -1) && 
				(found == scratch->end || scratch->positions[found+1] == -1))
			{
				solver_printf("Ignoring possibility %d for cell %d,%d\n", found+1, i%w,i/w);
				continue;
			}
			
			solver_printf("Single possibility for cell %d,%d\n", i%w,i/w);
			ret += solver_place(scratch, i, found);
		}
	}
	
	return ret;
}

static int solver_near(struct solver_scratch *scratch, cell near, number num, int distance)
{
	/* Remove marks which are too far away from a given cell */
	
	int w = scratch->w, s = scratch->h*w;
	int hdist, vdist;
	int ret = 0;
	cell i;
	
	assert(num >= 0 && num < s);
	
	for(i = 0; i < s; i++)
	{
		if(!GET_BIT(scratch->marks, i*s+num)) continue;
		hdist = (i%w) - (near%w);
		vdist = (i/w) - (near/w);
		if (scratch->mode == MODE_ORTHOGONAL ||
		   (IS_HEXAGONAL(scratch->mode) && ((hdist < 0 && vdist < 0) || (hdist > 0 && vdist > 0))))
		{
			/* Manhattan distance */
			if ((abs(hdist) + abs(vdist)) <= distance) continue;
		}
		else
		{
			/* Chebyshev distance */
			if (max(abs(hdist), abs(vdist)) <= distance) continue;
		}
		CLR_BIT(scratch->marks, i*s+num);
		ret++;
	}
	
	if(ret)
	{
		solver_printf("Removed %d mark%s of %d for being too far away from %d,%d (%d)\n", 
			ret, ret != 1 ? "s" : "", num+1, near%w, near/w, scratch->grid[near]+1);
	}
	
	return ret;
}

static int solver_proximity_simple(struct solver_scratch *scratch)
{
	/* Remove marks which aren't adjacent to a given sequential number */
	
	int end = scratch->end;
	cell i; number n;
	int ret = 0;
	
	for(n = 0; n <= end; n++)
	{
		i = scratch->positions[n];
		if(i < 0) continue;
		
		if(n > 0 && scratch->positions[n-1] == CELL_NONE)
			ret += solver_near(scratch, i, n-1, 1);
		if(n < end-1 && scratch->positions[n+1] == CELL_NONE)
			ret += solver_near(scratch, i, n+1, 1);
	}
	
	return ret;
}

static int solver_proximity_full(struct solver_scratch *scratch)
{
	/* Remove marks which are too far away from given sequential numbers */
	
	int end = scratch->end;
	cell i; number n, n2;
	int ret = 0;
	
	for(n = 0; n <= end; n++)
	{
		i = scratch->positions[n];
		if(i < 0) continue;
		
		n2 = n-1;
		while(n2 >= 0 && scratch->positions[n2] == CELL_NONE)
		{
			ret += solver_near(scratch, i, n2, abs(n-n2));
			n2--;
		}
		n2 = n+1;
		while(n2 <= end-1 && scratch->positions[n2] == CELL_NONE)
		{
			ret += solver_near(scratch, i, n2, abs(n-n2));
			n2++;
		}
	}
	
	return ret;
}

static int ascent_find_direction(cell i1, cell i2, int w, const ascent_movement *movement)
{
	int dir;
	for (dir = 0; dir < movement->dircount; dir++)
	{
		if (i2 - i1 == (movement->dirs[dir].dy * w + movement->dirs[dir].dx))
			return dir;
	}
	return -1;
}

#ifdef STANDALONE_SOLVER
static void solver_debug_path(struct solver_scratch *scratch)
{
	if(!solver_verbose || scratch->movement->dircount != 8) return;
	
	int w = scratch->w, h = scratch->h;
	int x, y, path;
	char c;
	
	for(y = 0; y < h; y++)
	{
		for(x = 0; x < w; x++)
		{
			path = scratch->path[y*w+x];
			printf("%c%c%c", path & 1 ? '\\' : ' ', path & 2 ? '|' : ' ', path & 4 ? '/' : ' ');
		}
		printf("\n");
		for(x = 0; x < w; x++)
		{
			path = scratch->path[y*w+x];
			c = path & FLAG_ENDPOINT && path & FLAG_COMPLETE ? '#' : 
				path & FLAG_ENDPOINT ? 'O' : 
				path & FLAG_COMPLETE ? 'X' : '*';
			printf("%c%c%c", path & 8 ? '-' : ' ', c, path & 16 ? '-' : ' ');
		}
		printf("\n");
		for(x = 0; x < w; x++)
		{
			path = scratch->path[y*w+x];
			printf("%c%c%c", path & 32 ? '/' : ' ', path & 64 ? '|' : ' ', path & 128 ? '\\' : ' ');
		}
		printf("\n");
	}
}
#else
#define solver_debug_path(...)
#endif

static void solver_initialize_path(struct solver_scratch *scratch)
{
	int w = scratch->w, h = scratch->h;
	int x, y, dir, x2, y2;

	for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	{
		scratch->path[y*w + x] = FLAG_ENDPOINT;
		for(dir = 0; dir < scratch->movement->dircount; dir++)
		{
			x2 = x + scratch->movement->dirs[dir].dx;
			y2 = y + scratch->movement->dirs[dir].dy;
			if(x2 < 0 || x2 >= w || y2 < 0 || y2 >= h)
				continue;
			scratch->path[y*w + x] |= (1<<dir);
		}
	}

	solver_debug_path(scratch);
}

static int solver_update_path(struct solver_scratch *scratch)
{
	int w = scratch->w, h = scratch->h, s = w*h, end = scratch->end;
	cell i, ib, ic; number n;
	int dir;
	int ret = 0;

	/* 
	 * If both endpoints are found, 
	 * set all other path segments as being somewhere in the middle. 
	 */
	ib = scratch->positions[0];
	ic = scratch->positions[end];
	if (!scratch->found_endpoints && ib != CELL_NONE && ic != CELL_NONE)
	{
		scratch->found_endpoints = true;
		ret++;
		for (i = 0; i < s; i++)
		{
			if (i == ib || i == ic) continue;
			scratch->path[i] &= ~FLAG_ENDPOINT;
		}
	}

	/* 
	 * If the first and second numbers are known, 
	 * set the path of the first number to point to the second number. 
	 */
	i = scratch->positions[1];
	if (i != CELL_NONE && ib != CELL_NONE && !(scratch->path[ib] & FLAG_COMPLETE))
	{
		scratch->path[ib] = (1 << ascent_find_direction(ib, i, w, scratch->movement)) | FLAG_ENDPOINT;
	}
	/* Do the same for the last number pointing to the penultimate number. */
	i = scratch->positions[end - 1];
	if (i != CELL_NONE && ic != CELL_NONE && !(scratch->path[ic] & FLAG_COMPLETE))
	{
		scratch->path[ic] = (1 << ascent_find_direction(ic, i, w, scratch->movement)) | FLAG_ENDPOINT;
	}

	/* 
	 * For all numbers in the middle, set the path 
	 * if the next and previous numbers are known. 
	 */
	for (n = 1; n <= end-1; n++)
	{
		i = scratch->positions[n];
		if (i == CELL_NONE || scratch->path[i] & FLAG_COMPLETE) continue;

		ib = scratch->positions[n-1];
		ic = scratch->positions[n+1];
		if (ib == CELL_NONE || ic == CELL_NONE) continue;

		scratch->path[i] = 1 << ascent_find_direction(i, ib, w, scratch->movement);
		scratch->path[i] |= 1 << ascent_find_direction(i, ic, w, scratch->movement);
	}

	for (i = 0; i < s; i++)
	{
		if (scratch->path[i] & FLAG_COMPLETE) continue;
		int count = 0;
		
		/* 
		 * Count the number of possible path segments at this cell. 
		 * If it is exactly two, rule out all other neighbouring cells 
		 * pointing toward this cell. An endpoint counts as one path segment. 
		 */
		for (dir = 0; dir <= MAXIMUM_DIRS; dir++)
		{
			if (scratch->path[i] & (1 << dir)) count++;
		}
		
		if (count == 2)
		{
			int x, y, dir;
			scratch->path[i] |= FLAG_COMPLETE;
			solver_printf("Completed path segment at %d,%d\n", i%w, i/w);
			ret++;
			for (dir = 0; dir < MAXIMUM_DIRS; dir++)
			{
				/*
				 * This loop depends critically on no path flags being set
				 * which are outside of the range of movement->dirs.
				 */
				if (scratch->path[i] & (1 << dir)) continue;

				x = (i%w) + scratch->movement->dirs[dir].dx;
				y = (i / w) + scratch->movement->dirs[dir].dy;
				if(x < 0 || y < 0 || x >= w || y >= h) continue;
				scratch->path[y*w + x] &= ~(1 << (scratch->movement->dircount - (dir+1)));
			}
		}
	}
	
	if(ret)
	{
		solver_debug_path(scratch);
	}
	return ret;
}

static int solver_remove_endpoints(struct solver_scratch *scratch)
{
	if(scratch->found_endpoints) return 0;
	int w = scratch->w, h = scratch->h, s = w*h;
	number end = scratch->end;
	cell i;
	int ret = 0;
	
	for (i = 0; i < s; i++)
	{
		/* Unset possible endpoint if there is no mark for the first and last number */
		if(scratch->path[i] & FLAG_ENDPOINT)
		{
			if(GET_BIT(scratch->marks, i*s) || GET_BIT(scratch->marks, i*s+end))
				continue;
			
			scratch->path[i] &= ~FLAG_ENDPOINT;
			solver_printf("Remove possible endpoint at %d,%d\n", i%w, i/w);
			ret++;
		}
		else
		{
			/* Remove the mark for the first and last number on confirmed middle segments */
			if(GET_BIT(scratch->marks, i*s))
			{
				CLR_BIT(scratch->marks, i*s);
				solver_printf("Clear mark for 1 on middle %d,%d\n", i%w, i/w);
				ret++;
			}
			if(GET_BIT(scratch->marks, i*s+end))
			{
				CLR_BIT(scratch->marks, i*s+end);
				solver_printf("Clear mark for %d on middle %d,%d\n", end+1, i%w, i/w);
				ret++;
			}
		}
	}
	
	return ret;
}

static int solver_adjacent_path(struct solver_scratch *scratch)
{
	int w = scratch->w, h = scratch->h, s = w*h;
	cell i, i2;
	number n, n1;
	int dir, ret = 0;

	for (i = 0; i < s; i++)
	{
		/* Find empty cells with a confirmed path */
		if (scratch->path[i] & FLAG_COMPLETE && scratch->grid[i] == NUMBER_EMPTY)
		{
			solver_printf("Found an unfilled %s at %d,%d", 
				scratch->path[i] & FLAG_ENDPOINT ? "endpoint" : "path segment", i%w, i/w);
			
			/* Check if one of the directions is a known number */
			for(dir = 0; dir < MAXIMUM_DIRS; dir++)
			{
				if(!(scratch->path[i] & (1<<dir))) continue;
				i2 = scratch->movement->dirs[dir].dy * w + scratch->movement->dirs[dir].dx + i;
				n1 = scratch->grid[i2];
				if(n1 >= 0)
				{
					solver_printf(" connected to %d", n1+1);
					/* 
					 * Rule out all pencil marks, 
					 * except those in sequence with the other number. 
					 */
					for(n = 0; n <= scratch->end; n++)
					{
						if(abs(n-n1) == 1) continue;
						
						if(!GET_BIT(scratch->marks, i*s+n)) continue;
						CLR_BIT(scratch->marks, i*s+n);
						solver_printf("\nClear mark for %d", n+1);
						ret++;
					}
				}
			}
			
			if(scratch->path[i] & FLAG_ENDPOINT)
			{
				/* Rule out all marks except the first and last number */
				for(n = 1; n < scratch->end; n++)
				{
					if(!GET_BIT(scratch->marks, i*s+n)) continue;
					CLR_BIT(scratch->marks, i*s+n);
					solver_printf("\nClear mark for %d on endpoint", n+1);
					ret++;
				}
			}
			
			solver_printf("\n");
		}
	}
	
	return ret;
}

static int solver_remove_path(struct solver_scratch *scratch)
{
	/* 
	 * Rule out path segments between two given numbers which are not in sequence.
	 */
	int w = scratch->w, h = scratch->h, s = w*h;
	cell i1, i2;
	number n1, n2;
	int dir, ret = 0;
	
	for (i1 = 0; i1 < s; i1++)
	{
		if (scratch->path[i1] & FLAG_COMPLETE) continue;
		n1 = scratch->grid[i1];
		if(n1 < 0) continue;
		for(dir = 0; dir < MAXIMUM_DIRS; dir++)
		{
			if(!(scratch->path[i1] & (1<<dir))) continue;
			i2 = scratch->movement->dirs[dir].dy * w + scratch->movement->dirs[dir].dx + i1;
			n2 = scratch->grid[i2];
			if(n2 >= 0 && abs(n1-n2) != 1)
			{
				solver_printf("Disconnect %d,%d (%d) and %d,%d (%d)\n", i1%w, i1/w, n1+1, i2%w, i2/w, n2+1);
				scratch->path[i1] &= ~(1 << dir);
				scratch->path[i2] &= ~(1 << (scratch->movement->dircount - (dir+1)));
				ret++;
			}
		}
	}
	
	if(ret)
	{
		solver_debug_path(scratch);
	}
	return ret;
}

static int solver_remove_blocks(struct solver_scratch *scratch)
{
	int i1, i2, dir;
	int w = scratch->w, s = w * scratch->h;
	int ret = 0;
	for (i1 = 0; i1 < s; i1++)
	{
		if(!IS_OBSTACLE(scratch->grid[i1])) continue;
		for(dir = 0; dir < MAXIMUM_DIRS; dir++)
		{
			if(!(scratch->path[i1] & (1<<dir))) continue;
			i2 = scratch->movement->dirs[dir].dy * w + scratch->movement->dirs[dir].dx + i1;
			solver_printf("Disconnect block %d,%d from %d,%d\n", i1%w, i1/w, i2%w, i2/w);
			scratch->path[i2] &= ~(1 << (scratch->movement->dircount - (dir+1)));
			ret++;
		}
		scratch->path[i1] = 0;
	}
	
	if(ret)
	{
		solver_debug_path(scratch);
	}
	return ret;
}

static int solver_overlap(struct solver_scratch *scratch)
{
	/*
	 * Rule out number marks which aren't adjacent to a mark of 
	 * both the previous number and the next number.
	 */
	int ret = 0;
	int w = scratch->w, h = scratch->h, s = w*h;
	cell i1, i2; number n;

	for(n = 0; n < scratch->end; n++)
	{
		if(scratch->positions[n] != CELL_NONE)
			continue;

		memset(scratch->overlap, 0, BITMAP_SIZE(s*2));

		if(n > 0)
		{
			for(i1 = 0; i1 < s; i1++)
			{
				if(GET_BIT(scratch->marks, i1*s+(n-1)))
				{
					for(i2 = 0; i2 < s; i2++)
					{
						if(is_near(i1, i2, scratch->w, scratch->mode))
							SET_BIT(scratch->overlap, i2);
					}
				}
			}
		}

		if(n < scratch->end - 1)
		{
			for(i1 = 0; i1 < s; i1++)
			{
				if(GET_BIT(scratch->marks, i1*s+(n+1)))
				{
					for(i2 = 0; i2 < s; i2++)
					{
						if(is_near(i1, i2, w, scratch->mode))
							SET_BIT(scratch->overlap, i2 + s);
					}
				}
			}
		}

		for(i1 = 0; i1 < s; i1++)
		{
			if(!GET_BIT(scratch->marks, i1*s+n))
				continue;

			if((n == 0 || GET_BIT(scratch->overlap, i1)) &&
				(n == scratch->end-1 || GET_BIT(scratch->overlap, i1+s)))
				continue;

			solver_printf("Rule out %d at %d,%d for not being near marks of adjacent numbers\n", 
					n+1, i1%w, i1/w);
			CLR_BIT(scratch->marks, i1*s+n);
			ret++;
		}
	}

	return ret;
}

static void solver_edges(struct solver_scratch *scratch)
{
	cell i1, i2; number n;
	int w = scratch->w, h = scratch->h, s = w*h;

	for (i1 = 0; i1 < s; i1++)
	{
		if (!IS_NUMBER_EDGE(scratch->grid[i1]))
			continue;
		n = NUMBER_EDGE(scratch->grid[i1]);

		for (i2 = 0; i2 < s; i2++)
		{
			if (GET_BIT(scratch->marks, i2*s + n) && !is_edge_valid(i1, i2, w, h))
				CLR_BIT(scratch->marks, i2*s + n);
		}
	}
}

static void ascent_solve(const number *puzzle, int diff, struct solver_scratch *scratch)
{
	int w = scratch->w, h = scratch->h, s=w*h;
	cell i; number n;
	if(puzzle != scratch->grid)
		memcpy(scratch->grid, puzzle, s*sizeof(number));
	update_positions(scratch->positions, scratch->grid, s);
	memset(scratch->marks, 0, BITMAP_SIZE(s*s));
	
	/* Set possibilities for numbers */
	for(n = 0; n < s; n++)
	{
		i = scratch->positions[n];
		if(i >= 0)
		{
			SET_BIT(scratch->marks, i*s+n);
			continue;
		}
		for(i = 0; i < s; i++)
		{
			if(scratch->grid[i] == NUMBER_EMPTY)
				SET_BIT(scratch->marks, i*s+n);
		}
	}

	solver_edges(scratch);
	
	solver_initialize_path(scratch);
	solver_remove_blocks(scratch);

	while(true)
	{
		if(solver_single_position(scratch))
			continue;
		
		if(solver_proximity_simple(scratch))
			continue;
		
		if (diff < DIFF_NORMAL) break;

		if (solver_update_path(scratch))
			continue;

		if (solver_adjacent_path(scratch))
			continue;

		if(solver_remove_endpoints(scratch))
			continue;
		
		if (solver_remove_path(scratch))
			continue;
		
		if(solver_proximity_full(scratch))
			continue;

		if((diff >= DIFF_HARD || scratch->mode == MODE_EDGES) && solver_overlap(scratch))
			continue;
		
		if(diff < DIFF_TRICKY) break;
		
		if(diff < DIFF_HARD && solver_single_number(scratch, true))
			continue;
		
		if(diff < DIFF_HARD) break;
		
		if(solver_single_number(scratch, false))
			continue;
		
		break;
	}
}

/* **************** *
 * Puzzle Generator *
 * **************** */

static void ascent_grid_size(const game_params *params, int *w, int *h)
{
	*w = params->w;
	*h = params->h;

	if (params->mode == MODE_HONEYCOMB)
		*w += ((*h + 1) / 2) - 1;
	else if (params->mode == MODE_EDGES)
	{
		*w += 2;
		*h += 2;
	}
}

static char ascent_add_edges(struct solver_scratch *scratch, number *grid,
                             const game_params *params, random_state *rs)
{
	/*
	 * Randomly move grid numbers to the edges. This is done by creating a
	 * bipartite graph connecting inner grid spaces to edge spaces, then
	 * finding any maximal matching that produces a viable puzzle.
	 */

	int attempts = 0;
	int w = scratch->w, h = scratch->h;
	int i, j, x, y, x2, y2;
	int aw = w-2, ah = h-2;

	/*
	* (aw*ah*4) Horizontal and vertical connections
	* (min(aw,ah)*4) Diagonal connections
	*/
	int *adjdata = snewn((aw*ah*4) + (min(aw,ah)*4), int);
	int **adjlists = snewn(aw*ah, int*);
	int *adjsizes = snewn(aw*ah, int);
	int *match = snewn(aw*ah, int);
	void *mscratch = smalloc(matching_scratch_size(aw*ah, w*h));
	int p = 0, count;

	for(i = 0; i < aw*ah; i++)
	{
		adjlists[i] = &adjdata[p];
		x = i%aw + 1;
		y = i/aw + 1;
		count = 0;

		/*
		 * If "Always show start and end points" is enabled, prevent the
		 * starting position from being moved to an edge.
		 */
		if(!params->removeends && grid[y*w+x] == 0)
		{
			adjsizes[i] = 0;
			continue;
		}

		/*
		* Connect grid space to all edge spaces pointing at this space.
		* Loop through the grid again to find indices of interest.
		*/
		for(j = 0; j < w*h; j++)
		{
			x2 = j % w;
			y2 = j / w;

			if((x2 == 0 || x2 == w - 1 || y2 == 0 || y2 == h - 1)
				&& is_edge_valid(j, y*w+x, w, h))
			{
				adjdata[p++] = j;
				count++;;
			}
		}

		adjsizes[i] = count;
	}

	while(attempts < MAX_ATTEMPTS)
	{
		int total = matching_with_scratch(mscratch, aw*ah, w*h, adjlists, adjsizes, rs, match, NULL);
		assert(total > 0);

		memcpy(scratch->grid, grid, w*h*sizeof(number));
		
		for(i = 0; i < aw*ah; i++)
		{
			if(match[i] == -1)
				continue;
			
			x = i%aw + 1;
			y = i/aw + 1;

			scratch->grid[match[i]] = NUMBER_EDGE(grid[y*w+x]);
			scratch->grid[y*w+x] = NUMBER_EMPTY;
		}

		ascent_solve(scratch->grid, params->diff, scratch);
		if (check_completion(scratch->grid, w, h, params->mode))
			break;
		
		attempts++;
	}

	memcpy(grid, scratch->grid, w*h*sizeof(number));

	for(i = 0; i < aw*ah; i++)
	{
		if(match[i] == -1)
			continue;
		
		x = i%aw + 1;
		y = i/aw + 1;

		grid[y*w+x] = NUMBER_EMPTY;
	}

	sfree(adjlists);
	sfree(adjdata);
	sfree(adjsizes);
	sfree(match);
	sfree(mscratch);

	return attempts < MAX_ATTEMPTS;
}

static char ascent_remove_numbers(struct solver_scratch *scratch, number *grid,
	const game_params *params, random_state *rs)
{
	int w = scratch->w, h = scratch->h;
	cell *spaces = snewn(w*h, cell);
	cell i1, i2, j;
	number temp1, temp2;

	for (j = 0; j < w*h; j++)
		spaces[j] = j;

	shuffle(spaces, w*h, sizeof(*spaces), rs);
	for(j = 0; j < w*h; j++)
	{
		i1 = spaces[j];
		i2 = (w*h) - (i1 + 1);
		temp1 = grid[i1];
		temp2 = grid[i2];
		if (temp1 < 0) continue;
		if (params->symmetrical && temp2 < 0) continue;
		if (!params->removeends && (temp1 == 0 || temp1 == scratch->end)) continue;
		if (!params->removeends && params->symmetrical && (temp2 == 0 || temp2 == scratch->end)) continue;
		grid[i1] = NUMBER_EMPTY;
		if (params->symmetrical)
			grid[i2] = NUMBER_EMPTY;

		ascent_solve(grid, params->diff, scratch);

		if (!check_completion(scratch->grid, w, h, params->mode))
		{
			if (params->symmetrical)
				grid[i2] = temp2;
			grid[i1] = temp1;
		}
	}

	sfree(spaces);

	return true;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
	int w, h;
	ascent_grid_size(params, &w, &h);

	bool success;
	cell i;
	struct solver_scratch *scratch = new_scratch(w, h, params->mode, (w*h)-1);
	number n;
	number *grid = NULL;

	do
	{
		scratch->end = (w*h)-1;

		sfree(grid);
		grid = NULL;
		while (!grid)
		{
			grid = generate_hamiltonian_path(w, h, rs, params);
		}

		for (i = 0; i < w*h; i++)
			if (IS_OBSTACLE(grid[i])) scratch->end--;

		if (params->mode == MODE_EDGES)
			success = ascent_add_edges(scratch, grid, params, rs);
		else
			success = ascent_remove_numbers(scratch, grid, params, rs);
	} while (!success);

	char *ret = snewn(w*h*4, char);
	char *p = ret;
	int run = 0;
	enum { RUN_NONE, RUN_BLANK, RUN_WALL, RUN_NUMBER } runtype = RUN_NONE;
	for(i = 0; i <= w*h; i++)
	{
		n = (i == w*h) ? NUMBER_EMPTY : grid[i];
		if(IS_NUMBER_EDGE(n)) n = NUMBER_EDGE(n);

		if(runtype == RUN_BLANK && (i == w*h || n != NUMBER_EMPTY))
		{
			while(run >= 26)
			{
				*p++ = 'z';
				run -= 26;
			}
			if(run)
				*p++ = 'a' + run-1;
			run = 0;
		}
		if(runtype == RUN_WALL && (i == w*h || !IS_OBSTACLE(n)))
		{
			while(run >= 26)
			{
				*p++ = 'Z';
				run -= 26;
			}
			if(run)
				*p++ = 'A' + run-1;
			run = 0;
		}

		if(i == w*h)
			break;

		if(n >= 0)
		{
			if(runtype == RUN_NUMBER)
				*p++ = '_';
			p += sprintf(p, "%d", n + 1);
			runtype = RUN_NUMBER;
		}
		else if(n == NUMBER_EMPTY)
		{
			runtype = RUN_BLANK;
			run++;
		}
		else if(IS_OBSTACLE(n))
		{
			runtype = RUN_WALL;
			run++;
		}
	}
	*p++ = '\0';
	ret = sresize(ret, p - ret, char);
	free_scratch(scratch);
	sfree(grid);
	return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	int w, h;
	ascent_grid_size(params, &w, &h);

	int s = w*h;
	const char *p = desc;
	number n, last = 0;
	int i = 0;
	while(*p)
	{
		if(isdigit((unsigned char) *p))
		{
			n = atoi(p);
			if(n > last) last = n;
			while (*p && isdigit((unsigned char) *p)) ++p;
			++i;
		}
		else if(*p >= 'a' && *p <= 'z')
			i += ((*p++) - 'a') + 1;
		else if(*p >= 'A' && *p <= 'Z')
			i += ((*p++) - 'A') + 1;
		else
			++p;
	}
	
	if(last > s)
		return "Number is too high";
	if(i < s)
		return "Not enough spaces";
	if(i > s)
		return "Too many spaces";
	
	return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
							const char *desc)
{
	int w, h;
	ascent_grid_size(params, &w, &h);

	int i, j, walls;
	
	game_state *state = snew(game_state);

	state->w = w;
	state->h = h;
	state->mode = params->mode;
	state->completed = state->cheated = false;
	state->path = NULL;
	state->grid = snewn(w*h, number);
	state->immutable = snewn(BITMAP_SIZE(w*h), bitmap);
	state->last = (w*h)-1;
	memset(state->immutable, 0, BITMAP_SIZE(w*h));
	
	for(i = 0; i < w*h; i++) state->grid[i] = NUMBER_EMPTY;
	
	const char *p = desc;
	i = 0;
	while(*p)
	{
		if(isdigit((unsigned char) *p))
		{
			state->grid[i] = atoi(p) - 1;
			SET_BIT(state->immutable, i);
			while (*p && isdigit((unsigned char) *p)) ++p;
			++i;
		}
		else if(*p >= 'a' && *p <= 'z')
			i += ((*p++) - 'a') + 1;
		else if(*p >= 'A' && *p <= 'Z')
		{
			walls = ((*p++) - 'A') + 1;
			for(j = i; j < walls + i; j++)
			{
				state->grid[j] = NUMBER_WALL;
				SET_BIT(state->immutable, j);
			}
			
			state->last -= walls;
			i += walls;
		}
		else
			++p;
	}

	if (state->mode == MODE_EDGES)
	{
		for (i = 0; i < w; i++)
		{
			j = i;
			if (state->grid[j] >= 0)
				state->grid[j] = NUMBER_EDGE(state->grid[j]);
			j = i + (w*(h - 1));
			if (state->grid[j] >= 0)
				state->grid[j] = NUMBER_EDGE(state->grid[j]);
		}
		for (i = 1; i < h - 1; i++)
		{
			j = w*i;
			if (state->grid[j] >= 0)
				state->grid[j] = NUMBER_EDGE(state->grid[j]);
			j = w*i + (w - 1);
			if (state->grid[j] >= 0)
				state->grid[j] = NUMBER_EDGE(state->grid[j]);
		}

		for (i = 0; i < w*h; i++)
		{
			if (IS_NUMBER_EDGE(state->grid[i]))
				state->last--;
		}
	}
	
	for(i = 0; i < w; i++)
	{
		if(state->grid[i] == NUMBER_WALL)
			state->grid[i] = NUMBER_BOUND;
		if(state->grid[(w*h)-(i+1)] == NUMBER_WALL)
			state->grid[(w*h)-(i+1)] = NUMBER_BOUND;
	}
	
	for(i = 0; i < h; i++)
	{
		if(state->grid[i*w] == NUMBER_WALL)
			state->grid[i*w] = NUMBER_BOUND;
		if(state->grid[(i*w)+(w-1)] == NUMBER_WALL)
			state->grid[(i*w)+(w-1)] = NUMBER_BOUND;
	}
	
	do
	{
		walls = 0;
		for(i = 0; i < w*h; i++)
		{
			if(state->grid[i] != NUMBER_WALL) continue;
			
			int x = i%w;
			int y = i/w;
			
			if((x < w-1 && state->grid[i+1] == NUMBER_BOUND) ||
				(x > 0 && state->grid[i-1] == NUMBER_BOUND) ||
				(y < w-1 && state->grid[i+w] == NUMBER_BOUND) ||
				(y > 0 && state->grid[i-w] == NUMBER_BOUND))
			{
				state->grid[i] = NUMBER_BOUND;
				walls++;
			}
		}
	} while(walls);
	
	return state;
}

static game_state *dup_game(const game_state *state)
{
	int w = state->w;
	int h = state->h;
	
	game_state *ret = snew(game_state);

	ret->w = w;
	ret->h = h;
	ret->mode = state->mode;
	ret->last = state->last;
	ret->completed = state->completed;
	ret->cheated = state->cheated;
	ret->grid = snewn(w*h, number);
	ret->immutable = snewn(BITMAP_SIZE(w*h), bitmap);
	
	memcpy(ret->grid, state->grid, w*h*sizeof(number));
	memcpy(ret->immutable, state->immutable, BITMAP_SIZE(w*h));

	if (state->path)
	{
		ret->path = snewn(w*h, int);
		memcpy(ret->path, state->path, w*h * sizeof(int));
	}
	else
		ret->path = NULL;
	
	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->grid);
	sfree(state->immutable);
	sfree(state->path);
	sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
						const char *aux, const char **error)
{
	int i, w = state->w, h = state->h;
	struct solver_scratch *scratch = new_scratch(w, h, state->mode, state->last);
	
	ascent_solve(state->grid, DIFFCOUNT, scratch);

	char *ret = snewn(w*h*4, char);
	char *p = ret;
	*p++ = 'S';
	for(i = 0; i < w*h; i++)
	{
		if(scratch->grid[i] >= 0)
			p += sprintf(p, "%d,", scratch->grid[i]+1);
		else
			p += sprintf(p, "-,");
	}
	
	*p++ = '\0';
	ret = sresize(ret, p - ret, char);
	free_scratch(scratch);
	return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
	return !IS_HEXAGONAL(params->mode);
}

static char *game_text_format(const game_state *state)
{
	int w = state->w, h = state->h;
	int x, y;
	int space = w*h >= 100 ? 3 : 2;
	
	char *ret = snewn(w*h*(space+1) + 1, char);
	char *p = ret;
	number n;
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		n = state->grid[y*w+x];
		if (IS_NUMBER_EDGE(n))
			n = NUMBER_EDGE(n);

		if(n >= 0)
			p += sprintf(p, "%*d", space, n+1);
		else if(n == NUMBER_WALL)
			p += sprintf(p, "%*s", space, "#");
		else if (n == NUMBER_BOUND)
			p += sprintf(p, "%*s", space, " ");
		else
			p += sprintf(p, "%*s", space, ".");
		*p++ = x < w-1 ? ' ' : '\n';
	}
	*p++ = '\0';
	
	return ret;
}

/* ************** *
 * User Interface *
 * ************** */

#define TARGET_SHOW 0x1
#define TARGET_CONNECTED 0x2

struct game_ui
{
	cell held;
	number select, next_target, prev_target;
	char next_target_mode, prev_target_mode;
	int dir;
	
	cell *positions;
	number *prevhints, *nexthints;
	int s;

	/* Current state of keyboard cursor */
	enum { CSHOW_NONE, CSHOW_KEYBOARD, CSHOW_MOUSE } cshow;
	cell typing_cell;
	number typing_number;
	int cx, cy;

	cell doubleclick_cell;
	int dragx, dragy;

	/* User interface tweaks */
	bool move_with_numpad;
};

static game_ui *new_ui(const game_state *state)
{
	int i, w = state ? state->w : 1, s = state ? w*state->h : 1;
	game_ui *ret = snew(game_ui);
	
	ret->held = CELL_NONE;
	ret->select = ret->next_target = ret->prev_target = NUMBER_EMPTY;
	ret->next_target_mode = ret->prev_target_mode = 0;
	ret->dir = 0;
	ret->positions = snewn(s, cell);
	ret->prevhints = snewn(s, number);
	ret->nexthints = snewn(s, number);
	ret->s = s;
	ret->cshow = CSHOW_NONE;

	ret->move_with_numpad = false;

	if (!state) return ret;

	/* Initialize UI from existing grid */
	for (i = 0; i < s; i++)
	{
		if (state->grid[i] != NUMBER_BOUND) break;
	}
	ret->cx = i%w;
	ret->cy = i/w;
	ret->typing_cell = CELL_NONE;
	ret->typing_number = 0;
	ret->dragx = ret->dragy = -1;
	ret->doubleclick_cell = -1;
	
	update_positions(ret->positions, state->grid, s);
	update_path_hints(ret->prevhints, ret->nexthints, state);
	return ret;
}

static void free_ui(game_ui *ui)
{
	sfree(ui->positions);
	sfree(ui->nexthints);
	sfree(ui->prevhints);
	sfree(ui);
}

static config_item *get_prefs(game_ui *ui)
{
	config_item *ret;

	ret = snewn(2, config_item);

	ret[0].name = "Numpad inputs";
	ret[0].kw = "numpad";
	ret[0].type = C_CHOICES;
	ret[0].u.choices.choicenames = ":Enter numbers:Move cursor";
	ret[0].u.choices.choicekws = ":number:cursor";
	ret[0].u.choices.selected = ui->move_with_numpad;

	ret[1].name = NULL;
	ret[1].type = C_END;

	return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
	ui->move_with_numpad = cfg[0].u.choices.selected;
}

static char *encode_ui_item(const int *arr, int s, char *p)
{
	int i, run = 0;
	for(i = 0; i < s; i++)
	{
		if(arr[i] != -1)
		{
			if(i != 0)
				*p++ = run ? 'a' + run-1 : '_';
			if(arr[i] == -2)
				*p++ = '-';
			else
				p += sprintf(p, "%d", arr[i]);
			run = 0;
		}
		else
		{
			if (run == 26)
			{
				*p++ = ('a'-1) + run;
				run = 0;
			}
			run++;
		}
	}
	if(run)
		*p++ = 'a' + run-1;
	
	return p;
}

static char *encode_ui(const game_ui *ui)
{
	/*
	 * Resuming a saved game will not create a ui based on the current state,
	 * but based on the original state. This causes most lines to disappear
	 * from the screen, until the user interacts with the game.
	 * To remedy this, the positions array is included in the save file.
	 */
	int s = ui->s;
	char *ret = snewn(s*12, char);
	char *p = ret;

	*p++ = 'P';
	p = encode_ui_item(ui->positions, s, p);

	*p++ = 'H';
	p = encode_ui_item(ui->prevhints, s, p);

	*p++ = 'N';
	p = encode_ui_item(ui->nexthints, s, p);

	*p++ = '\0';
	ret = sresize(ret, p - ret, char);
	return ret;
}

static const char *decode_ui_item(int *arr, int s, char stop, const char *p)
{
	int i = 0;
	while(*p && *p != stop && i < s)
	{
		if(isdigit((unsigned char) *p))
		{
			arr[i] = atoi(p);
			if(arr[i] >= s) arr[i] = -2;
			while (*p && isdigit((unsigned char) *p)) ++p;
			++i;
		}
		else if(*p == '-')
		{
			arr[i] = -2;
			++i;
			++p;
		}
		else if(*p >= 'a' && *p <= 'z')
			i += ((*p++) - 'a') + 1;
		else
			++p;
	}

	return p;
}

static void decode_ui(game_ui *ui, const char *encoding, const game_state *state)
{
	if(!encoding || encoding[0] != 'P') return;
	
	int s = ui->s;
	int i;
	const char *p = encoding+1;
	
	for(i = 0; i < s; i++)
	{
		ui->positions[i] = CELL_NONE;
		ui->prevhints[i] = NUMBER_EMPTY;
		ui->nexthints[i] = NUMBER_EMPTY;
	}

	p = decode_ui_item(ui->positions, s, 'H', p);
	p = decode_ui_item(ui->prevhints, s, 'N', p);
	p = decode_ui_item(ui->nexthints, s, '\0', p);
}

static void ui_clear(game_ui *ui)
{
	/* Deselect the current number */
	ui->held = CELL_NONE;
	ui->select = ui->next_target = ui->prev_target = NUMBER_EMPTY;
	ui->next_target_mode = ui->prev_target_mode = 0;
	ui->dir = 0;
}

static void ui_seek(game_ui *ui, const game_state *state)
{
	/* 
	 * Find the two numbers which should be highlighted. 
	 *
	 * When clicking a number which has both consecutive numbers known, this will
	 * be the two numbers on the edge of the current line.
	 *
	 * When clicking a number with neither consecutive numbers known, this will
	 * be the next placed number in either direction.
	 *
	 * When clicking a number which has only one consecutive number known, this
	 * will be the next placed number in one direction. The other highlight
	 * will be invisible.
	 */
	number start;

	if (ui->held < 0)
		start = NUMBER_EMPTY;
	else if (ui->nexthints[ui->held] != NUMBER_EMPTY)
		start = ascent_follow_path(state, ui->held, CELL_NONE, NULL);
	else
		start = state->grid[ui->held];

	ui->next_target_mode = ui->prev_target_mode = 0;

	if(start < 0)
	{
		ui->select = NUMBER_EMPTY;
		ui->next_target = NUMBER_EMPTY;
		ui->prev_target = NUMBER_EMPTY;
	}
	else
	{
		number n = start;
		bool hasnext = n == state->last || ui->positions[n + 1] != CELL_NONE;
		bool hasprev = n == 0 || ui->positions[n - 1] != CELL_NONE;
		ui->dir = n < 0 || (hasnext && hasprev) ? 0 : 
			hasnext ? -1 : hasprev ? +1 : 0;
		ui->select = start + ui->dir;

		n = start;
		do 
			n++;
		while(n + 1 <= state->last && ui->positions[n] == CELL_NONE);
		ui->next_target = n;

		n = start;
		do 
			n--;
		while(n - 1 >= 0 && ui->positions[n] == CELL_NONE);
		ui->prev_target = n;

		hasprev = start == 0 || abs(ui->prev_target - start) == 1;
		hasnext = start == state->last || abs(ui->next_target - start) == 1;

		if (!hasnext || hasprev)
			ui->next_target_mode |= TARGET_SHOW;
		if (!hasprev || hasnext)
			ui->prev_target_mode |= TARGET_SHOW;
		if (hasnext && hasprev)
		{
			ui->next_target_mode |= TARGET_CONNECTED;
			ui->prev_target_mode |= TARGET_CONNECTED;
		}

		/* Look for the edges of the current line */
		if(hasnext)
		{
			while(ui->next_target + 1 <= state->last && ui->positions[ui->next_target + 1] != CELL_NONE)
				ui->next_target++;
			if (ui->next_target == state->last)
				ui->next_target_mode &= ~TARGET_SHOW;
		}
		if(hasprev)
		{
			while(ui->prev_target - 1 >= 0 && ui->positions[ui->prev_target - 1] != CELL_NONE)
				ui->prev_target--;
			if (ui->prev_target == 0)
				ui->prev_target_mode &= ~TARGET_SHOW;
		}

		if (ui->next_target > state->last)
			ui->next_target = NUMBER_EMPTY;
	}
}

static void ui_backtrack(game_ui *ui, const game_state *state)
{
	/* 
	 * Move the selection backward until a placed number is found, 
	 * then point the selection forward again.
	 */

	number n = ui->select;
	if(!ui->dir || n < 0)
	{
		cell i = ui->held;
		int path = state->path && i >= 0 ? state->path[i] : 0;

		if(path && state->grid[i] == CELL_NONE)
		{
			const ascent_movement *movement = ascent_movement_for_mode(state->mode);
			int w = state->w;
			int dir;
			cell i2;

			n = 0;
			for(dir = 0; dir < movement->dircount && !n; dir++)
			{
				if(!(path & (1<<dir))) continue;

				i2 = (movement->dirs[dir].dy * w) + movement->dirs[dir].dx + i;
				n = ascent_follow_path(state, i2, i, NULL);
			}
		}

		ui->select = n;
		ui->dir = 0;
		ui_seek(ui, state);
		return;
	}
	
	do
	{
		n -= ui->dir;
		ui->held = ui->positions[n];
	}
	while(ui->dir && n > 0 && n < state->last && ui->held == CELL_NONE);
	
	ui->select = n + ui->dir;
	ui_seek(ui, state);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
	update_positions(ui->positions, newstate->grid, newstate->w*newstate->h);
	update_path_hints(ui->prevhints, ui->nexthints, newstate);
	
	if(ui->held >= 0 && ui->select >= 0 && newstate->grid[ui->held] == NUMBER_EMPTY)
	{
		ui_backtrack(ui, newstate);
	}
	if(!oldstate->completed && newstate->completed)
	{
		ui_clear(ui);
	}
	else
	{
		if (ui->held >= 0)
			ui->select = newstate->grid[ui->held];

		ui_seek(ui, newstate);
	}
}

struct game_drawstate {
	int tilesize, w, h;
	double thickness;
	int offsetx, offsety;

	int *colours;
	bool redraw;
	cell *oldpositions;
	number *oldgrid;
	cell oldheld;
	number old_next_target, old_prev_target;
	int *oldpath, *path;
	number *prevhints, *nexthints;

	/* Blitter for the background of the keyboard cursor */
	blitter *bl;
	bool bl_on;
	/* Position of the center of the blitter */
	int blx, bly;
	/* Radius of the keyboard cursor */
	int blr;
};

static int ascent_count_segments(const game_state *ret, cell i)
{
	const ascent_movement *movement = ascent_movement_for_mode(ret->mode);
	int segments = 0;
	int w = ret->w, s = w * ret->h;
	number n, n2;
	cell j;
	int dir;

	n = ret->grid[i];

	if(IS_OBSTACLE(n))
		return 2;

	if(n == 0 || n == ret->last)
		segments++;

	for(dir = 0; dir < movement->dircount; dir++)
	{
		if(ret->path && ret->path[i] & (1<<dir))
		{
			j = i + (w*movement->dirs[dir].dy) + movement->dirs[dir].dx;
			n2 = ret->grid[j];
			if(n < 0 || n2 < 0 || abs(n-n2) != 1)
				segments++;
		}
	}
	
	if(n >= 0)
	{
		for(j = 0; j < s; j++)
		{
			if(n > 0 && ret->grid[j] == n-1)
				segments++;
			if(ret->grid[j] == n+1)
				segments++;
		}
	}

	return segments;
}

static bool ascent_validate_path_move(cell i, const game_state *state, const game_ui *ui)
{
	if (ui->held < 0 || ui->held == i)
		return false;

	int w = state->w;
	number start = ui->held >= 0 ? state->grid[ui->held] : NUMBER_EMPTY;
	number n = state->grid[i];

	if (!is_near(ui->held, i, w, state->mode))
		return false;

	const ascent_movement *movement = ascent_movement_for_mode(state->mode);
	int dir1 = ascent_find_direction(ui->held, i, w, movement);
	int dir2 = ascent_find_direction(i, ui->held, w, movement);

	/* Don't draw a line between two adjacent confirmed numbers */
	if (state->grid[i] >= 0 && start >= 0)
		return false;

	/* Don't connect to a cell with two confirmed path segments, except when erasing a line*/
	if (ascent_count_segments(state, ui->held) == 2 && !(state->path && state->path[ui->held] & (1 << dir1)))
		return false;
	if (ascent_count_segments(state, i) == 2 && !(state->path && state->path[i] & (1 << dir2)))
		return false;

	if (state->path && !(state->path[i] & (1 << dir2)))
	{
		/* Don't connect a line to a confirmed number if the hints don't match */
		if (start >= 0 && ui->nexthints[i] != NUMBER_EMPTY &&
			ui->nexthints[i] - start != -1 &&
			ui->prevhints[i] - start != +1)
			return false;

		if (n >= 0 && ui->nexthints[ui->held] != NUMBER_EMPTY &&
			ui->nexthints[ui->held] - n != -1 &&
			ui->prevhints[ui->held] - n != +1)
			return false;

		/* Don't connect two line ends if both have hints, and they don't match */
		if (ui->nexthints[i] != NUMBER_EMPTY && ui->nexthints[ui->held] != NUMBER_EMPTY &&
			ui->nexthints[ui->held] - ui->prevhints[i] != -1 &&
			ui->prevhints[ui->held] - ui->nexthints[i] != +1)
			return false;
	}

	return true;
}

#define DRAG_RADIUS 0.6F

static char *ascent_mouse_click(const game_state *state, game_ui *ui,
                                int gx, int gy, int button, bool keyboard)
{
	/*
	 * There are four ways to enter a number:
	 *
	 * 1. Click a number to highlight it, then click (or drag to) an adjacent
	 * cell to place the next number in the sequence. The arrow keys and Enter
	 * can be used to emulate mouse clicks.
	 *
	 * 2. Click an empty cell, then type a multi-digit number. To confirm a
	 * number, either press Enter, an arrow key, or click any cell.
	 *
	 * 3. In Edges mode, click and drag from an edge number, then release in an
	 * empty grid cell in the same row, column or diagonal.
	 * 
	 * 4. Connect two numbers with a path, and all cells inbetween the 
	 * two numbers will be filled.
	 * 
	 * Paths can be added in two ways:
	 * 
	 * 1. Drag with the left mouse button between two adjacent cells.
	 * 2. Highlight a cell, then move the keyboard cursor to an adjacent cell
	 * and press Enter.
	 */
	
	char buf[80];
	int w = state->w, h = state->h;
	cell i = gy*w+gx;
	number n = state->grid[i];
	number start = ui->held >= 0 ? state->grid[ui->held] : NUMBER_EMPTY;

	switch(button)
	{
	case LEFT_BUTTON:
		ui->doubleclick_cell = ui->held == i ? i : -1;

		/* Click on edge number */
		if (IS_NUMBER_EDGE(n) && ui->positions[NUMBER_EDGE(n)] == CELL_NONE)
		{
			ui->held = i;
			ui->next_target = ui->prev_target = NUMBER_EMPTY;
			ui->select = n;
			ui->dir = 0;
			return NULL;
		}
		/* Click on wall */
		if(IS_OBSTACLE(n))
		{
			ui_clear(ui);
			return NULL;
		}
		if(n >= 0)
		{
			/* When using the keyboard, draw a line to this number */
			if (keyboard && ascent_validate_path_move(i, state, ui))
			{
				sprintf(buf, "L%d,%d", i, ui->held);
				ui->held = i;
				return dupstr(buf);
			}

			/* Highlight a placed number */
			ui->held = i;
			ui_seek(ui, state);
			return NULL;
		}
		if (n == NUMBER_EMPTY && IS_NUMBER_EDGE(ui->select) && is_edge_valid(ui->held, i, w, h))
		{
			n = NUMBER_EDGE(ui->select);
			sprintf(buf, "P%d,%d", i, n);
			
			ui->held = i;
			ui_seek(ui, state);

			return dupstr(buf);
		}
	/* Deliberate fallthrough */
	case LEFT_DRAG:
		if(ui->doubleclick_cell != i)
			ui->doubleclick_cell = -1;

		/* Update cursor position when dragging a number from the edge */
		if (IS_NUMBER_EDGE(ui->select) && button == LEFT_DRAG)
		{
			ui->dragx = gx;
			ui->dragy = gy;

			if (ui->held % w > 0 && ui->held % w < w - 1)
				ui->dragx = -1;
			if (ui->held / w > 0 && ui->held / w < h - 1)
				ui->dragy = -1;

			return NULL;
		}
		/* Dragging over a number in sequence will move the highlight forward or backward */
		if(n >= 0 && ui->held >= 0 && start >= 0 &&
			((n > start && (ui->next_target_mode & TARGET_CONNECTED) && n <= ui->next_target) ||
			(n < start && (ui->prev_target_mode & TARGET_CONNECTED) && n >= ui->prev_target)))
		{
			ui->held = i;
			ui_seek(ui, state);
			ui->cshow = CSHOW_NONE;
			return NULL;
		}
		/* Place the next number */
		if(n == NUMBER_EMPTY && ui->held >= CELL_NONE && ui->select >= 0 &&
			ui->positions[ui->select] == CELL_NONE && is_near(ui->held, i, w, state->mode) &&
			/* Don't place a number if it doesn't fit the suggested number */
			!(ui->nexthints[i] != NUMBER_EMPTY && ui->nexthints[i] != ui->select && ui->prevhints[i] != ui->select))
		{
			if (state->path && state->path[i] & FLAG_COMPLETE)
				return NULL;

			sprintf(buf, "P%d,%d", i, ui->select);
			
			ui->held = i;
			ui_seek(ui, state);
			
			if(!keyboard)
				ui->cshow = CSHOW_NONE;

			return dupstr(buf);
		}
		/* Keyboard-drag a pathline */
		else if (keyboard && !ui->dir && ascent_validate_path_move(i, state, ui))
		{
			sprintf(buf, "L%d,%d", i, ui->held);
			ui->held = i;
			return dupstr(buf);
		}
		/* Highlight an empty cell */
		else if(n == NUMBER_EMPTY && button == LEFT_BUTTON)
		{
			ui_clear(ui);
			ui->cx = i % w;
			ui->cy = i / w;
			ui->cshow = keyboard ? CSHOW_KEYBOARD : CSHOW_MOUSE;

			ui->held = i;
			ui->select = NUMBER_EMPTY;
			ui->dir = 0;
			ui_backtrack(ui, state);
			return NULL;
		}
		/* Drag a pathline */
		else if(!ui->dir && ascent_validate_path_move(i, state, ui))
		{
			sprintf(buf, "L%d,%d", i, ui->held);
			ui->held = i;
			ui->cshow = CSHOW_NONE;
			return dupstr(buf);
		}
	break;
	case LEFT_RELEASE:
		ui->dragx = ui->dragy = -1;

		if(ui->doubleclick_cell == i)
		{
			/* Deselect number */
			ui_clear(ui);
			if(ui->cshow == CSHOW_MOUSE)
				ui->cshow = CSHOW_NONE;
		}
		/* Drop number from edge into grid */
		else if (n == NUMBER_EMPTY && IS_NUMBER_EDGE(ui->select) && is_edge_valid(ui->held, i, w, h))
		{
			sprintf(buf, "P%d,%d", i, NUMBER_EDGE(ui->select));
			ui_clear(ui);
			return dupstr(buf);
		}
	break;
	case MIDDLE_BUTTON:
	case RIGHT_BUTTON:
		if(n == NUMBER_EMPTY || GET_BIT(state->immutable, i))
		{
			ui_clear(ui);
		}

	/* Deliberate fallthrough */
	case MIDDLE_DRAG:
	case RIGHT_DRAG:
		/* Drag over numbers to clear them */
		if(ui->typing_cell == CELL_NONE && !GET_BIT(state->immutable, i) &&
			(n != NUMBER_EMPTY || (state->path && state->path[i])))
		{
			sprintf(buf, "C%d", i);
			return dupstr(buf);
		}
	}

	return NULL;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int ox, int oy, int button)
{
	int w = state->w, h = state->h;
	int gx, gy;
	int tilesize = ds->tilesize;
	cell i;
	number n;
	char *ret = NULL;
	ascent_step dir = {0,0};
	bool finish_typing = false;
	
	oy -= ds->offsety;
	ox -= ds->offsetx;

	/*
	 * Handle dragging a number from the edge into the grid. When dragging
	 * from a diagonal edge number, adjust the coordinates to always move
	 * along the diagonal line.
	 */
	if (IS_NUMBER_EDGE(ui->select) && (button == LEFT_DRAG || button == LEFT_RELEASE))
	{
		int ex = ui->held % w;
		int ey = ui->held / w;
		int tx = ex * tilesize;
		int ty = ey * tilesize;

		if (ex > 0 && ex < w - 1)
			ox = tx;
		else if (ey > 0 && ey < h - 1)
			oy = ty;
		else
		{
			if (ex > 0)
				tx += (tilesize-1);
			if (ey > 0)
				ty += (tilesize-1);

			int distance = (abs(ox - tx) + abs(oy - ty) + 1) / 2;
			if (distance >= (min(w, h) - 1) * tilesize)
				distance = 0;
			ox = ex == 0 ? distance : tx - distance;
			oy = ey == 0 ? distance : ty - distance;
		}
	}

	gy = oy < 0 ? -1 : oy / tilesize;
	if (IS_HEXAGONAL(state->mode))
	{
		ox -= gy * tilesize / 2;
	}
	gx = ox < 0 ? -1 : ox / tilesize;

	if (IS_MOUSE_DOWN(button))
	{
		ui->cshow = CSHOW_NONE;
		finish_typing = true;
	}

	/* Parse keyboard cursor movement */
	if(ui->move_with_numpad)
	{
		if(button == (MOD_NUM_KEYPAD | '8')) button = CURSOR_UP;
		if(button == (MOD_NUM_KEYPAD | '2')) button = CURSOR_DOWN;
		if(button == (MOD_NUM_KEYPAD | '4')) button = CURSOR_LEFT;
		if(button == (MOD_NUM_KEYPAD | '6')) button = CURSOR_RIGHT;
	}
	else
		button &= ~MOD_NUM_KEYPAD;

	if(IS_HEXAGONAL(state->mode))
	{
		/*
		 * When moving across a hexagonal field, moving the cursor up or down
		 * will alternate between moving orthogonally and diagonally.
		 */
		if (button == CURSOR_UP && ui->cy > 0 && (ui->cy & 1) == 0)
			button = MOD_NUM_KEYPAD | '9';
		else if(button == CURSOR_DOWN && ui->cy < h-1 && ui->cy & 1)
			button = MOD_NUM_KEYPAD | '1';
		/* Moving top-left or down-right is replaced with moving directly up or down. */
		else if(button == (MOD_NUM_KEYPAD | '7'))
			button = CURSOR_UP;
		else if(button == (MOD_NUM_KEYPAD | '3'))
			button = CURSOR_DOWN;
	}

	/* Apply keyboard cursor movement */
	if      (button == CURSOR_UP)              dir = (ascent_step){ 0, -1};
	else if (button == CURSOR_DOWN)            dir = (ascent_step){ 0,  1};
	else if (button == CURSOR_LEFT)            dir = (ascent_step){-1,  0};
	else if (button == CURSOR_RIGHT)           dir = (ascent_step){ 1,  0};
	else if (button == (MOD_NUM_KEYPAD | '7')) dir = (ascent_step){-1, -1};
	else if (button == (MOD_NUM_KEYPAD | '1')) dir = (ascent_step){-1,  1};
	else if (button == (MOD_NUM_KEYPAD | '9')) dir = (ascent_step){ 1, -1};
	else if (button == (MOD_NUM_KEYPAD | '3')) dir = (ascent_step){ 1,  1};

	if (dir.dx || dir.dy)
	{
		ui->cshow = CSHOW_KEYBOARD;
		ui->cx += dir.dx;
		ui->cy += dir.dy;

		ui->cx = max(0, min(ui->cx, w-1));
		ui->cy = max(0, min(ui->cy, h-1));
		
		if (state->mode == MODE_HEXAGON)
		{
			int center = h / 2;
			if (ui->cy < center)
				ui->cx = max(ui->cx, center - ui->cy);
			else
				ui->cx = min(ui->cx, (w-1) + center - ui->cy);
		}
		if (state->mode == MODE_HONEYCOMB)
		{
			int extra = (h | ui->cy) & 1 ? 0 : 1;
			ui->cx = min(ui->cx, w - (ui->cy / 2) - 1);
			ui->cx = max(ui->cx, ((h - ui->cy) / 2) - extra);
		}

		finish_typing = true;
	}

	/* Clicking outside the grid clears the selection. */
	if (IS_MOUSE_DOWN(button) && (gx < 0 || gy < 0 || gx >= w || gy >= h))
		ui_clear(ui);

	/* Pressing Enter, Spacebar or Backspace when not typing will emulate a mouse click */
	if (button == '\b' && ui->typing_cell == CELL_NONE)
		button = CURSOR_SELECT2;
	if (IS_CURSOR_SELECT(button) && ui->cshow == CSHOW_KEYBOARD && ui->typing_cell == CELL_NONE)
	{
		ret = ascent_mouse_click(state, ui, ui->cx, ui->cy,
		      button == CURSOR_SELECT ? LEFT_BUTTON : RIGHT_BUTTON, true);
		if(!ret)
		ret = ascent_mouse_click(state, ui, ui->cx, ui->cy,
		      button == CURSOR_SELECT ? LEFT_RELEASE : RIGHT_RELEASE, true);
	}
	/* Press Enter to confirm typing */
	if (IS_CURSOR_SELECT(button))
		finish_typing = true;

	/* Typing a number */
	if (button >= '0' && button <= '9' && ui->cshow)
	{
		i = ui->cy*w + ui->cx;
		if (GET_BIT(state->immutable, i)) return NULL;
		if (ui->typing_cell == CELL_NONE && state->grid[i] != NUMBER_EMPTY) return NULL;
		n = ui->typing_number;
		n *= 10;
		n += button - '0';

		ui_clear(ui);
		ui->typing_cell = i;
		if (n < 1000)
			ui->typing_number = n;
		return MOVE_UI_UPDATE;
	}

	/* Remove the last digit when typing */
	if (button == '\b' && ui->typing_cell != CELL_NONE)
	{
		ui->typing_number /= 10;
		if (ui->typing_number == 0)
			ui->typing_cell = CELL_NONE;
		return MOVE_UI_UPDATE;
	}

	if (gx >= 0 && gx < w && gy >= 0 && gy < h)
	{
		if(IS_MOUSE_DRAG(button) && ui->held >= 0 && !IS_NUMBER_EDGE(ui->select))
		{
			int hx = (gx * tilesize) + (tilesize / 2);
			int hy = (gy * tilesize) + (tilesize / 2);
			
			/* 
			 * When dragging, the mouse must be close enough to the center of
			 * the new cell. The hitbox is octagon-shaped to avoid drawing a 
			 * straight line when trying to draw a diagonal line.
			 */
			if(abs(ox-hx) + abs(oy-hy) > DRAG_RADIUS*tilesize)
				return NULL;
		}
		ret = ascent_mouse_click(state, ui, gx, gy, button, false);
		finish_typing = true;
	}
	
	/* Confirm typed number */
	if (finish_typing && !ret && ui->typing_cell != CELL_NONE)
	{
		char buf[20];
		n = ui->typing_number - 1;
		i = ui->typing_cell;
		ui->typing_cell = CELL_NONE;
		ui->typing_number = 0;

		/* When clicking the cell being typed, initiate a drag */
		if (ui->cshow == CSHOW_MOUSE && ui->cy*w+ui->cx == i)
		{
			ui->held = i;
			ui->dir = n < state->last && ui->positions[n + 1] == CELL_NONE ? +1
				: n > 0 && ui->positions[n - 1] == CELL_NONE ? -1 : +1;
			ui->select = n + ui->dir;
			ui_seek(ui, state);
		}

		if (state->grid[i] == n || n > state->last)
			return MOVE_UI_UPDATE;

		sprintf(buf, "P%d,%d", i, n);
		ret = dupstr(buf);
	}

	if(finish_typing && !ret)
		return MOVE_UI_UPDATE;
	return ret;
}

static bool ascent_modify_path(game_state *ret, char move, cell i, cell i2)
{
	const ascent_movement *movement = ascent_movement_for_mode(ret->mode);
	int dir = ascent_find_direction(i, i2, ret->w, movement);

	if (dir == -1)
		return false;

	if (move == 'L' && !(ret->path[i] & (1 << dir)))
		ret->path[i] |= (1 << dir);
	else
		ret->path[i] &= ~(1 << dir);

	int segments = ascent_count_segments(ret, i);

	if(segments == 2)
		ret->path[i] |= FLAG_COMPLETE;
	else
		ret->path[i] &= ~FLAG_COMPLETE;

	return true;
}

static void ascent_clean_path(game_state *state)
{
	/*
	 * Remove all useless or unstable path segments. This function removes path
	 * segments between two confirmed numbers, and makes sure no cell contains
	 * more than two path segments.
	 */
	int w = state->w, h = state->h;
	int dir;
	cell i, i2;
	const ascent_movement *movement = ascent_movement_for_mode(state->mode);
	for(i = 0; i < w*h; i++)
	{
		if(state->grid[i] < 0) continue;

		/* Unset path lines connecting two adjacent numbers */
		for(dir = 0; dir < movement->dircount; dir++)
		{
			if(state->path[i] & (1<<dir))
			{
				i2 = (movement->dirs[dir].dy * w) + movement->dirs[dir].dx + i;
				if(state->grid[i2] >= 0)
				{
					ascent_modify_path(state, 'D', i, i2);
					ascent_modify_path(state, 'D', i2, i);
				}
			}
		}

		/* If any number connects to a sequential cell and has 2 unrelated path lines, unset all path lines */
		if(ascent_count_segments(state, i) > 2)
		{
			for(dir = 0; dir < movement->dircount; dir++)
			{
				if(state->path[i] & (1<<dir))
				{
					i2 = (movement->dirs[dir].dy * w) + movement->dirs[dir].dx + i;
					ascent_modify_path(state, 'D', i, i2);
					ascent_modify_path(state, 'D', i2, i);
				}
			}
		}
	}
}

static bool ascent_apply_path(game_state *state, const cell *positions)
{
	/* Check all numbers, and place an adjacent number when possible. */
	int w = state->w;
	cell i, i2; number n, n2, cn;
	bool ret = false;
	int dir;
	const ascent_movement *movement = ascent_movement_for_mode(state->mode);

	for(n = 0; n <= state->last; n++)
	{
		i = positions[n];
		if(i < 0) continue;
		if(!(state->path[i] & ~FLAG_COMPLETE)) continue;

		cn = NUMBER_EMPTY;

		n2 = n - 1;
		i2 = n > 0 ? positions[n2] : i;
		if(i2 != CELL_NONE && i2 != CELL_MULTIPLE)
			cn = n + 1;
		
		n2 = n + 1;
		i2 = n < state->last ? positions[n2] : i;
		if(i2 != CELL_NONE && i2 != CELL_MULTIPLE)
			cn = n - 1;
		
		for(dir = 0; dir < movement->dircount; dir++)
		{
			if(!(state->path[i] & (1<<dir))) continue;

			i2 = (movement->dirs[dir].dy * w) + movement->dirs[dir].dx + i;
			if(cn != NUMBER_EMPTY && state->grid[i2] == NUMBER_EMPTY)
			{
				state->grid[i2] = cn;
				ret = true;
			}
			else
			{
				n2 = ascent_follow_path(state, i2, i, NULL);
				if(n2 != NUMBER_EMPTY && abs(n-n2) > 1)
				{
					state->grid[i2] = n < n2 ? n + 1 : n - 1;
					ret = true;
				}
			}
		}
	}

	return ret;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	int w = state->w, h = state->h;
	cell i = -1, i2 = -1;
	number n = -1;
	const char *p = move;
	game_state *ret = dup_game(state);

	while (*p)
	{
		if (*p == 'P' &&
			sscanf(p + 1, "%d,%d", &i, &n) == 2 &&
			i >= 0 && i < w*h && n >= 0 && n <= state->last
			)
		{
			if (GET_BIT(state->immutable, i))
			{
				free_game(ret);
				return NULL;
			}

			ret->grid[i] = n;
		}
		else if ((*p == 'L' || *p == 'D') &&
			sscanf(p + 1, "%d,%d", &i, &i2) == 2 &&
			i >= 0 && i < w*h && i2 >= 0 && i2 < w*h)
		{
			if (*p == 'L' && !ret->path)
			{
				ret->path = snewn(w*h, int);
				memset(ret->path, 0, w*h * sizeof(int));
			}

			if(ret->path)
			{
				if(!ascent_modify_path(ret, *p, i, i2) || !ascent_modify_path(ret, *p, i2, i))
				{
					free_game(ret);
					return NULL;
				}
			}
		}
		else if (*p == 'C')
		{
			i = atoi(p + 1);
			if (i < 0 || i >= w*h || GET_BIT(state->immutable, i))
			{
				free_game(ret);
				return NULL;
			}

			ret->grid[i] = NUMBER_EMPTY;

			if(ret->path && ret->path[i])
			{
				int dir;
				const ascent_movement *movement = ascent_movement_for_mode(ret->mode);

				for(dir = 0; dir < movement->dircount; dir++)
				{
					if(ret->path[i] & (1<<dir))
					{
						i2 = (movement->dirs[dir].dy * w) + movement->dirs[dir].dx + i;
						ascent_modify_path(ret, 'D', i, i2);
						ascent_modify_path(ret, 'D', i2, i);
					}
				}
				ret->path[i] = 0;
			}
		}
		else if (*p == 'S')
		{
			p++;
			for (i = 0; i < w*h; i++)
			{
				if (*p != '-')
				{
					n = atoi(p) - 1;
					ret->grid[i] = n;
					while (*p && isdigit((unsigned char)*p))p++;
				}
				else if (*p == '-')
				{
					if (!GET_BIT(ret->immutable, i)) ret->grid[i] = NUMBER_EMPTY;
					p++;
				}
				if (!*p)
				{
					free_game(ret);
					return NULL;
				}
				p++; /* Skip comma */
			}
		}
		while (*p && *p != ';') p++;
		if (*p == ';') p++;
	}
	
	if(ret->path)
	{
		cell *positions = snewn(w*h, cell);

		do
		{
			ascent_clean_path(ret);
			update_positions(positions, ret->grid, w*h);
		} while(ascent_apply_path(ret, positions));

		for(i = 0; i < w*h; i++)
		{
			if(ret->path[i] & ~FLAG_COMPLETE)
				break;
		}
		if(i == w*h)
		{
			/* No path segments found, free path array */
			sfree(ret->path);
			ret->path = NULL;
		}

		sfree(positions);
	}
	
	if (check_completion(ret->grid, w, h, ret->mode))
		ret->completed = true;

	return ret;
}

/* **************** *
 * Drawing routines *
 * **************** */

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
	int cx = ui->cx, cy = ui->cy;
	if(ui->cshow) {
		*x = cx * ds->tilesize + ds->offsetx;
		*y = cy * ds->tilesize + ds->offsety;

		if (IS_HEXAGONAL(state->mode))
		{
			*x += cx * ds->tilesize / 2;
		}
		*w = *h = ds->tilesize;
	}
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	*x = (params->w+1) * tilesize;
	*y = (params->h+1) * tilesize;

	if (params->mode == MODE_HONEYCOMB)
		*x += (tilesize / 2);
	else if (params->mode == MODE_EDGES)
	{
		*x += (tilesize * 1.5);
		*y += (tilesize * 1.5);
	}
}

static void game_set_offsets(int h, int mode, int tilesize, int *offsetx, int *offsety)
{
	*offsetx = tilesize / 2;
	*offsety = tilesize / 2;
	if (mode == MODE_HEXAGON)
		*offsetx -= (h / 2) * (tilesize / 2);
	else if (mode == MODE_HONEYCOMB)
	{
		*offsetx -= ((h / 2) - 1) * tilesize;
		if (h & 1)
			*offsetx -= tilesize;
	}
	else if (mode == MODE_EDGES)
	{
		*offsetx -= tilesize / 4;
		*offsety -= tilesize / 4;
	}
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
	ds->tilesize = tilesize;
	ds->thickness = max(2.0L, tilesize / 7.0L);
	game_compute_size(params, tilesize, NULL, &ds->w, &ds->h);

	game_set_offsets(params->h, params->mode, tilesize, &ds->offsetx, &ds->offsety);

	ds->blr = tilesize*0.4;
	assert(!ds->bl);
	ds->bl = blitter_new(dr, tilesize, tilesize);
}

static float *game_colours(frontend *fe, int *ncolours)
{
	float *ret = snewn(3 * NCOLOURS, float);

	game_mkhighlight(fe, ret, COL_MIDLIGHT, COL_HIGHLIGHT, COL_LOWLIGHT);

	ret[COL_BORDER * 3 + 0] = 0.0F;
	ret[COL_BORDER * 3 + 1] = 0.0F;
	ret[COL_BORDER * 3 + 2] = 0.0F;
	
	ret[COL_LINE * 3 + 0] = 0.0F;
	ret[COL_LINE * 3 + 1] = 0.5F;
	ret[COL_LINE * 3 + 2] = 0.0F;

	ret[COL_IMMUTABLE * 3 + 0] = 0.0F;
	ret[COL_IMMUTABLE * 3 + 1] = 0.0F;
	ret[COL_IMMUTABLE * 3 + 2] = 1.0F;
	
	ret[COL_ERROR * 3 + 0] = 1.0F;
	ret[COL_ERROR * 3 + 1] = 0.0F;
	ret[COL_ERROR * 3 + 2] = 0.0F;
	
	ret[COL_CURSOR * 3 + 0] = 0.0F;
	ret[COL_CURSOR * 3 + 1] = 0.7F;
	ret[COL_CURSOR * 3 + 2] = 0.0F;

	ret[COL_ARROW * 3 + 0] = 1.0F;
	ret[COL_ARROW * 3 + 1] = 1.0F;
	ret[COL_ARROW * 3 + 2] = 0.8F;

	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	struct game_drawstate *ds = snew(struct game_drawstate);
	int s = state->w * state->h;
	
	ds->tilesize = 0;
	ds->oldheld = 0;
	ds->old_next_target = 0;
	ds->old_prev_target = 0;
	ds->redraw = true;
	ds->colours = snewn(s, int);
	ds->oldgrid = snewn(s, number);
	ds->oldpositions = snewn(s, cell);
	ds->oldpath = snewn(s, int);
	ds->path = snewn(s, int);
	ds->nexthints = snewn(s, number);
	ds->prevhints = snewn(s, number);

	memset(ds->colours, ~0, s*sizeof(int));
	memset(ds->oldgrid, ~0, s*sizeof(number));
	memset(ds->oldpositions, ~0, s*sizeof(cell));
	memset(ds->oldpath, ~0, s*sizeof(cell));
	memset(ds->path, ~0, s*sizeof(cell));
	memset(ds->nexthints, ~0, s*sizeof(number));
	memset(ds->prevhints, ~0, s*sizeof(number));

	ds->bl = NULL;
	ds->bl_on = false;
	ds->blx = -1;
	ds->bly = -1;
	ds->blr = -1;
	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->colours);
	sfree(ds->oldgrid);
	sfree(ds->oldpositions);
	sfree(ds->oldpath);
	sfree(ds->path);
	sfree(ds->nexthints);
	sfree(ds->prevhints);
	if (ds->bl)
		blitter_free(dr, ds->bl);
	sfree(ds);
}

static const float horizontal_arrow[10] = {
	0.45,   0,
	0.35,   0.45,
	-0.45,  0.45,
	-0.45,  -0.45,
	0.35,   -0.45,
};

static const float diagonal_arrow[8] = {
	-0.45,  0.3,
	-0.45,  -0.45,
	0.3,    -0.45,
	0.45,   0.45,
};

static void ascent_draw_arrow(drawing *dr, cell i, int w, int h, int tx, int ty, 
                              int fill, int border, int tilesize)
{
	int i2;

	/* Horizontal arrow */
	if ((i/w) > 0 && (i/w) < h - 1)
	{
		int coords[10];
		int hdir = (i%w) ? -1 : +1;

		for (i2 = 0; i2 < 10; i2 += 2)
		{
			coords[i2] = (horizontal_arrow[i2] * tilesize * hdir) + 1 + tx;
			coords[i2+1] = (horizontal_arrow[i2+1] * tilesize) + 1 + ty;
		}

		draw_polygon(dr, coords, 5, fill, border);
	}
	/* Vertical arrow */
	else if ((i%w) > 0 && (i%w) < w - 1)
	{
		int coords[10];
		int vdir = i > w ? -1 : +1;

		for (i2 = 0; i2 < 10; i2 += 2)
		{
			coords[i2 + 1] = (horizontal_arrow[i2] * tilesize * vdir) + 1 + ty;
			coords[i2] = (horizontal_arrow[i2 + 1] * tilesize) + 1 + tx;
		}

		draw_polygon(dr, coords, 5, fill, border);
	}
	/* Diagonal arrow */
	else
	{
		int coords[8];
		int hdir = (i%w) ? -1 : +1;
		int vdir = i > w ? -1 : +1;

		for (i2 = 0; i2 < 8; i2 += 2)
		{
			coords[i2] = (diagonal_arrow[i2] * tilesize * hdir) + 1 + tx;
			coords[i2 + 1] = (diagonal_arrow[i2 + 1] * tilesize * vdir) + 1 + ty;
		}

		draw_polygon(dr, coords, 4, fill, border);
	}
}

static number ascent_display_number(cell i, const game_drawstate *ds, const game_ui *ui, const game_state *state, const ascent_movement *movement)
{
	number n = state->grid[i];
	int w = state->w;

	if (n == NUMBER_BOUND || n == NUMBER_WALL)
		return n;

	/* Typing a number overrides all other symbols */
	if (ui->typing_cell == i)
		return ui->typing_number - 1;

	/*
	 * If a cell is adjacent to to the highlighted cell, a line can be drawn.
	 * Show a number if the seleced number is known, otherwise show a Move symbol.
	 */
	if (!IS_NUMBER_EDGE(ui->select) && ui->held >= 0 && ascent_validate_path_move(i, state, ui))
	{
		if(n == NUMBER_EMPTY)
			n = ui->select >= 0 && ui->positions[ui->select] == CELL_NONE ? ui->select :
				ui->cshow == CSHOW_KEYBOARD ? NUMBER_MOVE : NUMBER_EMPTY;
		else if(ui->cshow == CSHOW_KEYBOARD)
			n |= NUMBER_FLAG_MOVE;
	}

	/* When this cell has hints, only show candidate number if it matches one of these hints */
	if (n != NUMBER_MOVE && ui->nexthints[i] != NUMBER_EMPTY && ui->nexthints[i] != n && ui->prevhints[i] != n)
		n = NUMBER_EMPTY;

	/* Possible drop target for the selected edge number */
	if (n == NUMBER_EMPTY && IS_NUMBER_EDGE(ui->select) && is_edge_valid(ui->held, i, w, state->h))
		n = NUMBER_EDGE(ui->select);

	/* 
	 * Cells which cause a backtrack should display a Clear symbol instead of a Move symbol.
	 * Only show a Clear symbol when the cursor is over it, otherwise show the original number.
	 */
	if (state->path && state->path[i] & (1 << ascent_find_direction(i, ui->held, w, movement)))
	{
		if(n == NUMBER_MOVE)
			n = ui->cy*w + ui->cx == i ? NUMBER_CLEAR : NUMBER_EMPTY;
		else if (n >= 0 && n & NUMBER_FLAG_MOVE && ui->cy*w + ui->cx == i)
			n = NUMBER_CLEAR;
		else if (n >= 0)
			n &= ~NUMBER_FLAG_MOVE;
	}

	return n;
}

#define FLASH_FRAME 0.03F
#define FLASH_SIZE  4
#define ERROR_MARGIN 0.1F
static void game_redraw(drawing *dr, game_drawstate *ds,
						const game_state *oldstate, const game_state *state,
						int dir, const game_ui *ui,
						float animtime, float flashtime)
{
	int w = state->w;
	int h = state->h;
	int tilesize = ds->tilesize;
	int tx, ty, tx1, ty1, tx2, ty2;
	cell i, i2;
	number n, fn;
	char buf[20];
	const cell *positions = ui->positions;
	int flash = -2, colour;
	int margin = tilesize*ERROR_MARGIN;
	const ascent_movement *movement = ascent_movement_for_mode(state->mode);

	if(flashtime > 0)
		flash = (int)(flashtime/FLASH_FRAME);
	
	if (ds->bl_on)
	{
		blitter_load(dr, ds->bl,
			ds->blx - ds->blr, ds->bly - ds->blr);
		draw_update(dr,
			ds->blx - ds->blr, ds->bly - ds->blr,
			tilesize, tilesize);
		ds->bl_on = false;
	}

	if(ds->redraw)
	{
		draw_rect(dr, 0, 0, ds->w, ds->h, COL_MIDLIGHT);
		draw_update(dr, 0, 0, ds->w, ds->h);
	}
	
	/* Add confirmed path lines */
	for (i = 0; i < w*h; i++)
	{
		int pathline = (state->path ? state->path[i] : 0);
		int lines = 0;
		n = state->grid[i];

		if (n > 0 && positions[n] != CELL_MULTIPLE && positions[n - 1] >= 0)
		{
			i2 = positions[n - 1];
			if (is_near(i, i2, w, state->mode))
				pathline |= (1 << ascent_find_direction(i, i2, w, movement));
			else
				pathline |= FLAG_ERROR;
			lines++;
		}
		if (n >= 0 && n < state->last && positions[n] != CELL_MULTIPLE && positions[n + 1] >= 0)
		{
			i2 = positions[n + 1];
			if (is_near(i, i2, w, state->mode))
				pathline |= (1 << ascent_find_direction(i, i2, w, movement));
			else
				pathline |= FLAG_ERROR;
			lines++;
		}

		if (n == 0 || n == state->last)
			lines++;

		if (lines == 2)
			pathline |= FLAG_COMPLETE;

		if (state->path && state->path[i] & ~FLAG_COMPLETE)
			pathline |= FLAG_USER;

		ds->path[i] = pathline;
	}

	/* Invalidate squares */
	for(i = 0; i < w*h; i++)
	{
		bool dirty = false;

		n = ascent_display_number(i, ds, ui, state, movement);

		if(ds->oldgrid[i] != n)
		{
			dirty = true;
			ds->oldgrid[i] = n;
		}

		if (ds->oldpath[i] != ds->path[i])
		{
			dirty = true;

			/* Invalidate neighbours of adjacent cells */
			for (i2 = max(0, i - (w+1)); i2 < w*h && i2 < i + w+1; i2++)
			{
				if (is_near(i, i2, w, state->mode))
					ds->colours[i2] = -1;
			}

			ds->oldpath[i] = ds->path[i];
		}
			
		if (IS_NUMBER_EDGE(n) &&
			positions[NUMBER_EDGE(n)] != ds->oldpositions[NUMBER_EDGE(n)])
			dirty = true;

		if (ds->prevhints[i] != ui->prevhints[i] ||
			ds->nexthints[i] != ui->nexthints[i])
		{
			ds->prevhints[i] = ui->prevhints[i];
			ds->nexthints[i] = ui->nexthints[i];
			dirty = true;
		}
			
		if(dirty)
			ds->colours[i] = -1;
	}
		
	/* Invalidate numbers */
	for(n = 0; n <= state->last; n++)
	{
		if (ds->oldpositions[n] != positions[n])
		{
			if(ds->oldpositions[n] >= 0)
				ds->colours[ds->oldpositions[n]] = -1;
			if(positions[n] >= 0)
				ds->colours[positions[n]] = -1;
		
			ds->oldpositions[n] = positions[n];
		}
	}

	ds->redraw = false;
	ds->oldheld = ui->held;
	ds->old_next_target = ui->next_target_mode & TARGET_SHOW ? ui->next_target : NUMBER_EMPTY;
	ds->old_prev_target = ui->prev_target_mode & TARGET_SHOW ? ui->prev_target : NUMBER_EMPTY;
	
	/* Draw squares */
	for(i = 0; i < w*h; i++)
	{
		tx = (i%w) * tilesize + ds->offsetx;
		ty = (i/w) * tilesize + ds->offsety;

		if (IS_HEXAGONAL(state->mode))
		{
			tx += (i/w) * tilesize / 2;
		}
		tx1 = tx + (tilesize/2), ty1 = ty + (tilesize/2);
		n = state->grid[i];
		
		if (n == NUMBER_BOUND)
			continue;

		colour = n == NUMBER_WALL ? COL_BORDER :
			flash >= n && flash <= n + FLASH_SIZE ? COL_LOWLIGHT :
			ui->dragx == i%w || ui->dragy == i/w ? COL_HIGHLIGHT :
			ui->held == i || ui->typing_cell == i ||
				(ui->cshow == CSHOW_MOUSE && ui->cy*w+ui->cx == i) ? COL_LOWLIGHT :
			ds->old_next_target >= 0 && positions[ds->old_next_target] == i ? COL_HIGHLIGHT :
			ds->old_prev_target >= 0 && positions[ds->old_prev_target] == i ? COL_HIGHLIGHT :
			COL_MIDLIGHT;
		
		if(ds->colours[i] == colour) continue;
		
		fn = ascent_display_number(i, ds, ui, state, movement);
		n = fn < 0 ? fn : fn & ~NUMBER_FLAG_MASK;

		/* Draw tile background */
		clip(dr, tx, ty, tilesize+1, tilesize+1);
		draw_update(dr, tx, ty, tilesize+1, tilesize+1);
		draw_rect(dr, tx+1, ty+1, tilesize-1, tilesize-1,
			IS_NUMBER_EDGE(n) ? COL_MIDLIGHT : colour);
		ds->colours[i] = colour;
		
		if (ui->typing_cell != i)
		{
			int linecolour = ds->path[i] & FLAG_USER ? COL_LINE : COL_HIGHLIGHT;

			if (!IS_HEXAGONAL(state->mode))
			{
				int dy;
				/* Draw diagonal lines connecting neighbours */
				for (dy = -1; dy <= 1; dy += 2)
				{
					i2 = i + (w*dy);
					if (i2 < 0 || i2 >= w*h)
						continue;

					tx2 = (i2%w)*tilesize + ds->offsetx + (tilesize / 2);
					ty2 = (i2 / w)*tilesize + ds->offsety + (tilesize / 2);

					for (dir = 0; dir < movement->dircount; dir++)
					{
						if (!movement->dirs[dir].dy || !movement->dirs[dir].dx)
							continue;

						if (ds->path[i2] & (1 << dir))
							draw_thick_line(dr, ds->thickness,
								tx2 + (movement->dirs[dir].dx*tilesize),
								ty2 + (movement->dirs[dir].dy*tilesize),
								tx2, ty2, ds->path[i2] & FLAG_USER ? COL_LINE : COL_HIGHLIGHT);
					}
				}
			}

			/* Draw a circle on the beginning and the end of the path */
			if ((n == 0 || n == state->last) &&
				(GET_BIT(state->immutable, i) || positions[n] != CELL_MULTIPLE))
			{
				if(fn & NUMBER_FLAG_MOVE)
				{
					/* Draw a large lowlight circle under a slightly smaller light circle */
					draw_circle(dr, tx + (tilesize / 2), ty + (tilesize / 2),
						tilesize * 0.4, COL_LOWLIGHT, COL_LOWLIGHT);
					draw_circle(dr, tx + (tilesize / 2), ty + (tilesize / 2),
						tilesize * 0.3, COL_HIGHLIGHT, COL_HIGHLIGHT);
				}
				else
				{
					draw_circle(dr, tx + (tilesize / 2), ty + (tilesize / 2),
						tilesize / 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
				}
			}
			/* Draw a small circle with the same size as the line thickness, to round off corners */
			else if (ds->path[i] & ~FLAG_COMPLETE)
			{
				draw_circle(dr, tx + (tilesize / 2), ty + (tilesize / 2),
					ds->thickness / 2, linecolour, linecolour);
			}

			/* Draw path lines */
			for (dir = 0; dir < movement->dircount; dir++)
			{
				if (!(ds->path[i] & (1 << dir)))
					continue;

				i2 = i + (w * movement->dirs[dir].dy) + movement->dirs[dir].dx;
				tx2 = (i2%w)*tilesize + ds->offsetx + (tilesize / 2);
				if (IS_HEXAGONAL(state->mode))
					tx2 += (i2 / w) * tilesize / 2;
				ty2 = (i2 / w)*tilesize + ds->offsety + (tilesize / 2);

				draw_thick_line(dr, ds->thickness, tx1, ty1, tx2, ty2, linecolour);
			}
		}
		
		/* Draw square border */
		if (!IS_NUMBER_EDGE(n))
		{
			int sqc[8];
			sqc[0] = tx;
			sqc[1] = ty;
			sqc[2] = tx + tilesize;
			sqc[3] = ty;
			sqc[4] = tx + tilesize;
			sqc[5] = ty + tilesize;
			sqc[6] = tx;
			sqc[7] = ty + tilesize;
			draw_polygon(dr, sqc, 4, -1, COL_BORDER);
		}

		/* Draw a light circle on possible endpoints */
		if(state->grid[i] == NUMBER_EMPTY && (n == 0 || n == state->last))
		{
			draw_circle(dr, tx+(tilesize/2), ty+(tilesize/2),
				tilesize/3, colour, COL_LOWLIGHT);
		}
		
		/*
		 * Manually placed lines have a similar color to numbers.
		 * Draw a circle in the same color as the background over the lines,
		 * to make the number more readable.
		 */
		if (n > 0 && n < state->last && state->path && state->path[i] & ~FLAG_COMPLETE)
		{
			draw_circle(dr, tx + (tilesize / 2), ty + (tilesize / 2),
				tilesize / 3, colour, colour);
			
			if (fn > 0 && fn & NUMBER_FLAG_MOVE)
			{
				draw_circle(dr, tx1, ty1, tilesize * 0.22, COL_LOWLIGHT, COL_LOWLIGHT);
			}
		}
		/* Draw a slightly larger lowlight circle if there's a number, but no path */
		else if (n > 0 && n < state->last && fn & NUMBER_FLAG_MOVE)
		{
			draw_circle(dr, tx1, ty1, tilesize * 0.28, COL_LOWLIGHT, COL_LOWLIGHT);
		}
		/* Draw a normal lowlight circle in all other cases */
		else if (n == NUMBER_MOVE)
		{
			draw_circle(dr, tx1, ty1, tilesize * 0.22, COL_LOWLIGHT, COL_LOWLIGHT);
		}

		if (n == NUMBER_CLEAR)
		{
			/* Draw a cross */
			int shape = tilesize / 4;

			draw_thick_line(dr, tilesize / 7, tx + shape, ty + shape, tx + tilesize - shape, ty + tilesize - shape, COL_LOWLIGHT);
			draw_thick_line(dr, tilesize / 7, tx + tilesize - shape, ty + shape, tx + shape, ty + tilesize - shape, COL_LOWLIGHT);
		}

		/* Draw the number */
		if(n >= 0)
		{
			sprintf(buf, "%d", n+1);
			
			draw_text(dr, tx1, ty1,
					FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
					GET_BIT(state->immutable, i) ? COL_IMMUTABLE : 
					state->grid[i] == NUMBER_EMPTY && ui->typing_cell != i ? COL_LOWLIGHT :
					n <= state->last && positions[n] == CELL_MULTIPLE && ui->typing_cell != i ? COL_ERROR :
					COL_BORDER, buf);
			
			if(ds->path[i] & FLAG_ERROR)
			{
				draw_thick_line(dr, 2, tx+margin, ty+margin,
					(tx+tilesize)-margin, (ty+tilesize)-margin, COL_ERROR);
			}
		}
		else if (IS_NUMBER_EDGE(n))
		{
			i2 = positions[NUMBER_EDGE(n)];
			bool error = i2 >= 0 && !is_edge_valid(i, i2, w, h);
			sprintf(buf, "%d", NUMBER_EDGE(n) + 1);

			ascent_draw_arrow(dr, i, w, h, tx1, ty1, COL_ARROW, COL_BORDER, tilesize);

			draw_text(dr, tx1, ty1,
				FONT_VARIABLE, tilesize / 2, ALIGN_HCENTRE | ALIGN_VCENTRE,
				error ? COL_ERROR : i2 >= 0 ? COL_LOWLIGHT :
				COL_BORDER, buf);
		}
		else if(n != NUMBER_CLEAR)
		{
			if (ui->prevhints[i] >= 0)
			{
				sprintf(buf, "%d", ui->prevhints[i] + 1);
				draw_text(dr, tx1 - (tilesize / 4), ty1 - (tilesize / 4),
					FONT_VARIABLE, tilesize / 3, ALIGN_HCENTRE | ALIGN_VCENTRE,
					COL_BORDER, buf);
			}
			if (ui->nexthints[i] >= 0)
			{
				sprintf(buf, "%d", ui->nexthints[i] + 1);
				draw_text(dr, tx1 + (tilesize / 4), ty1 + (tilesize / 4),
					FONT_VARIABLE, tilesize / 3, ALIGN_HCENTRE | ALIGN_VCENTRE,
					COL_BORDER, buf);
			}
		}

		unclip(dr);
	}

	if (ui->cshow == CSHOW_KEYBOARD)
	{
		ds->blx = ui->cx*tilesize + ds->offsetx + (tilesize / 2);
		ds->bly = ui->cy*tilesize + ds->offsety + (tilesize / 2);

		if (IS_HEXAGONAL(state->mode))
			ds->blx += ui->cy * tilesize / 2;

		blitter_save(dr, ds->bl, ds->blx - ds->blr, ds->bly - ds->blr);
		ds->bl_on = true;

		draw_rect_corners(dr, ds->blx, ds->bly, ds->blr - 1, COL_CURSOR);
		draw_update(dr, ds->blx - ds->blr, ds->bly - ds->blr, tilesize, tilesize);
	}
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
	return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
	if (!oldstate->completed && newstate->completed &&
			!oldstate->cheated && !newstate->cheated)
		return FLASH_FRAME * (newstate->w*newstate->h + FLASH_SIZE);
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

/* Using 9mm squares */
#define PRINT_SQUARE_SIZE 900
static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
	int pw, ph;

	game_compute_size(params, PRINT_SQUARE_SIZE, ui, &pw, &ph);
	*x = pw / 100.0F;
	*y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int tilesize)
{
	int w = state->w;
	int h = state->h;
	int tx, ty, tx2, ty2;
	int offsetx, offsety;
	cell i, i2;
	number n;
	char buf[20];
	cell *positions = snewn(w*h, cell);

	int ink = print_mono_colour(dr, 0);
    int grey = print_grey_colour(dr, 0.8f);

	game_set_offsets(h, state->mode, tilesize, &offsetx, &offsety);
	update_positions(positions, state->grid, w*h);

    print_line_width(dr, tilesize / 5);
	for(n = 0; n < w*h; n++)
	{
		i = positions[n];
		tx = (i%w) * tilesize + offsetx + (tilesize/2);
		ty = (i/w) * tilesize + offsety + (tilesize/2);

		if (IS_HEXAGONAL(state->mode))
			tx += (i/w) * tilesize / 2;

		/* Draw a circle on the beginning and the end of the path */
		if((n == 0 || n == state->last) && i >= 0)
			draw_circle(dr, tx, ty, tilesize/4, grey, grey);

		if(n == w*h-1) break;

		i2 = positions[n+1];
		tx2 = (i2%w) * tilesize + offsetx + (tilesize/2);
		ty2 = (i2/w) * tilesize + offsety + (tilesize/2);

		if (IS_HEXAGONAL(state->mode))
			tx2 += (i2/w) * tilesize / 2;

		/* Draw path lines */
		if(i >= 0 && i2 >= 0 && is_near(i, i2, w, state->mode))
			draw_line(dr, tx, ty, tx2, ty2, grey);
	}

	print_line_width(dr, tilesize / 40);
	for(i = 0; i < w*h; i++)
	{
		tx = (i%w) * tilesize + offsetx;
		ty = (i/w) * tilesize + offsety;

		if (IS_HEXAGONAL(state->mode))
			tx += (i/w) * tilesize / 2;

		n = state->grid[i];
		if (n == NUMBER_BOUND)
			continue;

		/* Draw square border */
		if (!IS_NUMBER_EDGE(n))
		{
			int sqc[8];
			sqc[0] = tx;
			sqc[1] = ty;
			sqc[2] = tx + tilesize;
			sqc[3] = ty;
			sqc[4] = tx + tilesize;
			sqc[5] = ty + tilesize;
			sqc[6] = tx;
			sqc[7] = ty + tilesize;
			draw_polygon(dr, sqc, 4, n == NUMBER_WALL ? ink : -1, ink);
		}
		else
		{
			n = NUMBER_EDGE(n);
			ascent_draw_arrow(dr, i, w, h, tx + tilesize/2, ty + tilesize/2, -1, ink, tilesize);
		}

		/* Draw the number */
		if(n >= 0)
		{
			sprintf(buf, "%d", n+1);
			
			draw_text(dr, tx + tilesize/2, ty + tilesize/2,
			          FONT_VARIABLE, tilesize/2, 
			          ALIGN_HCENTRE|ALIGN_VCENTRE, ink, buf);
		}
	}

	sfree(positions);
}

#ifdef COMBINED
#define thegame ascent
#endif

const struct game thegame = {
	"Ascent", NULL, NULL,
	default_params,
	NULL, game_preset_menu,
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
    get_prefs, set_prefs,
	new_ui,
	free_ui,
	encode_ui,
	decode_ui,
	NULL, /* game_request_keys */
	game_changed_state,
	NULL, /* current_key_label */
	interpret_move,
	execute_move,
	48, game_compute_size, game_set_size,
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
	REQUIRE_RBUTTON, /* flags */
};

/* ***************** *
 * Standalone solver *
 * ***************** */

#ifdef STANDALONE_SOLVER
#include <time.h>

/* Most of the standalone solver code was copied from unequal.c and singles.c */

const char *quis;

static void usage_exit(const char *msg)
{
	if (msg)
		fprintf(stderr, "%s: %s\n", quis, msg);
	fprintf(stderr,
			"Usage: %s [-v] [--seed SEED] <params> | [game_id [game_id ...]]\n",
			quis);
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

	while (--argc > 0) {
		char *p = *++argv;
		if (!strcmp(p, "--seed")) {
			if (argc == 0)
				usage_exit("--seed needs an argument");
			seed = (time_t) atoi(*++argv);
			argc--;
		} else if (!strcmp(p, "-v"))
			solver_verbose = true;
		else if (*p == '-')
			usage_exit("unrecognised option");
		else
			id = p;
	}

	if (id) {
		desc = strchr(id, ':');
		if (desc)
			*desc++ = '\0';

		params = default_params();
		decode_params(params, id);
		err = validate_params(params, true);
		if (err) {
			fprintf(stderr, "Parameters are invalid\n");
			fprintf(stderr, "%s: %s", argv[0], err);
			exit(1);
		}
	}

	if (!desc) {
		char *desc_gen, *aux;
		rs = random_new((void *) &seed, sizeof(time_t));
		if (!params)
			params = default_params();
		printf("Generating puzzle with parameters %s\n",
			encode_params(params, true));
		desc_gen = new_game_desc(params, rs, &aux, false);

		if (!solver_verbose) {
			char *fmt = game_text_format(new_game(NULL, params, desc_gen));
			fputs(fmt, stdout);
			sfree(fmt);
		}

		printf("Game ID: %s\n", desc_gen);
	} else {
		game_state *input;
		struct solver_scratch *scratch;
		
		err = validate_desc(params, desc);
		if (err) {
			fprintf(stderr, "Description is invalid\n");
			fprintf(stderr, "%s", err);
			exit(1);
		}
		
		input = new_game(NULL, params, desc);
		scratch = new_scratch(input->w, input->h, input->mode, input->last);
		
		ascent_solve(input->grid, DIFFCOUNT, scratch);
		
		free_scratch(scratch);
		free_game(input);
	}

	return 0;
}
#endif
