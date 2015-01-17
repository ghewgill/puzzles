/*
 * spokes.c: Implementation of Spokes Puzzle
 * (C) 2014 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective: Draw lines to connect every hub into one group.
 * Lines can only be drawn between two points on two hubs.
 * The number on each hub indicates the number of connections.
 * Diagonal lines cannot cross.
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
	COL_BORDER,
	COL_HOLDING,
	COL_LINE,
	COL_MARK,
	COL_DONE,
	COL_ERROR,
	COL_CURSOR,
    NCOLOURS
};

#define DIFFLIST(A)                             \
	A(EASY,Easy, e)                             \
	A(TRICKY,Tricky, t)                         \

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const spokes_diffnames[] = { DIFFLIST(TITLE) };

static char const spokes_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

struct game_params {
    int w, h, diff;
};

#define DEFAULT_PRESET 2
const struct game_params spokes_presets[] = {
	{4, 4, DIFF_EASY},
	{4, 4, DIFF_TRICKY},
	{6, 6, DIFF_EASY},
	{6, 6, DIFF_TRICKY},
};

/* A hub consists of eight spokes, each two bits in size */
typedef unsigned int hub_t;
#define SPOKE_MASK   3

/* NOTE: Changing the SPOKE_ or DIR_ values will break existing savefiles. */

/* No line is possible */
#define SPOKE_HIDDEN 0
/* Default state. A line or mark can be placed */
#define SPOKE_EMPTY  1
/* Connected to another hub */
#define SPOKE_LINE   2
/* A line was ruled out by the user or solver */
#define SPOKE_MARKED 3

#define GET_SPOKE(s,d) ( ((s) >> ((d)*2)) & SPOKE_MASK )
#define SET_SPOKE(s,d,v) do { (s) &= ~(SPOKE_MASK << ((d)*2)); (s) |= (v) << ((d)*2); } while(FALSE)

/* Define directions */
#define INV_DIR(d) ( (d) ^ 4 )
enum {
	DIR_RIGHT, DIR_BOTRIGHT, DIR_BOT, DIR_BOTLEFT, 
	DIR_LEFT, DIR_TOPLEFT, DIR_TOP, DIR_TOPRIGHT
};

#define SPOKES_DEFAULT 0x5555;

struct spoke_dir {
	int dx, dy;
	/* Position of spoke for drawing purposes */
	float rx, ry;
};

#define SQRTHALF 0.70710678118654752440084436210485F
const struct spoke_dir spoke_dirs[] = {
	{  1,  0,  1,  0 },
	{  1,  1,  SQRTHALF,  SQRTHALF },
	{  0,  1,  0,  1 },
	{ -1,  1, -SQRTHALF,  SQRTHALF },
	{ -1,  0, -1,  0 },
	{ -1, -1, -SQRTHALF, -SQRTHALF },
	{  0, -1,  0, -1 },
	{  1, -1,  SQRTHALF, -SQRTHALF },
};

struct game_state {
    int w, h;
	
	char *numbers;
	
	hub_t *spokes;
	
	char completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);
    *ret = spokes_presets[DEFAULT_PRESET];
    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
	if (i < 0 || i >= lenof(spokes_presets))
		return FALSE;
		
	game_params *ret = snew(game_params);
	*ret = spokes_presets[i]; /* struct copy */
	*params = ret;
	
	char buf[80];
	sprintf(buf, "%dx%d %s", ret->w, ret->h, spokes_diffnames[ret->diff]);
	*name = dupstr(buf);
	
	return TRUE;
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
	
	/* Difficulty */
	if (*p == 'd') {
        int i;
        p++;
        params->diff = DIFFCOUNT + 1;   /* ...which is invalid */
        if (*p) {
            for (i = 0; i < DIFFCOUNT; i++) {
                if (*p == spokes_diffchars[i])
                    params->diff = i;
            }
            p++;
        }
    }
}

static char *encode_params(const game_params *params, int full)
{
    char buf[80];
	char *p = buf;
	p += sprintf(p, "%dx%d", params->w, params->h);
	if(full)
		p += sprintf(p, "d%c", spokes_diffchars[params->diff]);
	
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
	ret[0].sval = dupstr(buf);
	ret[0].ival = 0;
	
	ret[1].name = "Height";
	ret[1].type = C_STRING;
	sprintf(buf, "%d", params->h);
	ret[1].sval = dupstr(buf);
	ret[1].ival = 0;
	
	ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].sval = DIFFCONFIG;
    ret[2].ival = params->diff;
	
	ret[3].name = NULL;
	ret[3].type = C_END;
	ret[3].sval = NULL;
	ret[3].ival = 0;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);
	
	ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
	ret->diff = cfg[2].ival;
	
	return ret;
}

