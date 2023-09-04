/*
 * rome.c: Implementation of Nikoli's Rome puzzles.
 * (C) 2014 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective of the game: Fill every square with an arrow pointing
 * up, down, left or right, with the following rules:
 * 1. Every outlined area contains different arrows.
 * 2. Following the arrows must lead to one of the circled goals.
 *
 * Click and hold a square, then drag in one of the four directions to
 * place an arrow. Right-click and drag to place a pencil mark.
 * The keyboard can also be used. Move the cursor with the arrow keys,
 * and press Enter followed with an arrow key to place an arrow. Use spacebar
 * to add pencil marks. Alternatively, use the arrows on the numpad to enter
 * arrows directly.
 */

/*
 * In this implementation, rule 2 is rewritten as follows:
 * 2a. No arrow can point outside the grid.
 * 2b. Arrows must not form a loop.
 *
 * Arrows are connected using a disjoint-set forest. 
 * When an arrow points to a square, the two squares are merged. Each
 * set should contain one empty square or a goal. If a set contains only
 * arrows, then it has violated one of the rewritten rules stated above.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

enum {
	COL_BACKGROUND,
	COL_HIGHLIGHT,
	COL_LOWLIGHT,
	COL_BORDER,
	COL_ARROW_FIXED,
	COL_ARROW_GUESS,
	COL_ARROW_ERROR,
	COL_ARROW_PENCIL,
	COL_ARROW_ENTRY,
	COL_ERRORBG,
	COL_GOALBG,
	COL_GOAL,
	NCOLOURS
};

#define DIFFLIST(A)                             \
	A(EASY,Easy, e)                             \
	A(NORMAL,Normal, n)                         \
	A(TRICKY,Tricky, t)                         \

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const rome_diffnames[] = { DIFFLIST(TITLE) };

static char const rome_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

struct game_params {
	int w;
	int h;
	int diff;
};

#define EMPTY 0

#define FM_FIXED     0x0001
#define FM_GOAL      0x0002
#define FM_UP        0x0004
#define FM_DOWN      0x0008
#define FM_LEFT      0x0010
#define FM_RIGHT     0x0020

#define FE_BOUNDS    0x0040 /* Points outside the grid */
#define FE_DOUBLE    0x0080 /* Duplicate arrow in an outlined area */
#define FE_LOOP      0x0100 /* Is part of a loop */
/* Used to stop the error highlighting from spinning indefinitely */
#define FE_LOOPSTART 0x0200

#define FD_CURSOR    0x0400 /* Cursor moving */
#define FD_PLACE     0x0800 /* Cursor before placing arrow */
#define FD_PENCIL    0x1000 /* Cursor before placing mark */
#define FD_TOGOAL    0x2000 /* Leads to a goal */
#define FD_ENTRY     0x4000 /* Holding down the mouse button */

#define FM_ARROWMASK (FM_UP|FM_DOWN|FM_LEFT|FM_RIGHT)
#define FE_MASK (FE_LOOP|FE_LOOPSTART|FE_BOUNDS|FE_DOUBLE)
#define FD_KBMASK (FD_CURSOR|FD_PLACE|FD_PENCIL)

typedef int cell;

struct game_state {
	int w, h;
	
	/* region layout */
	DSF *dsf;
	
	cell *grid;
	cell *marks;
	
	bool completed, cheated;
};

#define DEFAULT_PRESET 3

static const struct game_params rome_presets[] = {
	{ 4,  4, DIFF_EASY},
	{ 4,  4, DIFF_NORMAL},
	{ 4,  4, DIFF_TRICKY},
	{ 6,  6, DIFF_EASY},
	{ 6,  6, DIFF_NORMAL},
	{ 6,  6, DIFF_TRICKY},
	{ 8,  8, DIFF_EASY},
	{ 8,  8, DIFF_NORMAL},
	{ 8,  8, DIFF_TRICKY},
	{10, 10, DIFF_EASY},
	{10, 10, DIFF_NORMAL},
	{10, 10, DIFF_TRICKY},
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
	game_params *ret;
	char buf[64];
	
	if(i < 0 || i >= lenof(rome_presets))
		return false;
	
	ret = snew(game_params);
	*ret = rome_presets[i]; /* struct copy */
	*params = ret;
	
	sprintf(buf, "%dx%d %s", ret->w, ret->h, rome_diffnames[ret->diff]);
	*name = dupstr(buf);
	
	return true;
}

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);

	*ret = rome_presets[DEFAULT_PRESET]; /* struct copy */

	return ret;
}

static void free_params(game_params *params)
{
	sfree(params);
}

static game_params *dup_params(const game_params *params)
{
	game_params *ret = snew(game_params);
	*ret = *params;		       /* structure copy */
	return ret;
}

static void decode_params(game_params *params, char const *string)
{
	char const *p = string;

	params->w = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
	if (*p == 'x') {
		p++;
		params->h = atoi(p);
		while (*p && isdigit((unsigned char)*p)) p++;
	} else {
		params->h = params->w;
	}
	
	if (*p == 'd') {
		int i;
		p++;
		params->diff = DIFFCOUNT + 1;   /* ...which is invalid */
		if (*p) {
			for (i = 0; i < DIFFCOUNT; i++) {
				if (*p == rome_diffchars[i])
					params->diff = i;
			}
			p++;
		}
	}
}

static char *encode_params(const game_params *params, bool full)
{
	char buf[80];

	sprintf(buf, "%dx%d", params->w, params->h);
	if(full)
		sprintf(buf+strlen(buf), "d%c", rome_diffchars[params->diff]);

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
	ret[2].u.choices.choicenames = DIFFCONFIG;
	ret[2].u.choices.selected = params->diff;
	
	ret[3].name = NULL;
	ret[3].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = default_params();
	
	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->diff = cfg[2].u.choices.selected;
	
	return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
	if(params->w < 3)
		return "Width must be at least 3";
	if(params->h < 3)
		return "Height must be at least 3";
	if(params->diff >= DIFFCOUNT)
		return "Unknown difficulty level";
	
	return NULL;
}

static void free_game(game_state *state)
{
	dsf_free(state->dsf);
	sfree(state->grid);
	sfree(state->marks);
	sfree(state);
}

/* ******************** *
 * Validation and Tools *
 * ******************** */

enum { STATUS_COMPLETE, STATUS_INCOMPLETE, STATUS_INVALID };
enum { VALID, INVALID_WALLS, INVALID_CLUES, INVALID_REGIONS, INVALID_GOALS };

