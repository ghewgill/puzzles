/*
 * mathrax.c : Implementation for Mathrax puzzles.
 * (C) 2019 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 * 
 * Objective: Fill in the latin square with digits 1 to N.
 * Some grid intersections contain clues:
 * - An 'E' indicates that the four adjacent digits are even.
 * - An 'O' indicates that the four adjacent digits are odd.
 * - A number indicates the result of the given operation when
 *   applied to each pair of diagonally adjacent digits.
 *   (topleft * bottomright) = (topright * bottomleft)
 * - An '=' indicates that diagonally adjacent digits are equal.
 * 
 * The inventor of this puzzle type is unknown.
 * More information:
 * http://www.janko.at/Raetsel/Mathrax/index.htm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "latin.h"

enum {
	COL_BACKGROUND,
	COL_HIGHLIGHT,
	COL_LOWLIGHT,
	COL_BORDER,
	COL_GUESS,
	COL_PENCIL,
	COL_ERROR,
	COL_ERRORBG,
	NCOLOURS
};

#define DIFFLIST(A)                             \
    A(EASY,Easy, e)                             \
    A(NORMAL,Normal, n)                         \
    A(TRICKY,Tricky, t)                         \
    A(RECURSIVE,Recursive, r)                   \

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
static char const *const mathrax_diffnames[] = { DIFFLIST(TITLE) };

static char const mathrax_diffchars[] = DIFFLIST(ENCODE);

struct game_params {
	int o, diff, options;
};

typedef unsigned int clue_t;

enum {
	CLUE_ADD = 1,
	CLUE_SUB,
	CLUE_MUL,
	CLUE_DIV,
	CLUE_EVN,
	CLUE_ODD
};
#define CLUEMASK       7

#define OPTION_ADD     1
#define OPTION_SUB     2
#define OPTION_MUL     4
#define OPTION_DIV     8
#define OPTION_EQL     16
#define OPTION_ODD     32

#define OPTIONSMASK    63

#define CLUENUM(x)     (int)((x)>>3)
#define SET_CLUENUM(x) (clue_t)((x)<<3)

#define F_IMMUTABLE    0x01
#define FE_COUNT       0x02
#define FE_TOPLEFT     0x04
#define FE_TOPRIGHT    0x08
#define FE_BOTLEFT     0x10
#define FE_BOTRIGHT    0x20
#define FE_ERRORMASK   0x3E

#define FD_FLASH      0x100
#define FD_CURSOR     0x200
#define FD_PENCIL     0x400

typedef unsigned int marks_t;

struct game_state {
	int o;
	digit *grid;
	unsigned int *flags;
	marks_t *marks;
	clue_t *clues;
	
	bool completed, cheated;
};

enum { DIFFLIST(ENUM) DIFFCOUNT,
       DIFF_IMPOSSIBLE = diff_impossible,
       DIFF_AMBIGUOUS = diff_ambiguous,
       DIFF_UNFINISHED = diff_unfinished };

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);

	ret->o = 5;
	ret->diff = DIFF_EASY;
	ret->options = OPTIONSMASK;

	return ret;
}


const struct game_params mathrax_presets[] = {
	{ 5, DIFF_EASY, OPTIONSMASK },
	{ 5, DIFF_NORMAL, OPTIONSMASK },
	{ 5, DIFF_TRICKY, OPTIONSMASK },
	{ 6, DIFF_EASY, OPTIONSMASK },
	{ 6, DIFF_NORMAL, OPTIONSMASK },
	{ 6, DIFF_TRICKY, OPTIONSMASK },
	{ 7, DIFF_NORMAL, OPTIONSMASK },
	{ 8, DIFF_NORMAL, OPTIONSMASK },
	{ 9, DIFF_NORMAL, OPTIONSMASK },
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
	if (i < 0 || i >= lenof(mathrax_presets))
		return false;
		
	game_params *ret = snew(game_params);
	*ret = mathrax_presets[i]; /* struct copy */
	*params = ret;
	
	int o = ret->o;
	
	char buf[80];
	sprintf(buf, "%dx%d %s", o, o, mathrax_diffnames[ret->diff]);
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

static void decode_params(game_params *ret, char const *string)
{
	char const *p = string;

	ret->options = 0;
	ret->o = atoi(p);
	while (*p && isdigit((unsigned char)*p)) p++;

	if (*p == 'd') {
		int i;
		p++;
		ret->diff = DIFFCOUNT + 1; /* ...which is invalid */
		if (*p) {
			for (i = 0; i < DIFFCOUNT; i++) {
				if (*p == mathrax_diffchars[i])
					ret->diff = i;
			}
			p++;
		}
	}

	if(*p == 'A')
	{
		ret->options |= OPTION_ADD;
		p++;
	}
	if(*p == 'S')
	{
		ret->options |= OPTION_SUB;
		p++;
	}
	if(*p == 'M')
	{
		ret->options |= OPTION_MUL;
		p++;
	}
	if(*p == 'D')
	{
		ret->options |= OPTION_DIV;
		p++;
	}
	if(*p == 'E')
	{
		ret->options |= OPTION_EQL;
		p++;
	}
	if(*p == 'O')
	{
		ret->options |= OPTION_ODD;
		p++;
	}

	if(!ret->options)
		ret->options = OPTIONSMASK;
}

static char *encode_params(const game_params *params, bool full)
{
	char ret[80];

	sprintf(ret, "%d", params->o);
	if (full)
	{
		sprintf(ret + strlen(ret), "d%c", mathrax_diffchars[params->diff]);
		if(params->options != OPTIONSMASK)
		{
			if(params->options & OPTION_ADD)
				sprintf(ret + strlen(ret), "A");
			if(params->options & OPTION_SUB)
				sprintf(ret + strlen(ret), "S");
			if(params->options & OPTION_MUL)
				sprintf(ret + strlen(ret), "M");
			if(params->options & OPTION_DIV)
				sprintf(ret + strlen(ret), "D");
			if(params->options & OPTION_EQL)
				sprintf(ret + strlen(ret), "E");
			if(params->options & OPTION_ODD)
				sprintf(ret + strlen(ret), "O");
		}
	}

	return dupstr(ret);
}

