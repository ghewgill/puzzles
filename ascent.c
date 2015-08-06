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

enum {
	COL_MIDLIGHT,
	COL_LOWLIGHT,
	COL_HIGHLIGHT,
	COL_BORDER,
	COL_IMMUTABLE,
	COL_ERROR,
	NCOLOURS
};

typedef int number;
typedef int cell;
typedef unsigned char bitmap;

#define IS_NEAR(a,b,w) ( ( abs(((a)/(w)) - ((b)/(w))) | abs(((a)%(w)) - ((b)%(w)))) == 1 )
static const int dir_x[] = {-1,  0,  1, -1, 1, -1, 0, 1};
static const int dir_y[] = {-1, -1, -1,  0, 0,  1, 1, 1};

#define BITMAP_SIZE(i) ( ((i)+7) / 8 )
#define GET_BIT(bmp, i) ( bmp[(i)/8] & 1<<((i)%8) )
#define SET_BIT(bmp, i) bmp[(i)/8] |= 1<<((i)%8)
#define CLR_BIT(bmp, i) bmp[(i)/8] &= ~(1<<((i)%8))

struct game_params {
	int w, h;
};

struct game_state {
	int w, h;
	
	number *grid;
	bitmap *immutable;
	
	number last;
	
	char completed, cheated;
};

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);

	ret->w = 7;
	ret->h = 6;

	return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
	char buf[256];
	game_params *ret;
	
	if(i != 0)
		return FALSE;
	ret = default_params();
	
	sprintf(buf, "%dx%d", ret->w, ret->h);
	
	*params = ret;
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
	params->w = params->h = atoi(string);
	while (*string && isdigit((unsigned char) *string)) ++string;
	if (*string == 'x') {
		string++;
		params->h = atoi(string);
		while (*string && isdigit((unsigned char)*string)) string++;
	}
}

static char *encode_params(const game_params *params, int full)
{
	char buf[256];
	sprintf(buf, "%dx%d", params->w, params->h);
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
	ret[2].sval = ":Easy";
	ret[2].ival = 0;
	
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
	
	return ret;
}

static char *validate_params(const game_params *params, int full)
{
	int w = params->w;
	int h = params->h;
	
	if(w*h >= 1000) return "Puzzle is too large";
	
	if(w < 2) return "Width must be at least 2";
	if(h < 2) return "Height must be at least 2";
	
	if(w > 50) return "Width must be no more than 50";
	if(h > 50) return "Height must be no more than 50";
	
	return NULL;
}