static char rome_validate_game(game_state *state, bool fullerrors, DSF *dsf, cell *sets)
{
	int w = state->w;
	int h = state->h;
	int x, y, i, c;
	bool hasdsf = dsf != NULL;
	bool hassets = sets != NULL;
	char ret = STATUS_COMPLETE;
	cell *seterrs;
	
	for(i = 0; i < w*h; i++)
		state->grid[i] &= ~(FE_MASK|FD_TOGOAL);
	
	if(!hasdsf)
		dsf = dsf_new_min(w*h);
	dsf_reinit(dsf);
	if(!hassets)
		sets = snewn(w*h, cell);
	seterrs = snewn(w*h, cell);
	
	memset(sets, EMPTY, w*h*sizeof(cell));
	memset(seterrs, EMPTY, w*h*sizeof(cell));
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		/*
		 * Merge all arrows pointing towards the same square, and mark
		 * any arrows pointing off the grid. If an arrow attempts to merge
		 * with another arrow pointing back to itself, mark it as the start
		 * of a loop.
		 */
		i = y*w+x;
		if(state->grid[i] & FM_UP)
		{
			if(y == 0)
				state->grid[i] |= FE_BOUNDS;
			else if(dsf_canonify(dsf, i) == dsf_canonify(dsf, i-w))
				state->grid[i] |= FE_LOOPSTART;
			else
				dsf_merge(dsf, i, i-w);
		}
		if(state->grid[i] & FM_DOWN)
		{
			if(y == h-1)
				state->grid[i] |= FE_BOUNDS;
			else if(dsf_canonify(dsf, i) == dsf_canonify(dsf, i+w))
				state->grid[i] |= FE_LOOPSTART;
			else
				dsf_merge(dsf, i, i+w);
		}
		if(state->grid[i] & FM_LEFT)
		{
			if(x == 0)
				state->grid[i] |= FE_BOUNDS;
			else if(dsf_canonify(dsf, i) == dsf_canonify(dsf, i-1))
				state->grid[i] |= FE_LOOPSTART;
			else
				dsf_merge(dsf, i, i-1);
		}
		if(state->grid[i] & FM_RIGHT)
		{
			if(x == w-1)
				state->grid[i] |= FE_BOUNDS;
			else if(dsf_canonify(dsf, i) == dsf_canonify(dsf, i+1))
				state->grid[i] |= FE_LOOPSTART;
			else
				dsf_merge(dsf, i, i+1);
		}
	}
	
	/* Mark every arrow in a loop */
	if(fullerrors)
	{
		for(i = 0; i < w*h; i++)
		{
			if(state->grid[i] & FE_LOOPSTART)
			{
				x = i%w;
				y = i/w;
				do
				{
					state->grid[y*w+x] |= FE_LOOP;
					if(state->grid[y*w+x] & FM_UP)
						y--;
					else if(state->grid[y*w+x] & FM_DOWN)
						y++;
					else if(state->grid[y*w+x] & FM_LEFT)
						x--;
					else if(state->grid[y*w+x] & FM_RIGHT)
						x++;			
				}
				while(!(state->grid[y*w+x] & FE_LOOPSTART));
			}
		}
	}
	
	/* Mark areas with duplicate arrows */
	for(i = 0; i < w*h; i++)
	{
		if(state->grid[i] == EMPTY)
			continue;
		c = dsf_canonify(state->dsf, i);
		if((state->grid[i] & FM_ARROWMASK) & sets[c])
			seterrs[c] |= state->grid[i] & FM_ARROWMASK;
		else
			sets[c] |= state->grid[i] & FM_ARROWMASK;
	}
	/* Mark duplicate arrows */
	for(i = 0; i < w*h; i++)
	{
		c = dsf_canonify(state->dsf, i);
		if(state->grid[i] & FM_ARROWMASK & seterrs[c])
			state->grid[i] |= FE_DOUBLE;
	}
	
	/* Mark arrows pointing at a goal */
	if(fullerrors)
	{
		for(i = 0; i < w*h; i++)
		{
			if(state->grid[i] & FM_GOAL)
			{
				c = dsf_minimal(dsf, i);
				for(x = c; x < w*h; x++)
				{
					if(c == dsf_minimal(dsf, x))
						state->grid[x] |= FD_TOGOAL;
				}
			}
		}
	}
	
	if(!hasdsf)
		dsf_free(dsf);
	if(!hassets)
		sfree(sets);
	sfree(seterrs);
	
	for(i = 0; i < w*h; i++)
	{
		if(state->grid[i] & FE_MASK)
			return STATUS_INVALID;
		if(state->grid[i] == EMPTY)
			ret = STATUS_INCOMPLETE;
	}
	return ret;
}

static int rome_read_desc(const game_params *params, const char *desc, game_state **retstate)
{
	int w = params->w;
	int h = params->h;
	int valid = VALID;
	int x, y, i, i1, i2, erun, wrun;
	game_state *state = snew(game_state);
	int hs = ((w-1)*h);
	int ws = hs + (w*(h-1));
	char *walls = snewn(ws, char);
	const char *p;
	
	memset(walls, false, ws*sizeof(char));
	
	state->w = w;
	state->h = h;
	state->dsf = dsf_new_min(w*h);
	
	state->completed = state->cheated = false;
	
	state->grid = snewn(w*h, cell);
	state->marks = snewn(w*h, cell);
	memset(state->grid, EMPTY, w*h*sizeof(cell));
	memset(state->marks, EMPTY, w*h*sizeof(cell));
	memset(walls, false, ws*sizeof(char));
	
	/* Read list of walls */
	p = desc;
	erun = wrun = 0;
	for(i = 0; i < ws; i++)
	{
		if(erun == 0 && wrun == 0)
		{
			if(isdigit((unsigned char)*p))
			{
				wrun = atoi(p);
				while (*p && isdigit((unsigned char)*p)) p++;
			}
			else if(*p >= 'a' && *p <= 'y')
			{
				erun = *p - 'a' + 1;
				wrun = 1;
				p++;
			}
			else if(*p == 'z')
			{
				erun = 'z' - 'a' + 1;
				p++;
			}
			else
				valid = INVALID_WALLS;
		}
		if(erun > 0)
		{
			walls[i] = false;
			erun--;
		}
		else if(erun == 0 && wrun > 0)
		{
			walls[i] = true;
			wrun--;
		}
	}
	
	/* Merge horizontally */
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		i = (y*(w-1))+x;
		i1 = y*w+x;
		i2 = y*w+x+1;
		if(!walls[i])
			dsf_merge(state->dsf, i1, i2);
	}
	
	/* Merge vertically */
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		i = hs + (y*w+x);
		i1 = y*w+x;
		i2 = (y+1)*w+x;
		if(!walls[i])
			dsf_merge(state->dsf, i1, i2);
	}
	
	p++;
	erun = 0;
	for(i = 0; i < w*h; i++)
	{
		char c = 0;
		if (erun == 0)
		{
			c = *p++;
			if (c >= 'a' && c <= 'z')
				erun = c - 'a' + 1;
		}

		if (erun > 0)
		{
			c = 'S';
			erun--;
		}
		
		switch(c)
		{
			case 'S':
				/* Empty */
				break;
			case 'U':
				state->grid[i] = FM_UP|FM_FIXED;
				break;
			case 'D':
				state->grid[i] = FM_DOWN|FM_FIXED;
				break;
			case 'L':
				state->grid[i] = FM_LEFT|FM_FIXED;
				break;
			case 'R':
				state->grid[i] = FM_RIGHT|FM_FIXED;
				break;
			case 'X':
				state->grid[i] = FM_GOAL|FM_FIXED;
				break;
			default:
				valid = INVALID_CLUES;
		}
	}
	
	sfree(walls);
	*retstate = state;
	return valid;
}

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
	game_state *state = NULL;
	rome_read_desc(params, desc, &state);
	
	assert(state);
	
	rome_validate_game(state, true, NULL, NULL);
	
	return state;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	int status = STATUS_INCOMPLETE, valid, i, s;
	s = params->w * params->h;
	game_state *state = NULL;
	
	valid = rome_read_desc(params, desc, &state);
	
	if(valid == VALID)
	{
		status = rome_validate_game(state, true, NULL, NULL);
		if(status != STATUS_INCOMPLETE)
		{
			free_game(state);
			return "Puzzle contains errors";
		}
		
		for(i = 0; i < s; i++)
		{
			int size = dsf_size(state->dsf, i);
			if(size > 4)
				valid = INVALID_REGIONS;
			if(state->grid[i] & FM_GOAL && size > 1)
				valid = INVALID_GOALS;
		}
		
		free_game(state);
	}
	
	if(valid == INVALID_WALLS)
		return "Region description contains invalid characters";
	if(valid == INVALID_CLUES)
		return "Clues contain invalid characters";
	if(valid == INVALID_REGIONS)
		return "A region is too large";
	if(valid == INVALID_GOALS)
		return "A goal is not placed in an area of 1 cell";
	
	return NULL;
}

