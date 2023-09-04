/*
 * crossing.c: Implementation of Nansuke/Number Skeleton puzzles
 * (C) 2013 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * https://www.nikoli.co.jp/en/puzzles/nansuke/
 *
 * Objective: Place all numbers in the list below into the grid.
 * Numbers can be placed horizontally or vertically. Every cell has one digit.
 */

/*
 * TODO Generator: 
 * - Some puzzles have isolated squares (1x1 areas)
 * - More difficulty levels
 *
 * TODO UI:
 * - Find a way to fit the number list on the screen. The size of the puzzle
 *   window can only be determined using the game parameters, but the amount of
 *   numbers can vary wildly using the same parameters.
 * 
 * - Redesign the graphics. Numbers are drawn on an outdented tile, and each
 *   number has a different color. Try changing or removing some of these
 *   effects to make puzzles easier to look at.
 *
 * - Puzzles should be printable.
 * - Optimize drawing routines
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
	COL_OUTERBG,
	COL_LOWLIGHT, COL_INNERBG, COL_HIGHLIGHT,
	COL_GRID, COL_ERROR,
	COL_WALL_L, COL_WALL_M, COL_WALL_H,
	COL_NUM1_L, COL_NUM1_M, COL_NUM1_H,
	COL_NUM2_L, COL_NUM2_M, COL_NUM2_H,
	COL_NUM3_L, COL_NUM3_M, COL_NUM3_H,
	COL_NUM4_L, COL_NUM4_M, COL_NUM4_H,
	COL_NUM5_L, COL_NUM5_M, COL_NUM5_H,
	COL_NUM6_L, COL_NUM6_M, COL_NUM6_H,
	COL_NUM7_L, COL_NUM7_M, COL_NUM7_H,
	COL_NUM8_L, COL_NUM8_M, COL_NUM8_H,
	COL_NUM9_L, COL_NUM9_M, COL_NUM9_H,
	NCOLOURS
};

struct game_params {
	int w, h;
	bool sym;
};

const struct game_params crossing_presets[] = {
	{5, 5, false},
	{7, 7, false},
	{9, 9, false},
};

#define NUM_BIT(i) (1 << ((i) - 1))

struct crossing_puzzle {
	int w, h;
	char *walls;
	
	int maxrow;
	char **numbers;
	int numcount;
	
	int refcount;
};

static int cmp_numbers(const void* va, const void* vb)
{
	const char *a = *(const char**)va;
	const char *b = *(const char**)vb;
	int ca, cb;
	
	ca = strlen(a);
	cb = strlen(b);
	if(ca != cb)
		return ca > cb ? +1 : -1;
	
	return strcmp(a, b);
}

struct game_state {
	struct crossing_puzzle *puzzle;
	
	/* User input */
	char *grid;
	int *marks;
	
	bool completed, cheated;
};

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);
	*ret = crossing_presets[0];

	return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
	if (i < 0 || i >= lenof(crossing_presets))
		return false;
		
	game_params *ret = snew(game_params);
	*ret = crossing_presets[i]; /* struct copy */
	*params = ret;
	
	char buf[80];
	sprintf(buf, "%dx%d", ret->w, ret->h);
	*name = dupstr(buf);
	
	return true;
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
	params->sym = false;
	
	params->w = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;
	if (*p == 'x') {
		p++;
		params->h = atoi(p);
		while (*p && isdigit((unsigned char)*p)) p++;
	} else {
		params->h = params->w;
	}
	if (*p == 'S') {
		params->sym = true;
	}
}

static char *encode_params(const game_params *params, bool full)
{
	char buf[80];
	sprintf(buf, "%dx%d", params->w, params->h);
	if(full && params->sym)
		strcat(buf, "S");
	
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
	
	ret[2].name = "Symmetric walls";
	ret[2].type = C_BOOLEAN;
	ret[2].u.boolean.bval = params->sym;
	
	ret[3].name = NULL;
	ret[3].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = snew(game_params);
	
	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->sym = cfg[2].u.boolean.bval;
	
	return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
	if(params->w < 4 && params->h < 4)
		return "The width or height must be at least 4";
	if(params->w < 2)
		return "Width must be at least 2";
	if(params->h < 2)
		return "Height must be at least 2";
		
	return NULL;
}

static struct crossing_puzzle *blank_puzzle(int w, int h)
{
	struct crossing_puzzle *puzzle = snew(struct crossing_puzzle);
	puzzle->w = w;
	puzzle->h = h;
	
	puzzle->walls = snewn(w*h, char);
	memset(puzzle->walls, false, w*h*sizeof(char));
	
	puzzle->maxrow = 0;
	puzzle->numbers = snewn(w*h, char*);
	puzzle->numcount = 0;
	
	puzzle->refcount = 1;
	
	return puzzle;
}

static void free_puzzle(struct crossing_puzzle *puzzle)
{
	return; // TODO FIX!
	
	if(!--puzzle->refcount)
	{
		int i;
		for(i = 0; i < puzzle->numcount; i++)
			sfree(puzzle->numbers[i]);
		sfree(puzzle->numbers);
		sfree(puzzle->walls);
		sfree(puzzle);
	}
}

static game_state *blank_game(int w, int h, struct crossing_puzzle *puzzle)
{
	game_state *state = snew(game_state);
	
	if(!puzzle)
		state->puzzle = blank_puzzle(w, h);
	else
	{
		state->puzzle = puzzle;
		puzzle->refcount++;
	}
	
	state->grid = snewn(w*h, char);
	memset(state->grid, 0, w*h*sizeof(char));
	state->marks = snewn(w*h, int);
	memset(state->marks, 0, w*h*sizeof(int));
	
	state->completed = state->cheated = false;
	
	return state;
}

static void free_game(game_state *state)
{
	free_puzzle(state->puzzle);
	sfree(state->grid);
	sfree(state->marks);
	sfree(state);
}

enum { 
	VALID, 
	INVALID_WALL, 
	INVALID_TOO_LONG,
	INVALID_MAXROW,
	INVALID_DUPLICATE, 
	INVALID_NUMBER
};

