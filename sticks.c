/*
 * sticks.c: Implementation of Tatebo-Yokobo puzzles.
 * (C) 2018 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective: Fill each white cell with a horizontal or vertical line
 * going through the center of the cell, with the following rules:
 * - A number overlapping a line indicates the length of that line.
 * - A line can't overlap more than one number.
 * - Numbers in black cells indicate the amount of lines connected to the cell.
 *
 * This puzzle type was invented by Nikoli.
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
int solver_verbose = false;

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
	COL_BACKGROUND,
	COL_GRID,
	COL_LINE,
	COL_NUMBER,
	COL_ERROR,
	COL_CURSOR,
	NCOLOURS
};

enum { SYMM_NONE, SYMM_REF2, SYMM_ROT2, SYMM_REF4, SYMM_ROT4, SYMM_MAX };

struct game_params {
	int w, h;
	int blackpc; /* %age of black squares */
	int symm;
};

#define F_HOR     0x01
#define F_VER     0x02
#define F_BLOCK   0x04
#define F_ERROR   0x08
#define F_CURSOR  0x10

struct game_state {
	int w, h;
	char *grid;
	int *numbers;
	
	bool completed, cheated;
};

const static struct game_params sticks_presets[] = {
	{ 7, 7, 20, SYMM_ROT2 },
	{ 10, 10, 20, SYMM_ROT2 },
};

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);

	*ret = sticks_presets[0]; /* structure copy */

	return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
	game_params *ret;
	char buf[80];

	if (i < 0 || i >= lenof(sticks_presets))
		return false;

	ret = snew(game_params);
	*ret = sticks_presets[i]; /* structure copy */

	sprintf(buf, "%dx%d", ret->w, ret->h);

	*name = dupstr(buf);
	*params = ret;
	return true;
}

static void free_params(game_params *params)
{
	sfree(params);
}

static game_params *dup_params(const game_params *params)
{
	game_params *ret = snew(game_params);
	*ret = *params;      /* structure copy */
	return ret;
}

#define EATNUM(x) do { \
    (x) = atoi(string); \
    while (*string && isdigit((unsigned char)*string)) string++; \
} while(0)

static void decode_params(game_params *params, char const *string)
{
	EATNUM(params->w);
	if (*string == 'x') {
		string++;
		EATNUM(params->h);
	}
	if (*string == 'b') {
		string++;
		EATNUM(params->blackpc);
	}
	if (*string == 's') {
		string++;
		EATNUM(params->symm);
	}
	else {
		/* cope with user input such as '18x10' by ensuring symmetry
		* is not selected by default to be incompatible with dimensions */
		if (params->symm == SYMM_ROT4 && params->w != params->h)
			params->symm = SYMM_ROT2;
	}
}

static char *encode_params(const game_params *params, bool full)
{
	char buf[80];

	if (full) {
		sprintf(buf, "%dx%db%ds%d",
			params->w, params->h, params->blackpc,
			params->symm);
	}
	else {
		sprintf(buf, "%dx%d", params->w, params->h);
	}
	return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
	config_item *ret;
	char buf[80];

	ret = snewn(5, config_item);

	ret[0].name = "Width";
	ret[0].type = C_STRING;
	sprintf(buf, "%d", params->w);
	ret[0].u.string.sval = dupstr(buf);

	ret[1].name = "Height";
	ret[1].type = C_STRING;
	sprintf(buf, "%d", params->h);
	ret[1].u.string.sval = dupstr(buf);

	ret[2].name = "%age of black squares";
	ret[2].type = C_STRING;
	sprintf(buf, "%d", params->blackpc);
	ret[2].u.string.sval = dupstr(buf);

	ret[3].name = "Symmetry";
	ret[3].type = C_CHOICES;
	ret[3].u.choices.choicenames = ":None"
		":2-way mirror:2-way rotational"
		":4-way mirror:4-way rotational";
	ret[3].u.choices.selected = params->symm;

	ret[4].name = NULL;
	ret[4].type = C_END;

	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
	game_params *ret = snew(game_params);

	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->blackpc = atoi(cfg[2].u.string.sval);
	ret->symm = cfg[3].u.choices.selected;

	return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
	if (params->w < 2 || params->h < 2)
		return "Width and height must be at least 2";
	if (full) {
		if (params->blackpc < 5 || params->blackpc > 100)
			return "Percentage of black squares must be between 5% and 100%";
		if (params->w != params->h) {
			if (params->symm == SYMM_ROT4)
				return "4-fold symmetry is only available with square grids";
		}
		if (params->symm < 0 || params->symm >= SYMM_MAX)
			return "Unknown symmetry type";
	}
	return NULL;
}