static game_state *dup_game(const game_state *state)
{
	int w = state->w;
	int h = state->h;
	
	game_state *ret = snew(game_state);

	ret->w = w;
	ret->h = h;
	ret->dsf = dsf_new_min(w*h);
	ret->grid = snewn(w*h, cell);
	ret->marks = snewn(w*h, cell);
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;

	dsf_copy(ret->dsf, state->dsf);
	memcpy(ret->grid, state->grid, w*h*sizeof(cell));
	memcpy(ret->marks, state->marks, w*h*sizeof(cell));
	
	return ret;
}

/* ****** *
 * Solver *
 * ****** */
 
static int rome_solver_single(game_state *state)
{
	/* If a square has a single possibility, place it */
	int ret = 0;
	int i;
	int s = state->w * state->h;
	
	for(i = 0; i < s; i++)
	{
		if(state->grid[i] != EMPTY)
			continue;
		
		if(state->marks[i] == FM_UP)
		{
			state->grid[i] = FM_UP;
			ret++;
		}
		if(state->marks[i] == FM_DOWN)
		{
			state->grid[i] = FM_DOWN;
			ret++;
		}
		if(state->marks[i] == FM_LEFT)
		{
			state->grid[i] = FM_LEFT;
			ret++;
		}
		if(state->marks[i] == FM_RIGHT)
		{
			state->grid[i] = FM_RIGHT;
			ret++;
		}
	}
	
	return ret;
}

static int rome_solver_doubles(game_state *state, cell *sets)
{
	/* Look at the currently placed arrows in each region, 
	 * and rule these out as possibilities */
	int ret = 0;
	int i;
	int s = state->w * state->h;
	cell prev;
	
	for(i = 0; i < s; i++)
	{
		prev = state->marks[i];
		state->marks[i] &= ~(sets[dsf_canonify(state->dsf, i)]);
		
		if(prev != state->marks[i])
			ret++;
	}
	
	return ret;
}

static int rome_solver_loops(game_state *state, DSF *dsf)
{
	/* Find nearby squares that would form a loop */
	int w = state->w;
	int h = state->h;
	int x, y;
	int ret = 0;
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		if(state->marks[y*w+x] & FM_UP && 
			dsf_canonify(dsf, y*w+x) == dsf_canonify(dsf, (y-1)*w+x))
		{
			state->marks[y*w+x] &= ~FM_UP;
			ret++;
		}
		if(state->marks[y*w+x] & FM_DOWN && 
			dsf_canonify(dsf, y*w+x) == dsf_canonify(dsf, (y+1)*w+x))
		{
			state->marks[y*w+x] &= ~FM_DOWN;
			ret++;
		}
		if(state->marks[y*w+x] & FM_LEFT && 
			dsf_canonify(dsf, y*w+x) == dsf_canonify(dsf, y*w+x-1))
		{
			state->marks[y*w+x] &= ~FM_LEFT;
			ret++;
		}
		if(state->marks[y*w+x] & FM_RIGHT && 
			dsf_canonify(dsf, y*w+x) == dsf_canonify(dsf, y*w+x+1))
		{
			state->marks[y*w+x] &= ~FM_RIGHT;
			ret++;
		}
	}
	
	return ret;
}

static int rome_find4_position(game_state *state)
{
	/* In a region of 4 squares, find if a certain arrow can be placed 
		in only one square */
	int ret = 0;
	int i, c;
	cell prev;
	int s = state->w * state->h;
	cell *singles = snewn(s, cell);
	cell *doubles = snewn(s, cell);
	
	memset(singles, EMPTY, s*sizeof(cell));
	memset(doubles, EMPTY, s*sizeof(cell));
	
	for(i = 0; i < s; i++)
	{
		if(dsf_size(state->dsf, i) != 4)
			continue;
		
		c = dsf_canonify(state->dsf, i);
		doubles[c] |= state->marks[i] & singles[c];
		singles[c] |= state->marks[i];
	}
	
	for(i = 0; i < s; i++)
	{
		if(dsf_size(state->dsf, i) != 4)
			continue;
		
		c = dsf_canonify(state->dsf, i);
		prev = state->marks[i];
		if(state->marks[i] & (singles[c] ^ doubles[c]))
			state->marks[i] &= singles[c] ^ doubles[c];
		
		if(prev != state->marks[i])
			ret++;
	}
	
	sfree(singles);
	sfree(doubles);
	return ret;
}

static int rome_naked_pairs(game_state *state)
{
	/* In a region, find two squares with the same possibilities, which must be
		exactly two. Then rule out these possibilities in the other squares
		in this region */
	int ret = 0;
	int i, j, k, c;
	int poss;
	cell prev;
	int s = state->w * state->h;
	
	for(i = 0; i < s; i++)
	{
		if(dsf_size(state->dsf, i) < 3)
			continue;
		
		/* Get the number of possibilities */
		poss = ((state->marks[i] & FM_UP) / FM_UP) +
			((state->marks[i] & FM_DOWN) / FM_DOWN) +
			((state->marks[i] & FM_LEFT) / FM_LEFT) +
			((state->marks[i] & FM_RIGHT) / FM_RIGHT);
		
		if(poss == 2)
		{
			c = dsf_canonify(state->dsf, i);
			/* Find the second one */
			for(j = i+1; j < s; j++)
			{
				if(state->marks[j] != state->marks[i] || 
					c != dsf_canonify(state->dsf, j))
					continue;
				
				/* We found two squares. Now look for the other ones */
				for(k = c; k < s; k++)
				{
					if(k == i || k == j || c != dsf_canonify(state->dsf, k))
						continue;
					
					prev = state->marks[k];
					state->marks[k] &= ~(state->marks[i]);
					if(state->marks[k] != prev)
						ret++;
				}
			}
		}
	}
	
	return ret;
}

static int rome_solver_expand(game_state *state, DSF *dsf)
{
	/* Check if there is one single possibility to expand the area pointing
	   to a goal. */
	
	int i, c, x, y, i1, i2;
	int w = state->w;
	int h = state->h;
	cell dir = EMPTY;
	int idx = -1;
	
	for(i = 0; i < w*h; i++)
	{
		if(state->grid[i] & FM_GOAL)
		{
			c = dsf_canonify(dsf, i);
			
			for(y = 0; y < h; y++)
			for(x = 0; x < w; x++)
			{
				i1 = y*w+x;
				
				i2 = y*w+x+1;
				if(x < w-1 && dsf_canonify(dsf, i2) == c && state->marks[i1] & FM_RIGHT)
				{
					if(dir != EMPTY)
						return 0;
					
					dir = FM_RIGHT;
					idx = i1;
				}
				
				i2 = y*w+x-1;
				if(x > 0 && dsf_canonify(dsf, i2) == c && state->marks[i1] & FM_LEFT)
				{
					if(dir != EMPTY)
						return 0;
					
					dir = FM_LEFT;
					idx = i1;
				}
				
				i2 = (y+1)*w+x;
				if(y < h-1 && dsf_canonify(dsf, i2) == c && state->marks[i1] & FM_DOWN)
				{
					if(dir != EMPTY)
						return 0;
					
					dir = FM_DOWN;
					idx = i1;
				}
				
				i2 = (y-1)*w+x;
				if(y > 0 && dsf_canonify(dsf, i2) == c && state->marks[i1] & FM_UP)
				{
					if(dir != EMPTY)
						return 0;
					
					dir = FM_UP;
					idx = i1;
				}
			}
		}
	}
	
	if(dir != EMPTY)
	{
		state->marks[idx] = dir;
		return 1;
	}
	
	return 0;
}