static int crossing_read_desc(const game_params *params,
							const char *desc, game_state **retstate)
{
	/*
	* TODO:
	* - Check for isolated squares (1x1)
	* - Check for too short description
	* - Check for invalid characters (such as 0)
	* - See if runs match given number lengths
	*/
	
	int w = params->w;
	int h = params->h;
	int valid = VALID;
	int erun, wrun, i, maxrow, l;
	game_state *state = blank_game(w, h, NULL);
	char *walls = state->puzzle->walls;
	const char *pp;
	const char *p = desc;
	
	/* Build walls */
	erun = wrun = 0;
	for(i = 0; i < w*h; i++)
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
				p++;
			}
			else if(*p == 'z')
			{
				erun = 'z' - 'a' + 1;
				p++;
			}
			else
				valid = INVALID_WALL;
		}
		if(erun > 0)
		{
			walls[i] = true;
			erun--;
		}
		else if(erun == 0 && wrun > 0)
		{
			walls[i] = false;
			wrun--;
		}
	}
	
	if(*p++ != ',')
		return INVALID_TOO_LONG;
	
	// TODO actually scan area for longest row
	maxrow = 9;
	
	if(maxrow > 9)
		valid = INVALID_MAXROW;
	state->puzzle->maxrow = maxrow;
	
	pp = p;
	while(*p)
	{
		char *num;
		while (*pp && isdigit((unsigned char)*pp)) pp++;
		pp++;
		l = pp - (p+1);
		
		if(l > maxrow)
			valid = INVALID_NUMBER;

		if (l >= 2)
		{
			num = snewn(l + 1, char);
			memcpy(num, p, l);
			num[l] = '\0';

			state->puzzle->numbers[state->puzzle->numcount++] = num;
		}

		p = pp;
	}

	qsort(state->puzzle->numbers, state->puzzle->numcount, sizeof(char*), cmp_numbers);
	
	for (i = 0; i < state->puzzle->numcount - 1; i++)
	{
		if (!strcmp(state->puzzle->numbers[i], state->puzzle->numbers[i + 1]))
			valid = INVALID_DUPLICATE;
	}

	*retstate = state;
	return valid;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	game_state *state = NULL;
	
	int valid = crossing_read_desc(params, desc, &state);

	free_game(state);
	
	switch(valid)
	{
		case INVALID_WALL:
			return "Block description contains invalid character";
		case INVALID_TOO_LONG:
			return "Block description is too long";
		case INVALID_MAXROW:
			return "One of the rows is too long";
		case INVALID_NUMBER:
			return "One of the numbers is too long";
		case INVALID_DUPLICATE:
			return "Duplicate numbers are not supported";
		default:
			return NULL;
	}
}

static key_label *game_request_keys(const game_params *params, int *nkeys)
{
	int i;

	key_label *keys = snewn(10, key_label);
	*nkeys = 10;

	for (i = 0; i < 9; i++)
	{
		keys[i].button = '1' + i;
		keys[i].label = NULL;
	}
	keys[9].button = '\b';
	keys[9].label = NULL;

	return keys;
}

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
	game_state *state = NULL;
	int valid;
	valid = crossing_read_desc(params, desc, &state);
	
	assert(valid != INVALID_WALL);
	assert(valid != INVALID_TOO_LONG);
	assert(valid != INVALID_MAXROW);
	assert(valid != INVALID_DUPLICATE);
	assert(valid != INVALID_NUMBER);
	
	return state;
}

static game_state *dup_game(const game_state *state)
{
	int w = state->puzzle->w;
	int h = state->puzzle->h;
	
	game_state *ret = blank_game(w, h, state->puzzle);
	ret->completed = state->completed;
	ret->cheated = state->cheated;
	
	memcpy(ret->grid, state->grid, w*h*sizeof(char));
	memcpy(ret->marks, state->marks, w*h*sizeof(int));
	
	return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
	return true;
}

static char *game_text_format(const game_state *state)
{
	struct crossing_puzzle *puzzle = state->puzzle;
	char *num;
	int w = puzzle->w;
	int h = puzzle->h;
	int x,y,i,s,l,pl;
	
	assert(puzzle->maxrow <= 9);
	
	s = 3;
	pl = 0;
	for (i = 0; i < puzzle->numcount; i++)
	{
		num = puzzle->numbers[i];
		l = strlen(num);
		if(l != pl)
			s+= 3;
		pl = l;
		s += l;
	}
	
	char *ret = snewn(s+(w+1)*h, char);
	char *p = ret;
	
	for(y = 0; y < h; y++)
	{
		for(x = 0; x < w; x++)
		{
			*p++ = (puzzle->walls[y*w+x] ? '#' :
				state->grid[y*w+x] ? state->grid[y*w+x] + '0' : '.');
		}
		*p++ = '\n';
	}
	*p++ = 'Q';
	
	pl = 0;
	for (i = 0; i < puzzle->numcount; i++)
	{
		num = puzzle->numbers[i];
		l = strlen(num);
		if(pl != l)
		{
			p--;
			p += sprintf(p, "\n%d: ", l);
		}
		pl = l;
		
		p += sprintf(p, "%s", num);
		*p++ = ',';
	}
	
	*p++ = '\0';
	return ret;
}

/* SOLVER */
struct crossing_run {
	int row, start, len;
	bool horizontal;
};

struct crossing_solver {
	int runcount;
	struct crossing_run *runs;
	int *done;
};

