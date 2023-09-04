/*
 * salad.c: Implemention of ABC End View puzzles.
 * (C) 2013 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * This puzzle has two different game modes: ABC End View and Number Ball.
 * Objective: Enter each letter once in each row and column. 
 * Some squares remain empty.
 * ABC End View:  The letters on the edge indicate which letter
 * is encountered first when 'looking' into the grid.
 * Number Ball: A circle indicates that a number must be placed here,
 * and a cross indicates a space that remains empty.
 *
 * Number Ball was invented by Inaba Naoki. 
 * I don't know who first designed ABC End View.
 *
 * http://www.janko.at/Raetsel/AbcEndView/index.htm
 * http://www.janko.at/Raetsel/Nanbaboru/index.htm
 */

/*
 * TODO:
 * - Add difficulty levels
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "latin.h"

enum { GAMEMODE_LETTERS, GAMEMODE_NUMBERS };

enum {
	COL_BACKGROUND,
	COL_HIGHLIGHT,
	COL_LOWLIGHT,
	COL_BORDER,
	COL_BORDERCLUE,
	COL_PENCIL,
	COL_I_NUM, /* Immutable */
	COL_I_BALL,
	COL_I_BALLBG,
	COL_I_HOLE,
	COL_G_NUM, /* Guess */
	COL_G_BALL,
	COL_G_BALLBG,
	COL_G_HOLE,
	COL_E_BORDERCLUE,
	COL_E_NUM, /* Error */
	COL_E_HOLE,
	NCOLOURS
};

#define DIFFLIST(A) \
	A(EASY,Normal,salad_solver_easy, e) \
	A(HARD,Extreme,NULL,x)
		
#define ENUM(upper,title,func,lower) DIFF_ ## upper,
#define TITLE(upper,title,func,lower) #title,
#define ENCODE(upper,title,func,lower) #lower
#define CONFIG(upper,title,func,lower) ":" #title
static char const salad_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCOUNT lenof(salad_diffchars)

enum { DIFFLIST(ENUM) DIFF_IMPOSSIBLE = diff_impossible,
	DIFF_AMBIGUOUS = diff_ambiguous, DIFF_UNFINISHED = diff_unfinished };

#define DIFF_HOLESONLY (DIFF_EASY - 1)

struct game_params {
	int order;
	int nums;
	int mode;
	int diff;
};

struct game_state {
	game_params *params;
	
	digit *borderclues;
	digit *gridclues;
	
	digit *grid;
	
	/* Extra map of confirmed holes/characters */
	char *holes;
	
	bool completed, cheated;
	
	unsigned int *marks;
};

#define DEFAULT_PRESET 0

static const struct game_params salad_presets[] = {
	{4, 3, GAMEMODE_LETTERS, DIFF_EASY},
	{5, 3, GAMEMODE_LETTERS, DIFF_EASY},
	{5, 3, GAMEMODE_NUMBERS, DIFF_EASY},
	{5, 4, GAMEMODE_LETTERS, DIFF_EASY},
	{6, 3, GAMEMODE_NUMBERS, DIFF_EASY},
	{6, 4, GAMEMODE_LETTERS, DIFF_EASY},
	{6, 4, GAMEMODE_NUMBERS, DIFF_EASY},
	{7, 4, GAMEMODE_LETTERS, DIFF_EASY},
	{7, 4, GAMEMODE_NUMBERS, DIFF_EASY},
	{8, 5, GAMEMODE_LETTERS, DIFF_EASY},
	{8, 5, GAMEMODE_NUMBERS, DIFF_EASY},
};

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);

	*ret = salad_presets[DEFAULT_PRESET]; /* struct copy */

	return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
	game_params *ret;
	char buf[64];
	
	if(i < 0 || i >= lenof(salad_presets))
		return false;
	
	ret = snew(game_params);
	*ret = salad_presets[i]; /* struct copy */
	*params = ret;
	
	if(ret->mode == GAMEMODE_LETTERS)
		sprintf(buf, "Letters: %dx%d A~%c", ret->order, ret->order, ret->nums + 'A' - 1);
	else
		sprintf(buf, "Numbers: %dx%d 1~%c", ret->order, ret->order, ret->nums + '0');
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
	*ret = *params; /* structure copy */
	return ret;
}

static void decode_params(game_params *params, char const *string)
{
	char const *p = string;
	params->order = atoi(string);
	while (*p && isdigit((unsigned char)*p)) ++p;

	if (*p == 'n')
	{
		++p;
		params->nums = atoi(p);
		while (*p && isdigit((unsigned char)*p)) ++p;
	}
	
	if (*p == 'B')
	{
		params->mode = GAMEMODE_NUMBERS;
	}
	else if(*p == 'L')
	{
		params->mode = GAMEMODE_LETTERS;
	}
	
	if (*p == 'd') {
		int i;
		p++;
		params->diff = DIFFCOUNT + 1;   /* ...which is invalid */
		if (*p) {
			for (i = 0; i < DIFFCOUNT; i++) {
				if (*p == salad_diffchars[i])
					params->diff = i;
			}
			p++;
		}
	}
}

static char *encode_params(const game_params *params, bool full)
{
	char ret[80];
	sprintf(ret, "%dn%d%c", params->order, params->nums,
		params->mode == GAMEMODE_LETTERS ? 'L' : 'B');
	if (full)
		sprintf(ret + strlen(ret), "d%c", salad_diffchars[params->diff]);
	
	return dupstr(ret);
}

static config_item *game_configure(const game_params *params)
{
	config_item *ret;
	char buf[80];
	
	ret = snewn(5, config_item);
	
	ret[0].name = "Game Mode";
	ret[0].type = C_CHOICES;
	ret[0].u.choices.choicenames = ":ABC End View:Number Ball";
	ret[0].u.choices.selected = params->mode;
	
	ret[1].name = "Size (s*s)";
	ret[1].type = C_STRING;
	sprintf(buf, "%d", params->order);
	ret[1].u.string.sval = dupstr(buf);

	ret[2].name = "Symbols";
	ret[2].type = C_STRING;
	sprintf(buf, "%d", params->nums);
	ret[2].u.string.sval = dupstr(buf);
	
	ret[3].name = "Difficulty";
	ret[3].type = C_CHOICES;
	ret[3].u.choices.choicenames = DIFFLIST(CONFIG);
	ret[3].u.choices.selected = params->diff;
	
	ret[4].name = NULL;
	ret[4].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = default_params();
	
	ret->mode = cfg[0].u.choices.selected;
	ret->order = atoi(cfg[1].u.string.sval);
	ret->nums = atoi(cfg[2].u.string.sval);
	ret->diff = cfg[3].u.choices.selected;
	
	return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
	if(params->nums < 2)
		return "Symbols must be at least 2.";
	if(params->nums >= params->order)
		return "Symbols must be lower than the size.";
	if(params->order < 3)
		return "Size must be at least 3.";
	if(params->nums > 9)
		return "Symbols must be no more than 9.";
	if (params->diff >= DIFFCOUNT)
        return "Unknown difficulty rating";
	
	return NULL;
}

static game_state *blank_game(const game_params *params)
{
	int o = params->order;
	int o2 = o*o;

	game_state *state = snew(game_state);
	
	state->params = snew(game_params);
	*(state->params) = *params;		       /* structure copy */
	state->grid = snewn(o2, digit);
	state->holes = snewn(o2, char);
	state->borderclues = snewn(o*4, digit);
	state->gridclues = snewn(o2, digit);
	state->marks = snewn(o2, unsigned int);
	state->completed = state->cheated = false;
	
	memset(state->marks, 0, o2 * sizeof(unsigned int));
	
	return state;
}

static game_state *dup_game(const game_state *state)
{
	int o = state->params->order;
	int o2 = o*o;
	game_state *ret = blank_game(state->params);

	memcpy(ret->grid, state->grid, o2 * sizeof(digit));
	memcpy(ret->holes, state->holes, o2 * sizeof(char));
	memcpy(ret->borderclues, state->borderclues, o*4 * sizeof(digit));
	memcpy(ret->gridclues, state->gridclues, o2 * sizeof(digit));
	memcpy(ret->marks, state->marks, o2 * sizeof(unsigned int));
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;

	return ret;
}

static void free_game(game_state *state)
{
	free_params(state->params);
	sfree(state->grid);
	sfree(state->holes);
	sfree(state->borderclues);
	sfree(state->gridclues);
	sfree(state->marks);
	sfree(state);
}

/* *********************** *
 * Latin square with holes *
 * *********************** */

/* This square definitely doesn't contain a character */
#define LATINH_CROSS  'X'
/* This square must contain a character */
#define LATINH_CIRCLE 'O'

struct solver_ctx {
	game_state *state;
	int order;
	int nums;
};

static struct solver_ctx *new_ctx(game_state *state, int order, int nums)
{
	struct solver_ctx *ctx = snew(struct solver_ctx);
	ctx->state = state;
	ctx->order = order;
	ctx->nums = nums;
	
	return ctx;
}

static void *clone_ctx(void *vctx)
{
	struct solver_ctx *octx = (struct solver_ctx *)vctx;
	struct solver_ctx *nctx = new_ctx(octx->state, octx->order, octx->nums);
	
	return nctx;
}

