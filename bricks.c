/*
 * bricks.c: Implementation for Tawamurenga puzzles.
 * (C) 2021 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 * 
 * Objective of the game: Shade several cells in the hexagonal grid 
 * while following these rules:
 * 1. Each shaded cell must have at least one shaded cell below it
 *    (unless it's on the bottom row).
 * 2. There can not be 3 or more shaded cells in a horizontal row.
 * 3. A number indicates the amount of shaded cells around it.
 * 4. Cells with numbers cannot be shaded.
 * 
 * This genre was invented by Nikoli.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

enum {
	COL_MIDLIGHT,
	COL_LOWLIGHT,
	COL_HIGHLIGHT,
	COL_BORDER,
	COL_SHADE,
	COL_ERROR,
	COL_CURSOR,
	NCOLOURS
};

typedef unsigned int cell;

#define NUM_MASK       0x007
#define F_BOUND        0x008

#define F_SHADE        0x010
#define F_UNSHADE      0x020
#define F_EMPTY        0x030
#define COL_MASK       0x030

#define FE_ERROR       0x040
#define FE_TOPLEFT     0x080
#define FE_TOPRIGHT    0x100
#define FE_LINE_LEFT   0x200
#define FE_LINE_RIGHT  0x400
#define ERROR_MASK     0x7C0

#define FE_CURSOR      0x800

struct game_params {
	/* User-friendly width and height */
#ifndef PORTRAIT_SCREEN
	int w, h;
#else
	int h, w;
#endif

	/* Difficulty and grid type */
	int diff;
};

#define DIFFLIST(A)                        \
	A(EASY,Easy,e)                         \
	A(NORMAL,Normal,n)                     \
	A(TRICKY,Tricky,t)                     \

#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title

#define DIFFENUM(upper,title,lower) DIFF_ ## upper,
enum { DIFFLIST(DIFFENUM) DIFFCOUNT };
static char const *const bricks_diffnames[] = { DIFFLIST(TITLE) };
static char const bricks_diffchars[] = DIFFLIST(ENCODE);

const static struct game_params bricks_presets[] = {
	{ 7,  6, DIFF_EASY },
	{ 7,  6, DIFF_NORMAL },
	{ 7,  6, DIFF_TRICKY },
	{ 10, 8, DIFF_EASY },
	{ 10, 8, DIFF_NORMAL },
	{ 10, 8, DIFF_TRICKY },
};

#define DEFAULT_PRESET 0

struct game_state {
	/* Original width and height, from parameters */
	int pw, h;

	/* Actual width, which includes grid padding */
	int w;

	cell *grid;

	bool completed, cheated;
};

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);

	*ret = bricks_presets[DEFAULT_PRESET];        /* structure copy */
	
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
	struct preset_menu *menu;
	menu = preset_menu_new();

	for (i = 0; i < lenof(bricks_presets); i++)
	{
		params = dup_params(&bricks_presets[i]);
		sprintf(buf, "%dx%d %s", params->w, params->h, 
			bricks_diffnames[params->diff]);
		preset_menu_add_preset(menu, dupstr(buf), params);
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
	if (*string == 'd') {
		int i;
		string++;
		params->diff = DIFFCOUNT + 1;   /* ...which is invalid */
		if (*string) {
			for (i = 0; i < DIFFCOUNT; i++) {
				if (*string == bricks_diffchars[i])
					params->diff = i;
			}
			string++;
		}
	}
}

static char *encode_params(const game_params *params, bool full)
{
	char buf[256];
	char *p = buf;
	p += sprintf(p, "%dx%d", params->w, params->h);
	if (full)
	{
		p += sprintf(p, "d%c", bricks_diffchars[params->diff]);
	}

	*p++ = '\0';
	
	return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
	config_item *ret;
	char buf[80];
	
	ret = snewn(4, config_item);
	
	ret[0].name = "Width";
	ret[0].type = C_STRING;
	sprintf(buf, "%d", params->w);
	ret[0].u.string.sval = dupstr(buf);
	
	ret[1].name = "Height";
	ret[1].type = C_STRING;
	sprintf(buf, "%d", params->h);
	ret[1].u.string.sval = dupstr(buf);
	
	ret[2].name = "Difficulty";
	ret[2].type = C_CHOICES;
	ret[2].u.choices.choicenames = DIFFLIST(CONFIG);
	ret[2].u.choices.selected = params->diff;
	
	ret[3].name = NULL;
	ret[3].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = snew(game_params);
	
	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->diff = cfg[2].u.choices.selected;
	
	return ret;
}


static const char *validate_params(const game_params *params, bool full)
{
	int w = params->w;
	int h = params->h;
	
	if(w < 2) return "Width must be at least 2";
	if(h < 2) return "Height must be at least 2";
	if (params->diff >= DIFFCOUNT)
		return "Unknown difficulty rating";
	
	return NULL;
}

/* ******************** *
 * Validation and Tools *
 * ******************** */

enum { STATUS_COMPLETE, STATUS_UNFINISHED, STATUS_INVALID };