static config_item *game_configure(const game_params *params)
{
	config_item *ret;
	char buf[80];
	
	ret = snewn(9, config_item);
	
	ret[0].name = "Size";
	ret[0].type = C_STRING;
	sprintf(buf, "%d", params->o);
	ret[0].u.string.sval = dupstr(buf);
	
	ret[1].name = "Difficulty";
	ret[1].type = C_CHOICES;
	ret[1].u.choices.choicenames = DIFFLIST(CONFIG);
	ret[1].u.choices.selected = params->diff;
	
	ret[2].name = "Addition clues";
	ret[2].type = C_BOOLEAN;
	ret[2].u.boolean.bval = params->options & OPTION_ADD;

	ret[3].name = "Subtraction clues";
	ret[3].type = C_BOOLEAN;
	ret[3].u.boolean.bval = params->options & OPTION_SUB;

	ret[4].name = "Multiplication clues";
	ret[4].type = C_BOOLEAN;
	ret[4].u.boolean.bval = params->options & OPTION_MUL;

	ret[5].name = "Division clues";
	ret[5].type = C_BOOLEAN;
	ret[5].u.boolean.bval = params->options & OPTION_DIV;

	ret[6].name = "Equality clues";
	ret[6].type = C_BOOLEAN;
	ret[6].u.boolean.bval = params->options & OPTION_EQL;

	ret[7].name = "Even/odd clues";
	ret[7].type = C_BOOLEAN;
	ret[7].u.boolean.bval = params->options & OPTION_ODD;

	ret[8].name = NULL;
	ret[8].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = snew(game_params);
	
	ret->o = atoi(cfg[0].u.string.sval);
	ret->diff = cfg[1].u.choices.selected;

	ret->options = 0;
	if(cfg[2].u.boolean.bval)
		ret->options |= OPTION_ADD;
	if(cfg[3].u.boolean.bval)
		ret->options |= OPTION_SUB;
	if(cfg[4].u.boolean.bval)
		ret->options |= OPTION_MUL;
	if(cfg[5].u.boolean.bval)
		ret->options |= OPTION_DIV;
	if(cfg[6].u.boolean.bval)
		ret->options |= OPTION_EQL;
	if(cfg[7].u.boolean.bval)
		ret->options |= OPTION_ODD;

	return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
	if(params->o < 3)
		return "Size must be at least 3";
	if(params->o > 9)
		return "Size must be no more than 9";
	if (params->diff >= DIFFCOUNT)
		return "Unknown difficulty rating";
	if(full && !params->options)
		return "At least one clue type must be enabled";

	return NULL;
}

static game_state *blank_game(int o)
{
	int s = o*o;
	int cs = (o-1)*(o-1);
	game_state *state = snew(game_state);
	
	state->o = o;
	state->completed = state->cheated = false;

	state->flags = snewn(s, unsigned int);
	state->marks = snewn(s, marks_t);
	state->clues = snewn(cs, clue_t);
	state->grid = snewn(s, digit);
	
	memset(state->flags, 0, s*sizeof(unsigned int));
	memset(state->marks, 0, s*sizeof(marks_t));
	memset(state->clues, 0, cs*sizeof(clue_t));
	memset(state->grid, 0, s*sizeof(digit));
	
	return state;
}

static game_state *dup_game(const game_state *state)
{
	int o = state->o, s = o*o;
	int cs = (o-1)*(o-1);

	game_state *ret = blank_game(o);

	memcpy(ret->flags, state->flags, s*sizeof(unsigned int));
	memcpy(ret->marks, state->marks, s*sizeof(marks_t));
	memcpy(ret->clues, state->clues, cs*sizeof(clue_t));
	memcpy(ret->grid, state->grid, s*sizeof(digit));
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;
	
	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->flags);
	sfree(state->marks);
	sfree(state->clues);
	sfree(state->grid);
	sfree(state);
}

enum { STATUS_COMPLETE, STATUS_UNFINISHED, STATUS_INVALID };
#define BIT(d) (marks_t)(1<<((d)-1))

/*
 * Get all marks which are valid when combined with the marks in the opposite square.
 */
static marks_t mathrax_options(int o, clue_t clue, marks_t mark, bool simple)
{
	clue_t cluetype = clue & CLUEMASK;
	switch(cluetype)
	{
		case CLUE_ADD:
		case CLUE_SUB:
		case CLUE_MUL:
		case CLUE_DIV:
		{
			/*
			 * In Easy mode, only look for options if the other space is confirmed
			 * (only one bit in mark is set)
			 */
			if(simple && mark & (mark - 1))
				return ~0;

			int cnum = CLUENUM(clue);
			marks_t ret = 0;
			digit a, b;
			for(a = 1; a <= 9; a++)
			{
				if(!(mark & BIT(a))) continue;
				for(b = 1; b <= 9; b++)
				{
					if(cluetype == CLUE_ADD && a + b == cnum)
						ret |= BIT(b);
					else if(cluetype == CLUE_SUB && abs(a - b) == cnum)
						ret |= BIT(b);
					else if(cluetype == CLUE_MUL && a * b == cnum)
						ret |= BIT(b);
					else if(cluetype == CLUE_DIV && max(a, b) / min(a, b) == cnum && max(a, b) % min(a, b) == 0)
						ret |= BIT(b);
				}
			}

			return ret;
		}
		case CLUE_EVN:
			/* 2, 4, 6, 8 */
			return 0xAA;
		case CLUE_ODD:
			/* 1, 3, 5, 7, 9 */
			return 0x155;
		default:
			/* All numbers */
			return ~0;
	}
}

