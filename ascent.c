/*
 * ascent.c: Implementation of Hidoku puzzles.
 * (C) 2015 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective: Place each number from 1 to n once.
 * Consecutive numbers must be orthogonally or diagonally adjacent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#ifdef STANDALONE_SOLVER
#include <stdarg.h>
int solver_verbose = FALSE;

void solver_printf(char *fmt, ...)
{
	if(!solver_verbose) return;
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("%s", buf);
}
#else
#define solver_printf(...)
#endif

enum {
	COL_MIDLIGHT,
	COL_LOWLIGHT,
	COL_HIGHLIGHT,
	COL_BORDER,
	COL_IMMUTABLE,
	COL_ERROR,
	COL_CURSOR,
	NCOLOURS
};

typedef int number;
typedef int cell;
typedef unsigned char bitmap;

#define NUMBER_EMPTY  ((number) -1 )
#define NUMBER_WALL   ((number) -2 )
#define NUMBER_BOUND  ((number) -3 )
#define IS_OBSTACLE(i) (i <= -2)

#define CELL_NONE     ((cell)   -1 )
#define CELL_MULTIPLE ((cell)   -2 )

static const int dir_x[] = {-1,  0,  1, -1, 1, -1, 0, 1};
static const int dir_y[] = {-1, -1, -1,  0, 0,  1, 1, 1};

#define FLAG_ENDPOINT (1<<8)
#define FLAG_COMPLETE (1<<10)

#define BITMAP_SIZE(i) ( ((i)+7) / 8 )
#define GET_BIT(bmp, i) ( bmp[(i)/8] & 1<<((i)%8) )
#define SET_BIT(bmp, i) bmp[(i)/8] |= 1<<((i)%8)
#define CLR_BIT(bmp, i) bmp[(i)/8] &= ~(1<<((i)%8))

struct game_params {
#ifndef PORTRAIT_SCREEN
	int w, h;
#else
	int h, w;
#endif

	int diff, mode;
	char removeends;
};

#define DIFFLIST(A)                             \
	A(EASY,Easy, e)                             \
	A(NORMAL,Normal, n)                         \
	A(TRICKY,Tricky, t)                         \
	A(HARD,Hard, h)                             \

#define MODELIST(A)                             \
	A(RECT,Rectangle, R)                        \
	A(HEXAGON,Hexagon, H)                       \

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
* Hexagonal grids are implemented as normal square grids, but disallowing
* movement in the top-left and bottom-right directions (dir 0 and dir 7).
*/
#define IS_HEXAGONAL(mode) ((mode) == MODE_HEXAGON)

const static struct game_params ascent_presets[] = {
	{ 7,  6, DIFF_EASY, MODE_RECT, FALSE },
	{ 7,  6, DIFF_NORMAL, MODE_RECT, FALSE },
	{ 7,  6, DIFF_TRICKY, MODE_RECT, FALSE },
	{ 7,  6, DIFF_HARD, MODE_RECT, FALSE },
	{ 10, 8, DIFF_EASY, MODE_RECT, FALSE },
	{ 10, 8, DIFF_NORMAL, MODE_RECT, FALSE },
	{ 10, 8, DIFF_TRICKY, MODE_RECT, FALSE },
	{ 10, 8, DIFF_HARD, MODE_RECT, FALSE },
#ifndef SMALL_SCREEN
	{ 14, 11, DIFF_EASY, MODE_RECT, FALSE },
	{ 14, 11, DIFF_NORMAL, MODE_RECT, FALSE },
	{ 14, 11, DIFF_TRICKY, MODE_RECT, FALSE },
	{ 14, 11, DIFF_HARD, MODE_RECT, FALSE },
#endif
};

const static struct game_params ascent_hexagonal_presets[] = {
	{ 7, 7, DIFF_EASY, MODE_HEXAGON, FALSE },
	{ 7, 7, DIFF_NORMAL, MODE_HEXAGON, FALSE },
	{ 7, 7, DIFF_TRICKY, MODE_HEXAGON, FALSE },
	{ 7, 7, DIFF_HARD, MODE_HEXAGON, FALSE },
	{ 9, 9, DIFF_EASY, MODE_HEXAGON, FALSE },
	{ 9, 9, DIFF_NORMAL, MODE_HEXAGON, FALSE },
	{ 9, 9, DIFF_TRICKY, MODE_HEXAGON, FALSE },
	{ 9, 9, DIFF_HARD, MODE_HEXAGON, FALSE },
};

#define DEFAULT_PRESET 0

struct game_state {
	int w, h, mode;
	
	number *grid;
	bitmap *immutable;
	
	number last;
	
	char completed, cheated;
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
	struct preset_menu *menu, *hex;
	menu = preset_menu_new();

	for (i = 0; i < lenof(ascent_presets); i++)
	{
		params = dup_params(&ascent_presets[i]);
		sprintf(buf, "%dx%d %s", params->w, params->h, ascent_diffnames[params->diff]);
		preset_menu_add_preset(menu, dupstr(buf), params);
	}

	hex = preset_menu_add_submenu(menu, dupstr("Hexagonal"));

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
		params->removeends = TRUE;
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
}