static char bricks_validate_threes(int w, int h, cell *grid)
{
	int x, y;
	char ret = STATUS_COMPLETE;

	/* Check for any three in a row, and mark errors accordingly */
	for (y = 0; y < h; y++) {
		for (x = 1; x < w-1; x++) {
			int i1 = y * w + x-1;
			int i2 = y * w + x;
			int i3 = y * w + x+1;

			if ((grid[i1] & COL_MASK) == F_SHADE &&
					(grid[i2] & COL_MASK) == F_SHADE &&
					(grid[i3] & COL_MASK) == F_SHADE) {
				ret = STATUS_INVALID;
				grid[i1] |= FE_LINE_LEFT;
				grid[i2] |= FE_LINE_LEFT|FE_LINE_RIGHT;
				grid[i3] |= FE_LINE_RIGHT;
			}
		}
	}

	return ret;
}

static char bricks_validate_gravity(int w, int h, cell *grid)
{
	int x, y;
	char ret = STATUS_COMPLETE;

	for (y = 0; y < h-1; y++) {
		for (x = 0; x < w; x++) {
			int i1 = y * w + x;

			if ((grid[i1] & COL_MASK) != F_SHADE)
				continue;

			int i2 = (y+1) * w + x-1;
			int i3 = (y+1) * w + x;

			cell n2 = x == 0 ? F_BOUND : grid[i2];
			if(!(n2 & F_BOUND))
				n2 &= COL_MASK;
			cell n3 = grid[i3];
			if(!(n3 & F_BOUND))
				n3 &= COL_MASK;

			if(n2 != F_SHADE && n3 != F_SHADE)
				ret = max(STATUS_UNFINISHED, ret);

			bool w2 = !n2 || n2 == F_UNSHADE || n2 == F_BOUND;
			bool w3 = !n3 || n3 == F_UNSHADE || n3 == F_BOUND;

			if(w2 && w3) {
				ret = STATUS_INVALID;
				grid[i1] |= FE_ERROR;
				if(n2 != F_BOUND)
					grid[i2] |= FE_TOPRIGHT;
				if(n3 != F_BOUND)
					grid[i3] |= FE_TOPLEFT;
			}
		}
	}

	return ret;
}

typedef struct {
	int dx, dy;
} bricks_step;
static bricks_step bricks_steps[6] = {
		{0, -1}, {1,-1},
	{-1, 0},         {1, 0},
		{-1, 1}, {0, 1},
};

static char bricks_validate_counts(int w, int h, cell *grid)
{
	int x, y, x2, y2, s;
	int shade, unshade;
	cell n, n2;
	char ret = STATUS_COMPLETE;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			n = grid[y*w+x];
			if((n & COL_MASK) || n & F_BOUND) continue;
			n &= NUM_MASK;
			if(n == 7) continue;

			shade = unshade = 0;

			for(s = 0; s < 6; s++) {
				x2 = x + bricks_steps[s].dx;
				y2 = y + bricks_steps[s].dy;

				if(x2 < 0 || x2 >= w || y2 < 0 || y2 >= h)
					unshade++;
				else {
					n2 = grid[y2*w+x2];
					if(n2 & COL_MASK) {
						n2 &= COL_MASK;
						if(n2 == F_SHADE)
							shade++;
						else if(n2 == F_UNSHADE)
							unshade++;
					} else
						unshade++;
				}
			}

			if(shade < n)
				ret =  max(ret, STATUS_UNFINISHED);
			
			if(shade > n || 6 - unshade < n) {
				ret = STATUS_INVALID;
				grid[y*w+x] |= FE_ERROR;
			}
		}
	}

	return ret;
}

static char bricks_validate(int w, int h, cell *grid, bool strict)
{
	int s = w * h;
	int i;
	char ret = STATUS_COMPLETE;
	char newstatus;

	for(i = 0; i < s; i++) {
		grid[i] &= ~ERROR_MASK;
	}

	newstatus = bricks_validate_threes(w, h, grid);
	ret = max(newstatus, ret);

	newstatus = bricks_validate_gravity(w, h, grid);
	ret = max(newstatus, ret);

	newstatus = bricks_validate_counts(w, h, grid);
	ret = max(newstatus, ret);

	if(strict) {
		for(i = 0; i < s; i++) {
			if((grid[i] & COL_MASK) == F_EMPTY)
				return STATUS_UNFINISHED;
		}
	}

	return ret;
}

static void bricks_grid_size(const game_params *params, int *w, int *h)
{
	*w = params->w;
	*h = params->h;

	*w += ((*h + 1) / 2) - 1;
}

static void bricks_apply_bounds(int w, int h, cell *grid)
{
	int i, x, y, extra;
	for(i = 0; i < w*h; i++)
		grid[i] = F_EMPTY;

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < y / 2; x++)
		{
			grid[(y*w)+(w-x-1)] = F_BOUND;
		}
		extra = (h | y) & 1 ? 0 : 1;
		for (x = 0; x + extra < (h-y) / 2; x++)
		{
			grid[y*w + x] = F_BOUND;
		}
	}
}