static int mathrax_validate_game(game_state *state, int *temp, bool is_solver)
{
	int o = state->o, co = o-1;
	int x, y;
	digit d;
	int ret = STATUS_COMPLETE;
	marks_t maxbits = (1<<o)-1;
	marks_t bits;
	bool hastemp = temp != NULL;
	
	if(!hastemp)
		temp = snewn(o*o*2, int);
	
	memset(temp, 0, o*o*2*sizeof(int));
	for (x = 0; x < o; x++)
	{
		for (y = 0; y < o; y++)
		{
			state->flags[y*o+x] &= ~FE_ERRORMASK;
			d = state->grid[y*o+x];
			if(!d) continue;
			temp[((d-1)*o)+y]++;
			temp[((d-1)*o)+(o*o)+x]++;
		}
	}
	
	for(y = 0; y < o; y++)
	for(x = 0; x < o; x++)
	{
		d = state->grid[y*o+x];
		bits = d ? BIT(d) : is_solver ? state->marks[y*o+x] : maxbits;

		if(!d)
		{
			if(ret == STATUS_COMPLETE)
				ret = STATUS_UNFINISHED;
		}
		else
		{
			if(temp[((d-1)*o)+y] > 1 || temp[((d-1)*o)+(o*o)+x] > 1)
				state->flags[y*o+x] |= FE_COUNT;
		}

		if(y < o-1 && x < o-1)
		{
			int other = state->grid[(y+1)*o+x+1];
			if(!(mathrax_options(o, state->clues[y*co+x], other ? BIT(other) : maxbits, false) & bits))
				state->flags[y*o+x] |= FE_BOTRIGHT;
		}
		if(y > 0 && x < o-1)
		{
			int other = state->grid[(y-1)*o+x+1];
			if(!(mathrax_options(o, state->clues[(y-1)*co+x], other ? BIT(other) : maxbits, false) & bits))
				state->flags[y*o+x] |= FE_TOPRIGHT;
		}
		if(y < o-1 && x > 0)
		{
			int other = state->grid[(y+1)*o+x-1];
			if(!(mathrax_options(o, state->clues[y*co+x-1], other ? BIT(other) : maxbits, false) & bits))
				state->flags[y*o+x] |= FE_BOTLEFT;
		}
		if(y > 0 && x > 0)
		{
			int other = state->grid[(y-1)*o+x-1];
			if(!(mathrax_options(o, state->clues[(y-1)*co+x-1], other ? BIT(other) : maxbits, false) & bits))
				state->flags[y*o+x] |= FE_TOPLEFT;
		}
		
		if(state->flags[y*o+x] & FE_ERRORMASK)
			ret = STATUS_INVALID;
	}
	
	if(!hastemp)
		sfree(temp);
	return ret;
}

struct solver_ctx {
	int o;
	marks_t *marks;
	clue_t *clues;
};

static struct solver_ctx *blank_ctx(int o)
{
	int co = o-1;
	struct solver_ctx *ctx = snew(struct solver_ctx);
	
	ctx->o = o;
	ctx->marks = snewn(o*o, marks_t);
	ctx->clues = snewn(co*co, clue_t);

	return ctx;
}

static struct solver_ctx *new_ctx(game_state *state)
{
	int o = state->o;
	int co = o-1;
	int i;
	marks_t maxbits = (1<<o)-1;
	struct solver_ctx *ctx = blank_ctx(o);

	memcpy(ctx->clues, state->clues, co*co*sizeof(clue_t));

	for(i = 0; i < o*o; i++)
		ctx->marks[i] = state->grid[i] ? BIT(state->grid[i]) : maxbits;

	return ctx;
}

static void *clone_ctx(void *vctx)
{
	struct solver_ctx *octx = (struct solver_ctx *)vctx;
	int o = octx->o;
	int co = o-1;
	struct solver_ctx *nctx = blank_ctx(o);
	
	memcpy(nctx->marks, octx->marks, o*o*sizeof(marks_t));
	memcpy(nctx->clues, octx->clues, co*co*sizeof(clue_t));
	
	return nctx;
}

static void free_ctx(void *vctx)
{
	struct solver_ctx *ctx = (struct solver_ctx *)vctx;
	sfree(ctx->marks);
	sfree(ctx->clues);
	sfree(ctx);
}

/* ****** *
 * Solver *
 * ****** */

static int mathrax_solver_apply_options(struct latin_solver *solver, struct solver_ctx *ctx, int diff)
{
	int o = solver->o, co = o-1;
	int x, y;
	digit d;
	int ret = 0;
	bool simple = diff == DIFF_EASY;

	for(y = 0; y < o; y++)
	for(x = 0; x < o; x++)
	for(d = 1; d <= o; d++)
	{
		/* Synchronize our own marks array with the latin solver. */
		if(!cube(x,y,d))
			ctx->marks[y*o+x] &= ~BIT(d);
	}

	for(y = 0; y < o; y++)
	for(x = 0; x < o; x++)
	{
		/* Remove all marks which would violate one of the grid clues */
		marks_t marks = ctx->marks[y*o+x];
		if(y < o-1 && x < o-1)
			marks &= mathrax_options(o, ctx->clues[y*co+x], ctx->marks[(y+1)*o+x+1], simple);
		if(y > 0 && x < o-1)
			marks &= mathrax_options(o, ctx->clues[(y-1)*co+x], ctx->marks[(y-1)*o+x+1], simple);
		if(y < o-1 && x > 0)
			marks &= mathrax_options(o, ctx->clues[y*co+x-1], ctx->marks[(y+1)*o+x-1], simple);
		if(y > 0 && x > 0)
			marks &= mathrax_options(o, ctx->clues[(y-1)*co+x-1], ctx->marks[(y-1)*o+x-1], simple);

		if(!marks)
			return -1;

		/* 
		 * On Normal mode and below, only apply clues if it immediately 
		 * confirms a number in a square (only one bit is set). 
		 */
		if (diff <= DIFF_NORMAL && (marks & (marks - 1)))
			continue;

		for(d = 1; d <= o; d++)
		{
			/* Synchronize the bitmap back to the latin solver. */
			if(cube(x,y,d) && !(marks & BIT(d)))
			{
				cube(x,y,d) = false;
				ret++;
			}
		}
	}

	return ret;
}