static int rome_solver_opposites(game_state *state)
{
	/* A square with only up/down as possibilities can not be pointed at

	   by another up or down arrow in the same region. 
	   The same goes for left/right. */
	
	int ret = 0;
	int w = state->w;
	int h = state->h;
	int x, y, i1, i2, c;
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		
		if(state->marks[i1] == (FM_UP|FM_DOWN))
		{
			c = dsf_canonify(state->dsf, i1);
			i2 = (y-1)*w+x;
			if(state->marks[i2] & FM_DOWN && dsf_canonify(state->dsf, i2) == c)
			{
				state->marks[i2] &= ~FM_DOWN;
				ret++;
			}
			
			i2 = (y+1)*w+x;
			if(state->marks[i2] & FM_UP && dsf_canonify(state->dsf, i2) == c)
			{
				state->marks[i2] &= ~FM_UP;
				ret++;
			}
		}
		
		if(state->marks[i1] == (FM_LEFT|FM_RIGHT))
		{
			c = dsf_canonify(state->dsf, i1);
			i2 = y*w+x-1;
			if(state->marks[i2] & FM_RIGHT && dsf_canonify(state->dsf, i2) == c)
			{
				state->marks[i2] &= ~FM_RIGHT;
				ret++;
			}
			
			i2 = y*w+x+1;
			if(state->marks[i2] & FM_LEFT && dsf_canonify(state->dsf, i2) == c)
			{
				state->marks[i2] &= ~FM_LEFT;
				ret++;
			}
		}
	}
	
	return ret;
}

static char rome_solve(game_state *state, int maxdiff)
{
	int w = state->w;
	int h = state->h;
	int i;
	char status;
	
	DSF *dsf = dsf_new_min(w*h);
	cell *sets = snewn(w*h, cell);
	
	/* Initialize all marks */
	for(i = 0; i < w*h; i++)
	{
		if(state->grid[i] == EMPTY)
			state->marks[i] = FM_ARROWMASK;
		else
			state->marks[i] = state->grid[i] & FM_ARROWMASK;
	}
	
	/* Disable marks near borders */
	for(i = 0; i < w; i++)
	{
		state->marks[i] &= ~FM_UP;
		state->marks[(h-1)*w+i] &= ~FM_DOWN;
	}
	for(i = 0; i < h; i++)
	{
		state->marks[i*w] &= ~FM_LEFT;
		state->marks[i*w+(w-1)] &= ~FM_RIGHT;
	}
	
	while(true)
	{
		status = rome_validate_game(state, false, dsf, sets);
		if(status != STATUS_INCOMPLETE)
			break;
		
		if(rome_solver_single(state))
			continue;
		
		if(rome_solver_doubles(state, sets))
			continue;
		
		if(rome_solver_loops(state, dsf))
			continue;
		
		if(maxdiff < DIFF_NORMAL)
			break;
		
		if(rome_find4_position(state))
			continue;
		
		if(rome_naked_pairs(state))
			continue;
		
		if(rome_solver_expand(state, dsf))
			continue;
		
		if(maxdiff < DIFF_TRICKY)
			break;
		
		if(rome_solver_opposites(state))
			continue;
		
		break;
	}
	
	dsf_free(dsf);
	sfree(sets);
	
	return status;
}

static char *solve_game(const game_state *state, const game_state *currstate,
			const char *aux, const char **error)
{
	int i;
	int s = state->w * state->h;
	char *ret = snewn(s+2, char);
	char *p = ret;
	game_state *solved = dup_game(state);
	
	rome_solve(solved, DIFFCOUNT);
	
	*p++ = 'S';
	for(i = 0; i < s; i++)
	{
		if(solved->grid[i] & FM_UP)
			*p++ = 'U';
		else if(solved->grid[i] & FM_DOWN)
			*p++ = 'D';
		else if(solved->grid[i] & FM_LEFT)
			*p++ = 'L';
		else if(solved->grid[i] & FM_RIGHT)
			*p++ = 'R';
		else
			*p++ = '-';
	}
	*p++ = '\0';
	
	free_game(solved);
	return ret;
}

/* ********* *
 * Generator *
 * ********* */
 
static void rome_join_arrows(game_state *state, DSF *arrdsf, cell *suggest)
{
	/*
	 * This function detects clusters of identical arrows, and gives
	 * suggestions to the generator to avoid growing a cluster even larger.
	 */
	
	int x, y, i1, i2;
	int w = state->w;
	int h = state->h;
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		i1 = y*w+x;
		i2 = y*w+x+1;
		if((state->grid[i1] & FM_ARROWMASK) == (state->grid[i2] & FM_ARROWMASK))
			dsf_merge(arrdsf, i1, i2);
	}
	
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		i2 = (y+1)*w+x;
		if((state->grid[i1] & FM_ARROWMASK) == (state->grid[i2] & FM_ARROWMASK))
			dsf_merge(arrdsf, i1, i2);
	}
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		
		i2 = y*w+x+1;
		if(x < w-1 && dsf_size(arrdsf, i2) >= 3)
			suggest[i1] |= state->grid[i2];
		
		i2 = y*w+x-1;
		if(x > 0 && dsf_size(arrdsf, i2) >= 3)
			suggest[i1] |= state->grid[i2];
		
		i2 = (y+1)*w+x;
		if(y < h-1 && dsf_size(arrdsf, i2) >= 3)
			suggest[i1] |= state->grid[i2];
		
		i2 = (y-1)*w+x;
		if(y > 0 && dsf_size(arrdsf, i2) >= 3)
			suggest[i1] |= state->grid[i2];
	}
}

static bool rome_generate_arrows(game_state *state, random_state *rs)
{
	int w = state->w;
	int h = state->h;
	int i, j, k;
	
	int *spaces = snewn(w*h, int);
	DSF *arrdsf = dsf_new_min(w*h);
	cell *suggest = snewn(w*h, cell);
	
	cell *arrows = snewn(4, cell);
	arrows[0] = FM_UP; arrows[1] = FM_DOWN;
	arrows[2] = FM_LEFT; arrows[3] = FM_RIGHT;
	
	memset(suggest, EMPTY, w*h*sizeof(cell));
	
	for(i = 0; i < w*h; i++)
	{
		spaces[i] = i;
		state->marks[i] = FM_ARROWMASK;
	}
	
	shuffle(spaces, w*h, sizeof(*spaces), rs);
	
	for(j = 0; j < w*h; j++)
	{
		i = spaces[j];
		
		if(state->grid[i] != EMPTY)
			continue;
		
		/* If no arrows are available here, place a goal */
		if(state->marks[i] == EMPTY)
		{
			state->grid[i] = FM_GOAL;
			continue;
		}
		
		/* Detect if there are certain arrows we should avoid if possible */
		rome_join_arrows(state, arrdsf, suggest);
		
		/* Remove arrows only if there are other options */
		if(state->marks[i] & ~suggest[i])
			state->marks[i] &= ~suggest[i];
		
		shuffle(arrows, 4, sizeof(*arrows), rs);
		
		for(k = 0; k < 4; k++)
		{
			if(state->marks[i] & arrows[k])
			{
				state->grid[i] = arrows[k];
				break;
			}
		}
		
		rome_solve(state, DIFF_EASY);
	}
	
	sfree(spaces);
	dsf_free(arrdsf);
	sfree(suggest);
	sfree(arrows);
	
	j = 0;
	for(i = 0; i < w*h; i++)
	{
		if(state->grid[i] & FM_GOAL)
			j++;
	}
	
	/* Keep the amount of Goal squares to a minimum */
	if(j > max(1,(w*h)/25) || rome_validate_game(state, false, NULL, NULL) != STATUS_COMPLETE)
		return false;
	
	return true;
}