enum { STATUS_COMPLETE, STATUS_UNFINISHED, STATUS_INVALID };

static void sticks_make_dsf(game_state *state, DSF *dsf, int *lengths)
{
	int w = state->w, h = state->h;
	int x, y, i;
	if (lengths)
	{
		for (i = 0; i < w*h; i++)
			lengths[i] = -1;
	}
	
	dsf_reinit(dsf);
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i = y*w+x;
		
		if(x < w-1 && state->grid[i] & F_HOR && state->grid[i+1] & F_HOR)
			dsf_merge(dsf, i, i+1);
		if(y < h-1 && state->grid[i] & F_VER && state->grid[i+w] & F_VER)
			dsf_merge(dsf, i, i+w);
	}

	if (lengths)
	{
		for (i = 0; i < w*h; i++)
		{
			if (state->numbers[i] != -1)
			{
				int c = dsf_canonify(dsf, i);
				lengths[c] = lengths[c] != -1 ? -2 : i;
			}
		}
	}
}

static int sticks_max_size_horizontal(game_state *state, DSF *dsf, int *lengths, int idx)
{
	int w = state->w;
	int c, x, y = idx / w;
	int ret = 1;
	int action;

	for (action = -1; action < 2; action += 2)
	{
		x = (idx%w) + action;
		while (x >= 0 && x < w)
		{
			if (state->grid[y*w + x] & (F_BLOCK | F_VER))
				break;

			c = dsf_canonify(dsf, y*w + x);
			if (lengths[c] != -1 && lengths[c] != idx)
				break;

			if (action == -1 && x > 1 && state->grid[y*w + x - 1] & F_HOR)
			{
				int other = lengths[dsf_canonify(dsf, y*w + x - 1)];
				if (other != -1 && other != idx)
					break;
			}

			if (action == 1 && x < w - 1 && state->grid[y*w + x + 1] & F_HOR)
			{
				int other = lengths[dsf_canonify(dsf, y*w + x + 1)];
				if (other != -1 && other != idx)
					break;
			}

			ret++;
			x += action;
		}
	}

	return ret;
}

static int sticks_max_size_vertical(game_state *state, DSF *dsf, int *lengths, int idx)
{
	int w = state->w, h = state->h;
	int c, y, x = idx%w;
	int ret = 1;
	int action;

	for (action = -1; action < 2; action += 2)
	{
		y = (idx / w) + action;
		while (y >= 0 && y < h)
		{
			if (state->grid[y*w + x] & (F_BLOCK | F_HOR))
				break;

			c = dsf_canonify(dsf, y*w + x);
			if (lengths[c] != -1 && lengths[c] != idx)
				break;

			if (action == -1 && y > 1 && state->grid[(y - 1)*w + x] & F_VER)
			{
				int other = lengths[dsf_canonify(dsf, (y - 1)*w + x)];
				if (other != -1 && other != idx)
					break;
			}

			if (action == 1 && y < h - 1 && state->grid[(y + 1)*w + x] & F_VER)
			{
				int other = lengths[dsf_canonify(dsf, (y + 1)*w + x)];
				if (other != -1 && other != idx)
					break;
			}

			ret++;
			y += action;
		}
	}

	return ret;
}