static char check_completion(number *grid, int w, int h)
{
	int x = -1, y = -1, x2, y2, i;
	number last = (w*h)-1;
	
	/* Check for empty squares, and locate path start */
	for(i = 0; i < w*h; i++)
	{
		if(grid[i] == -1) return FALSE;
		if(grid[i] == 0)
		{
			x = i%w;
			y = i/w;
		}
	}
	assert(x != -1 && y != -1);
	
	/* Keep selecting the next number in line */
	while(grid[y*w+x] != last)
	{
		for(i = 0; i < 8; i++)
		{
			x2 = x + dir_x[i];
			y2 = y + dir_y[i];
			if(y2 < 0 || y2 >= h || x2 < 0 || x2 >= w)
				continue;
			
			if(grid[y2*w+x2] == grid[y*w+x] + 1)
				break;
		}
		
		/* No neighbour found */
		if(i == 8) return FALSE;
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

static void reverse_path(int i1, int i2, int *pathx, int *pathy) {
	int i;
	int ilim = (i2-i1+1)/2;
	int temp;
	for (i=0; i<ilim; i++)
	{
		temp = pathx[i1+i];
		pathx[i1+i] = pathx[i2-i];
		pathx[i2-i] = temp;

		temp = pathy[i1+i];
		pathy[i1+i] = pathy[i2-i];
		pathy[i2-i] = temp;
	}
}

static int backbite_left(int step, int n, int *pathx, int *pathy, int w, int h) {
	int neighx, neighy;
	int i, inPath = FALSE;
	neighx = pathx[0]+dir_x[step];
	neighy = pathy[0]+dir_y[step];
	
	if (neighx < 0 || neighx >= w || neighy < 0 || neighy >= h) return n;
	
	for (i=1;i<n;i++) {
		if (neighx == pathx[i] && neighy == pathy[i]) { inPath = TRUE; break; }
	}
	if (inPath) {
		reverse_path(0, i-1, pathx, pathy);
	}
	else {
		reverse_path(0, n-1, pathx, pathy);
		pathx[n] = neighx;
		pathy[n] = neighy;
		n++;        
	}
	
	return n;
}

static int backbite_right(int step, int n, int *pathx, int *pathy, int w, int h) {
	int neighx, neighy;
	int i, inPath = FALSE;
	neighx = pathx[n-1]+dir_x[step];
	neighy = pathy[n-1]+dir_y[step];
	if (neighx < 0 || neighx >= w || neighy < 0 || neighy >= h) return n;
	for (i=n-2;i>=0;i--) {
		if (neighx == pathx[i] && neighy == pathy[i]) { inPath = TRUE; break; }
	}
	if (inPath) {
		reverse_path(i+1, n-1, pathx, pathy);
	}
	else {
		pathx[n] = neighx;
		pathy[n] = neighy;
		n++;        
	}
	
	return n;
}

static int backbite(int n, int *pathx, int *pathy, int w, int h, random_state *rs) {
	if (random_upto(rs, 2)) return backbite_left(random_upto(rs, 8), n, pathx, pathy, w, h);
	else                    return backbite_right(random_upto(rs, 8), n, pathx, pathy, w, h);
}

static number *generate_hamiltonian_path(int w, int h, random_state *rs) {
	int *pathx = snewn(w*h, int);
	int *pathy = snewn(w*h, int);
	int i, n = 1;
	number *ret;
	
	pathx[0] = random_upto(rs, w);
	pathy[0] = random_upto(rs, h);
	
	while (n < w*h) {
		n = backbite(n, pathx, pathy, w, h, rs);
	}

	ret = snewn(w*h, number);
	for(i = 0; i < w*h; i++)
		ret[pathy[i]*w+pathx[i]] = i;
	
	sfree(pathx);
	sfree(pathy);

	return ret;
}

static void update_positions(cell *positions, number *grid, int s)
{
	cell i;
	number n;

	for(n = 0; n < s; n++) positions[n] = -1;
	for(i = 0; i < s; i++)
	{
		n = grid[i];
		if(n == -1 || n >= s) continue;
		positions[n] = (positions[n] == -1 ? i : -2);
	}
}

struct solver_scratch {
	int w, h;
	cell *positions;
	number *grid;
	bitmap *marks; /* GET_BIT i*s+n */
	int *path;
};

static struct solver_scratch *new_scratch(int w, int h)
{
	int i, n = w*h;
	struct solver_scratch *ret = snew(struct solver_scratch);
	ret->w = w;
	ret->h = h;
	ret->positions = snewn(n, cell);
	ret->grid = snewn(n, number);
	ret->path = snewn(n, int);
	for(i = 0; i < n; i++)
	{
		ret->positions[i] = -1;
		ret->grid[i] = -1;
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
	int s = scratch->w*scratch->h;
	cell i; number n;
	scratch->grid[pos] = num;
	scratch->positions[num] = (scratch->positions[num] == -1 ? pos : -2);
	
	for(i = 0; i < s; i++)
	{
		if(i == pos) continue;
		CLR_BIT(scratch->marks, i*s+num);
	}
	
	for(n = 0; n < s; n++)
	{
		if(n == num) continue;
		CLR_BIT(scratch->marks, pos*s+n);
	}
	
	return 1;
}

static int solver_single(struct solver_scratch *scratch)
{
	int s = scratch->w*scratch->h;
	cell i, found; number n;
	int ret = 0;
	
	for(n = 0; n < s; n++)
	{
		if(scratch->positions[n] != -1) continue;
		found = -1;
		for(i = 0; i < s; i++)
		{
			if(scratch->grid[i] != -1) continue;
			if(!GET_BIT(scratch->marks, i*s+n)) continue;
			if(found == -1)
				found = i;
			else
				found = -2;
		}
		assert(found != -1);
		if(found >= 0)
			ret += solver_place(scratch, found, n);
	}
	
	return ret;
}

static int solver_near(struct solver_scratch *scratch, cell near, number num)
{
	int w = scratch->w, s = scratch->h*w;
	int ret = 0;
	cell i;
	
	for(i = 0; i < s; i++)
	{
		if(!GET_BIT(scratch->marks, i*s+num)) continue;
		if(IS_NEAR(i, near, w)) continue;
		CLR_BIT(scratch->marks, i*s+num);
		ret++;
	}
	
	return ret;
}

static int solver_proximity(struct solver_scratch *scratch)
{
	int w = scratch->w, h = scratch->h, s=w*h;
	cell i; number n;
	int ret = 0;
	
	for(n = 0; n < s; n++)
	{
		i = scratch->positions[n];
		if(i < 0) continue;
		
		if(n > 0)
			ret += solver_near(scratch, i, n-1);
		if(n < s-1)
			ret += solver_near(scratch, i, n+1);
	}
	
	return ret;
}

static void ascent_solve(const number *puzzle, struct solver_scratch *scratch)
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
			if(scratch->grid[i] == -1)
				SET_BIT(scratch->marks, i*s+n);
		}
	}
	
	while(TRUE)
	{
		if(solver_single(scratch))
			continue;
		
		if(solver_proximity(scratch))
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
	struct solver_scratch *scratch = new_scratch(w, h);
	number temp;
	cell *spaces = snewn(w*h, cell);
	number *grid;
	
	for(i = 0; i < w*h; i++)
		spaces[i] = i;
	
	grid = generate_hamiltonian_path(w, h, rs);
	
	shuffle(spaces, w*h, sizeof(*spaces), rs);
	for(j = 0; j < w*h; j++)
	{
		i = spaces[j];
		temp = grid[i];
		if(temp == 0 || temp == w*h-1) continue;
		grid[i] = -1;
		
		ascent_solve(grid, scratch);
		
		if(!check_completion(scratch->grid, w, h))
			grid[i] = temp;
	}
	
	char *ret = snewn(w*h*4, char);
	char *p = ret;
	int run = 0;
	for(i = 0; i < w*h; i++)
	{
		if(grid[i] != -1)
		{
			if(i != 0)
				*p++ = run ? 'a' + run-1 : '_';
			p += sprintf(p, "%d", grid[i]+1);
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
	free_scratch(scratch);
	sfree(grid);
	return ret;
}

static char *validate_desc(const game_params *params, const char *desc)
{
	// TODO validate_desc
	return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
							const char *desc)
{
	int w = params->w;
	int h = params->h;
	int i;
	
	game_state *state = snew(game_state);

	state->w = w;
	state->h = h;
	state->completed = state->cheated = FALSE;
	state->grid = snewn(w*h, number);
	state->immutable = snewn(BITMAP_SIZE(w*h), bitmap);
	state->last = (w*h)-1;
	memset(state->immutable, 0, BITMAP_SIZE(w*h));
	
	for(i = 0; i < w*h; i++) state->grid[i] = -1;
	
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
						const char *aux, char **error)
{
	int i, w = state->w, h = state->h;
	struct solver_scratch *scratch = new_scratch(w, h);
	
	ascent_solve(state->grid, scratch);

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
	return TRUE;
}

static char *game_text_format(const game_state *state)
{
	return NULL;
}

struct game_ui
{
	cell held;
	number select, target;
	int dir;
	
	cell *positions;
	int s;
};

static game_ui *new_ui(const game_state *state)
{
	int s = state->w*state->h;
	game_ui *ret = snew(game_ui);
	
	ret->held = ret->select = ret->target = -1;
	ret->dir = 0;
	ret->positions = snewn(s, cell);
	ret->s = s;
	
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
		if(ui->positions[i] >= 0)
		{
			if(i != 0)
				*p++ = run ? 'a' + run-1 : '_';
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
	
	for(i = 0; i < ui->s; i++) ui->positions[i] = -1;
	i = 0;
	while(*p && i < ui->s)
	{
		if(isdigit((unsigned char) *p))
		{
			ui->positions[i] = atoi(p);
			if(ui->positions[i] >= ui->s) ui->positions[i] = -1;
			while (*p && isdigit((unsigned char) *p)) ++p;
			++i;
		}
		else if(*p >= 'a' && *p <= 'z')
			i += ((*p++) - 'a') + 1;
		else
			++p;
	}
}

static void ui_clear(game_ui *ui)
{
	ui->held = ui->select = ui->target = -1;
	ui->dir = 0;
}

static void ui_seek(game_ui *ui, number last)
{
	if(ui->select < 0 || ui->select > last)
	{
		ui->select = -1;
		ui->target = -1;
	}
	else
	{
		ui->target = ui->select;
		while(ui->positions[ui->target] == -1)
			ui->target += ui->dir;
	}
}

static void ui_backtrack(game_ui *ui, number last)
{
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
	while(n > 0 && n < last && ui->held == -1);
	
	ui->select = n + ui->dir;
	ui->target = ui->select;
	while(ui->positions[ui->target] == -1)
		ui->target += ui->dir;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
							   const game_state *newstate)
{
	update_positions(ui->positions, newstate->grid, newstate->w*newstate->h);
	
	if(ui->held != -1 && newstate->grid[ui->held] == -1)
	{
		ui_backtrack(ui, oldstate->last);
	}
	if(!oldstate->completed && newstate->completed)
	{
		ui_clear(ui);
	}
}

struct game_drawstate {
	int tilesize;
};

#define FROMCOORD(x) ( ((x)-(tilesize/2)) / tilesize )
#define DRAG_RADIUS 0.6F
static char *interpret_move(const game_state *state, game_ui *ui,
							const game_drawstate *ds,
							int ox, int oy, int button)
{
	int w = state->w, h = state->h;
	int tilesize = ds->tilesize;
	cell i;
	number n;
	char buf[80];
	
	int gx = FROMCOORD(ox);
	int gy = FROMCOORD(oy);
	
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
				i = ui->held;
		}
		n = state->grid[i];
		
		switch(button)
		{
		case LEFT_BUTTON:
			update_positions(ui->positions, state->grid, w*h);
			
			if(n != -1)
			{
				if(i == ui->held && ui->dir != 0)
				{
					ui->dir *= -1;
				}
				else
				{
					ui->held = i;
					ui->dir = n < state->last && ui->positions[n+1] == -1 ? +1
						: n > 0 && ui->positions[n-1] == -1 ? -1 : +1;
				}
				ui->select = n + ui->dir;
				
				ui_seek(ui, state->last);
				return "";
			}
		/* Deliberate fallthrough */
		case LEFT_DRAG:
			if(n >= 0 && ui->select == n)
			{
				ui->held = i;
				ui->select += ui->dir;
				ui_seek(ui, state->last);
				return "";
			}
			if(n == -1 && ui->held != -1 && ui->target != ui->select && IS_NEAR(ui->held, i, w))
			{
				sprintf(buf, "P%d,%d", i, ui->select);
				
				ui->held = i;
				ui->select += ui->dir;
				
				return dupstr(buf);
			}
			else if(n == -1 && button == LEFT_BUTTON)
			{
				ui_clear(ui);
				return "";
			}
		break;
		case MIDDLE_BUTTON:
		case RIGHT_BUTTON:
			update_positions(ui->positions, state->grid, w*h);
		/* Deliberate fallthrough */
		case MIDDLE_DRAG:
		case RIGHT_DRAG:
			if(n == -1 || GET_BIT(state->immutable, i))
				return NULL;
			
			sprintf(buf, "C%d", i);
			return dupstr(buf);
		}
	}
	
	if(button == '\b' && ui->held != -1 && !GET_BIT(state->immutable, ui->held))
	{
		sprintf(buf, "C%d", ui->held);
		return dupstr(buf);
	}
	
	return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	int w = state->w, h = state->h;
	cell i = -1;
	number n = -1;
	
	if (move[0] == 'P' &&
			sscanf(move+1, "%d,%d", &i, &n) == 2 &&
			i >= 0 && i < w*h && n > 0 && n < state->last
			)
	{
		if(GET_BIT(state->immutable, i))
			return NULL;
		
		game_state *ret = dup_game(state);
		
		ret->grid[i] = n;
		
		if(check_completion(ret->grid, w, h))
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
		
		ret->grid[i] = -1;
		
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
				ret->grid[i] = -1;
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
	
	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	struct game_drawstate *ds = snew(struct game_drawstate);

	ds->tilesize = 0;

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
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
	int i, tx, ty, tx2, ty2;
	number n;
	char buf[8];
	const cell *positions = ui->positions;
	int flash = -2;
	int margin = tilesize*ERROR_MARGIN;
	
	if(flashtime > 0)
		flash = (int)(flashtime/FLASH_FRAME);
	
	draw_rect(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize, COL_MIDLIGHT);
	draw_rect(dr, (tilesize/2), (tilesize/2)-1, w*tilesize+1, h*tilesize+1, COL_BORDER);
	draw_update(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize);
	
	/* Draw square backgrounds */
	for(i = 0; i < w*h; i++)
	{
		tx = TOCOORD(i%w), ty = TOCOORD(i/w);
		
		draw_rect(dr, tx, ty, tilesize, tilesize,
			flash >= state->grid[i] && flash <= state->grid[i] + FLASH_SIZE ? COL_LOWLIGHT :
			ui->held == i ? COL_LOWLIGHT : 
			positions[ui->target] == i ? COL_HIGHLIGHT : COL_MIDLIGHT);
		
		if(i == positions[0] || i == positions[state->last])
		{
			draw_circle(dr, tx+(tilesize/2), ty+(tilesize/2),
				tilesize/3, COL_HIGHLIGHT, COL_HIGHLIGHT);
		}
	}
	
	/* Draw paths */
	for(i = 0; i < state->last; i++)
	{
		if(positions[i] < 0 || positions[i+1] < 0) continue;
		
		tx = (positions[i]%w)*tilesize + (tilesize);
		ty = (positions[i]/w)*tilesize + (tilesize);
		tx2 = (positions[i+1]%w)*tilesize + (tilesize);
		ty2 = (positions[i+1]/w)*tilesize + (tilesize);
		
		if(IS_NEAR(positions[i], positions[i+1], w))
			draw_thick_line(dr, 5.0, tx, ty, tx2, ty2, COL_HIGHLIGHT);
	}
	
	/* Draw square borders */
	for(i = 0; i < w*h; i++)
	{
		tx = TOCOORD(i%w), ty = TOCOORD(i/w);
		
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
	}
	
	/* Draw numbers and path errors */
	for(i = 0; i < w*h; i++)
	{
		n = -1;
		
		tx = TOCOORD(i%w), ty = TOCOORD(i/w);
		tx2 = tx+(tilesize/2), ty2 = ty+(tilesize/2);
		
		n = state->grid[i];
		if(n == -1 && ui->held != -1 && IS_NEAR(i, ui->held, w) && positions[ui->select] < 0)
			n = ui->select;
		
		if(n < 0) continue;
		sprintf(buf, "%d", n+1);
		
		draw_text(dr, tx2, ty2,
				FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
				positions[n] == -2 ? COL_ERROR :
				GET_BIT(state->immutable, i) ? COL_IMMUTABLE : 
				state->grid[i] == -1 ? COL_LOWLIGHT : COL_BORDER, buf);
		
		if(state->grid[i] >= 0 && (
			(n < state->last && positions[n+1] >= 0 && !IS_NEAR(i, positions[n+1], w)) || 
			(n > 0 && positions[n-1] >= 0 && !IS_NEAR(i, positions[n-1], w)) ) )
		{
			draw_thick_line(dr, 2, tx+margin, ty+margin,
				(tx+tilesize)-margin, (ty+tilesize)-margin, COL_ERROR);
		}
		
		char next = n <= 0 || positions[n-1] >= 0 ?1:0;
		char prev = n >= state->last || positions[n+1] >= 0 ?1:0;
		if(state->grid[i] >= 0 && next^prev)
		{
			// TODO draw marker for dead-end lines
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
	32, game_compute_size, game_set_size,
	game_colours,
	game_new_drawstate,
	game_free_drawstate,
	game_redraw,
	game_anim_length,
	game_flash_length,
	game_status,
	FALSE, FALSE, game_print_size, game_print,
	FALSE,			       /* wants_statusbar */
	FALSE, game_timing_state,
	0,				       /* flags */
};