static const char *validate_desc(const game_params *params, const char *desc)
{
	int w, h;
	bricks_grid_size(params, &w, &h);

	int s = params->w*params->h;
	const char *p = desc;
	cell n;

	int i = 0;
	while(*p)
	{
		if(isdigit((unsigned char) *p))
		{
			n = atoi(p);
			if(n > 7)
				return "Number is out of range";
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
	bricks_grid_size(params, &w, &h);

	int i, j;
	
	game_state *state = snew(game_state);

	state->w = w;
	state->h = h;
	state->pw = params->w;
	state->completed = state->cheated = false;
	state->grid = snewn(w*h, cell);
	bricks_apply_bounds(w, h, state->grid);
	
	const char *p = desc;
	i = j = 0;
	while(*p)
	{
		if(state->grid[i] == F_BOUND) {
			++i;
			continue;
		}

		if(j > 0) {
			++i;
			--j;
			continue;
		}

		if(isdigit((unsigned char) *p))
		{
			state->grid[i] = atoi(p);
			while (*p && isdigit((unsigned char) *p)) ++p;
			++j;
		}
		else if(*p >= 'a' && *p <= 'z')
			j += ((*p++) - 'a') + 1;
		else
			++p;
	}
	
	return state;
}

static game_state *dup_game(const game_state *state)
{
	int w = state->w;
	int h = state->h;
	
	game_state *ret = snew(game_state);

	ret->w = w;
	ret->h = h;
	ret->pw = state->pw;
	ret->completed = state->completed;
	ret->cheated = state->cheated;
	ret->grid = snewn(w*h, cell);
	
	memcpy(ret->grid, state->grid, w*h*sizeof(cell));

	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->grid);
	sfree(state);
}

/* ****** *
 * Solver *
 * ****** */

static int bricks_solver_try(game_state *state)
{
	int w = state->w, h = state->h, s = w * h;
	int i, d;
	int ret = 0;

	for (i = 0; i < s; i++)
	{
		if ((state->grid[i] & COL_MASK) != F_EMPTY)
			continue;

		for (d = 0; d <= 1; d++)
		{
			/* See if this leads to an invalid state */
			state->grid[i] = d ? F_SHADE : F_UNSHADE;
			if (bricks_validate(w, h, state->grid, false) == STATUS_INVALID)
			{
				state->grid[i] = d ? F_UNSHADE : F_SHADE;
				ret++;
				break;
			}
			else
				state->grid[i] = F_EMPTY;
		}
	}

	return ret;
}

static int bricks_solve_game(game_state *state, int maxdiff, cell *temp, bool clear, bool strict);

static int bricks_solver_recurse(game_state *state, int maxdiff, cell *temp)
{
	int s = state->w*state->h;
	int i, d;
	int ret = 0, tempresult;

	for (i = 0; i < s; i++)
	{
		if ((state->grid[i] & COL_MASK) != F_EMPTY)
			continue;

		for (d = 0; d <= 1; d++)
		{
			/* See if this leads to an invalid state */
			memcpy(temp, state->grid, s * sizeof(cell));
			state->grid[i] = d ? F_SHADE : F_UNSHADE;
			tempresult = bricks_solve_game(state, maxdiff - 1, NULL, false, false);
			memcpy(state->grid, temp, s * sizeof(cell));
			if (tempresult == STATUS_INVALID)
			{
				state->grid[i] = d ? F_UNSHADE : F_SHADE;
				ret++;
				break;
			}
		}
	}

	return ret;
}

static int bricks_solve_game(game_state *state, int maxdiff, cell *temp, bool clear, bool strict)
{
	int i;
	int w = state->w, h = state->h, s = w * h;
	int ret = STATUS_UNFINISHED;

	cell hastemp = temp != NULL;
	if (!hastemp && maxdiff >= DIFF_NORMAL)
		temp = snewn(s, cell);
	
	if(clear) {
		for(i = 0; i < s; i++) {
			if(state->grid[i] & COL_MASK)
				state->grid[i] = F_EMPTY;
		}
	}

	while ((ret = bricks_validate(w, h, state->grid, strict)) == STATUS_UNFINISHED)
	{
		if (bricks_solver_try(state))
			continue;

		if (maxdiff < DIFF_NORMAL) break;

		if (bricks_solver_recurse(state, maxdiff, temp))
			continue;

		break;
	}

	if (temp && !hastemp)
		sfree(temp);
	return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
	const char *aux, const char **error)
{
	int w = state->w, h = state->h, s = w * h;
	game_state *solved = dup_game(state);
	char *ret = NULL;
	cell n;
	int result;

	bricks_solve_game(solved, DIFF_TRICKY, NULL, true, true);

	result = bricks_validate(w, h, solved->grid, false);

	if (result != STATUS_INVALID) {
		char *p;
		int i;

		ret = snewn(s + 2, char);
		p = ret;
		*p++ = 'S';

		for (i = 0; i < s; i++) {
			n = solved->grid[i] & COL_MASK;
			*p++ = (n == F_SHADE ? '1' : n == F_UNSHADE ? '0' : '-');
		}

		*p++ = '\0';
	}
	else
		*error = "Puzzle is invalid.";

	free_game(solved);
	return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
	return true;
}

static char *game_text_format(const game_state *state)
{
	int sw = state->w;
	int h = state->h;
	int w = state->pw + 1;
	int x, y;
	cell n;

	char *ret = snewn((w*2)*h+1, char);
	for(x = 0; x < (w*2)*h; x++)
		ret[x] = ' ';
	for(y = 1; y <= h; y++)
		ret[(w*2*y)-1] = '\n';
	char *p = ret;

	for(y = 0; y < h; y++) {
		p = &ret[w*2*y];
		if(y & 1) p++;

		for(x = 0; x < sw; x++) {

			n = state->grid[y*sw+x] & ~ERROR_MASK;

			if(n == F_BOUND) continue;

			if(n == F_SHADE) { *p++ = '#'; }
			else if(n == F_UNSHADE) { *p++ = '-'; }
			else if(n == F_EMPTY) { *p++ = '.'; }
			else if(n == 7) { *p++ = '?'; }
			else if(n >= 0 && n <= 6) { *p++ = '0' + n; }
			else p++;

			p++;
		}
	}

	ret[(w*2)*h] = '\0';

	return ret;
}

/* **************** *
 * Puzzle Generator *
 * **************** */

static void bricks_fill_grid(game_state *state, random_state *rs)
{
	int w = state->w, h = state->h;
	int x, y;
	int run;

	for (y = h-1; y >= 0; y--) {
		run = 0;
		for (x = 0; x < w; x++) {
			int i1 = y * w + x;

			if (state->grid[i1] & F_BOUND)
				continue;

			int i2 = (y+1) * w + x-1;
			int i3 = (y+1) * w + x;

			cell n2 = (x == 0 || y == h-1) ? F_BOUND : state->grid[i2];
			if(!(n2 & F_BOUND))
				n2 &= COL_MASK;
			cell n3 = (y == h-1) ? F_BOUND : state->grid[i3];
			if(!(n3 & F_BOUND))
				n3 &= COL_MASK;

			if(run == 2 || (y != h-1 && n2 != F_SHADE && n3 != F_SHADE) || random_upto(rs, 3) == 0) {
				state->grid[i1] = F_UNSHADE;
				run = 0;
			}
			else {
				state->grid[i1] = F_SHADE;
				run++;
			}
		}
	}
}

static int bricks_build_numbers(game_state *state)
{
	int total = 0;
	int w = state->w, h = state->h;
	int x, y, x2, y2, s;
	int shade;
	cell n, n2;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			n = state->grid[y*w+x];
			if(n == F_SHADE) total++;

			if(n == F_SHADE || n & F_BOUND) continue;

			shade = 0;

			for(s = 0; s < 6; s++) {
				x2 = x + bricks_steps[s].dx;
				y2 = y + bricks_steps[s].dy;

				if(x2 < 0 || x2 >= w || y2 < 0 || y2 >= h)
					continue;

				n2 = state->grid[y2*w+x2];
				if((n2 & COL_MASK) == F_SHADE)
					shade++;
			}

			state->grid[y*w+x] = shade;
		}
	}

	return total;
}