static bool rome_generate_regions(game_state *state, random_state *rs)
{
	/*
	 * From a grid filled with arrows in 1x1 regions, randomly pick
	 * region borders and remove them if possible.
	 */
	
	int w = state->w;
	int h = state->h;
	int x, y, i, i1, i2;
	int hs = ((w-1)*h);
	int ws = hs + (w*(h-1));
	
	cell *cells = snewn(w*h, cell);
	cell c, c1, c2;
	int *spaces = snewn(ws, int);
	
	/* Initialize horizontal mergers */
	i = 0;
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		spaces[i++] = y*w+x;
	}
	
	/* Initialize vertical mergers */
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		spaces[i++] = (w*h) + y*w+x;
	}
	
	/* Initialize region arrow array */
	for(i = 0; i < w*h; i++)
	{
		assert(i == dsf_canonify(state->dsf, i));
		cells[i] = state->grid[i];
	}
	
	shuffle(spaces, ws, sizeof(*spaces), rs);
	
	for(i = 0; i < ws; i++)
	{
		i1 = spaces[i] % (w*h);
		i2 = spaces[i] >= w*h ? i1 + w : i1 + 1;
		
		c1 = cells[dsf_canonify(state->dsf, i1)];
		c2 = cells[dsf_canonify(state->dsf, i2)];
		
		/* If these two regions have arrows in common, they cannot merge */
		if(c1 & c2)
			continue;
		
		c = c1 | c2;
		
		if(c & FM_GOAL)
			continue;
		
		dsf_merge(state->dsf, i1, i2);
		
		cells[dsf_canonify(state->dsf, i1)] |= c;
	}
	
	sfree(spaces);
	sfree(cells);
	
	return true;
}

static bool rome_generate_clues(game_state *state, random_state *rs, int diff)
{
	/* Remove clues from the grid if the puzzle is solvable without them. */
	
	int s = state->w * state->h;
	int i, j;
	char status;
	
	int *spaces = snewn(s, int);
	cell *grid = snewn(s, cell);
	cell c;
	
	for(i = 0; i < s; i++)
		spaces[i] = i;
	
	shuffle(spaces, s, sizeof(*spaces), rs);
	memcpy(grid, state->grid, s*sizeof(cell));
	
	for(j = 0; j < s; j++)
	{
		i = spaces[j];
		
		c = state->grid[i];
		if(c & FM_GOAL)
			continue;
		
		state->grid[i] = EMPTY;
		
		status = rome_solve(state, diff);
		memcpy(state->grid, grid, s*sizeof(cell));
		
		if(status == STATUS_COMPLETE)
		{
			state->grid[i] = EMPTY;
			grid[i] = EMPTY;
		}
	}
	
	sfree(spaces);
	sfree(grid);
	
	return true;
}

static bool rome_generate(game_state *state, random_state *rs, int diff)
{
	game_state *solved;
	bool ret = true;
	
	if(!rome_generate_arrows(state, rs))
		return false;
	
	if(!rome_generate_regions(state, rs))
		return false;
	
	if(!rome_generate_clues(state, rs, diff))
		return false;
	
	solved = dup_game(state);
	rome_solve(solved, diff);
	if(rome_validate_game(solved, false, NULL, NULL) != STATUS_COMPLETE)
		ret = false;
	free_game(solved);
	
	if(ret && diff > 0)
	{
		solved = dup_game(state);
		rome_solve(solved, diff-1);
		if(rome_validate_game(solved, false, NULL, NULL) == STATUS_COMPLETE)
			ret = false;
		free_game(solved);
	}
	
	return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	int w = params->w;
	int h = params->h;
	int x, y, i;
	int hs = ((w-1)*h);
	int ws = hs + (w*(h-1));
	int erun, wrun;
	cell c;
	
	game_state *state = snew(game_state);
	
	char *walls = snewn(ws, char);
	char *p, *ret;
	
	state->w = w;
	state->h = h;
	state->dsf = dsf_new_min(w*h);
	state->grid = snewn(w*h, cell);
	state->marks = snewn(w*h, cell);
	
	do
	{
		dsf_reinit(state->dsf);
		memset(state->grid, EMPTY, w*h*sizeof(cell));
		memset(state->marks, EMPTY, w*h*sizeof(cell));
	} while(!rome_generate(state, rs, params->diff));
	
	/* Generate wall list */
	i = 0;
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		if(dsf_canonify(state->dsf, y*w+x) != dsf_canonify(state->dsf, y*w+x+1))
			walls[i] = true;
		else
			walls[i] = false;
		i++;
	}
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		if(dsf_canonify(state->dsf, y*w+x) != dsf_canonify(state->dsf, (y+1)*w+x))
			walls[i] = true;
		else
			walls[i] = false;
		i++;
	}
	
	ret = snewn(ws + (w*h), char);
	p = ret;
	
	erun = wrun = 0;
	for(i = 0; i < ws; i++)
	{
		if(!walls[i] && wrun > 0)
		{
			p += sprintf(p, "%d", wrun);
			wrun = 0;
			erun = 0;
		}
		else if(walls[i] && erun > 0)
		{
			*p++ = ('a' + erun - 1);
			erun = 0;
			wrun = -1;
		}
		
		if(!walls[i])
			erun++;
		else
			wrun++;
	}
	if(wrun > 0)
		p += sprintf(p, "%d", wrun);
	if(erun > 0)
		*p++ = ('a' + erun - 1);
	
	*p++ = ',';
	
	erun = 0;
	for(i = 0; i < w*h; i++)
	{
		c = state->grid[i];
		if(erun > 0 && c != EMPTY)
		{
			*p++ = ('a' + erun - 1);
			erun = 0;
		}
		if(c & FM_UP)
			*p++ = 'U';
		if(c & FM_DOWN)
			*p++ = 'D';
		if(c & FM_LEFT)
			*p++ = 'L';
		if(c & FM_RIGHT)
			*p++ = 'R';
		if(c & FM_GOAL)
			*p++ = 'X';
		if(c == EMPTY)
			erun++;
	}
	if(erun > 0)
		*p++ = ('a' + erun - 1);
	
	*p++ = '\0';
	
	sfree(walls);
	free_game(state);
	
	return dupstr(ret);
}

/* ************** *
 * User interface *
 * ************** */
 
static bool game_can_format_as_text_now(const game_params *params)
{
	return true;
}

static char *game_text_format(const game_state *state)
{
	return NULL;
}

enum { KEYMODE_OFF, KEYMODE_MOVE, KEYMODE_PLACE, KEYMODE_PENCIL };
enum { MOUSEMODE_OFF, MOUSEMODE_PLACE, MOUSEMODE_PENCIL };

struct game_ui
{
	int hx, hy;
	char kmode;
	char mmode;
	cell mdir;
	
	bool sloops, sgoals;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ret = snew(game_ui);
	ret->hx = 0;
	ret->hy = 0;
	ret->kmode = KEYMODE_OFF;
	ret->mmode = MOUSEMODE_OFF;
	ret->mdir = EMPTY;
	
	/* 
	 * Enable or disable the coloring of squares which point in a loop (red),
	 * and squares that point towards a goal (blue).
	 */
	ret->sloops = false;
	ret->sgoals = true;
	
	return ret;
}

static void free_ui(game_ui *ui)
{
	sfree(ui);
}

static config_item *get_prefs(game_ui *ui)
{
	config_item *ret;

	ret = snewn(3, config_item);

	ret[0].name = "Highlight arrows pointing towards goal";
	ret[0].kw = "goal";
	ret[0].type = C_BOOLEAN;
	ret[0].u.boolean.bval = ui->sgoals;

	ret[1].name = "Highlight loops";
	ret[1].kw = "loop";
	ret[1].type = C_BOOLEAN;
	ret[1].u.boolean.bval = ui->sloops;

	ret[2].name = NULL;
	ret[2].type = C_END;

	return ret;
}

static void set_prefs(game_ui *ui, const config_item *cfg)
{
	ui->sgoals = cfg[0].u.boolean.bval;
	ui->sloops = cfg[1].u.boolean.bval;
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
	bool redraw;
	int tilesize;
	int oldflash;
	cell *oldgrid;
	cell *oldpencil;
};