static void free_ctx(void *vctx)
{
	struct solver_ctx *ctx = (struct solver_ctx *)vctx;
	sfree(ctx);
}

static int latinholes_solver_sync(struct latin_solver *solver, struct solver_ctx *sctx)
{
	/* Check the marks for each square, and see if it confirms a square
	 * being empty or not empty */
	
	int i, n;
	int o = solver->o;
	int o2 = o*o;
	bool match;
	int nums = sctx->nums;
	int nchanged = 0;
	
	if(nums == o)
		return 0;
	
	for(i = 0; i < o2; i++)
	{
		if(sctx->state->holes[i])
			continue;
		
		/* Check for possibilities for numbers */
		match = false;
		for(n = 0; n < nums; n++)
		{
			if(cube(i%o, i/o, n+1))
				match = true;
		}
		if(!match)
		{
#ifdef STANDALONE_SOLVER
			if(solver_show_working)
				printf("Synchronize hole at %d\n", i);
#endif
			/* This square must be a hole */
			nchanged++;
			sctx->state->holes[i] = LATINH_CROSS;
			continue;
		}
		
		/* Check for possibilities for hole */
		match = false;
		for(n = nums; n < o; n++)
		{
			if(cube(i%o, i/o, n+1))
				match = true;
		}
		if(!match)
		{
#ifdef STANDALONE_SOLVER
			if(solver_show_working)
				printf("Synchronize number at %d\n", i);
#endif
			/* This square must be a number */
			nchanged++;
			sctx->state->holes[i] = LATINH_CIRCLE;
		}
	}
	
	return nchanged;
}

static int latinholes_solver_place_cross(struct latin_solver *solver, struct solver_ctx *sctx, int x, int y)
{
	int n;
	int nchanged = 0;
	int nums = sctx->nums;
	
#ifdef STANDALONE_SOLVER
	if(solver_show_working)
		printf("Place cross at %d,%d\n", x, y);
#endif
	
	for(n = 0; n < nums; n++)
	{
		if(!cube(x, y, n+1))
			continue;
		
		cube(x, y, n+1) = false;
		nchanged++;
	}
	
	return nchanged;
}

static int latinholes_solver_place_circle(struct latin_solver *solver, struct solver_ctx *sctx, int x, int y)
{
	int n;
	int nchanged = 0;
	int o = solver->o;
	int nums = sctx->nums;

#ifdef STANDALONE_SOLVER
	if(solver_show_working)
		printf("Place circle at %d,%d\n", x, y);
#endif

	for(n = nums; n < o; n++)
	{
		if(!cube(x, y, n+1))
			continue;
		
		cube(x, y, n+1) = false;
		nchanged++;
	}
	
	return nchanged;
}

static int latinholes_solver_count(struct latin_solver *solver, struct solver_ctx *sctx)
{
	int o = solver->o;
	int nums = sctx->nums;
	int dir, holecount, circlecount;
	int x, y, i, j;
	int nchanged = 0;
	
	x = 0; y = 0;
	
	for(dir = 0; dir < 2; dir++)
	{
		for(i = 0; i < o; i++)
		{
			if(dir) x = i; else y = i;
			
			holecount = 0;
			circlecount = 0;
			for(j = 0; j < o; j++)
			{
				if(dir) y = j; else x = j;
				
				if(sctx->state->holes[y*o+x] == LATINH_CROSS)
					holecount++;
				if(sctx->state->holes[y*o+x] == LATINH_CIRCLE)
					circlecount++;
			}
			
			if(holecount == (o-nums))
			{
				for(j = 0; j < o; j++)
				{
					if(dir) y = j; else x = j;
					
					if(!sctx->state->holes[y*o+x])
						nchanged += latinholes_solver_place_circle(solver, sctx, x, y);
				}
			}
			else if(circlecount == nums)
			{
				for(j = 0; j < o; j++)
				{
					if(dir) y = j; else x = j;
					
					if(!sctx->state->holes[y*o+x])
						nchanged += latinholes_solver_place_cross(solver, sctx, x, y);
				}
			}
		}
	}
	
	return nchanged;
}

static int latinholes_check(game_state *state)
{
	int o = state->params->order;
	int nums = state->params->nums;
	int od = o*nums;
	int x, y, i;
	bool fail;
	digit d;
	
	fail = false;
	
	int *rows, *cols, *hrows, *hcols;
	rows = snewn(od, int);
	cols = snewn(od, int);
	hrows = snewn(o, int);
	hcols = snewn(o, int);
	memset(rows, 0, od * sizeof(int));
	memset(cols, 0, od * sizeof(int));
	memset(hrows, 0, o * sizeof(int));
	memset(hcols, 0, o * sizeof(int));
	
	for(x = 0; x < o; x++)
	for(y = 0; y < o; y++)
	{
		d = state->grid[y*o+x];
		if(d == 0 || d > nums)
		{
			hrows[y]++;
			hcols[x]++;
		}
		else
		{
			rows[y*nums+d-1]++;
			cols[x*nums+d-1]++;
		}
		
		if(d == 0 && state->holes[y*o+x] == LATINH_CIRCLE)
			fail = true;
	}
	
	for(i = 0; i < o; i++)
	{
		if(hrows[i] != (o-nums) || hcols[i] != (o-nums))
		{
#ifdef STANDALONE_SOLVER
			if(solver_show_working)
				printf("Hole miscount in %d\n", i);
#endif
			fail = true;
		}
	}
	
	for(i = 0; i < od; i++)
	{
		if(rows[i] != 1 || cols[i] != 1)
		{
#ifdef STANDALONE_SOLVER
			if(solver_show_working)
				printf("Number miscount in %d\n", i);
#endif
			fail = true;
		}
	}
	
	sfree(rows);
	sfree(cols);
	sfree(hrows);
	sfree(hcols);
	
	return !fail;
}

/* ******************** *
 * Salad Letters solver *
 * ******************** */
static int salad_letters_solver_dir(struct latin_solver *solver, struct solver_ctx *sctx, int si, int di, int ei, int cd)
{
	char clue;
	clue = sctx->state->borderclues[cd];
	if(!clue)
		return 0;
	
	int i, j;
	int o = solver->o;
	int nums = sctx->nums;
	int nchanged = 0;
	
	int dist = 0;
	int maxdist;
	bool found = false;
	bool outofrange = false;
	
	/* 
	 * Determine max. distance by counting the holes
	 * which can never be used for the clue
	 */
	maxdist = o-nums;
	for(i = si + (di*(o-nums)); i != ei; i+=di)
	{
		if(sctx->state->holes[i] == LATINH_CROSS)
			maxdist--;
	}
	
	for(i = si; i != ei; i+=di)
	{
		/* Rule out other possibilities near clue */
		if(!found)
		{
			for(j = 1; j <= nums; j++)
			{
				if(j == clue)
					continue;
				
				if(cube(i%o, i/o, j))
				{
#ifdef STANDALONE_SOLVER
					if(solver_show_working)
						printf("Border %c (%d) rules out %c at %d,%d\n", clue+'A'-1, cd, j+'A'-1, i%o, i/o);
#endif
					cube(i%o, i/o, j) = false;
					nchanged++;
				}
			}
		}
		
		if(sctx->state->holes[i] != LATINH_CROSS)
			found = true;
		
		/* Rule out this possibility too far away from clue */
		
		if(outofrange)
		{
			if(cube(i%o, i/o, clue))
			{
#ifdef STANDALONE_SOLVER
				if(solver_show_working)
					printf("Border %c is too far away from %d,%d\n", clue+'A'-1, i%o, i/o);
#endif
				cube(i%o, i/o, clue) = false;
				nchanged++;
			}
		}
		dist++;
		
		if(sctx->state->holes[i] == LATINH_CIRCLE || dist > maxdist)
			outofrange = true;
	}
	
	return nchanged;
}
 
static int salad_letters_solver(struct latin_solver *solver, struct solver_ctx *sctx)
{
	int nchanged = 0;
	int o = solver->o;
	int o2 = o*o;
	int i;
	
	for(i = 0; i < o; i++)
	{
		/* Top */
		nchanged += salad_letters_solver_dir(solver, sctx, i, o, o2+i, i+0);
		/* Left */
		nchanged += salad_letters_solver_dir(solver, sctx, i*o, 1, ((i+1)*o), i+o);
		/* Bottom */
		nchanged += salad_letters_solver_dir(solver, sctx, (o2-o)+i, -o, i-o, i+(o*2));
		/* Right */
		nchanged += salad_letters_solver_dir(solver, sctx, ((i+1)*o)-1, -1, i*o - 1, i+(o*3));
	}
	
	return nchanged;
}

static bool game_can_format_as_text_now(const game_params *params)
{
	return true;
}