static char *validate_params(const game_params *params, int full)
{
	if(params->w < 2)
		return "Width must be at least 2";
	if(params->h < 2)
		return "Height must be at least 2";
	
    return NULL;
}

static char *validate_desc(const game_params *params, const char *desc)
{
	int i;
	int s = params->w * params->h;
	
	for(i = 0; i < s; i++)
	{
		if(!desc[i])
			return "Description too short";
		
		if((desc[i] >= '0' && desc[i] <= '8') || desc[i] == 'X')
			continue;
		return "Invalid character in description";
	}
	
	if(desc[s])
		return "Description too long";
	
    return NULL;
}

static game_state *blank_game(const game_params *params, game_state *previous)
{
	int w = params->w;
	int h = params->h;
	int i, x, y;
    game_state *state;

	if(!previous)
	{
		state = snew(game_state);
		state->numbers = snewn(w*h, char);
		state->spokes = snewn(w*h, hub_t);
	}
	else
		state = previous;
	
    state->w = w;
    state->h = h;
	
	state->completed = state->cheated = FALSE;
	
	for(i = 0; i < w*h; i++)
	{
		state->numbers[i] = 8;
		state->spokes[i] = SPOKES_DEFAULT;
	}
	
	/* Remove top and bottom spokes */
	for(x = 0; x < w; x++)
	{
		SET_SPOKE(state->spokes[x], DIR_TOPLEFT, SPOKE_HIDDEN);
		SET_SPOKE(state->spokes[x], DIR_TOP, SPOKE_HIDDEN);
		SET_SPOKE(state->spokes[x], DIR_TOPRIGHT, SPOKE_HIDDEN);
		
		SET_SPOKE(state->spokes[x + (w*(h-1))], DIR_BOTLEFT, SPOKE_HIDDEN);
		SET_SPOKE(state->spokes[x + (w*(h-1))], DIR_BOT, SPOKE_HIDDEN);
		SET_SPOKE(state->spokes[x + (w*(h-1))], DIR_BOTRIGHT, SPOKE_HIDDEN);
	}
	
	/* Remove left and right spokes */
	for(y = 0; y < h; y++)
	{
		SET_SPOKE(state->spokes[y*w], DIR_TOPLEFT, SPOKE_HIDDEN);
		SET_SPOKE(state->spokes[y*w], DIR_LEFT, SPOKE_HIDDEN);
		SET_SPOKE(state->spokes[y*w], DIR_BOTLEFT, SPOKE_HIDDEN);
		
		SET_SPOKE(state->spokes[y*w+(w-1)], DIR_TOPRIGHT, SPOKE_HIDDEN);
		SET_SPOKE(state->spokes[y*w+(w-1)], DIR_RIGHT, SPOKE_HIDDEN);
		SET_SPOKE(state->spokes[y*w+(w-1)], DIR_BOTRIGHT, SPOKE_HIDDEN);
	}
	
	return state;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
	int i, x, y, d, dx, dy;
	int w = params->w;
	int h = params->h;
	
	game_state *state = blank_game(params, NULL);
	
	for(i = 0; i < w*h; i++)
		state->numbers[i] = desc[i] >= '0' && desc[i] <= '8' ? desc[i] - '0' : -1;
	
	/* Remove spokes connecting to nothing */
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		if(state->numbers[y*w+x] > 0)
			continue;
		
		state->spokes[y*w+x] = 0;
		
		for(d = 0; d < 8; d++)
		{
			dx = x + spoke_dirs[d].dx;
			dy = y + spoke_dirs[d].dy;
			
			if(dx < 0 || dx >= w || dy < 0 || dy >= h) continue;
			SET_SPOKE(state->spokes[dy*w+dx], INV_DIR(d), SPOKE_HIDDEN);
		}
		/* Certain holes do not allow diagonal lines to go near it */
		if(state->numbers[y*w+x] != 0)
		{
			if(x > 0)
			{
				SET_SPOKE(state->spokes[y*w+x-1], DIR_TOPRIGHT, SPOKE_HIDDEN);
				SET_SPOKE(state->spokes[y*w+x-1], DIR_BOTRIGHT, SPOKE_HIDDEN);
			}
			if(x < w-1)
			{
				SET_SPOKE(state->spokes[y*w+x+1], DIR_TOPLEFT, SPOKE_HIDDEN);
				SET_SPOKE(state->spokes[y*w+x+1], DIR_BOTLEFT, SPOKE_HIDDEN);
			}
			if(y > 0)
			{
				SET_SPOKE(state->spokes[(y-1)*w+x], DIR_BOTLEFT, SPOKE_HIDDEN);
				SET_SPOKE(state->spokes[(y-1)*w+x], DIR_BOTRIGHT, SPOKE_HIDDEN);
			}
			if(y < h-1)
			{
				SET_SPOKE(state->spokes[(y+1)*w+x], DIR_TOPLEFT, SPOKE_HIDDEN);
				SET_SPOKE(state->spokes[(y+1)*w+x], DIR_TOPRIGHT, SPOKE_HIDDEN);
			}
			state->numbers[y*w+x] = 0;
		}
	}
    return state;
}