static int crossing_collect_runs(struct crossing_puzzle *puzzle, struct crossing_run *runs)
{
	int w = puzzle->w;
	int h = puzzle->h;
	int i, x, y;
	bool inrun;
	
	i = 0;
	/* Horizontal */
	for(y = 0; y < h; y++)
	{
		inrun = false;
		for(x = 0; x <= w; x++)
		{
			if(inrun && (x == w || puzzle->walls[y*w+x]))
			{
				inrun = false;
				i++;
				continue;
			}
			else if(x == w)
				continue;
			
			if(!inrun && puzzle->walls[y*w+x])
				continue;
			else if(!inrun && !puzzle->walls[y*w+x] && x < w-1 && !puzzle->walls[y*w+x+1])
			{
				inrun = true;
				runs[i].row = y;
				runs[i].start = x;
				runs[i].len = 0;
				runs[i].horizontal = true;
			}
			
			if(inrun && !puzzle->walls[y*w+x])
			{
				runs[i].len++;
			}
		}
	}
	/* Vertical */
	for(x = 0; x < w; x++)
	{
		inrun = false;
		for(y = 0; y <= h; y++)
		{
			if(inrun && (y == h || puzzle->walls[y*w+x]))
			{
				inrun = false;
				i++;
				continue;
			}
			else if(y == h)
				continue;
			
			if(!inrun && puzzle->walls[y*w+x])
				continue;
			else if(!inrun && !puzzle->walls[y*w+x] && y < h-1 && !puzzle->walls[(y+1)*w+x])
			{
				inrun = true;
				runs[i].row = x;
				runs[i].start = y;
				runs[i].len = 0;
				runs[i].horizontal = false;
			}
			
			if(inrun && !puzzle->walls[y*w+x])
			{
				runs[i].len++;
			}
		}
	}
#if 0
	int d;
	for(d = 0; d < i; d++)
	{
		printf("RUN: row=%i start=%i len=%i %s\n",
			runs[d].row, runs[d].start, runs[d].len,
			runs[d].horizontal ? "Horizontal" : "Vertical"); 
	}
#endif
	
	return i;
}

static struct crossing_solver *crossing_solver_init(game_state *state)
{
	int w = state->puzzle->w;
	int h = state->puzzle->h;
	int i, ts;
	for(i = 0; i < w*h; i++)
	{
		if(state->puzzle->walls[i])
			state->marks[i] = 0;
		else
			state->marks[i] = 0x1ff; /* bits 0..9 */
	}
	
	struct crossing_solver *ret = snew(struct crossing_solver);
	ret->runcount = 0;
	ret->runs = snewn(w*h, struct crossing_run);
	
	ts = state->puzzle->numcount;
	ret->done = snewn(ts, int);
	memset(ret->done, 0, ts * sizeof(int));
	
	ret->runcount = crossing_collect_runs(state->puzzle, ret->runs);
	
	return ret;
}

static void free_solver(struct crossing_solver *solver)
{
	sfree(solver->runs);
	sfree(solver->done);
	sfree(solver);
}

enum { STATUS_VALID, STATUS_INVALID, STATUS_PROGRESS };

static void crossing_iterate(struct crossing_run *run, int w, int *s, int *e, int *d)
{
	if(run->horizontal)
	{
		*s = run->row*w + run->start;
		*e = run->row*w + run->start + run->len;
		*d = 1;
	}
	else
	{
		*s = run->start*w + run->row;
		*e = (run->start + run->len)*w + run->row;
		*d = w;
	}
}

static int crossing_validate(const game_state *state, int runcount, struct crossing_run *runs, int *done, char *runerrs)
{
	int w = state->puzzle->w;
	int h = state->puzzle->h;
	int i, j, k, l;
	int len;
	bool any, match, full;
	char *num;
	int s, e, d;
	int status = STATUS_VALID;
	bool hasruns = runs != NULL;
	bool hasdone = done != NULL;
	
	if(!hasruns)
	{
		runs = snewn(w*h, struct crossing_run);
		runcount = crossing_collect_runs(state->puzzle, runs);
	}
	if(!hasdone)
	{
		int ts = state->puzzle->numcount;
		done = snewn(ts, int);
		memset(done, 0, ts);
	}
	
	if(done)
		memset(done, 0, runcount*sizeof(int));
	if(runerrs)
		memset(runerrs, false, runcount*sizeof(char));
	
	for(i = 0; i < runcount; i++)
	{
		crossing_iterate(&runs[i], w, &s, &e, &d);
		len = runs[i].len;
		any = false;
		full = true;
		
		for (l = 0; l < state->puzzle->numcount; l++)
		{
			num = state->puzzle->numbers[l];
			if(strlen(num) != len)
				continue;
			
			match = true;
			for(j = s, k = 0; j < e; j += d, k++)
			{
				if(state->grid[j] == 0)
					full = false;
				if(state->grid[j] != (num[k] - '0'))
					match = false;
			}
			
			if(match)
				any = true;
			if(done && match)
				done[l]++;
				
			//printf("%s %s %s| ", num, match ? "Match" : "No", full ? "Full " : "");
		}
		if(status == STATUS_VALID && !full)
			status = STATUS_PROGRESS;
		if(full && !any)
		{
			status = STATUS_INVALID;
			if(runerrs)
				runerrs[i] = true;
		}
		//printf("\n");
	}
	
	if(status != STATUS_INVALID)
	{
		for(i = 0; i < runcount; i++)
		{
			if(done[i] > 1)
			{
				status = STATUS_INVALID;
				break;
			}
			if(done[i] == 0)
				status = STATUS_PROGRESS;
		}
	}
	
	if(!hasruns)
		sfree(runs);
	if(!hasdone)
		sfree(done);
	
	return status;
}

static int crossing_solver_marks(game_state *state, struct crossing_solver *solver)
{
	int marks[9];
	int ret = 0;
	int i, j, k, l, len;
	int w = state->puzzle->w;
	int s, e, d;
	char n;
	bool match;
	char *num;
	
	for(i = 0; i < solver->runcount; i++)
	{
		crossing_iterate(&solver->runs[i], w, &s, &e, &d);
		len = solver->runs[i].len;
		
		memset(marks, 0, len*sizeof(int));	
		
		for (l = 0; l < state->puzzle->numcount; l++)
		{
			num = state->puzzle->numbers[l];
			if(solver->done[l]) continue;
			if(strlen(num) != len) continue;
			
			match = true;
			for(j = s, k = 0; j < e; j += d, k++)
			{
				n = num[k] - '0';
				if(!(state->marks[j] & NUM_BIT(n)))
					match = false;
			}
			
			if(!match) continue;
			
			for(k = 0; k < len; k++)
			{
				n = num[k] - '0';
				marks[k] |= NUM_BIT(n);
			}
		}

		//printf("Marks (%d) is ", len);
		//for(k = 0; k < len; k++)
		//	printf("%d, ", marks[k]);

		for(j = s, k = 0; j < e; j += d, k++)
		{
			if(!state->grid[j] && state->marks[j] != marks[k])
			{
				ret++;
				state->marks[j] &= marks[k];
				//printf("!");
			}
		}
		//printf("\n");
	}
	return ret;
}