static char *encode_params(const game_params *params, int full)
{
	char buf[256];
	char *p = buf;
	p += sprintf(p, "%dx%dm%c", params->w, params->h, ascent_modechars[params->mode]);
	if(full && params->removeends)
		*p++ = 'E';
	if (full)
		p += sprintf(p, "d%c", ascent_diffchars[params->diff]);

	*p++ = '\0';
	
	return dupstr(buf);
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
	
	ret[2].name = "Always show start and end points";
	ret[2].type = C_BOOLEAN;
	ret[2].u.boolean.bval = !params->removeends;
	
	ret[3].name = "Grid type";
	ret[3].type = C_CHOICES;
	ret[3].u.choices.choicenames = MODELIST(CONFIG);
	ret[3].u.choices.selected = params->mode;

	ret[4].name = "Difficulty";
	ret[4].type = C_CHOICES;
	ret[4].u.choices.choicenames = DIFFLIST(CONFIG);
	ret[4].u.choices.selected = params->diff;
	
	ret[5].name = NULL;
	ret[5].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = snew(game_params);
	
	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->removeends = !cfg[2].u.boolean.bval;
	ret->mode = cfg[3].u.choices.selected;
	ret->diff = cfg[4].u.choices.selected;
	
	return ret;
}


static const char *validate_params(const game_params *params, int full)
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
	
	return NULL;
}

static int is_near(int a, int b, const game_state *state)
{
	int w = state->w;
	int dx = (a % w) - (b % w);
	int dy = (a / w) - (b / w);

	if (IS_HEXAGONAL(state->mode) && dx == dy)
		return FALSE;

	return (abs(dx) | abs(dy)) == 1;
}