#define FROMCOORD(x) ( ((x)-(ds->tilesize/2)) / ds->tilesize )

static char *interpret_move(const game_state *state, game_ui *ui, const game_drawstate *ds,
				int mx, int my, int button)
{
	int w = state->w;
	int h = state->h;
	int x = ui->hx;
	int y = ui->hy;
	char m;
	char buf[80];
	
	button &= ~MOD_MASK;
	
	if(ui->mmode == MOUSEMODE_OFF)
	{
		/* Keyboard move */
		if (IS_CURSOR_MOVE(button) && 
			(ui->kmode == KEYMODE_OFF || ui->kmode == KEYMODE_MOVE))
		{
			move_cursor(button, &ui->hx, &ui->hy, w, h, false, NULL);
			ui->kmode = KEYMODE_MOVE;
			return MOVE_UI_UPDATE;
		}
		
		if(button == CURSOR_SELECT && !(state->grid[y*w+x] & FM_FIXED))
		{
			ui->kmode = ui->kmode != KEYMODE_PLACE ? KEYMODE_PLACE : KEYMODE_MOVE;
			return MOVE_UI_UPDATE;
		}
		
		if(button == CURSOR_SELECT2 && state->grid[y*w+x] == EMPTY
			&& ui->kmode != KEYMODE_PLACE)
		{
			ui->kmode = ui->kmode != KEYMODE_PENCIL ? KEYMODE_PENCIL : KEYMODE_MOVE;
			return MOVE_UI_UPDATE;
		}
		
		if(button == CURSOR_SELECT2 && ui->kmode == KEYMODE_PLACE)
		{
			ui->kmode = KEYMODE_MOVE;
			if(state->grid[y*w+x] & FM_FIXED)
				return MOVE_UI_UPDATE;
			
			sprintf(buf, "R%d,%d,-", x, y);
			return dupstr(buf);
		}
		
		if((ui->kmode == KEYMODE_PLACE || ui->kmode == KEYMODE_PENCIL)
			&& IS_CURSOR_MOVE(button))
		{
			m = ui->kmode == KEYMODE_PLACE ? 'R' : 'P';
			
			ui->kmode = KEYMODE_MOVE;
			if(state->grid[y*w+x] & FM_FIXED)
				return MOVE_UI_UPDATE;
			if(state->grid[y*w+x] != EMPTY && m == 'P')
				return MOVE_UI_UPDATE;
			
			if(button == CURSOR_UP && !(state->grid[y*w+x] & FM_UP))
				sprintf(buf, "%c%d,%d,U", m, x, y);
			else if(button == CURSOR_DOWN && !(state->grid[y*w+x] & FM_DOWN))
				sprintf(buf, "%c%d,%d,D", m, x, y);
			else if(button == CURSOR_LEFT && !(state->grid[y*w+x] & FM_LEFT))
				sprintf(buf, "%c%d,%d,L", m, x, y);
			else if(button == CURSOR_RIGHT && !(state->grid[y*w+x] & FM_RIGHT))
				sprintf(buf, "%c%d,%d,R", m, x, y);
			else
				return MOVE_UI_UPDATE;
			
			return dupstr(buf);
		}
		
		/* Directly type a letter */
		if(ui->kmode != KEYMODE_OFF
			&& !(state->grid[y*w+x] & FM_FIXED))
		{
			m = ui->kmode == KEYMODE_PENCIL ? 'P' : 'R';

			if(button == '8')
			{
				sprintf(buf, "%c%d,%d,U", m, x, y);
				ui->kmode = KEYMODE_MOVE;
				return dupstr(buf);
			}
			if(button == '2')
			{
				sprintf(buf, "%c%d,%d,D", m, x, y);
				ui->kmode = KEYMODE_MOVE;
				return dupstr(buf);
			}
			if(button == '4')
			{
				sprintf(buf, "%c%d,%d,L", m, x, y);
				ui->kmode = KEYMODE_MOVE;
				return dupstr(buf);
			}
			if(button == '6')
			{
				sprintf(buf, "%c%d,%d,R", m, x, y);
				ui->kmode = KEYMODE_MOVE;
				return dupstr(buf);
			}
			if(button == '\b')
			{
				sprintf(buf, "R%d,%d,-", x, y);
				ui->kmode = KEYMODE_MOVE;
				return dupstr(buf);
			}
		}
		
		if(button == LEFT_BUTTON || button == RIGHT_BUTTON)
		{
			x = FROMCOORD(mx);
			y = FROMCOORD(my);
			
			if(x < 0 || x >= w || y < 0 || y >= h)
				return NULL;
			
			if(state->grid[y*w+x] & FM_FIXED)
				return NULL;
			
			ui->hx = x;
			ui->hy = y;
			ui->kmode = KEYMODE_OFF;
			
			ui->mmode = button == LEFT_BUTTON ? MOUSEMODE_PLACE : MOUSEMODE_PENCIL;
			ui->mdir = EMPTY;
			return MOVE_UI_UPDATE;
		}
	}
	else if (IS_MOUSE_DRAG(button) || IS_MOUSE_RELEASE(button))
	{
		cell c;
		int cx = mx >= ds->tilesize/2 ? FROMCOORD(mx) : -1;
		int cy = my >= ds->tilesize/2 ? FROMCOORD(my) : -1;
		
		if(cx == ui->hx && cy == ui->hy)
			c = EMPTY;
		else if(abs(cx - x) < abs(cy - y))
			c = cy < y ? FM_UP : FM_DOWN;
		else
			c = cx < x ? FM_LEFT : FM_RIGHT;
		
		if(c != ui->mdir && IS_MOUSE_DRAG(button))
		{
			ui->mdir = c;
			return MOVE_UI_UPDATE;
		}
		
		if(IS_MOUSE_RELEASE(button))
		{
			char m = ui->mmode == MOUSEMODE_PLACE ? 'R' : 'P';
			ui->mmode = MOUSEMODE_OFF;
			if(c == EMPTY && m == 'P')
				return MOVE_UI_UPDATE;
			if(m == 'R' && c == state->grid[y*w+x])
				return MOVE_UI_UPDATE;
			
			sprintf(buf, "%c%d,%d,%c", m,
				x, y, c == FM_UP ? 'U' : c == FM_DOWN ? 'D' :
					c == FM_LEFT ? 'L' : c == FM_RIGHT ? 'R' : '-');
			
			return dupstr(buf);
		}
	}
	
	return NULL;
}