static bool mathrax_valid(struct latin_solver *solver, void *vctx) {
	return true;
}

static int mathrax_solver_easy(struct latin_solver *solver, void *vctx)
{
	struct solver_ctx *ctx = (struct solver_ctx *)vctx;
	return mathrax_solver_apply_options(solver, ctx, DIFF_EASY);
}

static int mathrax_solver_normal(struct latin_solver *solver, void *vctx)
{
	struct solver_ctx *ctx = (struct solver_ctx *)vctx;
	return mathrax_solver_apply_options(solver, ctx, DIFF_NORMAL);
}

static int mathrax_solver_tricky(struct latin_solver *solver, void *vctx)
{
	struct solver_ctx *ctx = (struct solver_ctx *)vctx;
	return mathrax_solver_apply_options(solver, ctx, DIFF_TRICKY);
}


static usersolver_t const mathrax_solvers[] = { mathrax_solver_easy, mathrax_solver_normal, mathrax_solver_tricky, NULL, NULL };

static int mathrax_solve(game_state *state, int maxdiff)
{
	int o = state->o;
	struct solver_ctx *ctx = new_ctx(state);
	struct latin_solver *solver = snew(struct latin_solver);
	int diff;
	
	latin_solver_alloc(solver, state->grid, o);
	
	diff = latin_solver_main(solver, maxdiff,
		DIFF_EASY, DIFF_NORMAL, DIFF_TRICKY,
		DIFF_TRICKY, DIFF_RECURSIVE,
		mathrax_solvers, mathrax_valid, ctx, clone_ctx, free_ctx);
	
	free_ctx(ctx);

	latin_solver_free(solver);
	sfree(solver);

	if (diff == DIFF_IMPOSSIBLE)
		return -1;
	if (diff == DIFF_UNFINISHED)
		return 0;
	if (diff == DIFF_AMBIGUOUS)
		return 2;
	return 1;
}

/* **************** *
 * Puzzle Generator *
 * **************** */

static void mathrax_strip_grid_clues(game_state *state, int diff, random_state *rs)
{
	int o = state->o, o2 = o*o;
	int *spaces = snewn(o2, int);
	digit *grid = snewn(o2, digit);
	int i, j;

	for (i = 0; i < o2; i++) spaces[i] = i;
	shuffle(spaces, o2, sizeof(*spaces), rs);

	for (i = 0; i < o2; i++)
	{
		j = spaces[i];

		/* Space is already empty */
		if (state->grid[j] == 0)
			continue;

		memcpy(grid, state->grid, o2 * sizeof(digit));

		state->grid[j] = 0;

		if (mathrax_solve(state, diff))
			grid[j] = 0;
		memcpy(state->grid, grid, o2 * sizeof(digit));
	}
	sfree(spaces);
	sfree(grid);
}

static void mathrax_strip_math_clues(game_state *state, int diff, random_state *rs)
{
	int o = state->o, o2 = o*o, co = o-1, cs = co*co;
	int *spaces = snewn(cs, int);
	digit *grid = snewn(o2, digit);
	int i, j;
	clue_t temp;

	memcpy(grid, state->grid, o2 * sizeof(digit));

	for (i = 0; i < cs; i++) spaces[i] = i;
	shuffle(spaces, cs, sizeof(*spaces), rs);

	for (i = 0; i < cs; i++)
	{
		j = spaces[i];

		temp = state->clues[j];

		/* Space is already empty */
		if (temp == 0)
			continue;

		state->clues[j] = 0;

		if (!mathrax_solve(state, diff))
			state->clues[j] = temp;
		memcpy(state->grid, grid, o2 * sizeof(digit));
	}
	sfree(spaces);
	sfree(grid);
}