static int crossing_solver_confirm(game_state *state)
{
	int ret = 0;
	int s = state->puzzle->w * state->puzzle->h;
	int i, j;
	for(i = 0; i < s; i++)
	{
		if(state->grid[i])
			continue;
		
		for(j = 0; j <= 9; j++)
		{
			if(state->marks[i] == NUM_BIT(j))
			{
				ret++;
				state->grid[i] = j;
			}
		}
	}
	
	return ret;
}

static int crossing_solve_game(game_state *state)
{
	struct crossing_solver *solver = crossing_solver_init(state);
	int status;
	int done = 0;
	
	while(true)
	{
		done = 0;
		status = crossing_validate(state, solver->runcount, solver->runs, solver->done, NULL);
		if(status != STATUS_PROGRESS)
			break;
		
		done += crossing_solver_marks(state, solver);
		done += crossing_solver_confirm(state);
		
		if(done)
			continue;
		
		// TODO harder techniques?
		break;
	}
	
	free_solver(solver);
	return status;
}

enum { GEN_BLANK, GEN_WALL, GEN_CELL };
static bool crossing_gen_walls_checkpool(int w, int h, char *walls)
{
	/* Find a 2x2 area without GEN_CELL */
	/* Also ensure no 2x2 area of GEN_CELL exists */
	bool ret = true;
	int x, y;
	
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w-1; x++)
	{
		/* Contains block of no cell */
		if(walls[y*w+x] != GEN_CELL &&
				walls[y*w+x+1] != GEN_CELL &&
				walls[(y+1)*w+x] != GEN_CELL &&
				walls[(y+1)*w+x+1] != GEN_CELL)
		{
			ret = false;
		}
		
		/* Wall on top-left */
		if(walls[y*w+x+1] == GEN_CELL &&
				walls[(y+1)*w+x] == GEN_CELL &&
				walls[(y+1)*w+x+1] == GEN_CELL)
		{
			walls[y*w+x] = GEN_WALL;
		}
		
		/* Wall on top-right */
		if(walls[y*w+x] == GEN_CELL &&
				walls[(y+1)*w+x] == GEN_CELL &&
				walls[(y+1)*w+x+1] == GEN_CELL)
		{
			walls[y*w+x+1] = GEN_WALL;
		}
		
		/* Wall on bottom-left */
		if(walls[y*w+x] == GEN_CELL &&
				walls[y*w+x+1] == GEN_CELL &&
				walls[(y+1)*w+x+1] == GEN_CELL)
		{
			walls[(y+1)*w+x] = GEN_WALL;
		}
		
		/* Wall on bottom-right */
		if(walls[y*w+x] == GEN_CELL &&
				walls[y*w+x+1] == GEN_CELL &&
				walls[(y+1)*w+x] == GEN_CELL)
		{
			walls[(y+1)*w+x+1] = GEN_WALL;
		}
	}
	return ret;
}

static bool crossing_gen_walls_checkdsf(int w, int h, char *walls)
{
	DSF *dsf = dsf_new(w*h);
	int i, s, i1, i2, x, y;
	int maxsize, maxcell, total;
	
	/* Horizontal merger */
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		i1 = y*w+x;
		i2 = y*w+x+1;
		if(walls[i1] == walls[i2])
		{
			dsf_merge(dsf, dsf_canonify(dsf, i1), dsf_canonify(dsf, i2));
		}
	}
	
	/* Vertical merger */
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		i2 = (y+1)*w+x;
		if(walls[i1] == walls[i2])
		{
			dsf_merge(dsf, dsf_canonify(dsf, i1), dsf_canonify(dsf, i2));
		}
	}
	
	total = 0;
	maxsize = -1;
	maxcell = -1;
	for(i = 0; i < w*h; i++)
	{
		if(walls[i] != GEN_CELL)
			continue;
		
		total++;
		s = dsf_size(dsf, i);
		if(s > maxsize)
		{
			maxsize = s;
			maxcell = dsf_canonify(dsf, i);
		}
	}
	dsf_free(dsf);
	return maxsize == total;
}

static bool crossing_gen_walls(struct crossing_puzzle *puzzle, random_state *rs, bool sym)
{
	int w = puzzle->w;
	int h = puzzle->h;
	int s = w*h;
	int i, j;
	char *walls = snewn(s, char);
	for(i = 0; i < s; i++) walls[i] = GEN_BLANK;
	int *spaces = snewn(s, int);
	for(i = 0; i < s; i++) spaces[i] = i;
	shuffle(spaces, s, sizeof(int), rs);
	
	for(j = 0; j < s; j++)
	{
		if(crossing_gen_walls_checkpool(w, h, walls) && 
				crossing_gen_walls_checkdsf(w, h, walls))
			break;
		
		i = spaces[j];
		
		if(walls[i] == GEN_BLANK)
			walls[i] = GEN_CELL;
		if(sym && walls[s-(i+1)] == GEN_BLANK)
			walls[s-(i+1)] = GEN_CELL;
	}
	
	/* Return wall array */
	for(i = 0; i < s; i++)
	{
		puzzle->walls[i] = (walls[i] != GEN_CELL);
	}
	
	sfree(spaces);
	sfree(walls);
	
	return true;
}

static char *crossing_gen_grid(struct crossing_puzzle *puzzle, random_state *rs)
{
	int w = puzzle->w;
	int h = puzzle->h;
	int i;
	char *ret = snewn(w*h, char);
	memset(ret, 0, w*h*sizeof(char));
	
	for(i = 0; i < w*h; i++)
	{
		ret[i] = 1 + random_upto(rs, 9);
	}
	
	return ret;
}