static game_state *duplicate_game(const game_state *state, game_state *previous)
{
	int w = state->w;
	int h = state->h;
    game_state *ret;
	if(!previous)
	{
		ret = snew(game_state);
		ret->numbers = snewn(w*h, char);
		ret->spokes = snewn(w*h, hub_t);
	}
	else
		ret = previous;
	
    ret->w = w;
    ret->h = h;
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;
	
	memcpy(ret->numbers, state->numbers, w*h*sizeof(char));
	memcpy(ret->spokes, state->spokes, w*h*sizeof(hub_t));

    return ret;
}

static game_state *dup_game(const game_state *state)
{
	return duplicate_game(state, NULL);
}

static void free_game(game_state *state)
{
    sfree(state->numbers);
    sfree(state->spokes);
    sfree(state);
}

static int spokes_place(game_state *state, int i, int dir, int s)
{
	int x, y;
	int w = state->w;
	SET_SPOKE(state->spokes[i], dir, s);
	
	x = (i%w) + spoke_dirs[dir].dx;
	y = (i/w) + spoke_dirs[dir].dy;
		
	if(x >= 0 && x < w && y >= 0 && y < state->h)
		SET_SPOKE(state->spokes[y*w+x], INV_DIR(dir), s);
	
	return 1;
}

static int spokes_count(hub_t hub, int s)
{
	int ret = 0;
	int j;
	
	for(j = 0; j < 8; j++)
		if(GET_SPOKE(hub, j) == s) ret++;
	
	return ret;
}

struct spokes_scratch {
	int *nodes;
	int *lines;
	int *marked;
	
	int *dsf;
};

static void spokes_solver_recount(const game_state *state, struct spokes_scratch *solver)
{
	int i, j, x, y;
	int w = state->w;
	int h = state->h;
	hub_t hub;
	for(i = 0; i < w*h; i++)
	{
		hub = state->spokes[i];
		solver->nodes[i] = 8 - spokes_count(hub, SPOKE_HIDDEN);
		solver->lines[i] = spokes_count(hub, SPOKE_LINE);
		solver->marked[i] = spokes_count(hub, SPOKE_MARKED);
	}
	
	dsf_init(solver->dsf, w*h);
	
	/* If the top-left square is empty, connect it to the first non-empty square. */
	if(!state->numbers[0])
	{
		for(i = 1; i < w*h; i++)
		{
			if(state->numbers[i])
			{
				dsf_merge(solver->dsf, i, 0);
				break;
			}
		}
	}
	
	for(i = 0; i < w*h; i++)
	{
		/* If the number is 0, join this with the top-left square.
		 * This ensures that the puzzle is always solved when there is
		 * one set of w*h squares, even when some of these squares are empty.
		 */
		if(!state->numbers[i])
			dsf_merge(solver->dsf, i, 0);
		else
		{
			for(j = 0; j < 4; j++)
			{
				x = (i%w) + spoke_dirs[j].dx;
				y = (i/w) + spoke_dirs[j].dy;
				
				if(x >= 0 && x < w && y >= 0 && y < h && 
						GET_SPOKE(state->spokes[i], j) == SPOKE_LINE)
					dsf_merge(solver->dsf, i, y*w+x);
			}
		}
	}
}

static int spokes_solver_full(game_state *state, struct spokes_scratch *solver)
{
	int ret = 0;
	int i, j;
	int s = state->w*state->h;
	
	for(i = 0; i < s; i++)
	{
		/* All lines on a hub should be filled */
		if(solver->nodes[i] - solver->marked[i] == state->numbers[i])
		{
			for(j = 0; j < 8; j++)
				if(GET_SPOKE(state->spokes[i], j) == SPOKE_EMPTY)
					ret += spokes_place(state, i, j, SPOKE_LINE);
		}
		
		if(solver->lines[i] == state->numbers[i])
		{
			/* No more lines can be placed here, mark the rest of the spokes */
			for(j = 0; j < 8; j++)
				if(GET_SPOKE(state->spokes[i], j) == SPOKE_EMPTY)
					ret += spokes_place(state, i, j, SPOKE_MARKED);
		}
	}
	
	return ret;
}