static char bricks_remove_numbers(game_state *state, int maxdiff, cell *tempgrid, random_state *rs)
{
	int w = state->w, h = state->h;
	int *spaces = snewn(w*h, int);
	int i1, j;
	cell temp;

	for (j = 0; j < w*h; j++)
		spaces[j] = j;

	shuffle(spaces, w*h, sizeof(*spaces), rs);
	for(j = 0; j < w*h; j++)
	{
		i1 = spaces[j];
		temp = state->grid[i1];
		if (temp & F_BOUND) continue;
		state->grid[i1] = F_EMPTY;

		if (bricks_solve_game(state, maxdiff, tempgrid, true, true) != STATUS_COMPLETE)
		{
			state->grid[i1] = temp;
		}
	}

	sfree(spaces);

	return true;
}

#define MINIMUM_SHADED 0.4
static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
	int w, h;
	int spaces = params->w*params->h, total;
	game_state *state = snew(game_state);
	bricks_grid_size(params, &w, &h);
	state->w = w;
	state->h = h;
	state->cheated = state->completed = false;

	int i;
	cell n;
	state->grid = snewn(w*h, cell);
	cell *tempgrid = snewn(w*h, cell);

	while(true)
	{
		bricks_apply_bounds(w, h, state->grid);

		bricks_fill_grid(state, rs);
		bricks_build_numbers(state);

		/* Find ambiguous areas by solving the game, then filling in all unknown squares with a number */
		bricks_solve_game(state, DIFF_EASY, tempgrid, true, false);
		total = bricks_build_numbers(state);

		/* Enforce minimum percentage of shaded squares */
		if((total * 1.0f) / spaces < MINIMUM_SHADED)
			continue;

		bricks_remove_numbers(state, params->diff, tempgrid, rs);

		/* Enforce minimum difficulty */
		if(params->diff > DIFF_EASY && spaces > 6 && bricks_solve_game(state, DIFF_EASY, tempgrid, true, true) == STATUS_COMPLETE)
			continue;

		break;
	}

	char *ret = snewn(w*h*4, char);
	char *p = ret;
	int run = 0;
	enum { RUN_NONE, RUN_BLANK, RUN_NUMBER } runtype = RUN_NONE;
	for(i = 0; i <= w*h; i++)
	{
		n = (i == w*h) ? 0 : state->grid[i];

		if(runtype == RUN_BLANK && (i == w*h || !(n & COL_MASK)))
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

		if(i == w*h)
			break;

		if(n >= 0 && n <= 7)
		{
			if(runtype == RUN_NUMBER)
				*p++ = '_';
			p += sprintf(p, "%d", n);
			runtype = RUN_NUMBER;
		}
		else if(n & COL_MASK)
		{
			runtype = RUN_BLANK;
			run++;
		}
	}
	*p++ = '\0';
	ret = sresize(ret, p - ret, char);
	free_game(state);
	sfree(tempgrid);
	return ret;
}