static game_state *execute_move(const game_state *oldstate, const char *move)
{
	int w = oldstate->w;
	int h = oldstate->h;
	int x, y;
	char c;
	game_state *state;
	
	if ((move[0] == 'P' || move[0] == 'R') &&
			sscanf(move+1, "%d,%d,%c", &x, &y, &c) == 3 &&
			x >= 0 && x < w && y >= 0 && y < h &&
			(c == 'U' || c == 'D' || c == 'L' || c == 'R' || c == '-')
			)
	{
		if(oldstate->grid[y*w+x] & FM_FIXED)
			return NULL;
		
		state = dup_game(oldstate);
		
		if(move[0] == 'R')
		{
			switch(c)
			{
				case 'U':
					state->grid[y*w+x] = FM_UP;
					break;
				case 'D':
					state->grid[y*w+x] = FM_DOWN;
					break;
				case 'L':
					state->grid[y*w+x] = FM_LEFT;
					break;
				case 'R':
					state->grid[y*w+x] = FM_RIGHT;
					break;
				default:
					state->grid[y*w+x] = EMPTY;
					break;
			}
		}
		if(move[0] == 'P')
		{
			switch(c)
			{
				case 'U':
					state->marks[y*w+x] ^= FM_UP;
					break;
				case 'D':
					state->marks[y*w+x] ^= FM_DOWN;
					break;
				case 'L':
					state->marks[y*w+x] ^= FM_LEFT;
					break;
				case 'R':
					state->marks[y*w+x] ^= FM_RIGHT;
					break;
				default:
					state->marks[y*w+x] = EMPTY;
					break;
			}
		}
		
		if(rome_validate_game(state, true, NULL, NULL) == STATUS_COMPLETE)
			state->completed = true;
		return state;
	}
	
	if(move[0] == 'S')
	{
		const char *p = move + 1;
		int i = 0;
		state = dup_game(oldstate);
		
		while(*p && i < w*h)
		{
			if(!(state->grid[i] & FM_FIXED))
			{
				switch(*p)
				{
					case 'U':
						state->grid[i] = FM_UP;
						break;
					case 'D':
						state->grid[i] = FM_DOWN;
						break;
					case 'L':
						state->grid[i] = FM_LEFT;
						break;
					case 'R':
						state->grid[i] = FM_RIGHT;
						break;
					default:
						state->grid[i] = EMPTY;
						break;
				}
			}
			p++;
			i++;
		}
		
		state->completed = (rome_validate_game(state, true, NULL, NULL) == STATUS_COMPLETE);
		state->cheated = state->completed;
		return state;
	}
	
	return NULL;
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
	if(ui->kmode) {
		*x = (ui->hx+0.5) * ds->tilesize;
		*y = (ui->hy+0.5) * ds->tilesize;
		*w = *h = ds->tilesize;
	}
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	*x = (params->w + 1) * tilesize;
	*y = (params->h + 1) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  const game_params *params, int tilesize)
{
	ds->tilesize = tilesize;
	ds->redraw = true;
}

static float *game_colours(frontend *fe, int *ncolours)
{
	float *ret = snewn(3 * NCOLOURS, float);

	frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);
	game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

	ret[COL_BORDER*3 + 0] = 0.0F;
	ret[COL_BORDER*3 + 1] = 0.0F;
	ret[COL_BORDER*3 + 2] = 0.0F;
	
	ret[COL_ARROW_FIXED*3 + 0] = 0.0F;
	ret[COL_ARROW_FIXED*3 + 1] = 0.0F;
	ret[COL_ARROW_FIXED*3 + 2] = 0.0F;
	
	ret[COL_ARROW_GUESS*3 + 0] = 0.0F;
	ret[COL_ARROW_GUESS*3 + 1] = 0.5F;
	ret[COL_ARROW_GUESS*3 + 2] = 0.0F;
	
	ret[COL_ERRORBG*3 + 0] = 1.0F;
	ret[COL_ERRORBG*3 + 1] = 0.85F * ret[COL_BACKGROUND * 3 + 1];
	ret[COL_ERRORBG*3 + 2] = 0.85F * ret[COL_BACKGROUND * 3 + 2];
	
	ret[COL_GOALBG*3 + 0] = 0.95F * ret[COL_BACKGROUND * 3 + 0];
	ret[COL_GOALBG*3 + 1] = 0.95F * ret[COL_BACKGROUND * 3 + 1];
	ret[COL_GOALBG*3 + 2] = 1.0F;
	
	ret[COL_ARROW_ERROR*3 + 0] = 1.0F;
	ret[COL_ARROW_ERROR*3 + 1] = 0.0F;
	ret[COL_ARROW_ERROR*3 + 2] = 0.0F;
	
	ret[COL_ARROW_PENCIL*3 + 0] = 0.0F;
	ret[COL_ARROW_PENCIL*3 + 1] = 0.5F;
	ret[COL_ARROW_PENCIL*3 + 2] = 0.5F;
	
	ret[COL_ARROW_ENTRY*3 + 0] = 0.0F;
	ret[COL_ARROW_ENTRY*3 + 1] = 0.0F;
	ret[COL_ARROW_ENTRY*3 + 2] = 1.0F;
	
	ret[COL_GOAL*3 + 0] = 0.0F;
	ret[COL_GOAL*3 + 1] = 0.0F;
	ret[COL_GOAL*3 + 2] = 0.5F;
	
	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	struct game_drawstate *ds = snew(struct game_drawstate);
	int s = state->w*state->h;
	
	ds->tilesize = 0;
	ds->oldflash = -1;
	ds->redraw = true;
	ds->oldgrid = snewn(s, cell);
	ds->oldpencil = snewn(s, cell);
	memset(ds->oldgrid, EMPTY, s*sizeof(cell));
	memset(ds->oldpencil, EMPTY, s*sizeof(cell));

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->oldgrid);
	sfree(ds->oldpencil);
	sfree(ds);
}

#define GRIDEXTRA 1
#define SIDE_SIZE 0.6F

static void rome_draw_line(drawing *dr, double thick, int x1, int y1, int x2, int y2, int color)
{
	if(thick <= 1)
		draw_line(dr, x1, y1, x2, y2, color);
	else
		draw_thick_line(dr, thick, x1, y1, x2, y2, color);
}

static void rome_draw_arrow(drawing *dr, int tx, int ty, double size, cell data, int ink)
{
	double thick = size <= 8 ? 1 : 2;
	double sd = size * SIDE_SIZE;
	int color;
	if(ink == -1)
		color = data & FD_ENTRY ? COL_ARROW_ENTRY : 
			data & FM_FIXED ? COL_ARROW_FIXED :
			data & FE_DOUBLE ? COL_ARROW_ERROR : COL_ARROW_GUESS;
	else
		color = ink;
	
	if(data & (FM_UP|FM_DOWN))
		rome_draw_line(dr, thick, tx, ty-size, tx, ty+size, color);
	else
		rome_draw_line(dr, thick, tx-size, ty, tx+size, ty, color);
	
	if(data & FM_UP)
	{
		rome_draw_line(dr, thick, tx, ty-size, tx-sd, ty, color);
		rome_draw_line(dr, thick, tx, ty-size, tx+sd, ty, color);
	}
	if(data & FM_LEFT)
	{
		rome_draw_line(dr, thick, tx, ty-sd, tx-size, ty, color);
		rome_draw_line(dr, thick, tx, ty+sd, tx-size, ty, color);
	}
	if(data & FM_RIGHT)
	{
		rome_draw_line(dr, thick, tx, ty-sd, tx+size, ty, color);
		rome_draw_line(dr, thick, tx, ty+sd, tx+size, ty, color);
	}
	if(data & FM_DOWN)
	{
		rome_draw_line(dr, thick, tx, ty+size, tx-sd, ty, color);
		rome_draw_line(dr, thick, tx, ty+size, tx+sd, ty, color);
	}
}

#define FLASH_TIME 0.7F
#define FLASH_FRAME 0.1F