static int spokes_solver_diagonal(game_state *state)
{
	int ret = 0;
	int x, y;
	int w = state->w;
	int h = state->h;
	
	/* Mark the spokes that would intersect with existing lines. */
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w-1; x++)
	{
		if(GET_SPOKE(state->spokes[y*w+x], DIR_BOTRIGHT) == SPOKE_LINE && 
				GET_SPOKE(state->spokes[y*w+x+1], DIR_BOTLEFT) == SPOKE_EMPTY)
			ret += spokes_place(state, y*w+x+1, DIR_BOTLEFT, SPOKE_MARKED);
		
		if(GET_SPOKE(state->spokes[y*w+x], DIR_BOTRIGHT) == SPOKE_EMPTY && 
				GET_SPOKE(state->spokes[y*w+x+1], DIR_BOTLEFT) == SPOKE_LINE)
			ret += spokes_place(state, y*w+x, DIR_BOTRIGHT, SPOKE_MARKED);
	}
	
	return ret;
}

static int spokes_solver_ones(game_state *state)
{
	/* Connecting two 1's would lead to a group of 2 hubs with no possibility
	 * of connecting the rest. Mark every spoke connecting two 1's. */
	
	int ret = 0;
	int count = 0;
	int w = state->w;
	int h = state->h;
	int x, y, j, dx, dy;
	
	/* Don't run this if the grid consists of exactly two hubs */
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		if(state->numbers[y*w+x]) count++;
	}
	
	if(count == 2) return 0;
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		if(state->numbers[y*w+x] != 1) continue;
		
		for(j = 0; j < 4; j++)
		{
			dx = x + spoke_dirs[j].dx;
			dy = y + spoke_dirs[j].dy;
			
			if(dx < 0 || dx >= w || dy < 0 || dy >= h) 
				continue;
			
			if(state->numbers[dy*w+dx] == 1)
				ret += spokes_place(state, y*w+x, j, SPOKE_MARKED);
		}
	}
	
	return ret;
}

enum { STATUS_INVALID, STATUS_INCOMPLETE, STATUS_VALID };
static int spokes_solve(game_state *state, int diff);

static int spokes_solver_attempt(game_state *state, int diff)
{
	int ret = 0;
	int i, dir, l, w = state->w, h = state->h;
	game_state *copy = dup_game(state);
	
	for(i = 0; i < w*h; i++)
	{
		for(dir = 0; dir < 8; dir++)
		{
			for(l = 0; l < 2; l++)
			{
				if(GET_SPOKE(state->spokes[i], dir) != SPOKE_EMPTY)
					continue;
				
				duplicate_game(state, copy);
				
				spokes_place(copy, i, dir, l ? SPOKE_LINE : SPOKE_MARKED);
				if(spokes_solve(copy, diff) == STATUS_INVALID)
					ret += spokes_place(state, i, dir, l ? SPOKE_MARKED : SPOKE_LINE);
			}
		}
	}
	free_game(copy);
	return ret;
}

static struct spokes_scratch *spokes_new_scratch(const game_state *state)
{
	int w = state->w;
	int h = state->h;
	struct spokes_scratch *solver = snew(struct spokes_scratch);
	
	solver->nodes = snewn(w*h, int);
	solver->lines = snewn(w*h, int);
	solver->marked = snewn(w*h, int);
	solver->dsf = snewn(w*h, int);
	
	return solver;
}

static void spokes_free_scratch(struct spokes_scratch *solver)
{
	sfree(solver->nodes);
	sfree(solver->lines);
	sfree(solver->marked);
	sfree(solver->dsf);
	sfree(solver);
}

static void spokes_find_isolated(const game_state *state, struct spokes_scratch *solver, int *open)
{
	/* For each set, count the possible lines that can be drawn
	 * from this set to other hubs.
	 */
	int i, w = state->w, h = state->h;
	
	memset(open, 0, w*h*sizeof(int));
	
	for(i = 0; i < w*h; i++)
	{
		open[dsf_canonify(solver->dsf, i)] += state->numbers[i] - solver->lines[i];
	}
}