/* ************** *
 * User Interface *
 * ************** */

#define TARGET_SHOW 0x1
#define TARGET_CONNECTED 0x2

struct game_ui
{
	/* Current state of keyboard cursor */
	bool cshow;
	int cx, cy;

	cell dragtype;
	int *drag;
	int ndrags;
};

static game_ui *new_ui(const game_state *state)
{
	int i, w = state->w, s = w*state->h;
	game_ui *ret = snew(game_ui);
	
	ret->cshow = false;

	for (i = 0; i < s; i++)
	{
		if (state->grid[i] != F_BOUND) break;
	}
	ret->cx = i%w;
	ret->cy = i/w;

	ret->ndrags = 0;
	ret->dragtype = 0;
	ret->drag = snewn(s, int);
	
	return ret;
}

static void free_ui(game_ui *ui)
{
	sfree(ui->drag);
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
}

struct game_drawstate {
	int tilesize, w, h;
	double thickness;
	int offsetx, offsety;

	cell *oldgrid;
	cell *grid;
	int prevdrags;
};

#define DRAG_RADIUS 0.6F

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int ox, int oy, int button)
{
	int w = state->w, h = state->h;
	int dx = 0, dy = 0;
	int gx, gy;
	int tilesize = ds->tilesize;
	int hx = ui->cx;
	int hy = ui->cy;
	int shift = button & MOD_SHFT, control = button & MOD_CTRL;
	button &= ~(MOD_SHFT|MOD_CTRL);
	
	/* Parse keyboard cursor movement */
	if(button == (MOD_NUM_KEYPAD | '8')) button = CURSOR_UP;
	if(button == (MOD_NUM_KEYPAD | '2')) button = CURSOR_DOWN;
	if(button == (MOD_NUM_KEYPAD | '4')) button = CURSOR_LEFT;
	if(button == (MOD_NUM_KEYPAD | '6')) button = CURSOR_RIGHT;

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

	/* Apply keyboard cursor movement */
	if      (button == CURSOR_UP)              { dx =  0; dy = -1; }
	else if (button == CURSOR_DOWN)            { dx =  0; dy =  1; }
	else if (button == CURSOR_LEFT)            { dx = -1; dy =  0; }
	else if (button == CURSOR_RIGHT)           { dx =  1; dy =  0; }
	else if (button == (MOD_NUM_KEYPAD | '7')) { dx = -1; dy = -1; }
	else if (button == (MOD_NUM_KEYPAD | '1')) { dx = -1; dy =  1; }
	else if (button == (MOD_NUM_KEYPAD | '9')) { dx =  1; dy = -1; }
	else if (button == (MOD_NUM_KEYPAD | '3')) { dx =  1; dy =  1; }

	if (dx || dy)
	{
		ui->cshow = true;
		ui->cx += dx;
		ui->cy += dy;

		ui->cx = max(0, min(ui->cx, w-1));
		ui->cy = max(0, min(ui->cy, h-1));
		
		int extra = (h | ui->cy) & 1 ? 0 : 1;
		ui->cx = min(ui->cx, w - (ui->cy / 2) - 1);
		ui->cx = max(ui->cx, ((h - ui->cy) / 2) - extra);

		if (shift | control)
		{
			int i1, i2;
			cell n1, n2;
			char c = shift && control ? 'C' : control ? 'A' : 'B';
			char buf[16];
			char *p = buf;

			buf[0] = '\0';
			
			i1 = hy*w + hx;
			i2 = ui->cy*w + ui->cx;
			n1 = state->grid[i1] & NUM_MASK;
			n2 = state->grid[i2] & NUM_MASK;
			if (!((c == 'A' && n1 == F_SHADE) ||
					(c == 'B' && n1 == F_UNSHADE) ||
					(c == 'C' && n1 == F_EMPTY)
				))
				p += sprintf(p, "%c%d;", c, i1);

			if (!(i1 == i2
					|| (c == 'A' && n2 == F_SHADE)
					|| (c == 'B' && n2 == F_UNSHADE)
					|| (c == 'C' && n2 == F_EMPTY)
					))
				p += sprintf(p, "%c%d;", c, i2);

			if(buf[0])
				return dupstr(buf);
		}
		return MOVE_UI_UPDATE;
	}

	oy -= ds->offsety;
	ox -= ds->offsetx;
	gy = oy < 0 ? -1 : oy / tilesize;
	ox -= gy * tilesize / 2;
	gx = ox < 0 ? -1 : ox / tilesize;
	if (IS_MOUSE_DOWN(button))
	{
		ui->dragtype = 0;
		ui->ndrags = 0;
	}

	if (IS_MOUSE_DOWN(button) || IS_MOUSE_DRAG(button))
	{
		if (gx >= 0 && gx < w && gy >= 0 && gy < h) {
			hx = gx;
			hy = gy;
			ui->cshow = false;
		}
		else
			return MOVE_NO_EFFECT;
	}

	if (IS_MOUSE_DOWN(button))
	{
		int i = hy * w + hx;
		cell old = state->grid[i];
		old &= COL_MASK;

		if (button == LEFT_BUTTON)
			ui->dragtype = (old == F_UNSHADE ? F_EMPTY : old == F_SHADE ? F_UNSHADE : F_SHADE);
		else if (button == RIGHT_BUTTON)
			ui->dragtype = (old == F_UNSHADE ? F_SHADE : old == F_SHADE ? F_EMPTY : F_UNSHADE);
		else
			ui->dragtype = F_EMPTY;

		ui->ndrags = 0;
		if (ui->dragtype || old)
			ui->drag[ui->ndrags++] = i;

		return MOVE_UI_UPDATE;
	}

	if (IS_MOUSE_DRAG(button) && ui->dragtype)
	{
		int i = hy * w + hx;
		int d;

		if (state->grid[i] == ui->dragtype)
			return MOVE_NO_EFFECT;

		for (d = 0; d < ui->ndrags; d++)
		{
			if (i == ui->drag[d])
				return MOVE_NO_EFFECT;
		}

		ui->drag[ui->ndrags++] = i;

		return MOVE_UI_UPDATE;
	}

	if (IS_MOUSE_RELEASE(button) && ui->ndrags)
	{
		int i, j;
		char *buf = snewn(ui->ndrags * 7, char);
		char *p = buf;
		char c = ui->dragtype == F_SHADE ? 'A' : ui->dragtype == F_UNSHADE ? 'B' : 'C';

		for (i = 0; i < ui->ndrags; i++)
		{
			j = ui->drag[i];
			if (!(state->grid[j] & COL_MASK)) continue;
			p += sprintf(p, "%c%d;", c, j);
		}
		*p++ = '\0';

		buf = sresize(buf, p - buf, char);
		ui->ndrags = 0;

		if (buf[0])
			return buf;
		
		sfree(buf);
		return MOVE_UI_UPDATE;
	}

	/* Place one */
	if (ui->cshow && (button == CURSOR_SELECT || button == CURSOR_SELECT2
		|| button == '\b' || button == '0' || button == '1'
		|| button == '2')) {
		char buf[80];
		char c;
		int i = ui->cy * w + ui->cx;
		cell old = state->grid[i] & COL_MASK;

		if (!old)
			return MOVE_NO_EFFECT;

		c = 'C';

		if (button == '0' || button == '2')
			c = 'B';
		else if (button == '1')
			c = 'A';

		/* Cycle through options */
		else if (button == CURSOR_SELECT)
			c = (old == F_EMPTY ? 'A' : old == F_SHADE ? 'B' : 'C');
		else if (button == CURSOR_SELECT2)
			c = (old == F_EMPTY ? 'B' : old == F_UNSHADE ? 'A' : 'C');

		if ((old == F_SHADE && c == 'A') ||
			(old == F_UNSHADE && c == 'B') ||
			(old == F_EMPTY && c == 'C'))
			return MOVE_NO_EFFECT;               /* don't put no-ops on the undo chain */

		sprintf(buf, "%c%d;", c, i);

		return dupstr(buf);
	}

	return MOVE_UNUSED;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	int w = state->w, h = state->h;
	int s = w * h;
	int i;
	char c;

	game_state *ret = dup_game(state);
	const char *p = move;

	while (*p)
	{
		if (*p == 'S')
		{
			for (i = 0; i < s; i++)
			{
				p++;

				if (!*p || !(*p == '1' || *p == '0' || *p == '-')) {
					free_game(ret);
					return NULL;
				}

				if (!(state->grid[i] & COL_MASK))
					continue;

				if (*p == '0')
					ret->grid[i] = F_UNSHADE;
				else if (*p == '1')
					ret->grid[i] = F_SHADE;
				else
					ret->grid[i] = F_EMPTY;
			}

			ret->cheated = true;
		}
		else if (sscanf(p, "%c%d", &c, &i) == 2 && i >= 0
			&& i < w*h && (c == 'A' || c == 'B'
				|| c == 'C'))
		{
			if (state->grid[i] & COL_MASK)
				ret->grid[i] = (c == 'A' ? F_SHADE : c == 'B' ? F_UNSHADE : F_EMPTY);
		}
		else
		{
			free_game(ret);
			return NULL;
		}

		while (*p && *p != ';')
			p++;
		if (*p == ';')
			p++;
	}

	if (bricks_validate(w, h, ret->grid, false) == STATUS_COMPLETE) ret->completed = true;
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

		*x += cx * ds->tilesize / 2;
		*w = *h = ds->tilesize;
	}
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	tilesize &= ~1;
	*x = (params->w+1) * tilesize;
	*y = (params->h+1) * tilesize;

	*x += (tilesize / 2);
}