#define MAXIMUM_ROW 9
static bool crossing_gen_numbers(struct crossing_puzzle *puzzle, char *grid)
{
	int w = puzzle->w;
	int i, j, k, s, e, d, runcount;
	struct crossing_run *runs = snewn(w * puzzle->h, struct crossing_run);
	runcount = crossing_collect_runs(puzzle, runs);
	char buf[MAXIMUM_ROW+1];
	char *num;
	bool ret = true;
	
	for(i = 0; i < runcount; i++)
	{
		crossing_iterate(&runs[i], w, &s, &e, &d);
		for(j = s, k = 0; j < e && k <= MAXIMUM_ROW; j += d, k++)
		{
			buf[k] = '0' + grid[j];
		}
		
		/* Check if this row is too long */
		if(k > MAXIMUM_ROW)
		{
			ret = false;
			break;
		}
		
		buf[k] = '\0';
		//printf("%s\n", buf);
		num = dupstr(buf);
		puzzle->numbers[puzzle->numcount++] = num;
	}

	qsort(puzzle->numbers, puzzle->numcount, sizeof(char*), cmp_numbers);
	
	for (i = 0; i < puzzle->numcount - 1; i++)
	{
		if (!strcmp(puzzle->numbers[i], puzzle->numbers[i + 1]))
			ret = false;
	}

	sfree(runs);
	return ret;
}

static bool crossing_gen_solve(struct crossing_puzzle *puzzle)
{
	int status;
	game_state *state = blank_game(puzzle->w, puzzle->h, puzzle);
	
	status = crossing_solve_game(state);
	
	free_game(state);
	
	return status == STATUS_VALID;
}

static bool crossing_generate(struct crossing_puzzle *puzzle, random_state *rs, const game_params *params)
{
	char *grid;
	bool ret = true;
	
	if(!crossing_gen_walls(puzzle, rs, params->sym))
		return false;
	
	grid = crossing_gen_grid(puzzle, rs);
	/*
	int w = params->w;
	int h = params->h;
	
	int x,y;
	for(y=0;y<h;y++){
		for(x=0;x<w;x++) printf("%s", puzzle->walls[y*w+x] ? "#" : ".");
		printf("\n");
	}
	*/
	ret = crossing_gen_numbers(puzzle, grid);
	sfree(grid);
	
	if(!ret) return false;
	
	return crossing_gen_solve(puzzle);
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	int w = params->w;
	int h = params->h;
	int i, erun, wrun;
	bool success;
	char *buf, *p;
	
	struct crossing_puzzle *puzzle = blank_puzzle(w, h);
	
	do
	{
		success = crossing_generate(puzzle, rs, params);
		if(!success)
		{
			for (i = 0; i < puzzle->numcount; i++)
				sfree(puzzle->numbers[i]);
			puzzle->numcount = 0;
		}
	} while(!success);
	
	buf = snewn(w*h*3, char);
	p = buf;
	
	erun = wrun = 0;
	for(i = 0; i < w*h; i++)
	{
		if(puzzle->walls[i] && wrun > 0)
		{
			p += sprintf(p, "%d", wrun);
			wrun = 0;
			erun = 0;
		}
		else if(!puzzle->walls[i] && erun > 0)
		{
			*p++ = ('a' + erun - 1);
			erun = 0;
			wrun = 0;
		}
		
		if(puzzle->walls[i])
			erun++;
		else
			wrun++;
	}
	if(wrun > 0)
		p += sprintf(p, "%d", wrun);
	if(erun > 0)
		*p++ = ('a' + erun - 1);
	
	*p++ = ',';
	
	for (i = 0; i < puzzle->numcount; i++)
	{
		p += sprintf(p, "%s,", puzzle->numbers[i]);
	}
	p--;
	*p++ = '\0';
	
	free_puzzle(puzzle);
	return buf;
}

static char *solve_game(const game_state *state, const game_state *currstate,
						const char *aux, const char **error)
{
	int s = state->puzzle->w*state->puzzle->h;
	char *ret = snewn(s+2, char);
	char *p = ret;
	int i;
	game_state *solved = dup_game(state);
	crossing_solve_game(solved);
	
	*p++ = 'S';
	for(i = 0; i < s; i++)
	{
		if(solved->grid[i] == 0)
			*p++ = '-';
		else
			*p++ = '0' + solved->grid[i];
	}
	*p++ = '\0';
	
	free_game(solved);
	return ret;
}

struct game_ui
{
	int cx, cy;
	bool cshow, cpencil, ckey;
	
	int runcount;
	struct crossing_run *runs;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ui = snew(game_ui);
	
	ui->cx = 0;
	ui->cy = 0;
	ui->cshow = ui->cpencil = ui->ckey = false;
	
	/* Generate runs */
	ui->runs = snewn(state->puzzle->w * state->puzzle->h, struct crossing_run);
	ui->runcount = crossing_collect_runs(state->puzzle, ui->runs);
	
	return ui;
}

static void free_ui(game_ui *ui)
{
	sfree(ui->runs);
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

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (ui->cshow && (button == CURSOR_SELECT))
        return ui->cpencil ? "Ink" : "Pencil";
    return "";
}

#define FE_LEFT   0x01
#define FE_RIGHT  0x02
#define FE_CENTER (FE_LEFT|FE_RIGHT)
#define FE_TOP    0x04
#define FE_BOT    0x08
#define FE_MID    (FE_TOP|FE_BOT)

struct game_drawstate {
	int tilesize;
	int *gridfs;
	char *runerrs;
	int *done;
};

#define FROMCOORD(x) ( ((x)-(tilesize/2)) / tilesize )