static int spokes_validate(game_state *state, struct spokes_scratch *solver)
{
	int i, w = state->w, h = state->h;
	int ret = STATUS_VALID;
	char hassolver = solver != NULL;
	if(!hassolver)
		solver = spokes_new_scratch(state);
	
	spokes_solver_recount(state, solver);
	
	for(i = 0; i < w*h && ret != STATUS_INVALID; i++)
	{
		if(solver->lines[i] < state->numbers[i])
			ret = STATUS_INCOMPLETE;
		
		/* Check if there are too many marks on one hub */
		if(solver->marked[i] > solver->nodes[i] - state->numbers[i])
			ret = STATUS_INVALID;
		/* Check if there are too many lines on one hub */
		if(solver->lines[i] > state->numbers[i])
			ret = STATUS_INVALID;
	}
	
	for(i = 0; i < w*h && ret != STATUS_INVALID; i++)
	{
		/* Check for intersecting lines */
		if(GET_SPOKE(state->spokes[i], DIR_BOTRIGHT) == SPOKE_LINE && 
				GET_SPOKE(state->spokes[i+1], DIR_BOTLEFT) == SPOKE_LINE)
			ret = STATUS_INVALID;
	}
	
	if(ret != STATUS_INVALID)
	{
		int *counts = snewn(w*h, int);
		spokes_find_isolated(state, solver, counts);
		
		/* Check if there is a set with no possibility for more lines */
		for(i = 0; i < w*h && ret != STATUS_INVALID; i++)
		{
			if(counts[i] == 0 && dsf_canonify(solver->dsf, i) == i && dsf_size(solver->dsf, i) < w*h)
				ret = STATUS_INVALID;
		}
		
		sfree(counts);
	}
	
	if(!hassolver)
		spokes_free_scratch(solver);
	
	return ret;
}

static int spokes_solve(game_state *state, int diff)
{
	struct spokes_scratch *solver = spokes_new_scratch(state);
	int ret;
	
	spokes_solver_ones(state);
	
	while(TRUE)
	{
		if(spokes_validate(state, solver) != STATUS_INCOMPLETE)
			break;
		
		if(spokes_solver_full(state, solver))
			continue;
		if(spokes_solver_diagonal(state))
			continue;
		
		if(diff < DIFF_TRICKY)
			break;
		
		if(spokes_solver_attempt(state, DIFF_EASY))
			continue;
		
		break;
	}
	
	ret = spokes_validate(state, solver);
	
	spokes_free_scratch(solver);
	
	return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
{
	int w = state->w;
	int h = state->h;
	int i, d;
	char *buf, *ret, *p;
	
	game_state *solved = dup_game(state);
	
	spokes_solve(solved, DIFFCOUNT);
	
	buf = snewn(3+(w*h*80), char);
	p = buf;
	
	p += sprintf(p, "S;");
	
	for(i = 0; i < w*h; i++)
	{
		for(d = 0; d < 4; d++)
		{
			if(GET_SPOKE(solved->spokes[i], d) == SPOKE_LINE)
				p += sprintf(p, "%d,%d,%d;", i, d, SPOKE_LINE);
			if(GET_SPOKE(solved->spokes[i], d) == SPOKE_MARKED)
				p += sprintf(p, "%d,%d,%d;", i, d, SPOKE_MARKED);
		}
	}
	
	ret = dupstr(buf);
	free_game(solved);
	sfree(buf);
    return ret;
}

#define ORTHOGONAL_LINE_ODDS 4
#define DIAGONAL_LINE_ODDS 4

/*
 * TODO: The generation process should not discard the entire grid if
 * the puzzle has multiple solutions. Instead, only the ambiguous parts
 * should be regenerated.
 */
static int spokes_generate(const game_params *params, game_state *state, random_state *rs)
{
	int w = params->w, h = params->h, diff = params->diff;
	int i, x, y, r, d, dx, dy;
	blank_game(params, state);
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w-1; x++)
	{
		r = random_upto(rs, ORTHOGONAL_LINE_ODDS);
		if(r)
			spokes_place(state, y*w+x, DIR_RIGHT, SPOKE_LINE);
	}
	
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w; x++)
	{
		r = random_upto(rs, ORTHOGONAL_LINE_ODDS);
		if(r)
			spokes_place(state, y*w+x, DIR_BOT, SPOKE_LINE);
	}
	
	for(y = 0; y < h-1; y++)
	for(x = 0; x < w-1; x++)
	{
		if(!random_upto(rs, DIAGONAL_LINE_ODDS))
			continue;
		
		if(random_upto(rs, 2))
			spokes_place(state, y*w+x, DIR_BOTRIGHT, SPOKE_LINE);
		else
			spokes_place(state, y*w+x+1, DIR_BOTLEFT, SPOKE_LINE);
	}
	
	for(i = 0; i < w*h; i++)
		state->numbers[i] = spokes_count(state->spokes[i], SPOKE_LINE);
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		if(state->numbers[y*w+x])
		{
			for(d = 0; d < 8; d++)
			{
				if(GET_SPOKE(state->spokes[y*w+x], d) == SPOKE_LINE)
					SET_SPOKE(state->spokes[y*w+x], d, SPOKE_EMPTY);
			}
		}
		else
		{
			for(d = 0; d < 8; d++)
			{
				dx = x + spoke_dirs[d].dx;
				dy = y + spoke_dirs[d].dy;
				
				if(dx < 0 || dx >= w || dy < 0 || dy >= h) continue;
				SET_SPOKE(state->spokes[dy*w+dx], INV_DIR(d), SPOKE_HIDDEN);
			}
		}
	}
	
	return spokes_solve(state, diff) == STATUS_VALID;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, int interactive)
{
	char *ret;
	int w = params->w, h = params->h;
	int i;
	game_state *state = snew(game_state);
	state->numbers = snewn(w*h, char);
	state->spokes = snewn(w*h, hub_t);
	
	while(!spokes_generate(params, state, rs));
	
	ret = snewn(w*h + 1, char);
	for(i = 0; i < w*h; i++)
	{
		ret[i] = state->numbers[i] + '0';
	}
	ret[w*h] = '\0';
	
	free_game(state);
	
    return ret;
}