static int sticks_validate(game_state *state, DSF *dsf, int *lengths)
{
	int w = state->w, h = state->h;
	int x, y, i;

	bool hastemp = dsf != NULL;
	if (!hastemp)
	{
		dsf = dsf_new_min(w*h);
		lengths = snewn(w*h, int);
	}
	bool error;
	char ret = STATUS_COMPLETE;

	sticks_make_dsf(state, dsf, lengths);

	for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	{
		i = y*w + x;
		state->grid[i] &= ~F_ERROR;

		if (!state->grid[i])
		{
			if (ret == STATUS_COMPLETE)
				ret = STATUS_UNFINISHED;
			continue;
		}

		if (state->numbers[y*w + x] == -1) continue;

		error = false;

		if (state->grid[y*w + x] & F_BLOCK)
		{
			int conn = 0;
			int other = 0;

			if (x == 0 || state->grid[i - 1] & (F_VER | F_BLOCK)) other++;
			if (x == w - 1 || state->grid[i + 1] & (F_VER | F_BLOCK)) other++;
			if (y == 0 || state->grid[i - w] & (F_HOR | F_BLOCK)) other++;
			if (y == h - 1 || state->grid[i + w] & (F_HOR | F_BLOCK)) other++;

			if (x != 0 && state->grid[i - 1] & F_HOR) conn++;
			if (x != w - 1 && state->grid[i + 1] & F_HOR) conn++;
			if (y != 0 && state->grid[i - w] & F_VER) conn++;
			if (y != h - 1 && state->grid[i + w] & F_VER) conn++;

			if (conn > state->numbers[i] || other > 4 - state->numbers[i])
				error = true;
		}
		else
		{
			int c = dsf_canonify(dsf, i);
			if (lengths[c] < 0)
				error = true;
			else
			{
				int s = dsf_size(dsf, c);
				int l = state->numbers[lengths[c]];

				if (s > l)
					error = true;
				else if (s < l && state->grid[i] & F_HOR)
				{
					if (sticks_max_size_horizontal(state, dsf, lengths, i) < l)
						error = true;
				}
				else if (s < l && state->grid[i] & F_VER)
				{
					if (sticks_max_size_vertical(state, dsf, lengths, i) < l)
						error = true;
				}
			}
		}

		if (error)
		{
			if (!hastemp)
				state->grid[i] |= F_ERROR;
			ret = STATUS_INVALID;
		}
	}

	if (!hastemp)
	{
		dsf_free(dsf);
		sfree(lengths);
	}
	return ret;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	int s = params->w * params->h;
	const char *p = desc;
	int pos = 0;

	while(*p) {
		if (*p >= 'a' && *p <= 'z') {
			pos += (*p - 'a') + 1;
		} else if(*p == 'B') {
			if(!isdigit((unsigned char)*(p+1)))
				++pos;
		} else if(isdigit((unsigned char) *p)) {
			while (*p && isdigit((unsigned char) *p)) ++p;
			++pos;
			continue;
		}
		else if(*p != '_')
			return "Description contains invalid characters";
		++p;
	}

	if(pos < s) {
		return "Description is too short";
	}
	if(pos > s) {
		return "Description is too long";
	}

	return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
							const char *desc)
{
	int w = params->w, h = params->h;
	int pos = 0;
	game_state *state = snew(game_state);

	state->w = w;
	state->h = h;
	state->completed = state->cheated = false;
	
	state->grid = snewn(w*h, char);
	state->numbers = snewn(w*h, int);
	
	memset(state->grid, 0, w*h*sizeof(char));
	for(pos = 0; pos < w*h; pos++)
		state->numbers[pos] = -1;

	if (!desc)
		return state;

	const char *p = desc;
	pos = 0;
	while(*p)
	{
		if (*p >= 'a' && *p < 'z') {
			pos += (*p - 'a') + 1;
		}
		else if (*p == 'z') {
			pos += 26;
		}
		else if(*p == 'B') {
			state->grid[pos] = F_BLOCK;
			if(!isdigit((unsigned char)*(p+1)))
				pos++;
		}
		else if(isdigit((unsigned char) *p)) {
			state->numbers[pos] = atoi(p);
			while (*p && isdigit((unsigned char) *p)) ++p;
			pos++;
			continue;
		}
		else if(*p != '_')
			assert(!"Description contains invalid characters");

		++p;
	}
	
	return state;
}

static game_state *dup_game(const game_state *state)
{
	int w = state->w, h = state->h;
	game_state *ret = snew(game_state);
	
	ret->w = w;
	ret->h = h;
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;
	
	ret->grid = snewn(w*h, char);
	ret->numbers = snewn(w*h, int);
	
	memcpy(ret->grid, state->grid, w*h*sizeof(char));
	memcpy(ret->numbers, state->numbers, w*h*sizeof(int));

	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->grid);
	sfree(state->numbers);
	sfree(state);
}

static int sticks_try(game_state *state, DSF *dsf, int *lengths)
{
	int s = state->w * state->h;
	int i;

	for (i = 0; i < s; i++)
	{
		if (state->grid[i]) continue;

		state->grid[i] = F_HOR;
		if (sticks_validate(state, dsf, lengths) == STATUS_INVALID)
		{
			state->grid[i] = F_VER;
			return 1;
		}

		state->grid[i] = F_VER;
		if (sticks_validate(state, dsf, lengths) == STATUS_INVALID)
		{
			state->grid[i] = F_HOR;
			return 1;
		}

		state->grid[i] = 0;
	}

	return 0;
}