static char *interpret_move(const game_state *state, game_ui *ui,
							const game_drawstate *ds,
							int ox, int oy, int button)
{
	int w = state->puzzle->w;
	int h = state->puzzle->h;
	int tilesize = ds->tilesize;
	
	int gx = FROMCOORD(ox);
	int gy = FROMCOORD(oy);
	int cx = ui->cx;
	int cy = ui->cy;
	
	char buf[64];
	button &= ~MOD_MASK;
	
	/* Mouse click */
	if (gx >= 0 && gx < w && gy >= 0 && gy < h)
	{
		/* Select square for number placement */
		if (button == LEFT_BUTTON)
		{
			/* Select */
			if(!ui->cshow || ui->cpencil || cx != gx || cy != gy)
			{
				ui->cx = gx;
				ui->cy = gy;
				ui->cpencil = false;
				ui->cshow = true;
			}
			/* Deselect */
			else
			{
				ui->cshow = false;
			}
			
			if(state->puzzle->walls[gy*w+gx])
				ui->cshow = false;
			
			ui->ckey = false;
			return MOVE_UI_UPDATE;
		}
		/* Select square for marking */
		else if (button == RIGHT_BUTTON)
		{
			/* Select */
			if(!ui->cshow || !ui->cpencil || cx != gx || cy != gy)
			{
				ui->cx = gx;
				ui->cy = gy;
				ui->cpencil = true;
				ui->cshow = true;
			}
			/* Deselect */
			else
			{
				ui->cshow = false;
			}
			
			/* Remove the cursor again if the clicked square has a confirmed number */
			if(state->grid[gy*w+gx] != 0)
				ui->cshow = false;
			if(state->puzzle->walls[gy*w+gx])
				ui->cshow = false;
			
			ui->ckey = false;
			return MOVE_UI_UPDATE;
		}
	}
	
	if (IS_CURSOR_MOVE(button))
	{
		move_cursor(button, &ui->cx, &ui->cy, w, h, 0, NULL);
		ui->cshow = ui->ckey = true;
		return MOVE_UI_UPDATE;
	}
	/* Keyboard change pencil cursor */
	if (ui->cshow && button == CURSOR_SELECT)
	{
		ui->cpencil = !ui->cpencil;
		ui->ckey = true;
		return MOVE_UI_UPDATE;
	}
	
	/* Enter or remove numbers */
	if(ui->cshow && (
			(button >= '1' && button <= '9') || 
			button == CURSOR_SELECT2 || button == '\b' || button == '0'))
	{
		char c;
		if (button >= '1' && button <= '9')
			c = button - '0';
		else
			c = 0;
		
		/* When in pencil mode, filled in squares cannot be changed */
		if (ui->cpencil && state->grid[cy*w+cx] != 0)
			return NULL;
		/* Avoid moves which don't change anything */
		if (!ui->cpencil && state->grid[cy*w+cx] == c) /* TODO hide cursor? */
			return NULL;
		/* Don't edit walls */
		if (state->puzzle->walls[cy*w+cx])
			return NULL;
		
		
		sprintf(buf, "%c%d,%d,%c",
				(char)(ui->cpencil ? 'P' : 'R'),
				cx, cy,
				(char)(c != 0 ? '0' + c : '-')
		);
		
		/* When not in keyboard mode, hide cursor */
		if (!ui->ckey && !ui->cpencil)
			ui->cshow = false;
		
		return dupstr(buf);
	}
	
	return NULL;
}

static game_state *execute_move(const game_state *oldstate, const char *move)
{
	int w = oldstate->puzzle->w;
	int h = oldstate->puzzle->h;
	int x, y;
	char c;
	game_state *state;
		
	if ((move[0] == 'P' || move[0] == 'R') &&
			sscanf(move+1, "%d,%d,%c", &x, &y, &c) == 3 &&
			x >= 0 && x < w && y >= 0 && y < h &&
			((c >= '1' && c <= '9' ) || c == '-')
			)
	{
		if(oldstate->puzzle->walls[y*w+x])
			return NULL;
		
		state = dup_game(oldstate);
		
		if(move[0] == 'R')
		{
			if(c == '-')
				state->grid[y*w+x] = 0;
			else
				state->grid[y*w+x] = c - '0';
		}
		if(move[0] == 'P')
		{
			if(c == '-')
				state->marks[y*w+x] = 0;
			else
				state->marks[y*w+x] ^= NUM_BIT(c - '0');
		}
		
		if(crossing_validate(state, 0, NULL, NULL, NULL) == STATUS_VALID)
			state->completed = true;
		
		return state;
	}
	
	if(move[0] == 'S')
	{
		const char *p = move + 1;
		char c;
		int i = 0;
		state = dup_game(oldstate);
		
		while(*p && i < w*h)
		{
			if(!state->puzzle->walls[i])
			{
				c = *p;
				if(c >= '1' && c <= '9')
					state->grid[i] = c - '0';
				else
					state->grid[i] = 0;
			}
			p++;
			i++;
		}
		
		state->completed = (crossing_validate(state, 0, NULL, NULL, NULL) == STATUS_VALID);
		state->cheated = state->completed;
		return state;
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
	if(ui->cshow) {
		*x = (ui->cx+0.5) * ds->tilesize;
		*y = (ui->cy+0.5) * ds->tilesize;
		*w = *h = ds->tilesize;
	}
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	*x = (params->w + 1) * tilesize;
	*y = (params->h + 1 + 3) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
						  const game_params *params, int tilesize)
{
	ds->tilesize = tilesize;
}

static const unsigned long bgcols[9] = {
    0xffa07a, /* lightsalmon */
    0x98fb98, /* green */
    0x7fffd4, /* aquamarine */
    0x9370db, /* medium purple */
    0xffa500, /* orange */
    0x87cefa, /* lightskyblue */
    0xddcc11, /* yellow-ish */
	0x4080ff, /* something */
	0x7092be, /* something else */
};

static float *game_colours(frontend *fe, int *ncolours)
{
	float *ret = snewn(3 * NCOLOURS, float);
	int i, c;
	
	game_mkhighlight(fe, ret, COL_INNERBG, COL_HIGHLIGHT, COL_LOWLIGHT);
	frontend_default_colour(fe, &ret[COL_OUTERBG * 3]);
	
	for(i = 0; i < 3; i++)
	{
		ret[COL_GRID*3 + i] = 0;
		ret[COL_WALL_M*3 + i] = 0.3;
	}
	
	ret[COL_ERROR*3 + 0] = 1;
	ret[COL_ERROR*3 + 1] = 0;
	ret[COL_ERROR*3 + 2] = 0;
	
	game_mkhighlight_specific(fe, ret, COL_WALL_M, COL_WALL_H, COL_WALL_L);
	
	for(c = 0; c < 9; c++)
	{
		ret[(COL_NUM1_M + (c*3))*3 + 0] = (float)((bgcols[c] & 0xff0000) >> 16) / 256.0F;
		ret[(COL_NUM1_M + (c*3))*3 + 1] = (float)((bgcols[c] & 0xff00) >> 8) / 256.0F;
		ret[(COL_NUM1_M + (c*3))*3 + 2] = (float)((bgcols[c] & 0xff)) / 256.0F;
		game_mkhighlight_specific(fe, ret, COL_NUM1_M+(c*3), COL_NUM1_H+(c*3), COL_NUM1_L+(c*3));
	}
	
	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	struct game_drawstate *ds = snew(struct game_drawstate);
	int s = state->puzzle->w * state->puzzle->h;
	
	ds->tilesize = 0;
	ds->gridfs = snewn(s, int);
	memset(ds->gridfs, 0, s*sizeof(int));
	ds->runerrs = snewn(s, char);
	memset(ds->runerrs, false, s*sizeof(char));
	ds->done = snewn(s, int);
	memset(ds->done, 0, s*sizeof(int));

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->gridfs);
	sfree(ds->runerrs);
	sfree(ds->done);
	sfree(ds);
}