static char *game_text_format(const game_state *state)
{
	int o = state->params->order;
	int mode = state->params->mode;
	int i, j;
	int lr = 8 + (o*2);
	int s = (lr * (o+4));
	char c;
	digit d;
	char hole;
	
	char *ret = snewn(s + 1, char);
	memset(ret, ' ', s * sizeof(char));
	ret[s] = '\0';
	
	/* Place newlines */
	for(i = 1; i <= o+4; i++)
	{
		ret[lr*i - 1] = '\n';
	}
	
	/* Draw corners */
	ret[lr + 2] = '+';
	ret[lr*2 - 4] = '+';
	ret[lr*(o+2) + 2] = '+';
	ret[lr*(o+3) - 4] = '+';
	
	/* Draw horizontal border */
	for(i = 3; i < lr-4; i++)
	{
		ret[i + lr] = '-';
		ret[i + (lr*(o+2))] = '-';
	}
	
	/* Draw vertical border */
	for(i = 2; i < 2+o; i++)
	{
		ret[i*lr + 2] = '|';
		ret[i*lr + (o*2) + 4] = '|';
	}
	
	/* Draw grid */
	for(i = 0; i < o; i++)
		for(j = 0; j < o; j++)
		{
			d = state->grid[i*o+j];
			hole = state->holes[i*o+j];
			if(hole == LATINH_CROSS)
				c = 'x';
			else if(!d)
				c = hole == LATINH_CIRCLE ? 'O' : '.';
			else
				c = mode == GAMEMODE_LETTERS ? 'A' + d - 1 : '0' + d;
			
			ret[(i+2)*lr + 2*j + 4] = c;
		}
	
	/* Draw border clues */
	for(i = 0; i < o; i++)
	{
		/* Top */
		if(state->borderclues[i])
			ret[(i*2)+4] = state->borderclues[i] + 'A' - 1;
		/* Left */
		if(state->borderclues[i + o])
			ret[((i+2)*lr)] = state->borderclues[i + o] + 'A' - 1;
		/* Bottom */
		if(state->borderclues[i + (o*2)])
			ret[(i*2)+4+(lr*(o+3))] = state->borderclues[i + (o*2)] + 'A' - 1;
		/* Right */
		if(state->borderclues[i + (o*3)])
			ret[((i+3)*lr)-2] = state->borderclues[i + (o*3)] + 'A' - 1;
	}
	
	return ret;
}

static game_state *load_game(const game_params *params, const char *desc, char **fail)
{
	int o = params->order;
	int nums = params->nums;
	int o2 = o*o;
	int ox4 = o*4;
	int c, pos;
	digit d;
	
	game_state *ret = blank_game(params);
	memset(ret->grid, 0, o2 * sizeof(digit));
	memset(ret->holes, 0, o2 * sizeof(char));
	memset(ret->borderclues, 0, ox4 * sizeof(char));
	memset(ret->gridclues, 0, o2 * sizeof(digit));
	
	const char *p = desc;
	/* Read border clues */
	if(params->mode == GAMEMODE_LETTERS)
	{
		pos = 0;
		while(*p && *p != ',')
		{
			c = *p++;
			d = 0;
			if(pos >= ox4)
			{
				free_game(ret);
				*fail = "Border description is too long.";
				return NULL;
			}
			
			if(c >= 'a' && c <= 'z')
				pos += (c - 'a') + 1;
			else if(c >= '1' && c <= '9')
				d = c - '0';
			else if(c >= 'A' && c <= 'I')
				d = (c - 'A') + 1;
			else
			{
				free_game(ret);
				*fail = "Border description contains invalid characters.";
				return NULL;
			}
			
			if(d > 0 && d <= nums)
			{
				ret->borderclues[pos] = d;
				pos++;
			}
			else if(d > nums)
			{
				free_game(ret);
				*fail = "Border clue is out of range.";
				return NULL;
			}
		}
		
		if(pos < ox4)
		{
			free_game(ret);
			*fail = "Description is too short.";
			return NULL;
		}
		
		if(*p == ',')
			p++;
	}
	/* Read grid clues */
	
	pos = 0;
	while(*p)
	{
		c = *p++;
		d = 0;
		if(pos >= o2)
		{
			free_game(ret);
			*fail = "Grid description is too long.";
			return NULL;
		}
		
		if(c >= 'a' && c <= 'z')
			pos += (c - 'a') + 1;
		else if(c >= '1' && c <= '9')
			d = c - '0';
		else if(c >= 'A' && c <= 'I')
			d = (c - 'A') + 1;
		else if(c == 'O')
		{
			ret->gridclues[pos] = LATINH_CIRCLE;
			ret->holes[pos] = LATINH_CIRCLE;
			pos++;
		}
		else if(c == 'X')
		{
			ret->gridclues[pos] = LATINH_CROSS;
			ret->holes[pos] = LATINH_CROSS;
			pos++;
		}
		else
		{
			free_game(ret);
			*fail = "Grid description contains invalid characters.";
			return NULL;
		}
		
		if(d > 0 && d <= nums)
		{
			ret->gridclues[pos] = d;
			ret->grid[pos] = d;
			ret->holes[pos] = LATINH_CIRCLE;
			pos++;
		}
		else if(d > nums)
		{
			free_game(ret);
			*fail = "Grid clue is out of range.";
			return NULL;
		}
	}
	
	if(pos > 0 && pos < o2)
	{
		free_game(ret);
		*fail = "Description is too short.";
		return NULL;
	}
	
	return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	char *fail = NULL;
	game_state *state = load_game(params, desc, &fail);
	if(state)
		free_game(state);
	
	if(fail)
		return fail;
	
	return NULL;
}

static int salad_solver_easy(struct latin_solver *solver, void *vctx)
{
	struct solver_ctx *ctx = (struct solver_ctx *)vctx;
	int nchanged = 0;
	
	nchanged += latinholes_solver_sync(solver, ctx);
	
	if(ctx->state->params->mode == GAMEMODE_LETTERS)
	{
		nchanged += salad_letters_solver(solver, ctx);
	}
	
	nchanged += latinholes_solver_count(solver, ctx);
	
	return nchanged;
}

static digit salad_scan_dir(digit *grid, char *holes, int si, int di, int ei, bool direct)
{
	int i;
	for(i = si; i != ei; i+=di)
	{
		if(direct && grid[i] == 0 && holes[i] != LATINH_CROSS)
			return 0;
		if(grid[i] != 0 && grid[i] != LATINH_CROSS)
			return grid[i];
	}
	
	return 0;
}

static bool salad_checkborders(game_state *state)
{
	int o = state->params->order;
	int o2 = o*o;
	int i; char c;
	
	for(i = 0; i < o; i++)
	{
		/* Top */
		if(state->borderclues[i])
		{		
			c = salad_scan_dir(state->grid, NULL, i, o, o2+i, false);
			if(c != state->borderclues[i])
				return false;
		}
		/* Left */
		if(state->borderclues[i+o])
		{
			c = salad_scan_dir(state->grid, NULL, i*o, 1, ((i+1)*o), false);
			if(c != state->borderclues[i+o])
				return false;
		}
		/* Bottom */
		if(state->borderclues[i+(o*2)])
		{
			c = salad_scan_dir(state->grid, NULL, (o2-o)+i, -o, i-o, false);
			if(c != state->borderclues[i+(o*2)])
				return false;
		}
		/* Right */
		if(state->borderclues[i+(o*3)])
		{
			c = salad_scan_dir(state->grid, NULL, ((i+1)*o)-1, -1, i*o - 1, false);
			if(c != state->borderclues[i+(o*3)])
				return false;
		}
	}
	
	return true;
}

static bool salad_valid(struct latin_solver *solver, void *vctx) {
	return true;
}

#define SOLVER(upper,title,func,lower) func,
static usersolver_t const salad_solvers[] = { DIFFLIST(SOLVER) };

static int salad_solve(game_state *state, int maxdiff)
{
	int o = state->params->order;
	int nums = state->params->nums;
	int o2 = o*o;
	struct solver_ctx *ctx = new_ctx(state, o, nums);
	struct latin_solver *solver = snew(struct latin_solver);
	int diff, i;
	
#ifdef STANDALONE_SOLVER
	if(solver_show_working)
		printf("Allocate solver\n");
#endif
	
	latin_solver_alloc(solver, state->grid, o);
	
	for(i = 0; i < o2; i++)
	{
		if(state->gridclues[i] && state->gridclues[i] != LATINH_CROSS && state->gridclues[i] != LATINH_CIRCLE)
		{
#ifdef STANDALONE_SOLVER
			if(solver_show_working)
				printf("Place clue %c at %d,%d\n", state->gridclues[i] + '0', i%o, i/o);
#endif
			latin_solver_place(solver, i%o, i/o, state->gridclues[i]);
		}
		else if(state->gridclues[i] == LATINH_CROSS)
		{
			latinholes_solver_place_cross(solver, ctx, i%o, i/o);
		}
		else if(state->gridclues[i] == LATINH_CIRCLE)
		{
			latinholes_solver_place_circle(solver, ctx, i%o, i/o);
		}
	}
	
#ifdef STANDALONE_SOLVER
	if(solver_show_working)
		printf("Begin solver\n");
#endif
	
	if(maxdiff != DIFF_HOLESONLY)
	{
		latin_solver_main(solver, maxdiff,
			DIFF_EASY, DIFF_HARD, DIFF_HARD,
			DIFF_HARD, DIFF_IMPOSSIBLE,
			salad_solvers, salad_valid, ctx, clone_ctx, free_ctx);
		
		diff = latinholes_check(state);
	}
	else
	{
		int holes = 0;
		int nchanged = 1;
		
#ifdef STANDALONE_SOLVER
		if(solver_show_working)
			printf("Check for holes only\n");
#endif

		while(nchanged)
		{
			nchanged = 0;
			nchanged += latinholes_solver_sync(solver, ctx);
			nchanged += latinholes_solver_count(solver, ctx);
		}
		
		for(i = 0; i < o2; i++)
		{
			if(state->holes[i] == LATINH_CROSS)
				holes++;
		}
		
#ifdef STANDALONE_SOLVER
		if(solver_show_working)
			printf("Holes found: %d\n", holes);
#endif
		diff = holes == ((o-nums) * o);
	}
	
	
#ifdef STANDALONE_SOLVER
	if(solver_show_working)
		printf("Solution is %s\n", diff ? "valid" : "invalid");
#endif
	
	free_ctx(ctx);
	latin_solver_free(solver);
	sfree(solver);

	if (!diff)
		return 0;
	
	return 1;
}