static char check_completion(number *grid, int w, int h, int mode)
{
	int x = -1, y = -1, x2 = -1, y2 = -1, i;
	int maxdirs = IS_HEXAGONAL(mode) ? 7 : 8;
	number last = (w*h)-1;
	
	/* Check for empty squares, and locate path start */
	for(i = 0; i < w*h; i++)
	{
		if(grid[i] == NUMBER_EMPTY) return FALSE;
		if(grid[i] == 0)
		{
			x = i%w;
			y = i/w;
		}
		if(IS_OBSTACLE(grid[i]))
			last--;
	}
	if(x == -1)
		return FALSE;
	
	/* Keep selecting the next number in line */
	while(grid[y*w+x] != last)
	{
		for(i = IS_HEXAGONAL(mode) ? 1 : 0; i < maxdirs; i++)
		{
			x2 = x + dir_x[i];
			y2 = y + dir_y[i];
			if(y2 < 0 || y2 >= h || x2 < 0 || x2 >= w)
				continue;
			
			if(grid[y2*w+x2] == grid[y*w+x] + 1)
				break;
		}
		
		/* No neighbour found */
		if(i == maxdirs) return FALSE;
		x = x2;
		y = y2;
	}
	
	return TRUE;
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

static int backbite_left(int step, int n, cell *path, int w, int h, const bitmap *walls)
{
	int neighx, neighy, neigh, i;
	neighx = (path[0] % w) + dir_x[step];
	neighy = (path[0] / w) + dir_y[step];

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

static int backbite_right(int step, int n, cell *path, int w, int h, const bitmap *walls)
{
	int neighx, neighy, neigh, i;
	neighx = (path[n-1] % w) + dir_x[step];
	neighy = (path[n-1] / w) + dir_y[step];

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

static int backbite(int step, int n, cell *path, int w, int h, random_state *rs, const bitmap *walls)
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
		int step = IS_HEXAGONAL(params->mode) ? random_upto(rs, 6) + 1 : random_upto(rs, 8);
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

static void update_positions(cell *positions, number *grid, int s)
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

struct solver_scratch {
	int w, h, mode;
	
	/* The position of each number. */
	cell *positions;
	
	number *grid;
	
	/* The last number of the path. */
	number end;
	
	/* All possible numbers in each cell */
	bitmap *marks; /* GET_BIT i*s+n */
	
	/* The possible path segments for each cell */
	int *path;
	char found_endpoints;
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
	ret->found_endpoints = FALSE;
	for(i = 0; i < n; i++)
	{
		ret->positions[i] = CELL_NONE;
		ret->grid[i] = NUMBER_EMPTY;
	}
	
	ret->marks = snewn(BITMAP_SIZE(w*h*n), bitmap);
	memset(ret->marks, 0, BITMAP_SIZE(w*h*n));
	memset(ret->path, 0, n*sizeof(int));
	
	return ret;
}

static void free_scratch(struct solver_scratch *scratch)
{
	sfree(scratch->positions);
	sfree(scratch->grid);
	sfree(scratch->marks);
	sfree(scratch->path);
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

static int solver_single_number(struct solver_scratch *scratch, char simple)
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
		if (IS_HEXAGONAL(scratch->mode) && ((hdist < 0 && vdist < 0) || (hdist > 0 && vdist > 0)))
		{
			if (abs(hdist + vdist) <= distance) continue;
		}
		else
		{
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

static int ascent_find_direction(cell i1, cell i2, const struct solver_scratch *scratch)
{
	int dir;
	for (dir = IS_HEXAGONAL(scratch->mode) ? 1 : 0; dir < (IS_HEXAGONAL(scratch->mode) ? 7 : 8); dir++)
	{
		if (i2 - i1 == (dir_y[dir] * scratch->w + dir_x[dir]))
			return dir;
	}
	return -1;
}

#ifdef STANDALONE_SOLVER
static void solver_debug_path(struct solver_scratch *scratch)
{
	if(!solver_verbose) return;
	
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
	int x, y;

	scratch->path[0] = 0xD0; /* top-left */
	scratch->path[w-1] = 0x68; /* top-right */
	scratch->path[(w*h) - w] = 0x16; /* bottom-left */
	scratch->path[(w*h) - 1] = 0x0B; /* bottom-right */
	
	for (x = 1; x < w - 1; x++)
	{
		scratch->path[x] = 0xF8; /* top */
		scratch->path[w*h - (x + 1)] = 0x1F; /* bottom */
	}
	for (y = 1; y < h - 1; y++)
	{
		scratch->path[y*w] = 0xD6; /* left */
		scratch->path[((y + 1)*w) - 1] = 0x6B; /* right */
	}
	for (y = 1; y < h - 1; y++)
	for (x = 1; x < w - 1; x++)
	{
		scratch->path[y*w + x] = 0xFF; /* center */
	}

	if (IS_HEXAGONAL(scratch->mode))
	{
		for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			scratch->path[y*w + x] &= 0x7E;
		}
	}

	for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	{
		scratch->path[y*w + x] |= FLAG_ENDPOINT;
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
		scratch->found_endpoints = TRUE;
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
		scratch->path[ib] = (1 << ascent_find_direction(ib, i, scratch)) | FLAG_ENDPOINT;
	}
	/* Do the same for the last number pointing to the penultimate number. */
	i = scratch->positions[end - 1];
	if (i != CELL_NONE && ic != CELL_NONE && !(scratch->path[ic] & FLAG_COMPLETE))
	{
		scratch->path[ic] = (1 << ascent_find_direction(ic, i, scratch)) | FLAG_ENDPOINT;
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

		scratch->path[i] = 1 << ascent_find_direction(i, ib, scratch);
		scratch->path[i] |= 1 << ascent_find_direction(i, ic, scratch);
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
		for (dir = 0; dir <= 8; dir++)
		{
			if (scratch->path[i] & (1 << dir)) count++;
		}
		
		if (count == 2)
		{
			int x, y, dir;
			scratch->path[i] |= FLAG_COMPLETE;
			solver_printf("Completed path segment at %d,%d\n", i%w, i/w);
			ret++;
			for (dir = 0; dir < 8; dir++)
			{
				if (scratch->path[i] & (1 << dir)) continue;

				x = (i%w) + dir_x[dir];
				y = (i / w) + dir_y[dir];
				if(x < 0 || y < 0 || x >= w || y >= h) continue;
				scratch->path[y*w + x] &= ~(1 << (7 - dir));
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
			for(dir = 0; dir < 8; dir++)
			{
				if(!(scratch->path[i] & (1<<dir))) continue;
				i2 = dir_y[dir] * w + dir_x[dir] + i;
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
		for(dir = 0; dir < 4; dir++)
		{
			if(!(scratch->path[i1] & (1<<dir))) continue;
			i2 = dir_y[dir] * w + dir_x[dir] + i1;
			n2 = scratch->grid[i2];
			if(n2 >= 0 && abs(n1-n2) != 1)
			{
				solver_printf("Disconnect %d,%d (%d) and %d,%d (%d)\n", i1%w, i1/w, n1+1, i2%w, i2/w, n2+1);
				scratch->path[i1] &= ~(1 << dir);
				scratch->path[i2] &= ~(1 << (7 - dir));
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
		if(scratch->grid[i1] >= NUMBER_EMPTY) continue;
		for(dir = 0; dir < 8; dir++)
		{
			if(!(scratch->path[i1] & (1<<dir))) continue;
			i2 = dir_y[dir] * w + dir_x[dir] + i1;
			solver_printf("Disconnect block %d,%d from %d,%d\n", i1%w, i1/w, i2%w, i2/w);
			scratch->path[i2] &= ~(1 << (7 - dir));
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

static void ascent_solve(const number *puzzle, int diff, struct solver_scratch *scratch)
{
	int w = scratch->w, h = scratch->h, s=w*h;
	cell i; number n;
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
	
	solver_initialize_path(scratch);
	solver_remove_blocks(scratch);

	while(TRUE)
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
		
		if(diff < DIFF_TRICKY) break;
		
		if(solver_single_number(scratch, TRUE))
			continue;
		
		if(diff < DIFF_HARD) break;
		
		if(solver_single_number(scratch, FALSE))
			continue;
		
		break;
	}
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, int interactive)
{
	int w = params->w;
	int h = params->h;
	cell i, j;
	struct solver_scratch *scratch = new_scratch(w, h, params->mode, (w*h)-1);
	number temp;
	cell *spaces = snewn(w*h, cell);
	number *grid = NULL;

	while (!grid)
	{
		grid = generate_hamiltonian_path(w, h, rs, params);
	}

	for(i = 0; i < w*h; i++)
	{
		if (IS_OBSTACLE(grid[i])) scratch->end--;
		spaces[i] = i;
	}
	shuffle(spaces, w*h, sizeof(*spaces), rs);
	for(j = 0; j < w*h; j++)
	{
		i = spaces[j];
		temp = grid[i];
		if (temp < 0) continue;
		if (!params->removeends && (temp == 0 || temp == scratch->end)) continue;
		grid[i] = NUMBER_EMPTY;
		
		ascent_solve(grid, params->diff, scratch);
		
		if (!check_completion(scratch->grid, w, h, params->mode))
			grid[i] = temp;
	}
	
	char *ret = snewn(w*h*4, char);
	char *p = ret;
	int run = 0;
	enum { RUN_NONE, RUN_BLANK, RUN_WALL, RUN_NUMBER } runtype = RUN_NONE;
	for(i = 0; i <= w*h; i++)
	{
		if(runtype == RUN_BLANK && (i == w*h || grid[i] != NUMBER_EMPTY))
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
		if(runtype == RUN_WALL && (i == w*h || !IS_OBSTACLE(grid[i])))
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

		if(grid[i] >= 0)
		{
			if(runtype == RUN_NUMBER)
				*p++ = '_';
			p += sprintf(p, "%d", grid[i] + 1);
			runtype = RUN_NUMBER;
		}
		else if(grid[i] == NUMBER_EMPTY)
		{
			runtype = RUN_BLANK;
			run++;
		}
		else if(IS_OBSTACLE(grid[i]))
		{
			runtype = RUN_WALL;
			run++;
		}
	}
	*p++ = '\0';
	ret = sresize(ret, p - ret, char);
	free_scratch(scratch);
	sfree(grid);
	sfree(spaces);
	return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	int s = params->w*params->h;
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
	int w = params->w;
	int h = params->h;
	int i, j, walls;
	
	game_state *state = snew(game_state);

	state->w = w;
	state->h = h;
	state->mode = params->mode;
	state->completed = state->cheated = FALSE;
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
	
	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->grid);
	sfree(state->immutable);
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

static int game_can_format_as_text_now(const game_params *params)
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
		if(n >= 0)
			p += sprintf(p, "%*d", space, n+1);
		else if(n == -2)
			p += sprintf(p, "%*s", space, "#");
		else if (n == -3)
			p += sprintf(p, "%*s", space, " ");
		else
			p += sprintf(p, "%*s", space, ".");
		*p++ = x < w-1 ? ' ' : '\n';
	}
	*p++ = '\0';
	
	return ret;
}

struct game_ui
{
	cell held;
	number select, target;
	int dir;
	
	cell *positions;
	int s;

	enum { CSHOW_NONE, CSHOW_KEYBOARD, CSHOW_MOUSE } cshow;
	cell typing_cell;
	number typing_number;
	int cx, cy;
	char move_with_numpad;
};

static game_ui *new_ui(const game_state *state)
{
	int i, w = state->w, s = w*state->h;
	game_ui *ret = snew(game_ui);
	
	ret->held = NUMBER_EMPTY;
	ret->select = ret->target = CELL_NONE;
	ret->dir = 0;
	ret->positions = snewn(s, cell);
	ret->s = s;
	ret->cshow = CSHOW_NONE;
	ret->move_with_numpad = FALSE;

	for (i = 0; i < s; i++)
	{
		if (state->grid[i] != NUMBER_BOUND) break;
	}
	ret->cx = i%w;
	ret->cy = i/w;
	ret->typing_cell = CELL_NONE;
	ret->typing_number = 0;
	
	update_positions(ret->positions, state->grid, s);
	return ret;
}

static void free_ui(game_ui *ui)
{
	sfree(ui->positions);
	sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
	/*
	 * Resuming a saved game will not create a ui based on the current state,
	 * but based on the original state. This causes most lines to disappear
	 * from the screen, until the user interacts with the game.
	 * To remedy this, the positions array is included in the save file.
	 */
	char *ret = snewn(ui->s*4, char);
	char *p = ret;
	int run = 0;
	int i;
	*p++ = 'P';
	for(i = 0; i < ui->s; i++)
	{
		if(ui->positions[i] != -1)
		{
			if(i != 0)
				*p++ = run ? 'a' + run-1 : '_';
			if(ui->positions[i] == CELL_MULTIPLE)
				*p++ = '-';
			else
				p += sprintf(p, "%d", ui->positions[i]);
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
	*p++ = '\0';
	ret = sresize(ret, p - ret, char);
	return ret;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
	if(!encoding || encoding[0] != 'P') return;
	
	int i;
	const char *p = encoding+1;
	
	for(i = 0; i < ui->s; i++) ui->positions[i] = CELL_NONE;
	i = 0;
	while(*p && i < ui->s)
	{
		if(isdigit((unsigned char) *p))
		{
			ui->positions[i] = atoi(p);
			if(ui->positions[i] >= ui->s) ui->positions[i] = CELL_NONE;
			while (*p && isdigit((unsigned char) *p)) ++p;
			++i;
		}
		else if(*p == '-')
		{
			ui->positions[i] = CELL_MULTIPLE;
			++i;
			++p;
		}
		else if(*p >= 'a' && *p <= 'z')
			i += ((*p++) - 'a') + 1;
		else
			++p;
	}
}

static void ui_clear(game_ui *ui)
{
	/* Deselect the current number */
	ui->held = NUMBER_EMPTY;
	ui->select = ui->target = CELL_NONE;
	ui->dir = 0;
}

static void ui_seek(game_ui *ui, number last)
{
	/* Move the selection forward until an unplaced number is found */
	if(ui->held == CELL_NONE || ui->select < 0 || ui->select > last)
	{
		ui->select = NUMBER_EMPTY;
		ui->target = NUMBER_EMPTY;
	}
	else
	{
		number n = ui->select;
		while(n + ui->dir >= 0 && n + ui->dir <= last && ui->positions[n] == CELL_NONE)
			n += ui->dir;
		ui->target = n;
	}
}

static void ui_backtrack(game_ui *ui, number last)
{
	/* 
	 * Move the selection backward until a placed number is found, 
	 * then point the selection forward again.
	 */
	number n = ui->select;
	if(!ui->dir)
	{
		ui_clear(ui);
		return;
	}
	
	do
	{
		n -= ui->dir;
		ui->held = ui->positions[n];
	}
	while(n > 0 && n < last && ui->held == CELL_NONE);
	
	ui->select = n + ui->dir;
	ui_seek(ui, last);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
	update_positions(ui->positions, newstate->grid, newstate->w*newstate->h);
	
	if(ui->held != CELL_NONE && newstate->grid[ui->held] == NUMBER_EMPTY)
	{
		ui_backtrack(ui, oldstate->last);
	}
	if(!oldstate->completed && newstate->completed)
	{
		ui_clear(ui);
	}
	else
	{
		ui_seek(ui, oldstate->last);
	}
}

struct game_drawstate {
	int tilesize;
	int *colours;
	char redraw;
	cell *oldpositions;
	number *oldgrid;
	cell oldheld;
	number oldtarget;

	/* Blitter for the background of the keyboard cursor */
	blitter *bl;
	char bl_on;
	/* Position of the center of the blitter */
	int blx, bly;
	/* Radius of the keyboard cursor */
	int blr;
};

#define FROMCOORD(x) ( ((x)-(tilesize/2)) / tilesize )
#define DRAG_RADIUS 0.6F
static char *interpret_move(const game_state *state, game_ui *ui,
							const game_drawstate *ds,
							int ox, int oy, int button)
{
	int w = state->w, h = state->h;
	int gx, gy;
	int tilesize = ds->tilesize;
	cell i;
	number n;
	char buf[80];
	int dir = -1;
	char finish_typing = FALSE;
	
	gy = oy < tilesize/2 ? -1 : FROMCOORD(oy);
	if (IS_HEXAGONAL(state->mode))
	{
		ox -= (gy - (h / 2)) * tilesize / 2;
	}
	gx = ox < tilesize/2 ? -1 : FROMCOORD(ox);

	if (IS_MOUSE_DOWN(button))
	{
		ui->cshow = CSHOW_NONE;
		finish_typing = TRUE;
	}

	if(!ui->move_with_numpad)
		button &= ~MOD_NUM_KEYPAD;

	/* Keyboard cursor movement */
	if (button == CURSOR_UP || button == (MOD_NUM_KEYPAD | '8'))
		dir = (IS_HEXAGONAL(state->mode) && ui->cy > 0 && (ui->cy & 1) == 0) ? 2 : 1;
	else if (button == CURSOR_DOWN || button == (MOD_NUM_KEYPAD | '2'))
		dir = IS_HEXAGONAL(state->mode) && ui->cy & 1 ? 5 : 6;
	else if (button == CURSOR_LEFT || button == (MOD_NUM_KEYPAD | '4'))
		dir = 3;
	else if (button == CURSOR_RIGHT || button == (MOD_NUM_KEYPAD | '6'))
		dir = 4;
	else if (button == (MOD_NUM_KEYPAD | '7'))
		dir = IS_HEXAGONAL(state->mode) ? 1 : 0;
	else if (button == (MOD_NUM_KEYPAD | '1'))
		dir = 5;
	else if (button == (MOD_NUM_KEYPAD | '9'))
		dir = 2;
	else if (button == (MOD_NUM_KEYPAD | '3'))
		dir = IS_HEXAGONAL(state->mode) ? 6 : 7;

	if (dir != -1)
	{
		ui->cshow = CSHOW_KEYBOARD;
		ui->cx += dir_x[dir];
		ui->cy += dir_y[dir];

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

		finish_typing = TRUE;
	}

	/* Clicking outside the grid clears the selection. */
	if (IS_MOUSE_DOWN(button) && (gx < 0 || gy < 0 || gx >= w || gy >= h))
		ui_clear(ui);

	/* Pressing Enter, Spacebar or Backspace when not typing will emulate a mouse click */
	if ((IS_CURSOR_SELECT(button) || button == '\b') && ui->cshow == CSHOW_KEYBOARD && ui->typing_cell == CELL_NONE)
	{
		gy = ui->cy;
		gx = ui->cx;
		button = (button == CURSOR_SELECT ? LEFT_BUTTON : RIGHT_BUTTON);
	}
	/* Press Enter to confirm typing */
	if (button == CURSOR_SELECT && ui->typing_cell != CELL_NONE)
		finish_typing = TRUE;

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
		return UI_UPDATE;
	}

	/* Remove the last digit when typing */
	if (button == '\b' && ui->typing_cell != CELL_NONE)
	{
		ui->typing_number /= 10;
		if (ui->typing_number == 0)
			ui->typing_cell = CELL_NONE;
		return UI_UPDATE;
	}

	if (gx >= 0 && gx < w && gy >= 0 && gy < h)
	{
		i = gy*w+gx;
		if(IS_MOUSE_DRAG(button) && ui->held >= 0)
		{
			int hx = (1+gx) * tilesize;
			int hy = (1+gy) * tilesize;
			
			/* 
			 * When dragging, the mouse must be close enough to the center of
			 * the new cell. The hitbox is octagon-shaped to avoid drawing a 
			 * straight line when trying to draw a diagonal line.
			 */
			if(abs(ox-hx) + abs(oy-hy) > DRAG_RADIUS*tilesize)
				return NULL;
		}
		n = state->grid[i];
		
		switch(button)
		{
		case LEFT_BUTTON:
			update_positions(ui->positions, state->grid, w*h);
			
			/* Click on wall */
			if(IS_OBSTACLE(n))
			{
				ui_clear(ui);
				finish_typing = TRUE;
				break;
			}
			if(n >= 0)
			{
				/* Click a placed number again to change direction */
				if(i == ui->held && ui->dir != 0)
				{
					ui->dir *= -1;
				}
				else
				{
					/* Highlight a placed number */
					ui->held = i;
					ui->dir = n < state->last && ui->positions[n+1] == CELL_NONE ? +1
						: n > 0 && ui->positions[n-1] == CELL_NONE ? -1 : +1;
				}
				ui->select = n + ui->dir;
				
				ui_seek(ui, state->last);
				finish_typing = TRUE;
				break;
			}
		/* Deliberate fallthrough */
		case LEFT_DRAG:
			/* Dragging over the next highlighted number moves the highlight forward */
			if(n >= 0 && ui->select == n && ui->select + ui->dir <= state->last && ui->select + ui->dir >= 0)
			{
				ui->held = i;
				ui->select += ui->dir;
				ui_seek(ui, state->last);
				ui->cshow = CSHOW_NONE;
				return UI_UPDATE;
			}
			/* Place the next number */
			if(n == NUMBER_EMPTY && ui->held != CELL_NONE && ui->positions[ui->select] == CELL_NONE && is_near(ui->held, i, state))
			{
				sprintf(buf, "P%d,%d", i, ui->select);
				
				ui->held = i;
				if(ui->select + ui->dir <= state->last)
					ui->select += ui->dir;
				
				ui->cshow = CSHOW_NONE;

				return dupstr(buf);
			}
			/* Highlight an empty cell */
			else if(n == NUMBER_EMPTY && button == LEFT_BUTTON)
			{
				ui_clear(ui);
				ui->cx = i % w;
				ui->cy = i / w;
				ui->cshow = CSHOW_MOUSE;
				finish_typing = TRUE;
			}
		break;
		case MIDDLE_BUTTON:
		case RIGHT_BUTTON:
			update_positions(ui->positions, state->grid, w*h);
			if(n == NUMBER_EMPTY || GET_BIT(state->immutable, i))
			{
				ui_clear(ui);
				finish_typing = TRUE;
			}

		/* Deliberate fallthrough */
		case MIDDLE_DRAG:
		case RIGHT_DRAG:
			/* Drag over numbers to clear them */
			if(ui->typing_cell == CELL_NONE && n != NUMBER_EMPTY && !GET_BIT(state->immutable, i))
			{
				sprintf(buf, "C%d", i);
				return dupstr(buf);
			}
		}
	}
	
	/* Confirm typed number */
	if (finish_typing && ui->typing_cell != CELL_NONE)
	{
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
			ui_seek(ui, state->last);
		}

		if (state->grid[i] == n || n > state->last)
			return UI_UPDATE;

		sprintf(buf, "P%d,%d", i, n);
		return dupstr(buf);
	}

	if(button == '\b' && !ui->cshow && ui->held >= 0 && !GET_BIT(state->immutable, ui->held))
	{
		sprintf(buf, "C%d", ui->held);
		return dupstr(buf);
	}
	
	return finish_typing ? UI_UPDATE : NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	int w = state->w, h = state->h;
	cell i = -1;
	number n = -1;
	
	if (move[0] == 'P' &&
			sscanf(move+1, "%d,%d", &i, &n) == 2 &&
			i >= 0 && i < w*h && n >= 0 && n <= state->last
			)
	{
		if(GET_BIT(state->immutable, i))
			return NULL;
		
		game_state *ret = dup_game(state);
		
		ret->grid[i] = n;
		
		if(check_completion(ret->grid, w, h, ret->mode))
			ret->completed = TRUE;
		
		return ret;
	}
	if (move[0] == 'C')
	{
		i = atoi(move+1);
		if(i < 0 || i >= w*h)
			return NULL;
		if(GET_BIT(state->immutable, i))
			return NULL;
		
		game_state *ret = dup_game(state);
		
		ret->grid[i] = NUMBER_EMPTY;
		
		return ret;
	}
	if (move[0] == 'S')
	{
		const char *p = move+1;
		game_state *ret = dup_game(state);
		for(i = 0; i < w*h; i++)
		{
			if(*p != '-')
			{
				n = atoi(p)-1;
				ret->grid[i] = n;
				while(*p && isdigit((unsigned char)*p))p++;
			}
			else if(*p == '-')
			{
				if(!GET_BIT(ret->immutable, i)) ret->grid[i] = NUMBER_EMPTY;
				p++;
			}
			if(!*p)
			{
				free_game(ret);
				return NULL;
			}
			p++; /* Skip comma */
		}
		return ret;
	}
	
	return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
	*x = (params->w+1) * tilesize;
	*y = (params->h+1) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
	ds->tilesize = tilesize;
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
	
	ret[COL_IMMUTABLE * 3 + 0] = 0.0F;
	ret[COL_IMMUTABLE * 3 + 1] = 0.0F;
	ret[COL_IMMUTABLE * 3 + 2] = 1.0F;
	
	ret[COL_ERROR * 3 + 0] = 1.0F;
	ret[COL_ERROR * 3 + 1] = 0.0F;
	ret[COL_ERROR * 3 + 2] = 0.0F;
	
	ret[COL_CURSOR * 3 + 0] = 0.0F;
	ret[COL_CURSOR * 3 + 1] = 0.7F;
	ret[COL_CURSOR * 3 + 2] = 0.0F;

	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	struct game_drawstate *ds = snew(struct game_drawstate);
	int s = state->w * state->h;
	
	ds->tilesize = 0;
	ds->oldheld = 0;
	ds->oldtarget = 0;
	ds->redraw = TRUE;
	ds->colours = snewn(s, int);
	ds->oldgrid = snewn(s, number);
	ds->oldpositions = snewn(s, cell);

	memset(ds->colours, ~0, s*sizeof(int));
	memset(ds->oldgrid, ~0, s*sizeof(number));
	memset(ds->oldpositions, ~0, s*sizeof(cell));

	ds->bl = NULL;
	ds->bl_on = FALSE;
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
	if (ds->bl)
		blitter_free(dr, ds->bl);
	sfree(ds);
}

#define FLASH_FRAME 0.03F
#define FLASH_SIZE  4
#define TOCOORD(x) ( (x)*tilesize + (tilesize/2) )
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
	number n;
	char error;
	char buf[8];
	const cell *positions = ui->positions;
	int flash = -2, colour;
	int margin = tilesize*ERROR_MARGIN;
	
	if(flashtime > 0)
		flash = (int)(flashtime/FLASH_FRAME);
	
	if (ds->bl_on)
	{
		blitter_load(dr, ds->bl,
			ds->blx - ds->blr, ds->bly - ds->blr);
		draw_update(dr,
			ds->blx - ds->blr, ds->bly - ds->blr,
			tilesize, tilesize);
		ds->bl_on = FALSE;
	}

	if(ds->redraw)
	{
		draw_rect(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize, COL_MIDLIGHT);
		draw_update(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize);

		memcpy(ds->oldgrid, state->grid, w*h * sizeof(number));
	}
	else
	{
		char dirty;
		
		/* Invalidate squares */
		for(i = 0; i < w*h; i++)
		{
			dirty = FALSE;
			n = state->grid[i];
			if(n == NUMBER_EMPTY && ui->held != CELL_NONE && is_near(i, ui->held, state) && positions[ui->select] == CELL_NONE)
				n = ui->select;
			if(i == ui->typing_cell)
				n = ui->typing_number - 1;
			
			if(ds->oldgrid[i] != n)
			{
				dirty = TRUE;
				ds->oldgrid[i] = n;
			}
			
			if(ui->held != ds->oldheld || ui->target != ds->oldtarget)
			{
				if(is_near(i, ui->held, state))
					dirty = TRUE;
				else if(is_near(i, ds->oldheld, state))
					dirty = TRUE;
			}
			
			if(dirty)
				ds->colours[i] = -1;
		}
		
		/* Invalidate numbers */
		for(n = 0; n <= state->last; n++)
		{
			dirty = FALSE;
			
			if(n > 0 && ds->oldpositions[n-1] != positions[n-1])
				dirty = TRUE;
			if(n < state->last && ds->oldpositions[n+1] != positions[n+1])
				dirty = TRUE;
			if(ds->oldpositions[n] != positions[n])
				dirty = TRUE;
			
			if(dirty)
			{
				if(ds->oldpositions[n] >= 0)
					ds->colours[ds->oldpositions[n]] = -1;
				if(positions[n] >= 0)
					ds->colours[positions[n]] = -1;
			}
		}
	}

	memcpy(ds->oldpositions, ui->positions, w*h * sizeof(cell));

	ds->redraw = FALSE;
	ds->oldheld = ui->held;
	ds->oldtarget = ui->target;
	
	/* Draw squares */
	for(i = 0; i < w*h; i++)
	{
		tx = TOCOORD(i%w), ty = TOCOORD(i/w);
		if (IS_HEXAGONAL(state->mode))
		{
			tx += ((i / w) - (h / 2)) * tilesize / 2;
		}
		tx1 = tx + (tilesize/2), ty1 = ty + (tilesize/2);
		n = state->grid[i];
		error = FALSE;
		
		if (n == NUMBER_BOUND)
			continue;

		colour = n == NUMBER_WALL ? COL_BORDER :
			flash >= n && flash <= n + FLASH_SIZE ? COL_LOWLIGHT :
			ui->held == i || ui->typing_cell == i ||
				(ui->cshow == CSHOW_MOUSE && ui->cy*w+ui->cx == i) ? COL_LOWLIGHT :
			ui->target >= 0 && positions[ui->target] == i ? COL_HIGHLIGHT :
			COL_MIDLIGHT;
		
		if(ds->colours[i] == colour) continue;
		
		/* Draw tile background */
		clip(dr, tx, ty, tilesize+1, tilesize+1);
		draw_update(dr, tx, ty, tilesize+1, tilesize+1);
		draw_rect(dr, tx, ty, tilesize, tilesize, colour);
		ds->colours[i] = colour;
		
		if (ui->typing_cell != i)
		{
			/* Draw a circle on the beginning and the end of the path */
			if ((n == 0 || n == state->last) && 
				(GET_BIT(state->immutable, i) || positions[n] != CELL_MULTIPLE))
			{
				draw_circle(dr, tx + (tilesize / 2), ty + (tilesize / 2),
					tilesize / 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
			}

			/* Draw path lines */
			if (n > 0 && positions[n] != CELL_MULTIPLE && positions[n - 1] >= 0)
			{
				i2 = positions[n - 1];
				tx2 = (i2%w)*tilesize + (tilesize);
				if (IS_HEXAGONAL(state->mode))
					tx2 += ((i2 / w) - (h / 2)) * tilesize / 2;
				ty2 = (i2 / w)*tilesize + (tilesize);
				if (is_near(i, i2, state))
					draw_thick_line(dr, 5.0, tx1, ty1, tx2, ty2, COL_HIGHLIGHT);
				else
					error = TRUE;
			}
			if (n >= 0 && n < state->last && positions[n] != CELL_MULTIPLE && positions[n + 1] >= 0)
			{
				i2 = positions[n + 1];
				tx2 = (i2%w)*tilesize + (tilesize);
				if (IS_HEXAGONAL(state->mode))
					tx2 += ((i2 / w) - (h / 2)) * tilesize / 2;
				ty2 = (i2 / w)*tilesize + (tilesize);
				if (is_near(i, i2, state))
					draw_thick_line(dr, 5.0, tx1, ty1, tx2, ty2, COL_HIGHLIGHT);
				else
					error = TRUE;
			}
		}
		
		/* Draw square border */
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

		if (n == NUMBER_EMPTY && ui->held != CELL_NONE && is_near(i, ui->held, state) && positions[ui->select] == CELL_NONE)
			n = ui->select;
		if (ui->typing_cell == i)
			n = ui->typing_number - 1;
		
		/* Draw a light circle on possible endpoints */
		if(state->grid[i] == NUMBER_EMPTY && (n == 0 || n == state->last))
		{
			draw_circle(dr, tx+(tilesize/2), ty+(tilesize/2),
				tilesize/3, colour, COL_LOWLIGHT);
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
			
			if(error)
			{
				draw_thick_line(dr, 2, tx+margin, ty+margin,
					(tx+tilesize)-margin, (ty+tilesize)-margin, COL_ERROR);
			}
		}
		
		unclip(dr);
	}

	if (ui->cshow == CSHOW_KEYBOARD)
	{
		ds->blx = (ui->cx+1)*tilesize;
		ds->bly = (ui->cy+1)*tilesize;

		if (IS_HEXAGONAL(state->mode))
			ds->blx += (ui->cy - (h / 2)) * tilesize / 2;

		blitter_save(dr, ds->bl, ds->blx - ds->blr, ds->bly - ds->blr);
		ds->bl_on = TRUE;

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

static int game_timing_state(const game_state *state, game_ui *ui)
{
	return TRUE;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
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
	TRUE, game_configure, custom_params,
	validate_params,
	new_game_desc,
	validate_desc,
	new_game,
	dup_game,
	free_game,
	TRUE, solve_game,
	TRUE, game_can_format_as_text_now, game_text_format,
	new_ui,
	free_ui,
	encode_ui,
	decode_ui,
	game_changed_state,
	interpret_move,
	execute_move,
	32, game_compute_size, game_set_size,
	game_colours,
	game_new_drawstate,
	game_free_drawstate,
	game_redraw,
	game_anim_length,
	game_flash_length,
	game_status,
	FALSE, FALSE, game_print_size, game_print,
	FALSE, /* wants_statusbar */
	FALSE, game_timing_state,
	0, /* flags */
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
			solver_verbose = TRUE;
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
		err = validate_params(params, TRUE);
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
			encode_params(params, TRUE));
		desc_gen = new_game_desc(params, rs, &aux, FALSE);

		if (!solver_verbose) {
			char *fmt = game_text_format(new_game(NULL, params, desc_gen));
			fputs(fmt, stdout);
			sfree(fmt);
		}

		printf("Game ID: %s\n", desc_gen);
	} else {
		game_state *input;
		struct solver_scratch *scratch;
		int w = params->w, h = params->h;
		
		err = validate_desc(params, desc);
		if (err) {
			fprintf(stderr, "Description is invalid\n");
			fprintf(stderr, "%s", err);
			exit(1);
		}
		
		input = new_game(NULL, params, desc);
		scratch = new_scratch(w, h, params->mode, input->last);
		
		ascent_solve(input->grid, DIFFCOUNT, scratch);
		
		free_scratch(scratch);
		free_game(input);
	}

	return 0;
}
#endif