static void game_set_offsets(int h, int tilesize, int *offsetx, int *offsety)
{
	*offsetx = tilesize / 2;
	*offsety = tilesize / 2;
	*offsetx -= ((h / 2) - 1) * tilesize;
	if (h & 1)
		*offsetx -= tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
	tilesize &= ~1;
	ds->tilesize = tilesize;
	ds->thickness = max(2.0L, tilesize / 7.0L);
	game_compute_size(params, tilesize, NULL, &ds->w, &ds->h);

	game_set_offsets(params->h, tilesize, &ds->offsetx, &ds->offsety);
}

static float *game_colours(frontend *fe, int *ncolours)
{
	float *ret = snewn(3 * NCOLOURS, float);

	game_mkhighlight(fe, ret, COL_MIDLIGHT, COL_HIGHLIGHT, COL_LOWLIGHT);

	ret[COL_BORDER * 3 + 0] = 0.0F;
	ret[COL_BORDER * 3 + 1] = 0.0F;
	ret[COL_BORDER * 3 + 2] = 0.0F;

	ret[COL_SHADE * 3 + 0] = 0.1F;
	ret[COL_SHADE * 3 + 1] = 0.1F;
	ret[COL_SHADE * 3 + 2] = 0.1F;
	
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
	ds->oldgrid = snewn(s, cell);
	ds->grid = snewn(s, cell);
	ds->prevdrags = 0;

	memset(ds->oldgrid, ~0, s*sizeof(cell));
	memcpy(ds->grid, state->grid, s*sizeof(cell));

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->oldgrid);
	sfree(ds->grid);
	sfree(ds);
}