static int sticks_solve_game(game_state *state)
{
	int s = state->w * state->h;
	int i;
	int ret = STATUS_UNFINISHED;

	DSF *dsf = dsf_new_min(s);
	int *lengths = snewn(s, int);

	for (i = 0; i < s; i++)
	{
		if (!(state->grid[i] & F_BLOCK))
			state->grid[i] = 0;
	}

	while ((ret = sticks_validate(state, dsf, lengths)) == STATUS_UNFINISHED)
	{
		if (sticks_try(state, dsf, lengths))
			continue;
		
		break;
	}

	dsf_free(dsf);
	sfree(lengths);
	return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
						const char *aux, const char **error)
{
	game_state *solved = dup_game(state);
	char *ret = NULL;
	int result;

	sticks_solve_game(solved);

	result = sticks_validate(solved, NULL, NULL);
	
	if (result != STATUS_INVALID) {
		int s = solved->w*solved->h;
		char *p;
		int i;

		ret = snewn(s + 2, char);
		p = ret;
		*p++ = 'S';

		for (i = 0; i < s; i++)
			*p++ = (solved->grid[i] & F_VER ? '1' : solved->grid[i] & F_HOR ? '0' : '-');

		*p++ = '\0';
	}
	else
		*error = "Puzzle is invalid.";

	free_game(solved);
	return ret;
}

/* Copied from lightup.c */
static void set_blacks(game_state *state, const game_params *params,
	random_state *rs)
{
	int x, y, degree = 0, rotate = 0, nblack;
	int rh, rw, i;
	int w = state->w, h = state->h;
	int wodd = (w % 2) ? 1 : 0;
	int hodd = (h % 2) ? 1 : 0;
	int xs[4], ys[4];

	switch (params->symm) {
	case SYMM_NONE: degree = 1; rotate = 0; break;
	case SYMM_ROT2: degree = 2; rotate = 1; break;
	case SYMM_REF2: degree = 2; rotate = 0; break;
	case SYMM_ROT4: degree = 4; rotate = 1; break;
	case SYMM_REF4: degree = 4; rotate = 0; break;
	default: assert(!"Unknown symmetry type");
	}
	if (params->symm == SYMM_ROT4 && (h != w))
		assert(!"4-fold symmetry unavailable without square grid");

	if (degree == 4) {
		rw = w / 2;
		rh = h / 2;
		if (!rotate) rw += wodd; /* ... but see below. */
		rh += hodd;
	}
	else if (degree == 2) {
		rw = w;
		rh = h / 2;
		rh += hodd;
	}
	else {
		rw = w;
		rh = h;
	}

	/* clear, then randomise, required region. */
	memset(state->grid, 0, w*h * sizeof(char));
	nblack = (rw * rh * params->blackpc) / 100;
	for (i = 0; i < nblack; i++) {
		do {
			x = random_upto(rs, rw);
			y = random_upto(rs, rh);
		} while (state->grid[y*w+x] & F_BLOCK);
		state->grid[y*w + x] |= F_BLOCK;
	}

	/* Copy required region. */
	if (params->symm == SYMM_NONE) return;

	for (x = 0; x < rw; x++) {
		for (y = 0; y < rh; y++) {
			if (degree == 4) {
				xs[0] = x;
				ys[0] = y;
				xs[1] = w - 1 - (rotate ? y : x);
				ys[1] = rotate ? x : y;
				xs[2] = rotate ? (w - 1 - x) : x;
				ys[2] = h - 1 - y;
				xs[3] = rotate ? y : (w - 1 - x);
				ys[3] = h - 1 - (rotate ? x : y);
			}
			else {
				xs[0] = x;
				ys[0] = y;
				xs[1] = rotate ? (w - 1 - x) : x;
				ys[1] = h - 1 - y;
			}
			for (i = 1; i < degree; i++) {
				state->grid[ys[i] * w + xs[i]] = state->grid[ys[0] * w + xs[0]];
			}
		}
	}
	/* SYMM_ROT4 misses the middle square above; fix that here. */
	if (degree == 4 && rotate && wodd &&
		(random_upto(rs, 100) <= (unsigned int)params->blackpc))
		state->grid[w*(h / 2 + hodd - 1) + (w / 2 + wodd - 1)] |= F_BLOCK;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	int w = params->w, h = params->h;
	DSF *dsf = dsf_new_min(w*h);
	int *spaces = snewn(w*h, int);
	int i, j;
	game_state *state = new_game(NULL, params, NULL);

	set_blacks(state, params, rs);

	do
	{
		for (i = 0; i < w*h; i++)
		{
			if (!(state->grid[i] & F_BLOCK))
				state->grid[i] = random_upto(rs, 2) ? F_HOR : F_VER;
			else
				state->grid[i] = F_BLOCK;
		}

		sticks_make_dsf(state, dsf, NULL);

		for (i = 0; i < w*h; i++)
			state->numbers[i] = -1;

		for (i = 0; i < w*h; i++)
		{
			if (state->grid[i] & F_BLOCK)
			{
				int n = 0;
				if (i%w > 0 && state->grid[i - 1] & F_HOR) n++;
				if (i%w < w - 1 && state->grid[i + 1] & F_HOR) n++;
				if (i / w > 0 && state->grid[i - w] & F_VER) n++;
				if (i / w < h - 1 && state->grid[i + w] & F_VER) n++;
				state->numbers[i] = n;
			}
			else if (dsf_minimal(dsf, i) == i)
			{
				int n = dsf_size(dsf, i);

				if (n == 1)
					state->numbers[i] = 1;
				else if (state->grid[i] & F_HOR)
					state->numbers[i + random_upto(rs, n)] = n;
				else if (state->grid[i] & F_VER)
					state->numbers[i + (w*random_upto(rs, n))] = n;
			}
		}
	} while (sticks_solve_game(state) != STATUS_COMPLETE);

	for (i = 0; i < w*h; i++)
		spaces[i] = i;
	shuffle(spaces, w*h, sizeof(int), rs);

	for (j = 0; j < w*h; j++)
	{
		i = spaces[j];
		int temp = state->numbers[i];
		if (temp == -1) continue;
		state->numbers[i] = -1;
		if (sticks_solve_game(state) != STATUS_COMPLETE)
			state->numbers[i] = temp;
	}

	char *ret = snewn((w*h * 2) + 1, char);
	char *p = ret;

	int run = 0;
	for (i = 0; i < w*h; i++)
	{
		if (state->numbers[i] != -1 || state->grid[i] & F_BLOCK)
		{
			if (run)
			{
				while (run > 26)
				{
					*p++ = 'z';
					run -= 26;
				}
				*p++ = 'a' + (run - 1);
				run = 0;
			}
			else if (i != 0 && !(state->grid[i] & F_BLOCK))
				*p++ = '_';
			if (state->grid[i] & F_BLOCK)
				*p++ = 'B';
			if (state->numbers[i] != -1)
				p += sprintf(p, "%d", state->numbers[i]);
		}
		else
			run++;
	}

	if (run)
	{
		while (run > 26)
		{
			*p++ = 'z';
			run -= 26;
		}
		*p++ = 'a' + (run - 1);
	}

	*p++ = '\0';
	ret = sresize(ret, p - ret, char);
	free_game(state);
	dsf_free(dsf);
	sfree(spaces);

	return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
	return true;
}