static void game_redraw(drawing *dr, game_drawstate *ds, const game_state *oldstate,
			const game_state *state, int dir, const game_ui *ui,
			float animtime, float flashtime)
{
	int w = state->w;
	int h = state->h;
	int x, y, i1, i2, cx, cy, cw, ch;
	int tilesize = ds->tilesize;
	DSF *dsf = state->dsf;
	int color;
	int kmode = ui->kmode;
	cell c, p;
	
	int flash = -1;
	if(flashtime > 0)
	{
		flash = (int)(flashtime / FLASH_FRAME) % 3;
		kmode = KEYMODE_OFF;
	}
	
	/*
	 * The initial contents of the window are not guaranteed and
	 * can vary with front ends. To be on the safe side, all games
	 * should start by drawing a big background-colour rectangle
	 * covering the whole window.
	 */
	if(ds->redraw)
	{
		draw_rect(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize, COL_BACKGROUND);
		draw_update(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize);
		draw_rect(dr, (0.5*tilesize) - (GRIDEXTRA*2), 
			(0.5*tilesize) - (GRIDEXTRA*2),
			(w*tilesize) + (GRIDEXTRA*2),
			(h*tilesize) + (GRIDEXTRA*2),
			COL_BORDER);
	}
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		c = state->grid[i1];
		p = state->marks[i1];
		
		if(ui->mmode == MOUSEMODE_PLACE && ui->hx == x && ui->hy == y)
			c = ui->mdir | FD_ENTRY;
		else if(ui->mmode == MOUSEMODE_PENCIL && ui->hx == x && ui->hy == y)
		{
			if(ui->mdir != EMPTY)
				p ^= ui->mdir;
			else
				p |= FD_ENTRY;
		}
		if(ui->kmode != KEYMODE_OFF && ui->hx == x && ui->hy == y)
			c |= ui->kmode == KEYMODE_PLACE ? FD_PLACE :
				ui->kmode == KEYMODE_PENCIL ? FD_PENCIL : FD_CURSOR;
		
		if(!ds->redraw && flash == ds->oldflash && 
			ds->oldgrid[i1] == c && ds->oldpencil[i1] == p)
			continue;
		
		cx = (x+0.5) * tilesize;
		cy = (y+0.5) * tilesize;
		cw = tilesize - 1;
		ch = tilesize - 1;
		
		ds->oldgrid[i1] = c;
		ds->oldpencil[i1] = p;
		draw_update(dr, cx, cy, cw, ch);
		
		if(flash == -1)
		{
			color = ui->sloops && state->grid[i1] & FE_LOOP ? COL_ERRORBG : 
				ui->sgoals && state->grid[i1] & FD_TOGOAL ? COL_GOALBG : 
				state->grid[i1] & FE_BOUNDS ? COL_ERRORBG :
				COL_BACKGROUND;
			
			if(kmode != KEYMODE_OFF && ui->hx == x && ui->hy == y)
				color = kmode == KEYMODE_PLACE ? COL_HIGHLIGHT : COL_LOWLIGHT;
		}
		else
			color = (x+y) % 3 == flash ? COL_BACKGROUND :
				(x+y+1) % 3 == flash ? COL_LOWLIGHT : COL_HIGHLIGHT;
		
		i2 = y*w+x-1; /* Left */
		if(x == 0 || dsf_canonify(dsf, i1) != dsf_canonify(dsf, i2))
		{
			cx += GRIDEXTRA;
			cw -= GRIDEXTRA;
		}
		
		i2 = y*w+x+1; /* Right */
		if(x == w-1 || dsf_canonify(dsf, i1) != dsf_canonify(dsf, i2))
			cw -= GRIDEXTRA*2;
		
		i2 = (y-1)*w+x; /* Top */
		if(y == 0 || dsf_canonify(dsf, i1) != dsf_canonify(dsf, i2))
		{
			cy += GRIDEXTRA;
			ch -= GRIDEXTRA;
		}
		
		i2 = (y+1)*w+x; /* Bottom */
		if(y == h-1 || dsf_canonify(dsf, i1) != dsf_canonify(dsf, i2))
			ch -= GRIDEXTRA*2;
		
		draw_rect(dr, cx, cy, cw, ch, color);
		
		/* Pencil cursor */
		if(kmode == KEYMODE_PENCIL && ui->hx == x && ui->hy == y)
		{
			draw_text(dr, (x+1)*tilesize, (y+1)*tilesize,
				FONT_FIXED, tilesize/1.8, ALIGN_VCENTRE | ALIGN_HCENTRE,
				COL_HIGHLIGHT, "?");
		}
		
		if((c & FD_KBMASK) == c)
		{
			if(p & FM_UP)
				rome_draw_arrow(dr, (x+1)*tilesize, (y+0.75)*tilesize,
					tilesize*0.12, FM_UP, COL_ARROW_PENCIL);
			
			if(p & FM_DOWN)
				rome_draw_arrow(dr, (x+1)*tilesize, (y+1.25)*tilesize,
					tilesize*0.12, FM_DOWN, COL_ARROW_PENCIL);
			
			if(p & FM_LEFT)
				rome_draw_arrow(dr, (x+0.75)*tilesize, (y+1)*tilesize,
					tilesize*0.12, FM_LEFT, COL_ARROW_PENCIL);
			
			if(p & FM_RIGHT)
				rome_draw_arrow(dr, (x+1.25)*tilesize, (y+1)*tilesize,
					tilesize*0.12, FM_RIGHT, COL_ARROW_PENCIL);
						
			if(p & FD_ENTRY)
			{
				draw_rect(dr, (x+1)*tilesize - 2, (y+1)*tilesize - 2,
					4, 4, COL_ARROW_PENCIL);
			}
		}
		
		/* TODO loose pixels for corners */
		
		if(c & FM_GOAL)
		{
			draw_circle(dr, (x+1)*tilesize, (y+1)*tilesize, 
				tilesize / 3, COL_GOAL, COL_GOAL);
		}
		else if(c & FM_ARROWMASK)
		{
			rome_draw_arrow(dr, (x+1)*tilesize, (y+1)*tilesize,
				tilesize *0.3, c, -1);
		}
		else if(c & FD_ENTRY)
		{
			draw_rect(dr, (x+1)*tilesize - 2, (y+1)*tilesize - 2,
				4, 4, COL_ARROW_ENTRY);
		}
	}

	ds->redraw = false;
	ds->oldflash = flash;
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
	int x, y, i1, i2;
	int ink = print_mono_colour(dr, 0);
	int line = print_grey_colour(dr, 0.90F);
	
	for(x = 1; x < w; x++)
	for(y = 0; y < h; y++)
	{
		draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
			(x+0.5)*tilesize, (y+1.5)*tilesize, line);
	}
	for(x = 0; x < w; x++)
	for(y = 1; y < h; y++)
	{
		draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
				(x+1.5)*tilesize, (y+0.5)*tilesize, line);
	}
	
	print_line_width(dr, tilesize / 30);
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		
		i2 = y*w+x+1;
		
		if(x == 0)
			draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
				(x+0.5)*tilesize, (y+1.5)*tilesize, ink);
		
		if(x == w-1 || dsf_canonify(state->dsf, i1) != dsf_canonify(state->dsf, i2))
			draw_line(dr, (x+1.5)*tilesize, (y+0.5)*tilesize, 
				(x+1.5)*tilesize, (y+1.5)*tilesize, ink);
		
		i2 = (y+1)*w+x;
		
		if(y == 0)
			draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
				(x+1.5)*tilesize, (y+0.5)*tilesize, ink);
		
		if(y == h-1 || dsf_canonify(state->dsf, i1) != dsf_canonify(state->dsf, i2))
			draw_line(dr, (x+0.5)*tilesize, (y+1.5)*tilesize, 
				(x+1.5)*tilesize, (y+1.5)*tilesize, ink);
		
		if(state->grid[i1] & FM_GOAL)
		{
			draw_circle(dr, (x+1)*tilesize, (y+1)*tilesize, 
				tilesize / 3, ink, ink);
		}
		if(state->grid[i1] & FM_ARROWMASK)
		{
			rome_draw_arrow(dr, (x+1)*tilesize, (y+1)*tilesize,
				tilesize *0.3, state->grid[i1], ink);
		}
	}
}

#ifdef COMBINED
#define thegame rome
#endif

const struct game thegame = {
	"Rome", NULL, NULL,
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
	false, game_can_format_as_text_now, game_text_format,
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
	40, game_compute_size, game_set_size,
	game_colours,
	game_new_drawstate,
	game_free_drawstate,
	game_redraw,
	game_anim_length,
	game_flash_length,
	game_get_cursor_location,
	game_status,
	true, false, game_print_size, game_print,
	false,			       /* wants_statusbar */
	false, game_timing_state,
	REQUIRE_RBUTTON, /* flags */
};