static int game_can_format_as_text_now(const game_params *params)
{
    return TRUE;
}

static char *game_text_format(const game_state *state)
{
    return NULL;
}

enum { DRAG_NONE, DRAG_LEFT, DRAG_RIGHT };

struct game_ui {
	int drag_start;
	int drag_end;
	int drag;
	
	char cshow;
	int cx, cy;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ui = snew(game_ui);
	
	ui->drag_start = -1;
	ui->drag_end = -1;
	ui->drag = DRAG_NONE;
	ui->cshow = FALSE;
	ui->cx = ui->cy = 0;
	
    return ui;
}

static void free_ui(game_ui *ui)
{
	sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

struct game_drawstate {
    int tilesize;
	int *isolated;
	struct spokes_scratch *scratch;
};

#define FROMCOORD(x) ( ((x) - (tilesize/2)) / tilesize )
#define TOCOORD(x) ( ((x) * tilesize) + (tilesize) )

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int ox, int oy, int button)
{
	int tilesize = ds->tilesize;
	int x = FROMCOORD(ox);
	int y = FROMCOORD(oy);
	int w = state->w;
	int h = state->h;
	int sx, sy, dx, dy, dir;
	int from = -1, to = -1, drag = DRAG_NONE;
	float angle;
	
	if(ox < tilesize/2) x = -1;
	if(oy < tilesize/2) y = -1;
	
	if(button == LEFT_BUTTON || button == RIGHT_BUTTON)
	{
		if(x < 0 || x >= w || y < 0 || y >= h)
			return NULL;
		
		ui->drag_start = y*w+x;
		ui->drag = button == LEFT_BUTTON ? DRAG_LEFT : DRAG_RIGHT;
		ui->cshow = FALSE;
	}
	if(button == LEFT_BUTTON || button == RIGHT_BUTTON || 
		button == LEFT_DRAG || button == RIGHT_DRAG)
	{
		if(ui->drag_start == -1)
			return NULL;
		
		sx = ui->drag_start % w;
		sy = ui->drag_start / w;
		
		dx = ox - TOCOORD(sx);
		dy = oy - TOCOORD(sy);
		
		angle = atan2(dy, dx);
		angle = (angle + (PI/8)) / (PI/4);
	    assert(angle > -16.0F);
	    dir = (int)(angle + 16.0F) & 7;
		
		x = sx+spoke_dirs[dir].dx;
		y = sy+spoke_dirs[dir].dy;
		
		if(x < 0 || x >= w || y < 0 || y >= h || 
			(dx*dx)+(dy*dy) < (tilesize*tilesize) / 22 )
			ui->drag_end = -1;
		else
			ui->drag_end = y*w+x;
		return "";
	}
	if(button == LEFT_RELEASE || button == RIGHT_RELEASE)
	{
		from = ui->drag_start;
		to = ui->drag_end;
		drag = ui->drag;
		ui->drag_start = -1;
		ui->drag_end = -1;
		ui->drag = DRAG_NONE;
	}
	
	if(ui->cshow && (button == CURSOR_SELECT || button == CURSOR_SELECT2))
	{
		int cx = (ui->cx + 1)/3;
		int cy = (ui->cy + 1)/3;
		dx = ((ui->cx + 1)%3)-1;
		dy = ((ui->cy + 1)%3)-1;
		
		from = cy*w+cx;
		to = from+(dy*w+dx);
		drag = button == CURSOR_SELECT ? DRAG_LEFT : DRAG_RIGHT;
	}
	
	if(drag != DRAG_NONE)
	{
		if(from == -1 || to == -1)
			return "";
		
		char buf[80];
		int old, new;
		int start = min(from, to);
		int end = max(from, to);
		
		sx = start % w;
		sy = start / w;
	
		for(dir = 0; dir < 4; dir++)
		{
			if((sy+spoke_dirs[dir].dy)*w+sx+spoke_dirs[dir].dx != end)
				continue;
			old = GET_SPOKE(state->spokes[start], dir);
			if(old == SPOKE_HIDDEN)
				continue;
			
			if(drag == DRAG_LEFT)
				new = old == SPOKE_EMPTY ? SPOKE_LINE : SPOKE_EMPTY;
			else
				new = old == SPOKE_EMPTY ? SPOKE_MARKED : SPOKE_EMPTY;
			
			/* Don't allow diagonal lines to cross */
			if(new == SPOKE_LINE && dir == DIR_BOTLEFT && 
					GET_SPOKE(state->spokes[start-1], DIR_BOTRIGHT) == SPOKE_LINE)
				continue;
			if(new == SPOKE_LINE && dir == DIR_BOTRIGHT && 
					GET_SPOKE(state->spokes[start+1], DIR_BOTLEFT) == SPOKE_LINE)
				continue;
			
			sprintf(buf, "%d,%d,%d", start, dir, new);
			
			return dupstr(buf);
		}
		return "";
	}
	
	if(IS_CURSOR_MOVE(button))
	{
		move_cursor(button, &ui->cx, &ui->cy, w*3-2, h*3-2, 0);
		ui->cshow = TRUE;
		return "";
	}
	
    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	const char *p = move;
	game_state *ret = NULL;
	int i, j, d, s;
	char cheated = FALSE;
	
	while(*p)
	{
		if(*p == 'S')
		{
			if(!ret) ret = dup_game(state);
			
			for(i = 0; i < (ret->w * ret->h); i++)
			{
				if(ret->spokes[i] == 0) continue;
				for(j = 0; j < 4; j++)
				{
					if(GET_SPOKE(ret->spokes[i], j) != SPOKE_HIDDEN)
						spokes_place(ret, i, j, SPOKE_EMPTY);
				}
			}
			
			cheated = TRUE;
		}
		
		else if(sscanf(p, "%d,%d,%d", &i, &d, &s) == 3)
		{
			if(d < 0 || d > 7 || s < 0 || s > 3) break;
			
			if(!ret) ret = dup_game(state);
			
			if(GET_SPOKE(ret->spokes[i], d) != SPOKE_HIDDEN)
				spokes_place(ret, i, d, s);
		}
		while(*p && *p++ != ';');
	}
	
	if(ret && spokes_validate(ret, NULL) == STATUS_VALID)
		ret->completed = TRUE;
	else
		/* Don't mark a game as cheated if the solver didn't complete the grid */
		cheated = FALSE;
	
	if(cheated) ret->cheated = TRUE;
	
    return ret;
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
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

	ret[COL_BORDER*3 + 0] = 0.3F;
	ret[COL_BORDER*3 + 1] = 0.3F;
	ret[COL_BORDER*3 + 2] = 0.3F;
	
	ret[COL_LINE*3 + 0] = 0;
	ret[COL_LINE*3 + 1] = 0;
	ret[COL_LINE*3 + 2] = 0;
	
	ret[COL_HOLDING*3 + 0] = 0;
	ret[COL_HOLDING*3 + 1] = 1.0F;
	ret[COL_HOLDING*3 + 2] = 0;
	
	ret[COL_MARK*3 + 0] = 0.3F;
	ret[COL_MARK*3 + 1] = 0.3F;
	ret[COL_MARK*3 + 2] = 1.0F;
	
	ret[COL_DONE*3 + 0] = 1.0F;
	ret[COL_DONE*3 + 1] = 1.0F;
	ret[COL_DONE*3 + 2] = 1.0F;
	
	ret[COL_ERROR*3 + 0] = 1.0F;
	ret[COL_ERROR*3 + 1] = 0;
	ret[COL_ERROR*3 + 2] = 0;
	
	ret[COL_CURSOR*3 + 0] = 0;
	ret[COL_CURSOR*3 + 1] = 0;
	ret[COL_CURSOR*3 + 2] = 1.0F;
	
    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
	ds->isolated = snewn(state->w * state->h, int);
    ds->scratch = spokes_new_scratch(state);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->isolated);
    sfree(ds->scratch);
    sfree(ds);
}