static void bricks_draw_err_rectangle(drawing *dr, int x, int y, int w, int h,
                                      int tilesize)
{
	double thick = tilesize / 10;
	double margin = tilesize / 20;

	draw_rect(dr, x+margin, y+margin, w-2*margin, thick, COL_ERROR);
	draw_rect(dr, x+margin, y+margin, thick, h-2*margin, COL_ERROR);
	draw_rect(dr, x+margin, y+h-margin-thick, w-2*margin, thick, COL_ERROR);
	draw_rect(dr, x+w-margin-thick, y+margin, thick, h-2*margin, COL_ERROR);
}

/* Copied from tents.c */
static void bricks_draw_err_gravity(drawing *dr, int tilesize, int x, int y)
{
	int coords[8];
	int yext, xext;

	/*
	* Draw a diamond.
	*/
	coords[0] = x - tilesize*2/5;
	coords[1] = y;
	coords[2] = x;
	coords[3] = y - tilesize*2/5;
	coords[4] = x + tilesize*2/5;
	coords[5] = y;
	coords[6] = x;
	coords[7] = y + tilesize*2/5;
	draw_polygon(dr, coords, 4, COL_ERROR, COL_BORDER);

	/*
	* Draw an exclamation mark in the diamond. This turns out to
	* look unpleasantly off-centre if done via draw_text, so I do
	* it by hand on the basis that exclamation marks aren't that
	* difficult to draw...
	*/
	xext = tilesize/16;
	yext = tilesize*2/5 - (xext*2+2);
	draw_rect(dr, x-xext, y-yext, xext*2+1, yext*2+1 - (xext*3), COL_HIGHLIGHT);
	draw_rect(dr, x-xext, y+yext-xext*2+1, xext*2+1, xext*2, COL_HIGHLIGHT);
}