static char *game_text_format(const game_state *state)
{
	int w = state->w, h = state->h;
	int x, y;

	char *ret = snewn((w*2)*h+1, char);
	char *p = ret;

	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		int tile = state->grid[y*w+x];
		*p++ = tile & F_HOR ? '-' :
			tile & F_VER ? '|' :
			tile & F_BLOCK ? '#' : '.';
		*p++ = x != w-1 ? ' ' : '\n';
	}

	*p = '\0';
	return ret;
}

enum { DRAG_NONE, DRAG_START, DRAG_LINE, DRAG_CLEAR };
struct game_ui {
	int cx, cy;
	bool cursor;

	int min_x, min_y, max_x, max_y;
	char dragtype;
	int *drag;
	char *dragmove;
	int ndrags;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ret = snew(game_ui);

	ret->cx = ret->cy = 0;
	ret->cursor = false;
	ret->ndrags = 0;
	ret->dragtype = DRAG_NONE;
	ret->dragmove = snewn(state->w*state->h, char);
	ret->drag = snewn(state->w*state->h, int);

	return ret;
}

static void free_ui(game_ui *ui)
{
	sfree(ui->dragmove);
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

#define COORD(x)     ( (x) * tilesize + tilesize/2 )
#define FROMCOORD(x) ( ((x)-(tilesize/2)) / tilesize )

struct game_drawstate
{
	int tilesize;
	char *grid;
};

#define DRAG_DELTA (tilesize*0.4)
static char *interpret_move(const game_state *state, game_ui *ui,
	const game_drawstate *ds,
	int ox, int oy, int button)
{
	int tilesize = ds->tilesize;

	int w = state->w, h = state->h;

	int shift = button & MOD_SHFT, control = button & MOD_CTRL;
	button &= ~MOD_MASK;

	if (IS_MOUSE_DOWN(button) || IS_MOUSE_DRAG(button))
		ui->cursor = false;

	/* Keyboard move */
	if (IS_CURSOR_MOVE(button)) {
		int ox = ui->cx, oy = ui->cy;
		move_cursor(button, &ui->cx, &ui->cy, w, h, 0, &ui->cursor);

		if (shift | control)
		{
			int i1, i2;
			char c = shift && control ? 'C' :
				control ? (button == CURSOR_LEFT || button == CURSOR_RIGHT ? 'A' : 'B') :
				(button == CURSOR_LEFT || button == CURSOR_RIGHT ? 'B' : 'A');
			char buf[16];
			char *p = buf;

			buf[0] = '\0';
			
			i1 = oy*w + ox;
			i2 = ui->cy*w + ui->cx;
			if (!(state->grid[i1] & F_BLOCK
					|| (c == 'A' && state->grid[i1] & F_HOR)
					|| (c == 'B' && state->grid[i1] & F_VER)
					|| (c == 'C' && !state->grid[i1])
					))
				p += sprintf(p, "%c%d;", c, i1);

			if (!(i1 == i2 || state->grid[i2] & F_BLOCK
					|| (c == 'A' && state->grid[i2] & F_HOR)
					|| (c == 'B' && state->grid[i2] & F_VER)
					|| (c == 'C' && !state->grid[i2])
					))
				p += sprintf(p, "%c%d;", c, i2);

			if(buf[0])
				return dupstr(buf);
		}
		return MOVE_UI_UPDATE;
	}

	/* Begin normal drag */
	if (button == LEFT_BUTTON || button == RIGHT_BUTTON)
	{
		ui->min_x = ui->max_x = ox;
		ui->min_y = ui->max_y = oy;

		ui->ndrags = 0;
		ui->dragtype = DRAG_START;
		return MOVE_UI_UPDATE;
	}

	/* Perform drag */
	if (IS_MOUSE_DRAG(button) && (ui->dragtype == DRAG_START || ui->dragtype == DRAG_LINE))
	{
		int i, d, dx, dy, hx, hy;
		char dragmove;

		ui->min_x = min(ui->min_x, ox);
		ui->max_x = max(ui->max_x, ox);
		ui->min_y = min(ui->min_y, oy);
		ui->max_y = max(ui->max_y, oy);

		dx = ui->max_x - ui->min_x;
		dy = ui->max_y - ui->min_y;

		if (dx > dy && dx > DRAG_DELTA)
			dragmove = F_HOR;
		else if (dy > dx && dy > DRAG_DELTA)
			dragmove = F_VER;
		else
			return NULL;

		hx = FROMCOORD((ui->min_x + ui->max_x) / 2);
		hy = FROMCOORD((ui->min_y + ui->max_y) / 2);

		ui->min_x = ui->max_x = ox;
		ui->min_y = ui->max_y = oy;

		if (hx < 0 || hx >= w || hy < 0 || hy >= h)
			return NULL;

		i = hy * w + hx;

		if (state->grid[i] & F_BLOCK)
			return NULL;

		if (ui->dragtype == DRAG_START && state->grid[i] & dragmove)
		{
			ui->dragtype = DRAG_CLEAR;
			dragmove = 0;
		}
		else
		{
			ui->dragtype = DRAG_LINE;

			for (d = 0; d < ui->ndrags; d++)
			{
				if (i == ui->drag[d])
				{
					ui->dragmove[d] = dragmove;
					return MOVE_UI_UPDATE;
				}
			}
		}

		ui->dragmove[ui->ndrags] = dragmove;
		ui->drag[ui->ndrags++] = i;

		return MOVE_UI_UPDATE;
	}

	/* Begin clearing drag */
	if (button == MIDDLE_BUTTON)
	{
		int hx = FROMCOORD(ox), hy = FROMCOORD(oy), i = hy*w + hx;
		ui->ndrags = 0;

		ui->dragtype = DRAG_CLEAR;
		if (state->grid[i] & (F_HOR | F_VER))
		{
			ui->dragmove[ui->ndrags] = 0;
			ui->drag[ui->ndrags++] = i;
		}

		return MOVE_UI_UPDATE;
	}

	/* Clearing drag */
	if (IS_MOUSE_DRAG(button) && ui->dragtype == DRAG_CLEAR)
	{
		int hx = FROMCOORD(ox), hy = FROMCOORD(oy), i = hy*w + hx;
		int d;

		if (!(state->grid[i] & (F_HOR | F_VER)))
			return NULL;

		for (d = 0; d < ui->ndrags; d++)
		{
			if (i == ui->drag[d])
				return NULL;
		}

		ui->dragmove[ui->ndrags] = 0;
		ui->drag[ui->ndrags++] = i;

		return MOVE_UI_UPDATE;
	}

	/* Mouse click */
	if (IS_MOUSE_RELEASE(button) && ui->dragtype == DRAG_START)
	{
		int hx = FROMCOORD((ui->min_x + ui->max_x) / 2);
		int hy = FROMCOORD((ui->min_y + ui->max_y) / 2);

		if (hx < 0 || hx >= w || hy < 0 || hy >= h)
		{
			ui->dragtype = DRAG_NONE;
			return MOVE_UI_UPDATE;
		}

		int i = hy*w + hx;

		int old = state->grid[i];

		if (button == LEFT_RELEASE)
			ui->dragmove[0] = (old == 0 ? F_VER : old & F_VER ? F_HOR : 0);
		if (button == RIGHT_RELEASE)
			ui->dragmove[0] = (old == 0 ? F_HOR : old & F_HOR ? F_VER : 0);
		ui->drag[0] = i;
		ui->ndrags = 1;
	}

	if (IS_MOUSE_RELEASE(button))
		ui->dragtype = DRAG_NONE;

	/* Confirm clicks and drags */
	if (IS_MOUSE_RELEASE(button) && ui->ndrags)
	{
		int i, j;
		char *buf = snewn(ui->ndrags * 7, char);
		char *p = buf;

		for (i = 0; i < ui->ndrags; i++)
		{
			j = ui->drag[i];
			char c = ui->dragmove[i] & F_HOR ? 'A' : ui->dragmove[i] & F_VER ? 'B' : 'C';
			if (state->grid[j] & F_BLOCK) continue;
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
	if (ui->cursor && (button == CURSOR_SELECT || button == CURSOR_SELECT2
		|| button == '\b' || button == '0' || button == '1'
		|| button == '2')) {
		char buf[80];
		char c, old;
		int i = ui->cy * w + ui->cx;

		if (state->grid[i] & F_BLOCK)
			return NULL;

		c = 'C';
		old = state->grid[i];

		if (button == '0' || button == '2')
			c = 'A';
		else if (button == '1')
			c = 'B';

		/* Cycle through options */
		else if (button == CURSOR_SELECT2)
			c = (old == 0 ? 'A' : old & F_HOR ? 'B' : 'C');
		else if (button == CURSOR_SELECT)
			c = (old == 0 ? 'B' : old & F_VER ? 'A' : 'C');

		if ((old & F_HOR && c == 'A') ||
			(old & F_VER && c == 'B') ||
			(old == 0 && c == 'C'))
			return NULL;               /* don't put no-ops on the undo chain */

		sprintf(buf, "%c%d;", c, i);

		return dupstr(buf);
	}
	return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	int w = state->w, h = state->h;
	int s = w * h;
	int i;
	char c;
	bool cheated = false;

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

				if (state->grid[i] & F_BLOCK)
					continue;

				if (*p == '1')
					ret->grid[i] = F_VER;
				else if (*p == '0')
					ret->grid[i] = F_HOR;
				else
					ret->grid[i] = 0;
			}

			cheated = true;
		}
		else if (sscanf(p, "%c%d", &c, &i) == 2 && i >= 0
			&& i < w*h && (c == 'A' || c == 'B'
				|| c == 'C'))
		{
			if (!(state->grid[i] & F_BLOCK))
				ret->grid[i] = (c == 'A' ? F_HOR : c == 'B' ? F_VER : 0);
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

	if (sticks_validate(ret, NULL, NULL) == STATUS_COMPLETE) ret->completed = true;
	if(cheated) ret->cheated = ret->completed;
	return ret;
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
	int tilesize = ds->tilesize;
	if(ui->cursor) {
		*x = COORD(ui->cx);
		*x = COORD(ui->cy);
		*w = *h = tilesize;
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
}

static float *game_colours(frontend *fe, int *ncolours)
{
	float *ret = snewn(3 * NCOLOURS, float);

	frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

	ret[COL_GRID * 3 + 0] = 0.0F;
	ret[COL_GRID * 3 + 1] = 0.0F;
	ret[COL_GRID * 3 + 2] = 0.0F;
	
	ret[COL_LINE * 3 + 0] = 0.0F;
	ret[COL_LINE * 3 + 1] = 0.7F;
	ret[COL_LINE * 3 + 2] = 0.0F;
	
	ret[COL_NUMBER * 3 + 0] = 1.0F;
	ret[COL_NUMBER * 3 + 1] = 1.0F;
	ret[COL_NUMBER * 3 + 2] = 1.0F;
	
	ret[COL_ERROR * 3 + 0] = 1.0F;
	ret[COL_ERROR * 3 + 1] = 0.0F;
	ret[COL_ERROR * 3 + 2] = 0.0F;
	
	ret[COL_CURSOR * 3 + 0] = 0.0F;
	ret[COL_CURSOR * 3 + 1] = 0.0F;
	ret[COL_CURSOR * 3 + 2] = 1.0F;
	
	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	int s = state->w*state->h;
	struct game_drawstate *ds = snew(struct game_drawstate);

	ds->tilesize = 0;
	ds->grid = snewn(s, char);
	memset(ds->grid, ~0, s * sizeof(char));

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->grid);
	sfree(ds);
}

#define FLASH_FRAME 0.1F
#define FLASH_TIME (FLASH_FRAME * 5)

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
	int w = state->w;
	int h = state->h;
	int x, y, d;
	int tilesize = ds->tilesize;
	char buf[8];
	
	char flash = flashtime > 0 && ((int)(flashtime / FLASH_FRAME) & 1) == 0;
	
	if (ds->grid[0] == ~0)
	{
		draw_rect(dr, 0, 0, (w + 1)*tilesize, (h + 1)*tilesize, COL_BACKGROUND);
		draw_update(dr, 0, 0, (w + 1)*tilesize, (h + 1)*tilesize);

		draw_rect(dr, COORD(0) - tilesize / 10, COORD(0) - tilesize / 10,
			tilesize*w + 2 * (tilesize / 10) - 1,
			tilesize*h + 2 * (tilesize / 10) - 1, COL_GRID);
	}

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			int tile = state->grid[y*w + x];

			if (!(tile & F_BLOCK))
			{
				for (d = 0; d < ui->ndrags; d++)
				{
					if (ui->drag[d] == y*w + x)
						tile = ui->dragmove[d];
				}
			}
			
			if (flash) tile &= ~(F_HOR|F_VER);
			if (ui->cursor && ui->cx == x && ui->cy == y) tile |= F_CURSOR;

			if(ds->grid[y*w + x] == tile)
				continue;
			
			ds->grid[y*w + x] = tile;
			draw_update(dr, COORD(x), COORD(y), tilesize, tilesize);
			
			draw_rect(dr, COORD(x), COORD(y), tilesize, tilesize, COL_GRID);
			draw_rect(dr, COORD(x), COORD(y), tilesize - 1, tilesize - 1, tile & F_BLOCK ? COL_GRID : COL_BACKGROUND);

			if(tile & F_HOR)
				draw_rect(dr, COORD(x), COORD(y) + (tilesize*2)/5, tilesize-1, tilesize/5, COL_LINE);
			if(tile & F_VER)
				draw_rect(dr, COORD(x) + (tilesize*2)/5, COORD(y), tilesize/5, tilesize-1, COL_LINE);
			
			if(state->numbers[y*w+x] != -1)
			{
				sprintf(buf, "%d", state->numbers[y*w+x]);
				draw_text(dr, COORD(x+0.5), COORD(y+0.5), 
					tile & F_BLOCK ? FONT_FIXED : FONT_VARIABLE,
					tilesize*0.7, ALIGN_HCENTRE|ALIGN_VCENTRE,
					tile & F_ERROR ? COL_ERROR : tile & F_BLOCK ? COL_NUMBER : COL_GRID, buf);
			}
			
			if (tile & F_CURSOR)
			{
				draw_rect(dr, COORD(x), COORD(y), tilesize / 12, tilesize - 1, COL_CURSOR);
				draw_rect(dr, COORD(x), COORD(y), tilesize - 1, tilesize / 12, COL_CURSOR);
				draw_rect(dr, COORD(x) + tilesize - 1 - tilesize / 12, COORD(y), tilesize / 12, tilesize - 1,
					COL_CURSOR);
				draw_rect(dr, COORD(x), COORD(y) + tilesize - 1 - tilesize / 12, tilesize - 1, tilesize / 12,
					COL_CURSOR);
			}
		}
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

static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int ts)
{
}

#ifdef COMBINED
#define thegame sticks
#endif

const struct game thegame = {
	"Sticks", NULL, NULL,
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
	false, false, game_print_size, game_print,
	false,			       /* wants_statusbar */
	false, game_timing_state,
	0,				       /* flags */
};

#ifdef STANDALONE_SOLVER
#include <time.h>

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
		
		game_state *solved = dup_game(input);

		int errcode = sticks_solve_game(solved);

		if(errcode == STATUS_INVALID)
			printf("Puzzle is INVALID.\n");
		char *fmt = game_text_format(solved);
		printf("%s", fmt);
		sfree(fmt);
		if(errcode == STATUS_UNFINISHED)
			printf("Solution not found.\n");

		free_game(input);
		free_game(solved);
	}
	
	return 0;
}
#endif