#define HIGHLIGHT_WIDTH (tilesize / 10)

static void draw_err_rectangle(drawing *dr, int tx, int ty, int x, int y, 
								int w, int h, int tilesize)
{
	double thick = tilesize / 10;
	double margin = tilesize / 20;

	clip(dr, tx, ty, tilesize, tilesize);
	draw_rect(dr, x+margin, y+margin, w-2*margin, thick, COL_ERROR);
	draw_rect(dr, x+margin, y+margin, thick, h-2*margin, COL_ERROR);
	draw_rect(dr, x+margin, y+h-margin-thick, w-2*margin, thick, COL_ERROR);
	draw_rect(dr, x+w-margin-thick, y+margin, thick, h-2*margin, COL_ERROR);
	unclip(dr);
}

static void draw_tile(drawing *dr, game_drawstate *ds, int tx, int ty, 
	int low, int mid, int high)
{
	int tilesize = ds->tilesize;
	
	clip(dr, tx+1, ty+1, tilesize-1, tilesize-1);
	draw_rect(dr, tx+1, ty+1, tilesize-1, tilesize-1, mid);

	int coords[6];

	coords[0] = tx + tilesize;
	coords[1] = ty + tilesize;
	coords[2] = tx + tilesize;
	coords[3] = ty + 1;
	coords[4] = tx + 1;
	coords[5] = ty + tilesize;
	draw_polygon(dr, coords, 3, low, low);

	coords[0] = tx + 1;
	coords[1] = ty + 1;
	draw_polygon(dr, coords, 3, high, high);

	draw_rect(dr, tx + 1 + HIGHLIGHT_WIDTH, ty + 1 + HIGHLIGHT_WIDTH,
		tilesize - 2*HIGHLIGHT_WIDTH,
		tilesize - 2*HIGHLIGHT_WIDTH, mid);
	
	unclip(dr);
	draw_update(dr, tx, ty, tilesize, tilesize);
}

static void draw_triangle(drawing *dr, int tx, int ty, int tilesize)
{
	int coords[6];
			
	coords[0] = tx;
	coords[1] = ty;
	coords[2] = tx+(tilesize/2);
	coords[3] = ty;
	coords[4] = tx;
	coords[5] = ty+(tilesize/2);
	draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);
}

static void draw_numbers(drawing *dr, game_drawstate *ds, int w, int h, char **numbers, int numcount)
{
	float tilesize = ds->tilesize;
	float x, y;
	int i = 0;
	char *num;
	int color;
	int l = 0;
	float hgt = 2.8 * tilesize;
	float wdt = w * tilesize;
	int rows = 4;
	float fontsz;
	float tmpwdt;
	float whprop = 0.6;
	float space = 0.8;
	float yoff = (h + 1.2) * tilesize;

	/* Figure out the number of rows and font size that make it all fit */
	while (1)
	{
		fontsz = hgt / rows / 1.4;
		tmpwdt = -space;
		for (i = rows - 1; i < numcount + rows - 1; i += rows)
		{
			l = strlen(numbers[min(i, numcount - 1)]);
			tmpwdt += l * whprop + space;
		}
		if (fontsz * tmpwdt <= wdt)
		{
			if (numcount > rows)
				space += (wdt / fontsz - tmpwdt) / ((numcount + rows - 1) / rows - 1);
			break;
		}
		if (wdt / tmpwdt > hgt / (rows + 1) / 1.4)
		{
			fontsz = wdt / tmpwdt;
			break;
		}
		rows++;
	}

	x = 0.5 * tilesize - space * fontsz;
	y = yoff;
	l = 0;
	for (i = 0; i < numcount; i++)
	{
		if (i % rows == 0)
		{
			x += (l * whprop + space) * fontsz;
			y = yoff;
		}
		num = numbers[i];
		l = strlen(num);
		color = ds->done[i] == 0 ? COL_GRID : ds->done[i] == 1 ? COL_LOWLIGHT : COL_ERROR;
		draw_text(dr, x, y, FONT_FIXED, fontsz,
			  ALIGN_VNORMAL | ALIGN_HLEFT, color, num);
		y += hgt / rows;
	}
}

#define FLASH_FRAME 0.08F
#define FLASH_TIME (FLASH_FRAME * 9)