#define FLASH_FRAME 0.12F
#define FLASH_TIME (FLASH_FRAME * 5)
static void game_redraw(drawing *dr, game_drawstate *ds,
						const game_state *oldstate, const game_state *state,
						int dir, const game_ui *ui,
						float animtime, float flashtime)
{
	int w = state->w;
	int h = state->h;
	int tilesize = ds->tilesize;
	int tx, ty, tx1, ty1, clipw;
	int i;
	cell n;
	char buf[20];
	int colour;

	bool flash = false;
	
	if(flashtime > 0)
		flash = (int)(flashtime/FLASH_FRAME) & 1;
	else if(ds->prevdrags >= ui->ndrags)
		memcpy(ds->grid, state->grid, w*h*sizeof(cell));

	for(i = ds->prevdrags; i < ui->ndrags; i++)
	{
		if(ds->grid[ui->drag[i]] & COL_MASK)
			ds->grid[ui->drag[i]] = ui->dragtype;
	}

	ds->prevdrags = ui->ndrags;
	if(ds->prevdrags > 0)
		bricks_validate(w, h, ds->grid, false);
	
	/* Draw squares */
	for(i = 0; i < w*h; i++)
	{
		n = ds->grid[i];
		if(n & F_BOUND) continue;

		tx = (i%w) * tilesize + ds->offsetx;
		ty = (i/w) * tilesize + ds->offsety;

		tx += (i/w) * tilesize / 2;
		
		if(flash && (n & COL_MASK) == F_SHADE)
			n = F_EMPTY;
		
		if(ui->cshow && ui->cx == i%w && ui->cy == i/w)
			n |= FE_CURSOR;

		if(ds->oldgrid[i] == n) continue;

		colour = n & F_BOUND ? COL_MIDLIGHT : 
			(n & COL_MASK) == F_SHADE ? COL_SHADE :
			(n & COL_MASK) == F_UNSHADE || !(n & COL_MASK) ? COL_HIGHLIGHT :
			COL_MIDLIGHT;
		
		tx1 = tx;
		clipw = tilesize+1;

		/* Expand clip size near horizontal edges to draw error diamonds */
		if(i%w == 0 || ds->grid[i-1] & F_BOUND) {
			tx1 -= tilesize;
			clipw += tilesize;
			draw_update(dr, tx1+1, ty, tilesize+1, tilesize+1);
			draw_rect(dr, tx1+1, ty+1, tilesize-1, tilesize-1, COL_MIDLIGHT);
		}

		if(i%w == w-1 || ds->grid[i+1] & F_BOUND) {
			clipw += tilesize;
			draw_update(dr, tx+tilesize+1, ty, tilesize+1, tilesize+1);
			draw_rect(dr, tx+tilesize+1, ty+1, tilesize-1, tilesize-1, COL_MIDLIGHT);
		}
		
		/* Draw tile background */
		clip(dr, tx1, ty, clipw, tilesize+1);
		draw_update(dr, tx, ty, tilesize+1, tilesize+1);
		draw_rect(dr, tx+1, ty+1, tilesize-1, tilesize-1, colour);
		ds->oldgrid[i] = n;
		
		/* Draw square border */
		if(!(n & F_BOUND)) {
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
		
		/* Draw 3-in-a-row errors */
		if (n & (FE_LINE_LEFT | FE_LINE_RIGHT)) {
			int left = tx + 1, right = tx + tilesize - 1;
			if (n & FE_LINE_LEFT)
				right += tilesize/2;
			if (n & FE_LINE_RIGHT)
				left -= tilesize/2;
			bricks_draw_err_rectangle(dr, left, ty+1, right-left, tilesize-1, tilesize);
		}

		tx1 = tx + (tilesize/2), ty1 = ty + (tilesize/2);

		/* Draw the number */
		if(!(n & (COL_MASK|F_BOUND)))
		{
			if((n & NUM_MASK) == 7)
				sprintf(buf, "?");
			else
				sprintf(buf, "%d", n & NUM_MASK);
			
			draw_text(dr, tx1, ty1,
					FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
					n & FE_ERROR ? COL_ERROR : COL_BORDER, buf);
		} else if(n & FE_ERROR) {
			bricks_draw_err_gravity(dr, tilesize, tx1, ty + tilesize);
		}

		if(n & FE_TOPLEFT) {
			bricks_draw_err_gravity(dr, tilesize, tx, ty);
		}
		if(n & FE_TOPRIGHT) {
			bricks_draw_err_gravity(dr, tilesize, tx + tilesize, ty);
		}
		
		if(n & FE_CURSOR) {
			draw_rect_corners(dr, tx1, ty1, tilesize / 3, COL_CURSOR);
		}

		unclip(dr);
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
	int tx, ty, tx1, ty1;
	int i;
	cell n;
	char buf[20];
	int ink = print_mono_colour(dr, 0);
	int offsetx, offsety;
	game_set_offsets(h, tilesize, &offsetx, &offsety);

	/* Draw squares */
	for(i = 0; i < w*h; i++)
	{
		n = state->grid[i];
		if(n & F_BOUND) continue;

		tx = (i%w) * tilesize + offsetx;
		ty = (i/w) * tilesize + offsety;

		tx += (i/w) * tilesize / 2;
		
		/* Draw tile background */
		if((n & COL_MASK) == F_SHADE) {
			draw_rect(dr, tx+1, ty+1, tilesize-1, tilesize-1, ink);
		} else if(!(n & F_BOUND)) {
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
			draw_polygon(dr, sqc, 4, -1, ink);
		}
		
		tx1 = tx + (tilesize/2), ty1 = ty + (tilesize/2);

		/* Draw the number */
		if(!(n & (COL_MASK|F_BOUND)))
		{
			if((n & NUM_MASK) == 7)
				sprintf(buf, "?");
			else
				sprintf(buf, "%d", n & NUM_MASK);
			
			draw_text(dr, tx1, ty1,
					FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
					ink, buf);
		}
	}
}

#ifdef COMBINED
#define thegame bricks
#endif

const struct game thegame = {
	"Bricks", NULL, NULL,
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
    NULL, NULL, /* get_prefs, set_prefs */
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
		} else if (*p == '-')
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

		char *fmt = game_text_format(new_game(NULL, params, desc_gen));
		fputs(fmt, stdout);
		sfree(fmt);

		printf("Game ID: %s\n", desc_gen);
	} else {
		game_state *input;
		
		err = validate_desc(params, desc);
		if (err) {
			fprintf(stderr, "Description is invalid\n");
			fprintf(stderr, "%s", err);
			exit(1);
		}
		
		input = new_game(NULL, params, desc);

		bricks_solve_game(input, DIFF_TRICKY, NULL, true, false);

		char *fmt = game_text_format(input);
		fputs(fmt, stdout);
		sfree(fmt);
		
		free_game(input);
	}

	return 0;
}
#endif