static key_label *game_request_keys(const game_params *params, int *nkeys)
{
	int i;
	int n = params->nums;
	char base = params->mode == GAMEMODE_LETTERS ? 'A' : '1';

	key_label *keys = snewn(n + 3, key_label);
	*nkeys = n + 3;

	for (i = 0; i < n; i++)
	{
		keys[i].button = base + i;
		keys[i].label = NULL;
	}
	keys[n].button = 'X';
	keys[n].label = NULL;
	keys[n+1].button = 'O';
	keys[n+1].label = NULL;
	keys[n+2].button = '\b';
	keys[n+2].label = NULL;

	return keys;
}

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
	char *fail;
	
	game_state *state = load_game(params, desc, &fail);
	assert(state);
	
	return state;
}

static char *salad_serialize(const digit *input, int s, char base)
{
	char *ret, *p;
	ret = snewn(s + 1, char);
	p = ret;
	int i, run = 0;
	for (i = 0; i < s; i++)
	{
		if (input[i] != 0)
		{
			if (run)
			{
				*p++ = ('a'-1) + run;
				run = 0;
			}
			
			if (input[i] == LATINH_CROSS)
				*p++ = 'X';
			else if (input[i] == LATINH_CIRCLE)
				*p++ = 'O';
			else
				*p++ = input[i] + base;
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
	if (run) {
		*p++ = ('a'-1) + run;
		run = 0;
	}
	*p = '\0';
	
	return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
			const char *aux, const char **error)
{
	game_state *solved = dup_game(state);
	char *ret = NULL;
	int result;
	
	result = salad_solve(solved, DIFF_HARD);
	
	if(result)
	{
		int o = solved->params->order;
		int o2 = o*o;
		
		ret = snewn(o2 + 2, char);
		char *p = ret;
		*p++ = 'S';
		
		int i;
		for(i = 0; i < o2; i++)
		{
			if(solved->grid[i] && solved->holes[i] != LATINH_CROSS)
				*p++ = (solved->grid[i] + '0');
			else
				*p++ = 'X';
		}
		
		*p++ = '\0';
	}
	else
		*error = "No solution found.";
	
	free_game(solved);
	return ret;
}

static void salad_strip_clues(game_state *state, random_state *rs, digit *clues, int m, int diff)
{
	int *spaces = snewn(m, int);
	int o = state->params->order;
	int o2 = o*o;
	int i, j;
	digit temp;
	
	for(i = 0; i < m; i++) spaces[i] = i;
	shuffle(spaces, m, sizeof(*spaces), rs);
	
	for(i = 0; i < m; i++)
	{
		j = spaces[i];
		
		temp = clues[j];
		
		/* Space is already empty */
		if(temp == 0)
			continue;
		
		clues[j] = 0;
		
		memset(state->grid, 0, o2 * sizeof(digit));
		memset(state->holes, 0, o2 * sizeof(char));
		
		if(!salad_solve(state, diff))
		{
			clues[j] = temp;
		}
	}
	sfree(spaces);
}

/* ********* *
 * Generator *
 * ********* */
static char *salad_new_numbers_desc(const game_params *params, random_state *rs, char **aux)
{
	int o = params->order;
	int o2 = o*o;
	int nums = params->nums;
	int i, j;
	int diff = params->diff;
	char temp;
	digit *grid = NULL;
	game_state *state = NULL;
	int *spaces = snewn(o2, int);

	while(true)
	{
		/* Generate a solved grid */
		grid = latin_generate(o, rs);
		state = blank_game(params);
		memset(state->borderclues, 0, o * 4 * sizeof(digit));
		
		for(i = 0; i < o2; i++)
		{
			if(grid[i] > nums)
				state->gridclues[i] = LATINH_CROSS;
			else
				state->gridclues[i] = grid[i];
		}
		sfree(grid);
		
		/* Remove grid clues */
		for(i = 0; i < o2; i++) spaces[i] = i;
		shuffle(spaces, o2, sizeof(*spaces), rs);
		
		for(i = 0; i < o2; i++)
		{
			j = spaces[i];
			
			temp = state->gridclues[j];
			
			if(temp == 0)
				continue;
			
			/* Remove the hole or the number on a ball */
			if(temp == LATINH_CROSS || temp == LATINH_CIRCLE)
				state->gridclues[j] = 0;
			else
				state->gridclues[j] = LATINH_CIRCLE;
			
			memset(state->grid, 0, o2 * sizeof(digit));
			memset(state->holes, 0, o2 * sizeof(digit));
			
			if(!salad_solve(state, diff))
			{
				state->gridclues[j] = temp;
				continue;
			}
			
			/* See if we can remove the entire ball */
			temp = state->gridclues[j];
			if(temp == 0)
				continue;
			
			state->gridclues[j] = 0;
			memset(state->grid, 0, o2 * sizeof(digit));
			memset(state->holes, 0, o2 * sizeof(digit));
			
			if(!salad_solve(state, diff))
			{
				state->gridclues[j] = temp;
			}
		}
		
		/* 
		 * Quality check: See if all the holes can be placed
		 * at the start, without entering numbers. If yes,
		 * the puzzle is thrown away.
		 */
		memset(state->grid, 0, o2 * sizeof(digit));
		memset(state->holes, 0, o2 * sizeof(digit));
		if(!salad_solve(state, DIFF_HOLESONLY))
			break;
		
		free_game(state);
	}
	
	char *ret = salad_serialize(state->gridclues, o2, '0');
	free_game(state);
	sfree(spaces);
	
	return ret;
}

static char *salad_new_letters_desc(const game_params *params, random_state *rs, char **aux)
{
	int o = params->order;
	int o2 = o*o;
	int ox4 = o*4;
	int nums = params->nums;
	int diff = params->diff;
	int i;
	digit *grid;
	game_state *state;
	bool nogrid = false;
	
	/*
	 * Quality check: With certain parameters, force the grid 
	 * to contain no clues.
	 */
	if(o < 8)
		nogrid = true;
	
	while(true)
	{
		grid = latin_generate(o, rs);
		state = blank_game(params);
		memset(state->borderclues, 0, ox4 * sizeof(char));
		for(i = 0; i < o2; i++)
		{
			if(grid[i] <= nums)
				state->gridclues[i] = grid[i];
			else
				state->gridclues[i] = LATINH_CROSS;
		}
		sfree(grid);
		
		/* Add border clues */
		for(i = 0; i < o; i++)
		{
			/* Top */
			state->borderclues[i] = salad_scan_dir(state->gridclues, NULL, i, o, o2+i, false);
			/* Left */
			state->borderclues[i+o] = salad_scan_dir(state->gridclues, NULL, i*o, 1, ((i+1)*o), false);
			/* Bottom */
			state->borderclues[i+(o*2)] = salad_scan_dir(state->gridclues, NULL, (o2-o)+i, -o, i-o, false);
			/* Right */
			state->borderclues[i+(o*3)] = salad_scan_dir(state->gridclues, NULL, ((i+1)*o)-1, -1, i*o - 1, false);
		}
		
		if(nogrid)
		{
			/* Remove all grid clues, and attempt to solve it */
			memset(state->gridclues, 0, o2 * sizeof(char));
			memset(state->grid, 0, o2 * sizeof(digit));
			memset(state->holes, 0, o2 * sizeof(char));
			if(!salad_solve(state, diff))
			{
				free_game(state);
				continue;
			}
		}
		else
		{
			/* Remove grid clues, with full border clues */
			salad_strip_clues(state, rs, state->gridclues, o2, diff);
		}
		/* Remove border clues */
		salad_strip_clues(state, rs, state->borderclues, ox4, diff);
		
		break;
	}
	/* Encode game */
	char *borderstr = salad_serialize(state->borderclues, ox4, 'A' - 1);
	
	char *gridstr = salad_serialize(state->gridclues, o2, 'A' - 1);
	free_game(state);
	char *ret = snewn(ox4+o2+2, char);
	sprintf(ret, "%s,%s", borderstr, gridstr);
	sfree(borderstr);
	sfree(gridstr);
	
	return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	if(params->mode == GAMEMODE_NUMBERS)
		return salad_new_numbers_desc(params, rs, aux);
	else
		return salad_new_letters_desc(params, rs, aux);
}

/* ************** *
 * User interface *
 * ************** */
struct game_ui {
	int hx, hy;
	bool hpencil;
	bool hshow;
	bool hcursor;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ret = snew(game_ui);
	
	ret->hx = ret->hy = 0;
	ret->hpencil = ret->hshow = ret->hcursor = false;
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
}
static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (ui->hshow && (button == CURSOR_SELECT))
        return ui->hpencil ? "Ink" : "Pencil";
    return "";
}

#define FD_CURSOR  0x01
#define FD_PENCIL  0x02
#define FD_ERROR   0x04
#define FD_CIRCLE  0x08
#define FD_CROSS   0x10

#define FD_MASK    0x1f

struct game_drawstate {
	int tilesize;
	bool redraw;
	int oldflash;
	int *gridfs;
	int *borderfs;
	
	digit *grid;
	int *oldgridfs;
	int *oldborderfs;
	unsigned int *marks;
	
	/* scratch space */
	int *rowcount;
	int *colcount;
};

#define TILE_SIZE (ds->tilesize)
#define DEFAULT_TILE_SIZE 40
#define FLASH_TIME 0.7F
#define FLASH_FRAME 0.1F
#define FROMCOORD(x) ( ((x)/ TILE_SIZE) - 1 )

static char *interpret_move(const game_state *state, game_ui *ui, const game_drawstate *ds,
				int x, int y, int button)
{
	int i, o, nums, pos, gx, gy;
	char buf[80];
	o = state->params->order;
	nums = state->params->nums;
	pos = ui->hx+(o*ui->hy);
	gx = FROMCOORD(x);
	gy = FROMCOORD(y);
	
	button &= ~MOD_MASK;
	
	if(gx >= 0 && gx < o && gy >= 0 && gy < o)
	{
		if(button == LEFT_BUTTON || button == RIGHT_BUTTON)
		{
			int newpencil = button == RIGHT_BUTTON;
			
			if((state->gridclues[(gy*o)+gx] == 0 || state->gridclues[(gy*o)+gx] == LATINH_CIRCLE)
				&& (!ui->hshow || (newpencil ? !ui->hpencil : ui->hpencil)
				|| ui->hx != gx || ui->hy != gy))
			{
				ui->hx = gx;
				ui->hy = gy;
				ui->hpencil = newpencil;
				ui->hcursor = false;
				ui->hshow = true;
			}
			/* Deselect */
			else
			{
				ui->hshow = false;
			}
			return MOVE_UI_UPDATE;
		}
		
		/* Quick add Circle or Hole */
		if(button == MIDDLE_BUTTON && state->gridclues[(gy*o)+gx] == 0)
		{
			if(state->holes[(gy*o)+gx] == 0)
			{
				sprintf(buf, "%c%d,%d,%c", 'R',
					gx, gy, 'O');
				ui->hshow = false;
				return dupstr(buf);
			}
			else if(state->holes[(gy*o)+gx] == LATINH_CIRCLE && state->grid[(gy*o)+gx] == 0)
			{
				sprintf(buf, "%c%d,%d,%c", 'R',
					gx, gy, 'X');
				ui->hshow = false;
				return dupstr(buf);
			}
			else if(state->holes[(gy*o)+gx] == LATINH_CROSS)
			{
				sprintf(buf, "%c%d,%d,%c", 'R',
					gx, gy, '-');
				ui->hshow = false;
				return dupstr(buf);
			}
		}
	}
	
	/* Keyboard move */
	if (IS_CURSOR_MOVE(button))
	{
		gx = ui->hx; gy = ui->hy;
		move_cursor(button, &gx, &gy, o, o, 0, NULL);
		ui->hx = gx; ui->hy = gy;
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
	
	if(ui->hshow && (state->gridclues[pos] == 0 || state->gridclues[pos] == LATINH_CIRCLE))
	{
		if ((button >= '0' && button <= '9') || 
			(button >= 'a' && button <= 'i') || 
			(button >= 'A' && button <= 'I') || 
			button == '\b')
		{
			digit d = 0;
			
			if (button >= '1' && button <= '9')
			{
				d = button - '0';
				if(d > nums)
					return NULL;
			}
			if (button >= 'a' && button <= 'i')
			{
				d = button - 'a' + 1;
				if(d > nums)
					return NULL;
			}
			if (button >= 'A' && button <= 'I')
			{
				d = button - 'A' + 1;
				if(d > nums)
					return NULL;
			}
			
			sprintf(buf, "%c%d,%d,%c", (char)(ui->hpencil ? 'P'	: 'R'),
				ui->hx, ui->hy, (char)(d ? d + '0' : '-'));
			
			/* When not in keyboard and pencil mode, hide cursor */
			if (!ui->hcursor && !ui->hpencil)
				ui->hshow = false;
					
			return dupstr(buf);
		}
		
		if(button == 'X' || button == 'x' || button == '-' || button == '_')
		{
			if(state->gridclues[pos] == LATINH_CIRCLE)
				return NULL;
			
			sprintf(buf, "%c%d,%d,%c", (char)(ui->hpencil ? 'P'	: 'R'),
				ui->hx, ui->hy, 'X');
			
			/* When not in keyboard and pencil mode, hide cursor */
			if (!ui->hcursor && !ui->hpencil)
				ui->hshow = false;
					
			return dupstr(buf);
		}
		
		if(button == 'O' || button == 'o' || button == '+' || button == '=')
		{
			if(state->gridclues[pos] == LATINH_CIRCLE && ui->hpencil)
				return NULL;
			if(state->grid[pos] != 0 && ui->hpencil)
				return NULL;
			
			sprintf(buf, "%c%d,%d,%c", (char)(ui->hpencil ? 'P'	: 'R'),
				ui->hx, ui->hy, 'O');
			
			/* When not in keyboard and pencil mode, hide cursor */
			if (!ui->hcursor && !ui->hpencil)
				ui->hshow = false;
					
			return dupstr(buf);
		}
	}
	
	if(button == 'm' || button == 'M')
	{
		unsigned int allmarks = (1<<(nums+1))-1;
		unsigned int marks = (1<<nums)-1;
		
		for(i = 0; i < o*o; i++)
		{
			if(!state->grid[i] && state->holes[i] != LATINH_CROSS &&
				state->marks[i] != (state->holes[i] == LATINH_CIRCLE ? marks : allmarks))
			return dupstr("M");
		}
	}
	
	return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	game_state *ret = NULL;
	int o = state->params->order;
	int nums = state->params->nums;
	int x, y, i;
	char c; digit d;
	
	/* Auto-solve game */
	if(move[0] == 'S')
	{
		const char *p = move + 1;
		ret = dup_game(state);
		
		for (i = 0; i < o*o; i++) {
			
			if(*p >= '1' && *p <= '9')
			{
				ret->grid[i] = *p - '0';
				ret->holes[i] = LATINH_CIRCLE;
			}
			else if(*p == 'X')
			{
				ret->grid[i] = 0;
				ret->holes[i] = LATINH_CROSS;
			}
			else
			{
				free_game(ret);
				return NULL;
			}
			p++;
		}
		
		ret->completed = ret->cheated = true;
		return ret;
	}
	/* Write number or pencil mark in square */
	if((move[0] == 'P' || move[0] == 'R') && sscanf(move+1, "%d,%d,%c", &x, &y, &c) == 3)
	{		
		ret = dup_game(state);
		
		/* Clear square */
		if(c == '-')
		{
			/* If square is already empty, remove pencil marks */
			if(!ret->grid[y*o+x] && ret->holes[y*o+x] != LATINH_CROSS)
				ret->marks[y*o+x] = 0;
			
			ret->grid[y*o+x] = 0;
			if(ret->gridclues[y*o+x] != LATINH_CIRCLE)
				ret->holes[y*o+x] = 0;
		}
		/* Enter number */
		else if(move[0] == 'R' && c != 'X' && c != 'O')
		{
			d = (digit)c - '0';
			ret->grid[y*o+x] = d;
			ret->holes[y*o+x] = LATINH_CIRCLE;
		}
		/* Add/remove pencil mark */
		else if(move[0] == 'P' && c != 'X' && c != 'O')
		{
			d = (digit)c - '1';
			ret->marks[y*o+x] ^= 1 << d;
		}
		/* Add hole */
		else if(move[0] == 'R' && c == 'X')
		{
			ret->grid[y*o+x] = 0;
			ret->holes[y*o+x] = LATINH_CROSS;
		}
		/* Add/remove pencil mark for hole */
		else if(move[0] == 'P' && c == 'X')
		{
			ret->marks[y*o+x] ^= 1 << nums;
		}
		/* Add circle and empty square */
		else if(move[0] == 'R' && c == 'O')
		{
			ret->grid[y*o+x] = 0;
			ret->holes[y*o+x] = LATINH_CIRCLE;
		}
		/* Toggle circle without emptying */
		else if(move[0] == 'P' && c == 'O')
		{
			if(ret->holes[y*o+x] == 0)
				ret->holes[y*o+x] = LATINH_CIRCLE;
			else if(ret->holes[y*o+x] == LATINH_CIRCLE)
				ret->holes[y*o+x] = 0;
		}
		
		/* Check for completion */
		if(latinholes_check(ret) && salad_checkborders(ret))
			ret->completed = true;
		
		return ret;
	}
	/*
	 * Fill in every possible mark. If a square has a circle set,
	 * don't include a mark for a cross.
	 */
	if(move[0] == 'M')
	{
		unsigned int allmarks = (1<<(nums+1))-1;
		unsigned int marks = (1<<nums)-1;
		ret = dup_game(state);
		
		for(i = 0; i < o*o; i++)
		{
			if(!state->grid[i] && state->holes[i] != LATINH_CROSS)
				ret->marks[i] = (state->holes[i] == LATINH_CIRCLE ? marks : allmarks);
		}
		
		return ret;
	}
	
	return NULL;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
	if(ui->hshow) {
		*x = (ui->hx+1) * TILE_SIZE;
		*y = (ui->hy+1) * TILE_SIZE;
		*w = *h = TILE_SIZE;
	}
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	*x = *y = (params->order+2) * tilesize;
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
	
	ret[COL_BORDERCLUE*3 + 0] = 0.0F;
	ret[COL_BORDERCLUE*3 + 1] = 0.0F;
	ret[COL_BORDERCLUE*3 + 2] = 0.0F;
	
	ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
	ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
	ret[COL_PENCIL * 3 + 2] = ret[COL_BACKGROUND * 3 + 2];
	
	/* Immutable colors */
	ret[COL_I_NUM*3 + 0] = 0.0F;
	ret[COL_I_NUM*3 + 1] = 0.0F;
	ret[COL_I_NUM*3 + 2] = 0.0F;
	
	ret[COL_I_HOLE*3 + 0] = 0.0F;
	ret[COL_I_HOLE*3 + 1] = 0.0F;
	ret[COL_I_HOLE*3 + 2] = 0.0F;
	
	ret[COL_I_BALL*3 + 0] = 0.0F;
	ret[COL_I_BALL*3 + 1] = 0.0F;
	ret[COL_I_BALL*3 + 2] = 0.0F;

	ret[COL_I_BALLBG*3 + 0] = 1.0F;
	ret[COL_I_BALLBG*3 + 1] = 1.0F;
	ret[COL_I_BALLBG*3 + 2] = 1.0F;
	
	/* Guess colors */
	ret[COL_G_NUM*3 + 0] = 0.0F;
	ret[COL_G_NUM*3 + 1] = 0.5F;
	ret[COL_G_NUM*3 + 2] = 0.0F;
	
	ret[COL_G_HOLE*3 + 0] = 0.0F;
	ret[COL_G_HOLE*3 + 1] = 0.25F;
	ret[COL_G_HOLE*3 + 2] = 0.0F;
	
	ret[COL_G_BALL*3 + 0] = 0.0F;
	ret[COL_G_BALL*3 + 1] = 0.1F;
	ret[COL_G_BALL*3 + 2] = 0.0F;

	ret[COL_G_BALLBG*3 + 0] = 0.95F;
	ret[COL_G_BALLBG*3 + 1] = 1.0F;
	ret[COL_G_BALLBG*3 + 2] = 0.95F;
	
	/* Error colors */
	ret[COL_E_BORDERCLUE*3 + 0] = 1.0F;
	ret[COL_E_BORDERCLUE*3 + 1] = 0.0F;
	ret[COL_E_BORDERCLUE*3 + 2] = 0.0F;
	
	ret[COL_E_NUM*3 + 0] = 1.0F;
	ret[COL_E_NUM*3 + 1] = 0.0F;
	ret[COL_E_NUM*3 + 2] = 0.0F;
	
	ret[COL_E_HOLE*3 + 0] = 1.0F;
	ret[COL_E_HOLE*3 + 1] = 0.0F;
	ret[COL_E_HOLE*3 + 2] = 0.0F;
	
	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	int o = state->params->order;
	int o2 = o*o;
	int ox4 = o*4;
	
	struct game_drawstate *ds = snew(struct game_drawstate);

	ds->tilesize = DEFAULT_TILE_SIZE;
	ds->redraw = true;
	ds->oldflash = -1;
	ds->gridfs = snewn(o2, int);
	ds->grid = snewn(o2, digit);
	ds->marks = snewn(o2, unsigned int);
	ds->borderfs = snewn(ox4, int);
	
	ds->oldgridfs = snewn(o2, int);
	ds->oldborderfs = snewn(ox4, int);
	ds->rowcount = snewn(o2, int);
	ds->colcount = snewn(o2, int);
	
	memset(ds->gridfs, 0, o2 * sizeof(int));
	memset(ds->grid, 0, o2 * sizeof(digit));
	memset(ds->marks, 0, o2 * sizeof(unsigned int));
	memset(ds->oldgridfs, ~0, o2 * sizeof(int));
	memset(ds->borderfs, 0, ox4 * sizeof(int));
	memset(ds->oldborderfs, ~0, ox4 * sizeof(int));

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->gridfs);
	sfree(ds->borderfs);
	sfree(ds->grid);
	sfree(ds->marks);
	sfree(ds->oldgridfs);
	sfree(ds->oldborderfs);
	sfree(ds->rowcount);
	sfree(ds->colcount);
	sfree(ds);
}

static void salad_draw_pencil(drawing *dr, const game_state *state, int x, int y, char base, int tilesize, int tx, int ty)
{
	/* Draw the entered clues for a square. */
	int o = state->params->order;
	int mmx = state->params->nums + 1;
	int nhints, i, j, hw, hh, hmax, fontsz;
	char str[2];

	/* (can assume square has just been cleared) */

	/* Draw hints; steal ingenious algorithm (basically)
	* from solo.c:draw_number() */
	for (i = nhints = 0; i < mmx; i++) {
		if (state->marks[y*o+x] & (1<<i)) nhints++;
	}

	for (hw = 1; hw * hw < nhints; hw++);
	
	if (hw < 3) hw = 3;
		hh = (nhints + hw - 1) / hw;
	if (hh < 2) hh = 2;
		hmax = max(hw, hh);
		fontsz = tilesize/(hmax*(11-hmax)/8);

	for (i = j = 0; i < mmx; i++)
	{
		if (state->marks[y*o+x] & (1<<i))
		{
			int hx = j % hw, hy = j / hw;

			str[0] = base+i+1;
			if(i == mmx - 1)
				str[0] = 'X';
			
			str[1] = '\0';
			draw_text(dr,
							tx + (4*hx+3) * tilesize / (4*hw+2),
							ty + (4*hy+3) * tilesize / (4*hh+2),
							FONT_VARIABLE, fontsz,
							ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, str);
			j++;
		}
	}
}

static void salad_set_drawflags(game_drawstate *ds, const game_ui *ui, const game_state *state, int hshow)
{
	int o = state->params->order;
	int nums = state->params->nums;
	int o2 = o*o;
	int x, y, i;
	char c; digit d;
	
	/* Count numbers */
	memset(ds->rowcount, 0, o2*sizeof(int));
	memset(ds->colcount, 0, o2*sizeof(int));
	for(x = 0; x < o; x++)
	for(y = 0; y < o; y++)
	{
		if(state->holes[y*o+x] == LATINH_CROSS)
		{
			ds->rowcount[y+(nums*o)]++;
			ds->colcount[x+(nums*o)]++;
		}
		else if(state->grid[y*o+x])
		{
			d = state->grid[y*o+x] - 1;
			ds->rowcount[y+(d*o)]++;
			ds->colcount[x+(d*o)]++;
		}
	}
	
	/* Grid flags */
	for(x = 0; x < o; x++)
		for(y = 0; y < o; y++)
		{
			i = y*o+x;
			/* Unset all flags */
			ds->gridfs[i] &= ~FD_MASK;
			
			/* Set cursor flags */
			if(hshow && ui->hx == x && ui->hy == y)
				ds->gridfs[i] |= ui->hpencil ? FD_PENCIL : FD_CURSOR;
			
			/* Mark count errors */
			d = state->grid[i];
			if(state->holes[i] == LATINH_CROSS && 
				(ds->rowcount[y+(nums*o)] > (o-nums) || ds->colcount[x+(nums*o)] > (o-nums)))
			{
				ds->gridfs[i] |= FD_ERROR;
			}
			else if(d > 0 && (ds->rowcount[y+((d-1)*o)] > 1 || ds->colcount[x+((d-1)*o)] > 1))
			{
				ds->gridfs[i] |= FD_ERROR;
			}
			
			if(state->holes[i] == LATINH_CROSS)
				ds->gridfs[i] |= FD_CROSS;
			if(state->holes[i] == LATINH_CIRCLE)
				ds->gridfs[i] |= FD_CIRCLE;
		}
	
	/* Check border clues */
	for(i = 0; i < o; i++)
	{
		/* Top */
		if(state->borderclues[i])
		{		
			c = salad_scan_dir(state->grid, state->holes, i, o, o2+i, true);
			if(c && c != state->borderclues[i])
				ds->borderfs[i] |= FD_ERROR;
			else
				ds->borderfs[i] &= ~FD_ERROR;
		}
		/* Left */
		if(state->borderclues[i+o])
		{
			c = salad_scan_dir(state->grid, state->holes, i*o, 1, ((i+1)*o), true);
			if(c && c != state->borderclues[i+o])
				ds->borderfs[i+o] |= FD_ERROR;
			else
				ds->borderfs[i+o] &= ~FD_ERROR;
		}
		/* Bottom */
		if(state->borderclues[i+(o*2)])
		{
			c = salad_scan_dir(state->grid, state->holes, (o2-o)+i, -o, i-o, true);
			if(c && c != state->borderclues[i+(o*2)])
				ds->borderfs[i+(o*2)] |= FD_ERROR;
			else
				ds->borderfs[i+(o*2)] &= ~FD_ERROR;
		}
		/* Right */
		if(state->borderclues[i+(o*3)])
		{
			c = salad_scan_dir(state->grid, state->holes, ((i+1)*o)-1, -1, i*o - 1, true);
			if(c && c != state->borderclues[i+(o*3)])
				ds->borderfs[i+(o*3)] |= FD_ERROR;
			else
				ds->borderfs[i+(o*3)] &= ~FD_ERROR;
		}
	}
}

static void salad_draw_balls(drawing *dr, game_drawstate *ds, int x, int y, int flash, const game_state *state)
{
	int mode = state->params->mode;
	int o = state->params->order;
	int tx, ty, bgcolor, color;
	
	int i = x+(y*o);
	if(mode == GAMEMODE_LETTERS && state->grid[i] != 0)
		return;
	if(state->holes[i] != LATINH_CIRCLE)
		return;
	
	tx = (x+1)*TILE_SIZE + (TILE_SIZE/2);
	ty = (y+1)*TILE_SIZE + (TILE_SIZE/2);
	
	/* Draw ball background */
	if(mode == GAMEMODE_NUMBERS)
		bgcolor = ((x+y) % 3 == flash ? COL_BACKGROUND :
					(x+y+1) % 3 == flash ? COL_LOWLIGHT : 
					state->gridclues[i] ? COL_I_BALLBG : COL_G_BALLBG);
	else /* Transparent */
		bgcolor = (ds->gridfs[i] & FD_CURSOR ? COL_LOWLIGHT : COL_BACKGROUND);
	color = (state->gridclues[i] ? COL_I_BALL : COL_G_BALL);
	
	draw_circle(dr, tx, ty, TILE_SIZE*0.4, color, color);
	draw_circle(dr, tx, ty, TILE_SIZE*0.38, bgcolor, color);
}

static void salad_draw_cross(drawing *dr, game_drawstate *ds, int x, int y, double thick, const game_state *state)
{
	int tx, ty, color;
	int i = x+(y*state->params->order);
	if(state->holes[i] != LATINH_CROSS)
		return;
	
	tx = (x+1)*TILE_SIZE;
	ty = (y+1)*TILE_SIZE;
	
	color = (state->gridclues[i] ? COL_I_HOLE : 
		ds->gridfs[i] & FD_ERROR ? COL_E_HOLE : COL_G_HOLE);
	draw_thick_line(dr, thick,
		tx + (TILE_SIZE*0.2), ty + (TILE_SIZE*0.2),
		tx + (TILE_SIZE*0.8), ty + (TILE_SIZE*0.8),
		color);
	draw_thick_line(dr, thick,
		tx + (TILE_SIZE*0.2), ty + (TILE_SIZE*0.8),
		tx + (TILE_SIZE*0.8), ty + (TILE_SIZE*0.2),
		color);
}

static void game_redraw(drawing *dr, game_drawstate *ds, const game_state *oldstate,
			const game_state *state, int dir, const game_ui *ui,
			float animtime, float flashtime)
{
	int mode = state->params->mode;
	int o = state->params->order;
	int nums = state->params->nums;
	int x, y, i, j, tx, ty, color;
	char base = (mode == GAMEMODE_LETTERS ? 'A' - 1 : '0');
	char buf[80];
	bool hshow = ui->hshow;
	double thick = (TILE_SIZE <= 21 ? 1 : 2.5);
	
	int flash = -1;
	if(flashtime > 0)
	{
		flash = (int)(flashtime / FLASH_FRAME) % 3;
		hshow = false;
	}
	
	if(ds->redraw)
	{
		/* Draw background */
		draw_rect(dr, 0, 0, (o+2)*TILE_SIZE, (o+2)*TILE_SIZE, COL_BACKGROUND);

#ifndef STYLUS_BASED	
		/* Draw the status bar only when there is no virtual keyboard */
		sprintf(buf, "%c~%c", base + 1, base + nums);
		status_bar(dr, buf);
#endif
		
		draw_update(dr, 0, 0, (o+2)*TILE_SIZE, (o+2)*TILE_SIZE);
	}
	
	salad_set_drawflags(ds, ui, state, hshow);
	
	buf[1] = '\0';
	for(x = 0; x < o; x++)
		for(y = 0; y < o; y++)
		{
			tx = (x+1)*TILE_SIZE;
			ty = (y+1)*TILE_SIZE;
			i = x+(y*o);
			
			if(!ds->redraw && ds->oldgridfs[i] == ds->gridfs[i] && 
					ds->grid[i] == state->grid[i] && 
					ds->marks[i] == state->marks[i] &&
					ds->oldflash == flash)
				continue;
			
			ds->oldgridfs[i] = ds->gridfs[i];
			ds->grid[i] = state->grid[i];
			ds->marks[i] = state->marks[i];
			
			draw_update(dr, tx, ty, TILE_SIZE, TILE_SIZE);
			
			/* Draw cursor */
			if(mode == GAMEMODE_LETTERS && flash >= 0)
			{
				color = (x+y) % 3 == flash ? COL_BACKGROUND :
						(x+y+1) % 3 == flash ? COL_LOWLIGHT : COL_HIGHLIGHT;
				
				draw_rect(dr, tx, ty, TILE_SIZE, TILE_SIZE, color);
			}
			else
				draw_rect(dr, tx, ty, TILE_SIZE, TILE_SIZE, COL_BACKGROUND);
			
			if(flash == -1 && ds->gridfs[y*o+x] & FD_PENCIL)
			{
				int coords[6];
				coords[0] = tx;
				coords[1] = ty;
				coords[2] = tx+(TILE_SIZE/2);
				coords[3] = ty;
				coords[4] = tx;
				coords[5] = ty+(TILE_SIZE/2);
				draw_polygon(dr, coords, 3, COL_LOWLIGHT, COL_LOWLIGHT);
			}
			else if(flash == -1 && ds->gridfs[y*o+x] & FD_CURSOR)
			{
				draw_rect(dr, tx, ty, TILE_SIZE, TILE_SIZE, COL_LOWLIGHT);
			}
			
			/* Define a square */
			int sqc[8];
			sqc[0] = tx;
			sqc[1] = ty - 1;
			sqc[2] = tx + TILE_SIZE;
			sqc[3] = ty - 1;
			sqc[4] = tx + TILE_SIZE;
			sqc[5] = ty + TILE_SIZE - 1;
			sqc[6] = tx;
			sqc[7] = ty + TILE_SIZE - 1;
			draw_polygon(dr, sqc, 4, -1, COL_BORDER);
			
			if(ds->gridfs[i] & FD_CIRCLE)
				salad_draw_balls(dr, ds, x, y, flash, state);
			else if(ds->gridfs[i] & FD_CROSS)
				salad_draw_cross(dr, ds, x, y, thick, state);
			
			/* Draw pencil marks */
			if(state->grid[i] == 0 && state->holes[i] != LATINH_CROSS)
			{
				if(state->holes[i] == LATINH_CIRCLE)
				{
					/* Draw the clues smaller */
					salad_draw_pencil(dr, state, x, y, base, TILE_SIZE * 0.8F, 
						(x+1.1F)*TILE_SIZE, (y+1.1F)*TILE_SIZE);
				}
				else
				{
					salad_draw_pencil(dr, state, x, y, base, TILE_SIZE, tx, ty);
				}
			}
			
			/* Draw number/letter */
			else if(state->grid[i] != 0)
			{
				color = (state->gridclues[i] > 0 && state->gridclues[i] <= o ? COL_I_NUM :
					ds->gridfs[i] & FD_ERROR ? COL_E_NUM : COL_G_NUM);
				buf[0] = state->grid[i] + base;
				
				draw_text(dr, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
					FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE, color,
					buf);
			}
		}
	
	/* Draw border clues */
	for(i = 0; i < o; i++)
	{
		/* Top */
		j = i;
		if(state->borderclues[j] != 0 && ds->borderfs[j] != ds->oldborderfs[j])
		{
			color = ds->borderfs[j] & FD_ERROR ? COL_E_BORDERCLUE : COL_BORDERCLUE;
			tx = (i+1)*TILE_SIZE;
			ty = 0;
			draw_rect(dr, tx, ty, TILE_SIZE-1, TILE_SIZE-1, COL_BACKGROUND);
			draw_update(dr, tx, ty, tx+TILE_SIZE-1, ty+TILE_SIZE-1);
			buf[0] = state->borderclues[j] + base;
			draw_text(dr, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
				FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE, color,
				buf);
			ds->oldborderfs[j] = ds->borderfs[j];
		}
		/* Left */
		j = i+o;
		if(state->borderclues[j] != 0 && ds->borderfs[j] != ds->oldborderfs[j])
		{
			color = ds->borderfs[j] & FD_ERROR ? COL_E_BORDERCLUE : COL_BORDERCLUE;
			tx = 0;
			ty = (i+1)*TILE_SIZE;
			draw_rect(dr, tx, ty, TILE_SIZE-1, TILE_SIZE-1, COL_BACKGROUND);
			draw_update(dr, tx, ty, tx+TILE_SIZE-1, ty+TILE_SIZE-1);
			buf[0] = state->borderclues[j] + base;
			draw_text(dr, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
				FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE, color,
				buf);
			ds->oldborderfs[j] = ds->borderfs[j];
		}
		/* Bottom */
		j = i+(o*2);
		if(state->borderclues[j] != 0 && ds->borderfs[j] != ds->oldborderfs[j])
		{
			color = ds->borderfs[j] & FD_ERROR ? COL_E_BORDERCLUE : COL_BORDERCLUE;
			tx = (i+1)*TILE_SIZE;
			ty = (o+1)*TILE_SIZE;
			draw_rect(dr, tx, ty, TILE_SIZE-1, TILE_SIZE-1, COL_BACKGROUND);
			draw_update(dr, tx, ty, tx+TILE_SIZE-1, ty+TILE_SIZE-1);
			buf[0] = state->borderclues[j] + base;
			draw_text(dr, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
				FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE, color,
				buf);
			ds->oldborderfs[j] = ds->borderfs[j];
		}
		/* Right */
		j = i+(o*3);
		if(state->borderclues[j] != 0 && ds->borderfs[j] != ds->oldborderfs[j])
		{
			color = ds->borderfs[j] & FD_ERROR ? COL_E_BORDERCLUE : COL_BORDERCLUE;
			tx = (o+1)*TILE_SIZE;
			ty = (i+1)*TILE_SIZE;
			draw_rect(dr, tx+1, ty+1, TILE_SIZE-2, TILE_SIZE-2, COL_BACKGROUND);
			draw_update(dr, tx+1, ty+1, tx+TILE_SIZE-2, ty+TILE_SIZE-2);
			buf[0] = state->borderclues[j] + base;
			draw_text(dr, tx + TILE_SIZE/2, ty + TILE_SIZE/2,
				FONT_VARIABLE, TILE_SIZE/2, ALIGN_HCENTRE|ALIGN_VCENTRE, color,
				buf);
			ds->oldborderfs[j] = ds->borderfs[j];
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

/* Using 8mm squares */
#define PRINT_SQUARE_SIZE 800
static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
	int pw, ph;

	/* Add an extra line for the character range 
	 (which would normally be in the statusbar) */
	game_compute_size(params, PRINT_SQUARE_SIZE, ui, &pw, &ph);
	*x = pw / 100.0F;
	*y = (ph+PRINT_SQUARE_SIZE) / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int tilesize)
{
	int o = state->params->order;
	int mode = state->params->mode;
	char base = mode == GAMEMODE_LETTERS ? 'A' - 1 : '0';
	int i, x, y, tx, ty;
	
	int ink = print_mono_colour(dr, 0);
	int paper = print_mono_colour(dr, 1);
	
	char buf[80];
	
	/* Draw character range */
	tx = tilesize/2;
	ty = ((o+1) * tilesize) + tilesize/2;
	sprintf(buf, "%c~%c", base + 1, base + state->params->nums);
	draw_text(dr, tx + tilesize/2, ty + tilesize/2,
		FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
		ink, buf);
	
	buf[1] = '\0';

	/* Draw border clues */
	for(i = 0; i < o; i++)
	{
		/* Top */
		if(state->borderclues[i] != 0)
		{
			tx = (i+1)*tilesize;
			ty = 0;
			buf[0] = state->borderclues[i] + base;
			draw_text(dr, tx, ty,
				FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE, ink,
				buf);
		}
		/* Left */
		if(state->borderclues[i+o] != 0)
		{
			tx = 0;
			ty = (i+1)*tilesize;
			buf[0] = state->borderclues[i+o] + base;
			draw_text(dr, tx, ty,
				FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE, ink,
				buf);
		}
		/* Bottom */
		if(state->borderclues[i+(o*2)] != 0)
		{
			tx = (i+1)*tilesize;
			ty = (o+1)*tilesize;
			buf[0] = state->borderclues[i+(o*2)] + base;
			draw_text(dr, tx, ty,
				FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE, ink,
				buf);
		}
		/* Right */
		if(state->borderclues[i+(o*3)] != 0)
		{
			tx = (o+1)*tilesize;
			ty = (i+1)*tilesize;
			buf[0] = state->borderclues[i+(o*3)] + base;
			draw_text(dr, tx, ty,
				FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE, ink,
				buf);
		}
	}
	
	/* Draw grid */
	for (x = 0; x < o; x++)
		for (y = 0; y < o; y++)
		{
			int tx = x*tilesize + (tilesize/2);
			int ty = y*tilesize + (tilesize/2);
			
			/* Draw the border */
			int coords[8];
			coords[0] = tx;
			coords[1] = ty - 1;
			coords[2] = tx + tilesize;
			coords[3] = ty - 1;
			coords[4] = tx + tilesize;
			coords[5] = ty + tilesize - 1;
			coords[6] = tx;
			coords[7] = ty + tilesize - 1;
			draw_polygon(dr, coords, 4, -1, ink);
			
			/* Draw cross */
			if(state->gridclues[y*o+x] == LATINH_CROSS)
			{
				draw_thick_line(dr, 2.5,
					tx + (tilesize*0.2), ty + (tilesize*0.2),
					tx + (tilesize*0.8), ty + (tilesize*0.8),
					ink);
				draw_thick_line(dr, 2.5,
					tx + (tilesize*0.2), ty + (tilesize*0.8),
					tx + (tilesize*0.8), ty + (tilesize*0.2),
					ink);
			}
			/* Draw circle */
			if(state->gridclues[y*o+x] == LATINH_CIRCLE ||
				(mode != GAMEMODE_LETTERS && state->holes[y*o+x] == LATINH_CIRCLE))
			{
				draw_circle(dr, tx+ (tilesize/2), ty+ (tilesize/2), tilesize*0.4, ink, ink);
				draw_circle(dr, tx+ (tilesize/2), ty+ (tilesize/2), tilesize*0.38, paper, ink);
			}
			
			/* Draw character */
			if (state->grid[y*o+x] != 0)
			{
				buf[0] = state->grid[y*o+x] + base;
				
				draw_text(dr, tx + tilesize/2, ty + tilesize/2,
						FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
						ink, buf);
			}
		}
}

#ifdef COMBINED
#define thegame salad
#endif

const struct game thegame = {
	"Salad", NULL, NULL,
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
	DEFAULT_TILE_SIZE, game_compute_size, game_set_size,
	game_colours,
	game_new_drawstate,
	game_free_drawstate,
	game_redraw,
	game_anim_length,
	game_flash_length,
	game_get_cursor_location,
	game_status,
	true, false, game_print_size, game_print,
#ifndef STYLUS_BASED
	true,			       /* wants_statusbar */
#else
	false,
#endif
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
	fprintf(stderr, "Usage: %s [-v] [--seed SEED] <params> | [game_id [game_id ...]]\n", quis);
	exit(1);
}

int main(int argc, char *argv[])
{
	random_state *rs;
	time_t seed = time(NULL);
	int i, attempts = 1;
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
			solver_show_working = true;
		else if(!strcmp(p, "--soak"))
			attempts = 10000;
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
		char *desc_gen = NULL;
		char *aux = NULL;
		char *fail = NULL;
		char *fmt = NULL;
		printf("Generating puzzle with parameters %s\n", encode_params(params, true));
		
		for(i = 0; i < attempts; i++)
		{
			desc_gen = new_game_desc(params, rs, &aux, false);
			printf("Game ID: %s\n",desc_gen);
			
			game_state *state = load_game(params, desc_gen, &fail);
			if(fail)
			{
				printf("The generated puzzle was invalid: %s\n", fail);
				return -1;
			}
			
			fmt = game_text_format(state);
			printf("%s\n", fmt);
			sfree(fmt);
			
			free_game(state);
			sfree(desc_gen);
			sfree(aux);
		}
	}
	else
	{
		char *fmt = NULL;
		err = validate_desc(params, desc);
		if (err)
		{
			fprintf(stderr, "Description is invalid\n");
			fprintf(stderr, "%s", err);
			exit(1);
		}
		
		game_state *state = new_game(NULL, params, desc);
		salad_solve(state, DIFF_HARD);
		fmt = game_text_format(state);
		printf("%s\n", fmt);
		
		free_game(state);
		sfree(fmt);
	}
	
	return 0;
}
#endif