static void spokes_draw_hub(drawing *dr, int tx, int ty, float radius, 
		float thick, hub_t hub, int border, int fill, int mark)
{
	float edge = radius-thick;
	float pr = radius/4;
	int d, c;
	int px, py;
	
	draw_circle(dr, tx, ty, radius, border, border);
	
	for(d = 0; d < 8; d++)
	{
		if(GET_SPOKE(hub, d) != SPOKE_HIDDEN)
		{
			px = tx+(edge*spoke_dirs[d].rx);
			py = ty+(edge*spoke_dirs[d].ry);
			draw_circle(dr, px, py, pr+thick, border, border);
			
			c = GET_SPOKE(hub, d) == SPOKE_MARKED ? mark : fill;
			draw_circle(dr, px, py, pr, c, c);
		}
	}
	
	draw_circle(dr, tx, ty, edge, fill, fill);
}

#define FLASH_FRAME 0.12F
#define FLASH_TIME (FLASH_FRAME * 5)

/* TODO: Optimize drawing routines */
static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
	int w = state->w;
	int h = state->h;
	int tilesize = ds->tilesize;
	int i, x, y, d, tx, ty, connected;
	int cx = (ui->cx + 1)/3;
	int cy = (ui->cy + 1)/3;
	int dx = (ui->cx + 1)%3;
	int dy = (ui->cy + 1)%3;
	char flash, cshow;
	char buf[2];
	int fill, border, txt, lines;
	buf[1] = '\0';
	
	if(flashtime > 0)
		flash = (int)(flashtime/FLASH_FRAME) & 1;
	else
		flash = FALSE;
	cshow = ui->cshow && !flashtime;
	
	double thick = (tilesize <= 21 ? 1 : 2);
	float radius = tilesize/3.5F;
	
    draw_rect(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize, COL_BACKGROUND);
    draw_update(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize);
	
	spokes_solver_recount(state, ds->scratch);
	spokes_find_isolated(state, ds->scratch, ds->isolated);
	
	connected = dsf_size(ds->scratch->dsf, 0) == w*h;
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i = y*w+x;
		if(state->spokes[i])
		{
			tx = (x+1)*tilesize;
			ty = (y+1)*tilesize;
			for(d = 0; d < 4; d++)
			{
				if(GET_SPOKE(state->spokes[y*w+x], d) == SPOKE_LINE)
					draw_thick_line(dr, thick,
						tx, ty, 
						tx + (spoke_dirs[d].dx * tilesize),
						ty + (spoke_dirs[d].dy * tilesize),
						COL_LINE);
			}
			
			lines = ds->scratch->lines[i];
			
			fill = lines == state->numbers[i] ? COL_DONE : COL_BACKGROUND;
			border = flash ? COL_DONE : i == ui->drag_start || i == ui->drag_end ? COL_HOLDING : 
				!connected && !ds->isolated[dsf_canonify(ds->scratch->dsf, i)] ? COL_ERROR : COL_BORDER;
			spokes_draw_hub(dr, tx, ty, radius, thick, state->spokes[i], border, fill, COL_MARK);
			
			buf[0] = state->numbers[i] + '0';
			txt = lines > state->numbers[i] || 
				ds->scratch->marked[i] > ds->scratch->nodes[i] - state->numbers[i] 
				? COL_ERROR : cshow && cx == x && cy == y ? COL_CURSOR : COL_LINE;
			draw_text(dr, tx, ty, FONT_FIXED, tilesize/2.5F, ALIGN_VCENTRE|ALIGN_HCENTRE, txt, buf);
		}
		if(cshow && cx == x && cy == y)
		{
			draw_rect_corners(dr, (cx+((2+dx)/3.0))*tilesize, 
				(cy+((2+dy)/3.0))*tilesize, tilesize*0.2, COL_CURSOR);
		}
	}
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
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