static void game_redraw(drawing *dr, game_drawstate *ds,
						const game_state *oldstate, const game_state *state,
						int dir, const game_ui *ui,
						float animtime, float flashtime)
{
	int tilesize = ds->tilesize;
	struct crossing_puzzle *puzzle = state->puzzle;
	int w = puzzle->w;
	int h = puzzle->h;
	int i,x,y,n,c, tx, ty, color;
	char buf[2];
	bool cshow = ui->cshow && flashtime == 0;
	bool flash = false;
	buf[1] = '\0';
	
	if(flashtime > 0)
		flash = (int)(flashtime/FLASH_FRAME);
	
	draw_rect(dr, 0, 0, (w+1)*ds->tilesize, (h+1+3)*ds->tilesize, COL_OUTERBG);
	draw_update(dr, 0, 0, (w+1)*ds->tilesize, (h+1+3)*ds->tilesize);
	
	/* Find errors */
	memset(ds->gridfs, 0, w*h*sizeof(int));
	crossing_validate(state, ui->runcount, ui->runs, ds->done, ds->runerrs);
	
	for(i = 0; i < ui->runcount; i++)
	{
		int j, s, e, d;
		bool horizontal = ui->runs[i].horizontal;
		if(!ds->runerrs[i]) continue;
		
		crossing_iterate(&ui->runs[i], w, &s, &e, &d);
		
		for(j = s; j < e; j += d)
		{
			if(j == s)
				ds->gridfs[j] |= horizontal ? FE_LEFT : FE_TOP;
			else if(j == e-d)
				ds->gridfs[j] |= horizontal ? FE_RIGHT : FE_BOT;
			else
				ds->gridfs[j] |= horizontal ? FE_CENTER : FE_MID;
		}
	}
	
	/* Draw cells */
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		int tile = ds->gridfs[y*w+x];
		tx = (x*tilesize)+(tilesize/2);
		ty = (y*tilesize)+(tilesize/2);
		
		if(!state->grid[y*w+x])
		{
			if(cshow && !ui->cpencil && !ui->ckey && ui->cx == x && ui->cy == y)
				color = COL_HIGHLIGHT;
			else
				color = COL_INNERBG;
			draw_rect(dr, tx, ty, tilesize, tilesize, color);
		}
		
		if(puzzle->walls[y*w+x])
		{
			draw_tile(dr, ds, tx, ty, 
				COL_WALL_H, COL_WALL_M, COL_WALL_L);
		}
		else if(state->grid[y*w+x])
		{
			n = state->grid[y*w+x];
			c = n-1;
			buf[0] = n + '0';
			if(flash)
				c = (x+y+flash) % 9;
			color = COL_NUM1_M + (c*3);
			
			if(cshow && !ui->cpencil && !ui->ckey && ui->cx == x && ui->cy == y)
			{
				draw_tile(dr, ds, tx, ty, 
					color+1, color, color-1);
				draw_text_outline(dr, (x+1)*tilesize, (y+1)*tilesize,
					FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
					color-1, COL_GRID, buf);
			}
			else
			{
				draw_tile(dr, ds, tx, ty, 
					color-1, color, color+1);
				draw_text_outline(dr, (x+1)*tilesize, (y+1)*tilesize,
					FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
					color+1, COL_GRID, buf);
			}
		}
		
		/* Draw errors */
		if (tile & (FE_LEFT | FE_RIGHT)) {
			int left = tx+1, right = tx + tilesize;
			if ((tile & FE_LEFT))
				right += tilesize/2;
			if ((tile & FE_RIGHT))
				left -= tilesize/2;
			draw_err_rectangle(dr, tx, ty, left, ty+1, right-left, tilesize-1, tilesize);
		}
		if (tile & (FE_TOP | FE_BOT)) {
			int top = ty+1, bottom = ty + tilesize;
			if ((tile & FE_TOP))
				bottom += tilesize/2;
			if ((tile & FE_BOT))
				top -= tilesize/2;
			draw_err_rectangle(dr, tx, ty, tx+1, top, tilesize-1, bottom-top, tilesize);
		}
		
		if(cshow && ui->cpencil && ui->cx == x && ui->cy == y)
			draw_triangle(dr, tx, ty, tilesize);
		
		if(!puzzle->walls[y*w+x] && !state->grid[y*w+x])
		{
			/* Draw pencil marks */
			int nhints, i, j, hw, hh, hmax, fontsz;
			for (i = nhints = 0; i < 9; i++) {
				if (state->marks[y*w+x] & (1<<i)) nhints++;
			}

			for (hw = 1; hw * hw < nhints; hw++);
			
			if (hw < 3) hw = 3;
			hh = (nhints + hw - 1) / hw;
			if (hh < 2) hh = 2;
			hmax = max(hw, hh);
			fontsz = tilesize/(hmax*(11-hmax)/8);

			for (i = j = 0; i < 9; i++)
			{
				if (state->marks[y*w+x] & (1<<i))
				{
					int hx = j % hw, hy = j / hw;
					color = COL_NUM1_L + (i*3);
					buf[0] = i+'1';
					
					draw_text(dr,
						tx + (4*hx+3) * tilesize / (4*hw+2),
						ty + (4*hy+3) * tilesize / (4*hh+2),
						FONT_VARIABLE, fontsz,
						ALIGN_VCENTRE | ALIGN_HCENTRE, color, buf);
					j++;
				}
			}
		}
		
		/* Keyboard cursor */
		if(cshow && !ui->cpencil && ui->ckey && ui->cx == x && ui->cy == y)
			draw_rect_corners(dr, (1+x)*tilesize, (1+y)*tilesize, tilesize*0.35, COL_HIGHLIGHT);
		
		draw_rect_outline(dr, tx, ty, tilesize+1, tilesize+1, COL_GRID);
	}
	
	draw_numbers(dr, ds, w, h, puzzle->numbers, puzzle->numcount);
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

static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int ts)
{
}

#ifdef COMBINED
#define thegame crossing
#endif

const struct game thegame = {
	"Crossing", NULL, NULL,
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
	40, game_compute_size, game_set_size,
	game_colours,
	game_new_drawstate,
	game_free_drawstate,
	game_redraw,
	game_anim_length,
	game_flash_length,
	game_get_cursor_location,
	game_status,
	false, false, game_print_size, game_print,
	false,                 /* wants_statusbar */
	false, game_timing_state,
	REQUIRE_RBUTTON, /* flags */
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

		printf("\nGame ID: %s\n", desc_gen);
	} else {
		game_state *input, *solved;
		char *move;
		const char *error = NULL;

		err = validate_desc(params, desc);
		if (err) {
			fprintf(stderr, "Description is invalid\n");
			fprintf(stderr, "%s", err);
			exit(1);
		}

		input = new_game(NULL, params, desc);
		
		move = solve_game(input, NULL, NULL, &error);
		solved = execute_move(input, move);

		char *fmt = game_text_format(solved);
		fputs(fmt, stdout);
		sfree(fmt);
		sfree(move);

		free_game(input);
		free_game(solved);
	}

	return 0;
}
#endif