static clue_t mathrax_candidate_clue(digit a1, digit b1, digit a2, digit b2, int options)
{
	assert(a1 && b1 && a2 && b2);

	if (a1 < b1)
	{
		digit temp = a1;
		a1 = b1;
		b1 = temp;
	}
	if (a2 < b2)
	{
		digit temp = a2;
		a2 = b2;
		b2 = temp;
	}

	if ((options & OPTION_ADD) && (a1 + b1 == a2 + b2))
		return CLUE_ADD | SET_CLUENUM(a1 + b1);
	if ((options & OPTION_SUB) && (a1 - b1 == a2 - b2 && a1 - b1 > 0))
		return CLUE_SUB | SET_CLUENUM(a1 - b1);
	if ((options & OPTION_EQL) && (a1 == b1 && a2 == b2))
		return CLUE_SUB | SET_CLUENUM(0);
	if ((options & OPTION_MUL) && (a1 * b1 == a2 * b2))
		return CLUE_MUL | SET_CLUENUM(a1 * b1);
	if ((options & OPTION_DIV) && (a1 / b1 == a2 / b2 && a1 % b1 == 0 && a2 % b2 == 0 && a1 / b1 != 1))
		return CLUE_DIV | SET_CLUENUM(a1 / b1);
	if ((options & OPTION_ODD) && (a1 & b1 & a2 & b2 & 1))
		return CLUE_ODD;
	if ((options & OPTION_ODD) && !((a1 | b1 | a2 | b2) & 1))
		return CLUE_EVN;

	return 0;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	int o = params->o, s = o*o, i, x, y, co = o-1, cs = co*co;
	int options = params->options;
	if(!options) options = OPTIONSMASK;
	
	game_state *state = blank_game(o);
	
	state->grid = latin_generate(o, rs);
	
	
	for (y = 0; y < co; y++)
	for (x = 0; x < co; x++)
	{
		state->clues[y*co + x] = mathrax_candidate_clue(
			state->grid[y*o + x],
			state->grid[(y+1)*o + x + 1],
			state->grid[(y+1)*o + x],
			state->grid[y*o + x + 1],
			options
		);
	}

	mathrax_strip_grid_clues(state, params->diff, rs);
	mathrax_strip_math_clues(state, params->diff, rs);
	
	char *ret, *p;
	ret = snewn((s*3) + 2, char);
	p = ret;
	int run = 0;
	for (i = 0; i < s; i++)
	{
		if (state->grid[i] != 0)
		{
			if (run)
			{
				*p++ = ('a'-1) + run;
				run = 0;
			}
			*p++ = state->grid[i] + '0';
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
	if (run)
		*p++ = ('a'-1) + run;

	*p++ = ',';
	run = 0;
	for (i = 0; i < cs; i++)
	{
		clue_t clue = state->clues[i];
		if (clue != 0)
		{
			if (run)
			{
				*p++ = ('a' - 1) + run;
				run = 0;
			}
			switch (clue & CLUEMASK)
			{
				case CLUE_ADD:
					p += sprintf(p, "A%d", CLUENUM(clue));
					break;
				case CLUE_SUB:
					p += sprintf(p, "S%d", CLUENUM(clue));
					break;
				case CLUE_MUL:
					p += sprintf(p, "M%d", CLUENUM(clue));
					break;
				case CLUE_DIV:
					p += sprintf(p, "D%d", CLUENUM(clue));
					break;
				case CLUE_EVN:
					p += sprintf(p, "E");
					break;
				case CLUE_ODD:
					p += sprintf(p, "O");
					break;
				default:
					/* Should never get here. Place an empty space just to be safe */
					p += sprintf(p, "a");
			}
		}
		else
		{
			if (run == 26)
			{
				*p++ = ('a' - 1) + run;
				run = 0;
			}
			run++;
		}
	}
	if (run)
		*p++ = ('a' - 1) + run;
	
	free_game(state);
	
	*p++ = '\0';
	return ret;
}

static game_state *load_game(const game_params *params, const char *desc, const char **fail)
{
	int o = params->o, s = o*o, co = o-1, cs = co*co;
	const char *p = desc;
	char c;
	digit d;
	int pos, num;
	
	game_state *ret = blank_game(o);
	
	pos = 0;
	while(*p && *p != ',')
	{
		c = *p++;
		d = 0;
		if(pos >= s)
		{
			free_game(ret);
			*fail = "Grid description is too long.";
			return NULL;
		}
		
		if(c >= 'a' && c <= 'z')
			pos += (c - 'a') + 1;
		else if(c >= '1' && c <= '9')
			d = c - '0';
		else
		{
			free_game(ret);
			*fail = "Grid description contains invalid characters.";
			return NULL;
		}
		
		if(d > 0 && d <= o)
		{
			ret->flags[pos] |= F_IMMUTABLE;
			ret->grid[pos] = d;
			pos++;
		}
		else if(d > o)
		{
			free_game(ret);
			*fail = "Grid clue is out of range.";
			return NULL;
		}
	}
	
	if(pos > 0 && pos < s)
	{
		free_game(ret);
		*fail = "Description is too short.";
		return NULL;
	}

	if(*p == ',')
	{
		p++;
		pos = 0;
		while(*p)
		{
			if(pos >= cs)
			{
				free_game(ret);
				*fail = "Clue description is too long.";
				return NULL;
			}
			
			c = *p++;
			
			if(c >= 'a' && c <= 'z')
				pos += (c - 'a') + 1;
			if(c >= 'A' && c <= 'Z')
			{
				switch(c)
				{
					case 'A':
						ret->clues[pos] = CLUE_ADD;
						break;
					case 'S':
						ret->clues[pos] = CLUE_SUB;
						break;
					case 'M':
						ret->clues[pos] = CLUE_MUL;
						break;
					case 'D':
						ret->clues[pos] = CLUE_DIV;
						break;
					case 'E':
						ret->clues[pos] = CLUE_EVN;
						break;
					case 'O':
						ret->clues[pos] = CLUE_ODD;
						break;
					default:
						free_game(ret);
						*fail = "Invalid clue in description.";
						return NULL;
				}
				num = atoi(p);
				if(num > 99)
				{
					free_game(ret);
					*fail = "Number is too high in clue description.";
					return NULL;
				}
				ret->clues[pos++] |= SET_CLUENUM(num);
				while(*p && isdigit((unsigned char) *p)) p++;
			}
		}
		
		if(pos > 0 && pos < cs)
		{
			free_game(ret);
			*fail = "Clue description is too short.";
			return NULL;
		}
	}
	
	return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	const char *fail = NULL;
	game_state *state = load_game(params, desc, &fail);
	if(state)
		free_game(state);
	
	return fail;
}

static game_state *new_game(midend *me, const game_params *params,
							const char *desc)
{
	const char *fail = NULL;
	
	game_state *state = load_game(params, desc, &fail);
	if(!state)
		fatal("Load game failed: %s", fail);
	
	return state;
}

static char *solve_game(const game_state *state, const game_state *currstate,
						const char *aux, const char **error)
{
	int i;
	int o = state->o, s = o*o;
	char *ret = snewn(s+2, char);
	game_state *solved = dup_game(state);
	
	mathrax_solve(solved, DIFF_RECURSIVE);
	
	ret[0] = 'S';
	for(i = 0; i < s; i++)
		ret[i+1] = '0' + solved->grid[i];
	ret[s+1] = '\0';
	
	free_game(solved);
	return ret;
}

/* ************** *
 * User Interface *
 * ************** */

static bool game_can_format_as_text_now(const game_params *params)
{
	return true;
}

static char *game_text_format(const game_state *state)
{
	return NULL;
}

static key_label *game_request_keys(const game_params *params, int *nkeys)
{
	int i;
	int n = params->o;

	key_label *keys = snewn(n + 1, key_label);
	*nkeys = n + 1;

	for (i = 0; i < n; i++)
	{
		keys[i].button = '1' + i;
		keys[i].label = NULL;
	}
	keys[n].button = '\b';
	keys[n].label = NULL;

	return keys;
}

struct game_ui
{
	int hx, hy;
	bool cshow, ckey, cpencil;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ret = snew(game_ui);
	
	ret->hx = ret->hy = 0;
	ret->cshow = ret->ckey = ret->cpencil = false;
	
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
    if (ui->cshow && (button == CURSOR_SELECT))
        return ui->cpencil ? "Ink" : "Pencil";
    return "";
}

struct mathrax_symbols {
	char *minus_sign, *times_sign, *divide_sign;
};

struct game_drawstate {
	bool redraw;
	int tilesize;
	
	struct mathrax_symbols symbols;

	digit *grid;
	unsigned int *flags;
	marks_t *marks;
};

#define FROMCOORD(x) ( ((x)-(tilesize/2)) / tilesize )
static char *interpret_move(const game_state *state, game_ui *ui,
							const game_drawstate *ds,
							int ox, int oy, int button)
{
	int o = state->o;
	int tilesize = ds->tilesize;
	
	int gx = FROMCOORD(ox);
	int gy = FROMCOORD(oy);
	int hx = ui->hx;
	int hy = ui->hy;
	
	char buf[80];
	
	button &= ~MOD_MASK;
	
	/* Mouse click */
	if (gx >= 0 && gx < o && gy >= 0 && gy < o)
	{
		/* Select square for letter placement */
		if (button == LEFT_BUTTON)
		{
			/* Select */
			if(!ui->cshow || ui->cpencil || hx != gx || hy != gy)
			{
				ui->hx = gx;
				ui->hy = gy;
				ui->cpencil = false;
				ui->cshow = true;
			}
			/* Deselect */
			else
			{
				ui->cshow = false;
			}
			
			if(state->flags[gy*o+gx] & F_IMMUTABLE)
				ui->cshow = false;
			
			ui->ckey = false;
			return MOVE_UI_UPDATE;
		}
		/* Select square for marking */
		else if (button == RIGHT_BUTTON)
		{
			/* Select */
			if(!ui->cshow || !ui->cpencil || hx != gx || hy != gy)
			{
				ui->hx = gx;
				ui->hy = gy;
				ui->cpencil = true;
				ui->cshow = true;
			}
			/* Deselect */
			else
			{
				ui->cshow = false;
			}
			
			/* Remove the cursor again if the clicked square has a confirmed number */
			if(state->grid[gy*o+gx] != 0)
				ui->cshow = false;
			
			ui->ckey = false;
			return MOVE_UI_UPDATE;
		}
	}
	
	/* Keyboard move */
	if (IS_CURSOR_MOVE(button))
	{
		move_cursor(button, &ui->hx, &ui->hy, o, o, 0, NULL);
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
			
		/* Don't enter numbers out of range */
		if (c > o)
			return NULL;
		/* When in pencil mode, filled in squares cannot be changed */
		if (ui->cpencil && state->grid[hy*o+hx] != 0)
			return NULL;
		/* Avoid moves which don't change anything */
		if (!ui->cpencil && state->grid[hy*o+hx] == c)
		{
			if(ui->ckey) return NULL;
			ui->cshow = false;
			return MOVE_UI_UPDATE;
		}
		/* Don't edit immutable numbers */
		if (state->flags[hy*o+hx] & F_IMMUTABLE)
			return NULL;
		
		
		sprintf(buf, "%c%d,%d,%c",
				(char)(ui->cpencil ? 'P' : 'R'),
				hx, hy,
				(char)(c != 0 ? '0' + c : '-')
		);
		
		/* When not in keyboard mode, hide cursor */
		if (!ui->ckey && !ui->cpencil)
			ui->cshow = false;
		
		return dupstr(buf);
	}
	
	/* Set maximum pencil marks on all empty cells */
	if(button == 'M' || button == 'm')
	{
		int i;
		for(i = 0; i < o*o; i++)
		{
			/* Only move if anything changes */
			if(state->grid[i] == 0 && state->marks[i] != (1<<o)-1)
				return dupstr("M");
		}
	}
	
	return NULL;
}

static game_state *execute_move(const game_state *oldstate, const char *move)
{
	int o = oldstate->o;
	int x, y;
	char c;
	game_state *state;
	
	/* Place number or pencil mark */
	if ((move[0] == 'P' || move[0] == 'R') &&
			sscanf(move+1, "%d,%d,%c", &x, &y, &c) == 3 &&
			x >= 0 && x < o && y >= 0 && y < o &&
			((c >= '1' && c <= '9' ) || c == '-')
			)
	{
		if(oldstate->flags[y*o+x] & F_IMMUTABLE)
			return NULL;
		
		state = dup_game(oldstate);
		
		if(move[0] == 'R')
		{
			if(c == '-')
				state->grid[y*o+x] = 0;
			else
				state->grid[y*o+x] = c - '0';
		}
		if(move[0] == 'P')
		{
			if(c == '-')
				state->marks[y*o+x] = 0;
			else
				state->marks[y*o+x] ^= 1<<(c - '1');
		}
		
		if(mathrax_validate_game(state, NULL, false) == STATUS_COMPLETE)
			state->completed = true;
		return state;
	}
	
	/* Read solution */
	if(move[0] == 'S')
	{
		const char *p = move + 1;
		int i = 0;
		state = dup_game(oldstate);
		
		while(*p && i < o*o)
		{
			if(!(state->flags[i] & F_IMMUTABLE))
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
		
		state->completed = (mathrax_validate_game(state, NULL, false) == STATUS_COMPLETE);
		state->cheated = state->completed;
		return state;
	}
	
	/* Set maximum pencil marks */
	if(move[0] == 'M')
	{
		int i;
		state = dup_game(oldstate);
		for(i = 0; i < o*o; i++)
		{
			if(state->grid[i] == 0)
				state->marks[i] = (1<<o)-1;
		}
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
	if(ui->cshow) {
		*x = (ui->hx+0.5) * ds->tilesize;
		*y = (ui->hy+0.5) * ds->tilesize;
		*w = *h = ds->tilesize;
	}
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	int o = params->o;
	
	*x = *y = (o+1) * tilesize;
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
	
	ret[COL_GUESS*3 + 0] = 0.0F;
	ret[COL_GUESS*3 + 1] = 0.5F;
	ret[COL_GUESS*3 + 2] = 0.0F;
	
	ret[COL_PENCIL*3 + 0] = 0.0F;
	ret[COL_PENCIL*3 + 1] = 0.5F;
	ret[COL_PENCIL*3 + 2] = 0.5F;
	
	ret[COL_ERROR*3 + 0] = 1.0F;
	ret[COL_ERROR*3 + 1] = 0.0F;
	ret[COL_ERROR*3 + 2] = 0.0F;
	
	ret[COL_ERRORBG*3 + 0] = 1.0F;
	ret[COL_ERRORBG*3 + 1] = 0.85F * ret[COL_BACKGROUND * 3 + 1];
	ret[COL_ERRORBG*3 + 2] = 0.85F * ret[COL_BACKGROUND * 3 + 2];
	
	*ncolours = NCOLOURS;
	return ret;
}

static const char *const minus_signs[] = { "\xE2\x88\x92", "-" };
static const char *const times_signs[] = { "\xC3\x97", "*" };
static const char *const divide_signs[] = { "\xC3\xB7", "/" };

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	int o = state->o, s = o*o;
	struct game_drawstate *ds = snew(struct game_drawstate);

	ds->tilesize = 0;
	ds->redraw = true;

	ds->flags = snewn(s, unsigned int);
	ds->marks = snewn(s, marks_t);
	ds->grid = snewn(s, digit);
	
	memset(ds->flags, ~0, s*sizeof(unsigned int));
	memset(ds->marks, ~0, s*sizeof(marks_t));
	memset(ds->grid, ~0, s*sizeof(digit));

	ds->symbols.minus_sign = text_fallback(dr, minus_signs, lenof(minus_signs));
	ds->symbols.times_sign = text_fallback(dr, times_signs, lenof(times_signs));
	ds->symbols.divide_sign = text_fallback(dr, divide_signs, lenof(divide_signs));
	
	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->flags);
	sfree(ds->marks);
	sfree(ds->grid);
	sfree(ds->symbols.minus_sign);
	sfree(ds->symbols.times_sign);
	sfree(ds->symbols.divide_sign);
	sfree(ds);
}

#define FLASH_TIME 0.7F
#define FLASH_FRAME 0.1F

static int mathrax_clue_label(struct mathrax_symbols symbols, char *buf, clue_t clue)
{
	switch(clue & CLUEMASK)
	{
		case CLUE_ADD:
			return sprintf(buf, "%d+", CLUENUM(clue));
		case CLUE_SUB:
			if(CLUENUM(clue))
				return sprintf(buf, "%d%s", CLUENUM(clue), symbols.minus_sign);
			else
				return sprintf(buf, "=");
		case CLUE_MUL:
			return sprintf(buf, "%d%s", CLUENUM(clue), symbols.times_sign);
		case CLUE_DIV:
			return sprintf(buf, "%d%s", CLUENUM(clue), symbols.divide_sign);
		case CLUE_EVN:
			return sprintf(buf, "E");
		case CLUE_ODD:
			return sprintf(buf, "O");
		default:
			buf[0] = '\0';
			return 0;
	}
}

static void mathrax_draw_clue(drawing *dr, game_drawstate *ds, clue_t clue, int x, int y, int error)
{
	char buf[16];
	int tilesize = ds->tilesize;

	if(!clue) return;

	mathrax_clue_label(ds->symbols, buf, clue);

	draw_circle(dr, x, y, tilesize/3, error ? COL_ERRORBG : COL_HIGHLIGHT, error ? COL_ERROR : COL_BORDER);
	draw_text(dr, x, y, FONT_VARIABLE, tilesize/3, ALIGN_HCENTRE|ALIGN_VCENTRE, COL_BORDER, buf);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
	int o = state->o;
	int co = o-1;
	int x, y, tx, ty, fs;
	int tilesize = ds->tilesize;
	char buf[8];
	int flash = -1;
	
	if(flashtime > 0)
		flash = (int)(flashtime / FLASH_FRAME) % 3;
	
	if(ds->redraw)
	{
		draw_rect(dr, 0, 0, (o+1)*tilesize, (o+1)*tilesize, COL_BACKGROUND);
		draw_rect(dr, (tilesize/2), (tilesize/2)-1, o*tilesize+1, o*tilesize+1, COL_BORDER);
		draw_update(dr, 0, 0, (o+1)*tilesize, (o+1)*tilesize);
	}
	
	for(y = 0; y < o; y++)
	for(x = 0; x < o; x++)
	{
		tx = x*tilesize + (tilesize/2);
		ty = y*tilesize + (tilesize/2);
		
		fs = state->flags[y*o+x];
		
		if(flashtime > 0 && (x+y) % 3 == flash)
			fs |= FD_FLASH;
		
		if(flashtime == 0 && ui->cshow && ui->hx == x && ui->hy == y)
			fs |= ui->cpencil ? FD_PENCIL : FD_CURSOR;
		
		if(state->marks[y*o+x] == ds->marks[y*o+x] && 
				state->grid[y*o+x] == ds->grid[y*o+x] && fs == ds->flags[y*o+x])
			continue;
		
		ds->marks[y*o+x] = state->marks[y*o+x];
		ds->grid[y*o+x] = state->grid[y*o+x];
		ds->flags[y*o+x] = fs;
		
		clip(dr, tx, ty, tilesize, tilesize);
		
		draw_update(dr, tx, ty, tilesize, tilesize);
		
		draw_rect(dr, tx, ty, tilesize, tilesize,
			fs & (FD_FLASH|FD_CURSOR) ? COL_LOWLIGHT : COL_BACKGROUND);
		
		if(fs & FD_PENCIL)
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
		
		/* Define a square */
		int sqc[8];
		sqc[0] = tx;
		sqc[1] = ty - 1;
		sqc[2] = tx + tilesize;
		sqc[3] = ty - 1;
		sqc[4] = tx + tilesize;
		sqc[5] = ty + tilesize - 1;
		sqc[6] = tx;
		sqc[7] = ty + tilesize - 1;
		draw_polygon(dr, sqc, 4, -1, COL_BORDER);
		
		if(state->grid[y*o+x])
		{
			buf[0] = state->grid[y*o+x] + '0';
			buf[1] = '\0';
			
			draw_text(dr, tx + (tilesize/2), ty + (tilesize/2),
				FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
				fs & F_IMMUTABLE ? COL_BORDER : 
				fs & FE_COUNT ? COL_ERROR : COL_GUESS,
				buf);
		}
		else if(state->marks[y*o+x]) /* Draw pencil marks */
		{
			int nhints, i, j, hw, hh, hmax, fontsz;
			for (i = nhints = 0; i < 9; i++) {
				if (state->marks[y*o+x] & (1<<i)) nhints++;
			}

			for (hw = 1; hw * hw < nhints; hw++);
			
			if (hw < 3) hw = 3;			
			hh = (nhints + hw - 1) / hw;
			if (hh < 2) hh = 2;
			hmax = max(hw, hh);
			fontsz = tilesize/(hmax*(11-hmax)/8);

			for (i = j = 0; i < 9; i++)
			{
				if (state->marks[y*o+x] & (1<<i))
				{
					int hx = j % hw, hy = j / hw;

					buf[0] = i+'1';
					buf[1] = '\0';
					
					draw_text(dr,
						tx + (4*hx+3) * tilesize / (4*hw+2),
						ty + (4*hy+3) * tilesize / (4*hh+2),
						FONT_VARIABLE, fontsz,
						ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, buf);
					j++;
				}
			}
		}

		if(y < o-1 && x < o-1 && state->clues[y*co+x])
			mathrax_draw_clue(dr, ds, state->clues[y*co+x], tx+tilesize, ty+tilesize, fs & FE_BOTRIGHT);
		if(y > 0 && x < o-1 && state->clues[(y-1)*co+x])
			mathrax_draw_clue(dr, ds, state->clues[(y-1)*co+x], tx+tilesize, ty, fs & FE_TOPRIGHT);
		if(y < o-1 && x > 0 && state->clues[y*co+x-1])
			mathrax_draw_clue(dr, ds, state->clues[y*co+x-1], tx, ty+tilesize, fs & FE_BOTLEFT);
		if(y > 0 && x > 0 && state->clues[(y-1)*co+x-1])
			mathrax_draw_clue(dr, ds, state->clues[(y-1)*co+x-1], tx, ty, fs & FE_TOPLEFT);
		
		unclip(dr);
	}
	
	ds->redraw = false;
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
	int o = state->o, co = o-1;
	int x, y;
	int ink = print_mono_colour(dr, 0);
	int paper = print_grey_colour(dr, 1);
	char buf[16];
    struct mathrax_symbols symbols;

	symbols.minus_sign = text_fallback(dr, minus_signs, lenof(minus_signs));
    symbols.times_sign = text_fallback(dr, times_signs, lenof(times_signs));
    symbols.divide_sign = text_fallback(dr, divide_signs, lenof(divide_signs));

	for(x = 0; x <= o; x++)
	for(y = 0; y < o; y++)
	{
		draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
			(x+0.5)*tilesize, (y+1.5)*tilesize, ink);
	}
	for(x = 0; x < o; x++)
	for(y = 0; y <= o; y++)
	{
		draw_line(dr, (x+0.5)*tilesize, (y+0.5)*tilesize, 
				(x+1.5)*tilesize, (y+0.5)*tilesize, ink);
	}

	for(x = 0; x < o-1; x++)
	for(y = 0; y < o-1; y++)
	{
		if(state->clues[y*co+x])
		{
			int tx = x*tilesize + (tilesize*1.5);
			int ty = y*tilesize + (tilesize*1.5);
			mathrax_clue_label(symbols, buf, state->clues[y*co+x]);

			draw_circle(dr, tx, ty, tilesize/3, paper, ink);
			draw_text(dr, tx, ty, FONT_VARIABLE, tilesize/3, ALIGN_HCENTRE|ALIGN_VCENTRE, ink, buf);
		}
	}

	buf[1] = '\0';
	for(x = 0; x < o; x++)
	for(y = 0; y < o; y++)
	{
		if(!state->grid[y*o+x]) continue;
		buf[0] = state->grid[y*o+x] + '0';
		draw_text(dr, (x+1)*tilesize,
			  (y+1)*tilesize,
			  FONT_VARIABLE, tilesize/2,
			  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, buf);
	}

	sfree(symbols.minus_sign);
	sfree(symbols.times_sign);
	sfree(symbols.divide_sign);
}

#ifdef COMBINED
#define thegame mathrax
#endif

const struct game thegame = {
	"Mathrax", NULL, NULL,
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
	true, false, game_print_size, game_print,
	false,			       /* wants_statusbar */
	false, game_timing_state,
	REQUIRE_RBUTTON, /* flags */
};