static int game_timing_state(const game_state *state, game_ui *ui)
{
    return TRUE;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
    int pw, ph;

    /* Using 15mm squares. Includes padding */
    game_compute_size(params, 1500, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
	int ink = print_mono_colour(dr, 0);
	int paper = print_mono_colour(dr, 1);
	int w = state->w, h = state->h;
	int x, y, i, tx, ty, d;
	char buf[2];
	buf[1] = '\0';
	
	double thick = 5;
	float radius = tilesize/3.5F;
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i = y*w+x;
		if(!state->spokes[i]) continue;
		
		tx = (x+1)*tilesize;
		ty = (y+1)*tilesize;
		for(d = 0; d < 4; d++)
		{
			if(GET_SPOKE(state->spokes[y*w+x], d) == SPOKE_LINE)
				draw_thick_line(dr, thick,
					tx, ty, 
					tx + (spoke_dirs[d].dx * tilesize),
					ty + (spoke_dirs[d].dy * tilesize),
					ink);
		}
		
		spokes_draw_hub(dr, tx, ty, radius, thick, state->spokes[i], ink, paper, paper);
		
		buf[0] = state->numbers[i] + '0';
		draw_text(dr, tx, ty, FONT_FIXED, tilesize/2.5F, ALIGN_VCENTRE|ALIGN_HCENTRE, ink, buf);
	}
}

#ifdef COMBINED
#define thegame spokes
#endif

const struct game thegame = {
    "Spokes", NULL, NULL,
    default_params,
    game_fetch_preset,
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
    FALSE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    48, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
    TRUE, FALSE, game_print_size, game_print,
    FALSE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    0,				       /* flags */
};
