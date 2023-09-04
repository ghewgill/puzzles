/*
 * boats.c: Implementation for Battleship puzzles.
 * (C) 2012 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective of the game: Place the given fleet in the grid.
 * Boats can be placed horizontally or vertically.
 * Boats cannot touch each other horizontally, vertically or diagonally.
 * The numbers outside the grid show the amount of squares occupied by a boat.
 * Some squares are given.
 *
 * Unofficial terminology:
 * Boat: A set of filled squares surrounded by empty squares
 * Fleet: The collection of boats to be placed
 * Ship: Square confirmed to be occupied by boat
 * Water or Blank: Square confirmed to be empty
 * Collision: Two ships are diagonally adjacent
 *
 * More information: http://en.wikipedia.org/wiki/Battleship_(puzzle)
 */

/*
 * TODO solver:
 * - Watch for smaller runs that cannot fit any boat, and fill them with water
 * - Recursion?
 *
 * TODO ui:
 * - Certain custom fleets don't fit in the UI
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
bool solver_steps = false;

#define solver_printf if(!solver_verbose) {} else printf

#else
#define solver_printf(...)
#endif

enum {
	COL_BACKGROUND,
	COL_GRID,
	COL_CURSOR_A,
	COL_CURSOR_B,
	COL_WATER,
	COL_SHIP_CLUE,
	COL_SHIP_GUESS,
	COL_SHIP_ERROR,
	COL_SHIP_FLEET,
	COL_SHIP_FLEET_DONE,
	COL_SHIP_FLEET_STRIPE,
	COL_COUNT,
	COL_COUNT_ERROR,
	COL_COLLISION_ERROR,
	COL_COLLISION_TEXT,
	NCOLOURS
};

/*
 * Note that all ship parts must come last, 
 * and that SHIP_VAGUE is the first of the ship parts
 */
enum {
	EMPTY,
	CORRUPT, /* Occurs when the solver makes a mistake */
	WATER,
	SHIP_VAGUE, /* Part of a ship, but not sure which part */
	SHIP_TOP,
	SHIP_BOTTOM,
	SHIP_CENTER,
	SHIP_LEFT,
	SHIP_RIGHT,
	SHIP_SINGLE,
};

#define NO_CLUE (-1)
#define IS_SHIP(x) ( (x) >= SHIP_VAGUE )

struct game_params {
	int w;
	int h;
	int fleet;
	int *fleetdata;
	
	/* full */
	int diff;
	bool strip;
};

struct game_state {
	/* puzzle data */
	int w;
	int h;
	int fleet;
	char *gridclues;
	int *borderclues;
	int *fleetdata;
	
	/* play data */
	char *grid;
	bool completed, cheated;
};

#define DIFFLIST(A)                             \
	A(EASY,Easy, e)                             \
	A(NORMAL,Normal, n)                         \
	A(TRICKY,Tricky, t)                         \
	A(HARD,Hard, h)                             

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const boats_diffnames[] = { DIFFLIST(TITLE) };

static char const boats_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

static int *boats_default_fleet(int fleet)
{
	int *ret = snewn(fleet, int);
	int i;
	
	memset(ret, 0, fleet*sizeof(int));

	for(i = 0; i < fleet; i++)
		ret[i] = fleet - i;
	
	return ret;
}

static int *boats_decode_fleet(const char *input, int fleet)
{
	int *ret = snewn(fleet, int);
	const char *p = input;
	int i;
	for(i = 0; i < fleet; i++)
	{
		while (*p && !isdigit((unsigned char) *p)) ++p;
		if(*p)
		{
			ret[i] = atoi(p);
			while (*p && isdigit((unsigned char) *p)) ++p;
		}
		else
			ret[i] = 0;
	}
	return ret;
}

static char *boats_encode_fleet(const int *input, int fleet)
{
	char buf[256];
	char *p = buf;
	int i;
	for(i = 0; i < fleet; i++)
	{
		p += sprintf(p, "%d,", input[i]);
	}
	p--;
	*p = '\0';
	return dupstr(buf);
}

const static struct game_params boats_presets[] = {
    { 6, 6, 3, NULL, DIFF_EASY, false },
    { 6, 6, 3, NULL, DIFF_NORMAL, false },
    { 6, 6, 3, NULL, DIFF_HARD, false },
    { 8, 8, 4, NULL, DIFF_EASY, false },
    { 8, 8, 4, NULL, DIFF_NORMAL, false },
    { 8, 8, 4, NULL, DIFF_HARD, false },
    { 10, 10, 4, NULL, DIFF_EASY, false },
    { 10, 10, 4, NULL, DIFF_NORMAL, false },
    { 10, 10, 4, NULL, DIFF_TRICKY, false },
    { 10, 10, 4, NULL, DIFF_HARD, false },
    { 10, 12, 5, NULL, DIFF_TRICKY, false },
    { 10, 12, 5, NULL, DIFF_HARD, false },
};

#define DEFAULT_PRESET 7

static bool game_fetch_preset(int i, char **name, game_params **params)
{
	game_params *ret = snew(game_params);
	char buf[80];
	
	if(i < 0 || i >= lenof(boats_presets))
		return false;
	
	*ret = boats_presets[i];
	ret->fleetdata = boats_default_fleet(ret->fleet);
	
	sprintf(buf, "%ix%i, size %i %s", ret->w, ret->h, ret->fleet, boats_diffnames[ret->diff]);
	
	*params = ret;
	if(name)
		*name = dupstr(buf);
	
	return true;
}

static game_params *default_params(void)
{
	assert(DEFAULT_PRESET < lenof(boats_presets));
	game_params *ret = NULL;

	game_fetch_preset(DEFAULT_PRESET, NULL, &ret);
	
	return ret;
}

static void free_params(game_params *params)
{
	sfree(params->fleetdata);
	sfree(params);
}

static game_params *dup_params(const game_params *params)
{
	game_params *ret = snew(game_params);
	*ret = *params;		       /* structure copy */
	
	ret->fleetdata = snewn(ret->fleet, int);
	memcpy(ret->fleetdata, params->fleetdata, ret->fleet * sizeof(int));
	
	return ret;
}

static void decode_params(game_params *params, char const *string)
{
	char const *p = string;
	params->fleet = 4;
	params->strip = false;
	
	/* Find width */
	params->w = params->h = atoi(p);
	while (*p && isdigit((unsigned char) *p)) ++p;
	
	/* Find height */
	if (*p == 'x')
	{
		++p;
		params->h = atoi(p);
	}
	while (*p && isdigit((unsigned char) *p)) ++p;
		
	/* Find fleet size */
	if (*p == 'f')
	{
		++p;
		params->fleet = atoi(p);
	}
	while (*p && isdigit((unsigned char) *p)) ++p;
	
	/* Difficulty */
	if (*p == 'd') {
        int i;
        p++;
        params->diff = DIFFCOUNT + 1;   /* ...which is invalid */
        if (*p) {
            for (i = 0; i < DIFFCOUNT; i++) {
                if (*p == boats_diffchars[i])
                    params->diff = i;
            }
            p++;
        }
    }
	
	if(*p == 'S')
	{
		params->strip = true;
		p++;
	}
	
	if(params->fleetdata)
		sfree(params->fleetdata);
	
	if(*p == ',')
		params->fleetdata = boats_decode_fleet(p, params->fleet);
	else
		params->fleetdata = boats_default_fleet(params->fleet);
}

static char *encode_params(const game_params *params, bool full)
{
	char data[256];
	char *p = data;
	char *fleet;
	
	p += sprintf(p, "%dx%df%d", params->w, params->h, params->fleet);
	if(full)
		p += sprintf(p, "d%c", boats_diffchars[params->diff]);
	if(full && params->strip)
		p += sprintf(p, "S");
	
	fleet = boats_encode_fleet(params->fleetdata, params->fleet);
	p += sprintf(p, ",%s", fleet);
	sfree(fleet);
	
	return dupstr(data);
}

static config_item *game_configure(const game_params *params)
{
	config_item *ret;
    char buf[80];
	int *fleet = boats_default_fleet(params->fleet);

    ret = snewn(7, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);
	
    ret[2].name = "Fleet size";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->fleet);
    ret[2].u.string.sval = dupstr(buf);

	ret[3].name = "Fleet configuration";
	ret[3].type = C_STRING;
	/* Only fill this field if the fleet is non-standard */
	if(memcmp(params->fleetdata, fleet, params->fleet*sizeof(int)))
		ret[3].u.string.sval = boats_encode_fleet(params->fleetdata, params->fleet);
	else
		ret[3].u.string.sval = dupstr("");
	
    ret[4].name = "Difficulty";
    ret[4].type = C_CHOICES;
    ret[4].u.choices.choicenames = DIFFCONFIG;
    ret[4].u.choices.selected = params->diff;
	
	ret[5].name = "Remove numbers";
	ret[5].type = C_BOOLEAN;
	ret[5].u.boolean.bval = params->strip;

    ret[6].name = NULL;
    ret[6].type = C_END;

	sfree(fleet);
    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->fleet = atoi(cfg[2].u.string.sval);
    ret->diff = cfg[4].u.choices.selected;
    ret->strip = cfg[5].u.boolean.bval;

	if(ret->fleet < 1 || ret->fleet > 9)
		ret->fleetdata = NULL;
	else if(strcmp(cfg[3].u.string.sval, ""))
		ret->fleetdata = boats_decode_fleet(cfg[3].u.string.sval, ret->fleet);
	else
		ret->fleetdata = boats_default_fleet(ret->fleet);
	
    return ret;
}

static game_state *blank_game(int w, int h, int f, int *fleetdata)
{	
	game_state *ret = snew(game_state);

	ret->w = w;
	ret->h = h;
	ret->fleet = f;
	ret->gridclues = snewn(w*h, char);
	ret->grid = snewn(w*h, char);
	ret->borderclues = snewn(w+h, int);
	
	ret->fleetdata = snewn(f, int);
	memcpy(ret->fleetdata, fleetdata, f * sizeof(int));
	
	ret->completed = ret->cheated = false;
	
	return ret;
}

static game_state *new_game(midend *me, const game_params *params, const char *desc)
{
	int w = params->w;
	int h = params->h;
	int fleet = params->fleet;
	
	int i, j;
	int num;
	const char *p = desc;
	
	game_state *state = blank_game(w, h, fleet, params->fleetdata);
	memset(state->gridclues, EMPTY, w*h*sizeof(char));
	memset(state->grid, EMPTY, w*h*sizeof(char));
	
	i = 0;
	j = 0;
	while(*p)
	{
		if(isdigit((unsigned char) *p))
		{
			num = atoi(p);
			state->borderclues[i++] = num;
			while (*p && isdigit((unsigned char) *p)) ++p;
		}
		else if (*p == '-')
		{
			state->borderclues[i++] = NO_CLUE;
			++p;
		}
		else if(*p >= 'a' && *p <= 'z')
		{
			j += *p - 'a' + 1;
			++p;
		}
		else if(*p >= 'A' && *p <= 'Z')
		{
			switch(*p)
			{
				case 'S':
					state->gridclues[j] = SHIP_SINGLE;
					break;
				case 'V':
					state->gridclues[j] = SHIP_VAGUE;
					break;
				case 'T':
					state->gridclues[j] = SHIP_TOP;
					break;
				case 'B':
					state->gridclues[j] = SHIP_BOTTOM;
					break;
				case 'C':
					state->gridclues[j] = SHIP_CENTER;
					break;
				case 'L':
					state->gridclues[j] = SHIP_LEFT;
					break;
				case 'R':
					state->gridclues[j] = SHIP_RIGHT;
					break;
				case 'W':
					state->gridclues[j] = WATER;
					break;
				default:
					assert(!"Invalid character");
			}
			
			state->grid[j] = IS_SHIP(state->gridclues[j]) ? SHIP_VAGUE : WATER;
			
			++j;
			++p;
		}
		else
			++p;
	}
	
	return state;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
	int w = params->w;
	int h = params->h;
	int bcs = 0;
	int gds = 0;
	
	const char *p = desc;
	
	while(*p)
	{
		if(isdigit((unsigned char) *p))
		{
			++bcs;
			while (*p && isdigit((unsigned char) *p)) ++p;
		}
		else if (*p == '-')
		{
			++bcs;
			++p;
		}
		else if(*p >= 'a' && *p <= 'z')
		{
			gds += *p - 'a' + 1;
			++p;
		}
		else if(*p >= 'A' && *p <= 'Z')
		{
			switch(*p)
			{
				case 'S':
				case 'V':
				case 'T':
				case 'B':
				case 'C':
				case 'L':
				case 'R':
				case 'W':
					++gds;
					++p;
					break;
				default:
					return "Description contains invalid characters";
			}
		}
		else
			p++;
	}
	
	if(bcs < w+h)
		return "Not enough border clues";
	if(bcs > w+h)
		return "Too many border clues";
	if(gds > w*h)
		return "Too many grid clues";
	
	return NULL;
}

static game_state *dup_game(const game_state *state)
{
	int w = state->w;
	int h = state->h;
	int f = state->fleet;
	
	game_state *ret = blank_game(w, h, f, state->fleetdata);

	memcpy(ret->gridclues, state->gridclues, w*h*sizeof(char));
	memcpy(ret->grid, state->grid, w*h*sizeof(char));
	memcpy(ret->borderclues, state->borderclues, (w+h)*sizeof(int));
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;

	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->gridclues);
	sfree(state->borderclues);
	
	sfree(state->grid);
	sfree(state->fleetdata);
	
	sfree(state);
}

static bool game_can_format_as_text_now(const game_params *params)
{
	return params->w <= 10 && params->h <= 10;
}

static char *game_text_format(const game_state *state)
{
	int w = state->w;
	int h = state->h;
	
	int lr = (w * 2) + 2;
	int s = (lr*(h+1)) + 1;
	int i;
	char ship, c;
	
	char *ret = snewn(s, char);
	
	memset(ret, ' ' , s*sizeof(char));
	
	/* Place newlines */
	for(i = 0; i < h + 1; i++)
	{
		ret[((i+1)*lr)-1] = '\n';
	}
	
	/* Place horizontal counts */
	for(i = 0; i < w; i++)
	{
		if(state->borderclues[i] != NO_CLUE)
			ret[(lr*h) + (i*2)] = state->borderclues[i] + '0';
	}
	/* Place vertical counts */
	for(i = 0; i < h; i++)
	{
		if(state->borderclues[i+w] != NO_CLUE)
			ret[((i+1)*lr) - 2] = state->borderclues[i+w] + '0';
	}
	
	/* Place items */
	for(i = 0; i < w*h; i++)
	{
		ship = EMPTY;
		if(state->gridclues[i] != EMPTY)
			ship = state->gridclues[i];
		else
			ship = state->grid[i];
		
		switch(ship)
		{
			case EMPTY:
				c = '.';
				break;
			case WATER:
				c = '-';
				break;
			case SHIP_VAGUE:
				c = '@';
				break;
			case SHIP_TOP:
				c = '^';
				break;
			case SHIP_BOTTOM:
				c = 'V';
				break;
			case SHIP_CENTER:
				c = '#';
				break;
			case SHIP_LEFT:
				c = '<';
				break;
			case SHIP_RIGHT:
				c = '>';
				break;
			case SHIP_SINGLE:
				c = 'O';
				break;
			default:
				c = '?';
				break;
		}
		
		ret[((i%w) * 2) + ((i/w)*lr)] = c;
	}
	
	ret[s-1] = '\0';
	
	return ret;
}

enum { STATUS_COMPLETE, STATUS_INCOMPLETE, STATUS_INVALID };

/* ******************** *
 * Validation and Tools *
 * ******************** */

#define FE_COLLISION 0x01 
#define FE_MISMATCH  0x02
#define FE_FLEET     0x04
#define FD_CURSOR    0x08

struct boats_run 
{
	int row;
	int start;
	int len;
	int ships;
	bool horizontal;
};
 
static int boats_count_ships(const game_state *state, int *blankcounts, int *shipcounts, int *errs)
{
	/* 
	 * Count the ships and blank squares in the grid, and returns the status.
	 * Optionally fills in an array of found ships or blank squares.
	 * Can also fill in a grid of errors.
	 */
	
	int w = state->w;
	int h = state->h;
	int x, y;
	int blanks, ships;
	int ret = STATUS_COMPLETE;
	
	/* Column counts */
	for(x = 0; x < w; x++)
	{
		blanks = 0;
		ships = 0;
		
		for(y = 0; y < h; y++)
		{
			if(state->grid[y*w+x] == WATER)
				blanks++;
			else if (IS_SHIP(state->grid[y*w+x]))
				ships++;
			else if(state->grid[y*w+x] == CORRUPT)
				ret = STATUS_INVALID;
		}
		
		if(blankcounts)
			blankcounts[x] = blanks;
		if(shipcounts)
			shipcounts[x] = ships;
		
		if(state->borderclues[x] == NO_CLUE)
			continue;
		
		/* Too many ships or blank squares */
		if(ships > state->borderclues[x] || blanks > (h - state->borderclues[x]))
		{
			ret = STATUS_INVALID;
			if(errs)
				errs[x] = STATUS_INVALID;
		}
		/* Not enough ships */
		else if (ships < state->borderclues[x])
		{
			ret = (ret != STATUS_INVALID ? STATUS_INCOMPLETE : ret);
			if(errs)
				errs[x] = STATUS_INCOMPLETE;
		}
		else if(errs)
			errs[x] = STATUS_COMPLETE;
	}
	
	/* Row counts */
	for(y = 0; y < h; y++)
	{
		blanks = 0;
		ships = 0;
		
		for(x = 0; x < w; x++)
		{
			if(state->grid[y*w+x] == WATER)
				blanks++;
			else if (IS_SHIP(state->grid[y*w+x]))
				ships++;
		}
		
		if(blankcounts)
			blankcounts[y+w] = blanks;
		if(shipcounts)
			shipcounts[y+w] = ships;
		
		if(state->borderclues[y+w] == NO_CLUE)
			continue;
		
		if(ships > state->borderclues[y+w] || blanks > (w - state->borderclues[y+w]))
		{
			ret = STATUS_INVALID;
			if(errs)
				errs[y+w] = STATUS_INVALID;
		}
		else if (ships < state->borderclues[y+w])
		{
			ret = (ret != STATUS_INVALID ? STATUS_INCOMPLETE : ret);
			if(errs)
				errs[y+w] = STATUS_INCOMPLETE;
		}
		else if(errs)
			errs[y+w] = STATUS_COMPLETE;
	}
	
	return ret;
}

static int boats_adjust_ships(game_state *state)
{	
	int w = state->w;
	int h = state->h;
	int x, y, i;
	int maxships = 0;
	int shipsum = 0;
	int watersum = 0;
	int ret = STATUS_COMPLETE;
	char sleft, sright, sup, sdown;
	bool edge;
	
	/* Count the current and required amount of ships */
	for(i = 0; i < state->fleet; i++)
		maxships += state->fleetdata[i] * (i+1);
	for(i = 0; i < w*h; i++)
	{
		if(IS_SHIP(state->grid[i]))
			shipsum++;
		else if(state->grid[i] == WATER)
			watersum++;
	}
	
	if(shipsum > maxships || (w*h) - watersum < maxships)
		ret = STATUS_INVALID;
	else if(shipsum < maxships)
		ret = STATUS_INCOMPLETE;
	
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if(!IS_SHIP(state->grid[y*w+x]))
			continue;
		
		sleft = x == 0 ? WATER 
			: state->grid[y*w+(x-1)];
		sright = x == w-1 ? WATER 
			: state->grid[y*w+(x+1)];
		sup = y == 0 ? WATER 
			: state->grid[(y-1)*w+x];
		sdown = y == h-1 ? WATER 
			: state->grid[(y+1)*w+x];
		
		edge = IS_SHIP(state->gridclues[y*w+x]) && 
			state->gridclues[y*w+x] != SHIP_VAGUE && 
			state->gridclues[y*w+x] != SHIP_CENTER;
		
		/* 
		 * If there is exactly enough ships in the grid,
		 * assume all other squares are water 
		 */
		if(maxships == shipsum)
		{
			sleft = sleft == EMPTY ? WATER : sleft;
			sright = sright == EMPTY ? WATER : sright;
			sup = sup == EMPTY ? WATER : sup;
			sdown = sdown == EMPTY ? WATER : sdown;
		}
		
		if(sleft == WATER && sright == WATER && 
			sup == WATER && sdown == WATER)
			state->grid[y*w+x] = SHIP_SINGLE;
		else if((IS_SHIP(sleft) && IS_SHIP(sright)) || 
			(IS_SHIP(sup) && IS_SHIP(sdown)))
			state->grid[y*w+x] = SHIP_CENTER;
		else if((edge || sleft == WATER) && IS_SHIP(sright))
			state->grid[y*w+x] = SHIP_LEFT;
		else if((edge || sright == WATER) && IS_SHIP(sleft))
			state->grid[y*w+x] = SHIP_RIGHT;
		else if((edge || sup == WATER) && IS_SHIP(sdown))
			state->grid[y*w+x] = SHIP_TOP;
		else if((edge || sdown == WATER) && IS_SHIP(sup))
			state->grid[y*w+x] = SHIP_BOTTOM;
		else
			state->grid[y*w+x] = SHIP_VAGUE;
	}
	
	return ret;
}

static bool boats_check_collision(const game_state *state, int *grid)
{
	/*
	 * Check if a collision occurs.
	 * Optionally sets/unsets the FE_COLLISION flag in the given array.
	 */
	
	int w = state->w;
	int h = state->h;
	int x, y;
	bool ret = false;
	
	for(x = 0; x < w-1; x++)
	for(y = 0; y < h-1; y++)
	{
		if( (IS_SHIP(state->grid[y*w+x]) && 
				IS_SHIP(state->grid[(y+1)*w+(x+1)]))
			|| (IS_SHIP(state->grid[(y+1)*w+x]) && 
				IS_SHIP(state->grid[y*w+(x+1)])) )
		{
			if(grid)
				grid[y*w+x] |= FE_COLLISION;
			ret = true;
		}
		else if(grid)
			grid[y*w+x] &= ~FE_COLLISION;
	}
	
	return ret;
}

static char boats_check_fleet(const game_state *state, int *fleetcount, int *errs)
{
	/*
	 * Count all confirmed ships (without SHIP_VAGUE).
	 * Optionally fills an array of ships with the actual counts.
	 * Can also mark boats that don't appear in the fleet.
	 */
	
	int fleet = state->fleet;
	int w = state->w;
	int h = state->h;
	int x, y, i, len;
	char hasfleetcount = fleetcount != NULL;
	char ret = STATUS_COMPLETE;
	bool inship, iserror;
	
	if(!hasfleetcount)
		fleetcount = snewn(fleet, int);
	
	memset(fleetcount, 0, fleet * sizeof(int));
	
	if(errs)
	{
		for(i = 0; i < w*h; i++)
			errs[i] &= ~FE_FLEET;
	}
	
	/* Count singles */
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if(fleet >= 1 && state->grid[y*w+x] == SHIP_SINGLE)
		{
			if(errs && state->fleetdata[0] == 0)
				errs[y*w+x] |= FE_FLEET;
			
			fleetcount[0]++;
		}
	}
	
	/* Count vertical ships */
	for(x = 0; x < w; x++)
	{
		len = 0;
		inship = false;
		for(y = 0; y < h; y++)
		{
			iserror = false;
			if(state->grid[y*w+x] == SHIP_TOP)
				inship = true;
			
			if(inship)
				len++;
			
			if(inship && state->grid[y*w+x] == SHIP_BOTTOM)
			{
				inship = false;
				if(len > fleet)
				{
					ret = STATUS_INVALID;
					iserror = true;
				}
				else if (len > 0)
				{
					fleetcount[len - 1]++;
					if(state->fleetdata[len - 1] == 0)
						iserror = true;
				}
				
				if(errs && iserror)
				{
					for(i = 0; i < len; i++)
						errs[(y-i)*w+x] |= FE_FLEET;
				}
				len = 0;
			}
			else if(state->grid[y*w+x] == SHIP_VAGUE)
			{
				inship = false;
				len = 0;
			}
		}
	}
	
	/* Count horizontal ships */
	for(y = 0; y < h; y++)
	{
		len = 0;
		inship = false;
		for(x = 0; x < w; x++)
		{
			iserror = false;
			if(state->grid[y*w+x] == SHIP_LEFT)
				inship = true;
			
			if(inship)
				len++;
			
			if(inship && state->grid[y*w+x] == SHIP_RIGHT)
			{
				inship = false;
				if(len > fleet)
				{
					ret = STATUS_INVALID;
					iserror = true;
				}
				else if (len > 0)
				{
					fleetcount[len - 1]++;
					if(state->fleetdata[len - 1] == 0)
						iserror = true;
				}
				if(errs && iserror)
				{
					for(i = 0; i < len; i++)
						errs[y*w+x-i] |= FE_FLEET;
				}
				len = 0;
			}
			else if(state->grid[y*w+x] == SHIP_VAGUE)
			{
				inship = false;
				len = 0;
			}
		}
	}
	
	/* Validate counts */
	for(x = 0; x < fleet; x++)
	{
		if(fleetcount[x] < state->fleetdata[x] && ret != STATUS_INVALID)
			ret = STATUS_INCOMPLETE;
		else if(fleetcount[x] > state->fleetdata[x])
			ret = STATUS_INVALID;
	}
	
	if(!hasfleetcount)
		sfree(fleetcount);
	
	return ret;
}

static int boats_collect_runs(game_state *state, struct boats_run *runs)
{
	/*
	 * Find all runs of spaces, separated by water.
	 * This can be used to find positions for certain boats.
	 */
	
	int i = 0;
	int w = state->w;
	int h = state->h;
	int x, y;
	bool inrun = false;
	
	/* Horizontal */
	for(y = 0; y < h; y++)
	{
		inrun = false;
		for(x = 0; x <= w; x++)
		{
			if(inrun && (x == w || state->grid[y*w+x] == WATER))
			{
				inrun = false;
				i++;
				continue;
			}
			else if(x == w)
				continue;
			
			if(!inrun && state->grid[y*w+x] == WATER)
				continue;
			else if(!inrun && state->grid[y*w+x] != WATER)
			{
				inrun = true;
				runs[i].row = y;
				runs[i].start = x;
				runs[i].len = 0;
				runs[i].horizontal = true;
				runs[i].ships = 0;
			}
			
			if(inrun && state->grid[y*w+x] != WATER)
			{
				runs[i].len++;
			}
			if(inrun && IS_SHIP(state->grid[y*w+x]))
				runs[i].ships++;
		}
	}
	
	/* Vertical */
	for(x = 0; x < w; x++)
	{
		inrun = false;
		for(y = 0; y <= h; y++)
		{
			if(inrun && (y == h || state->grid[y*w+x] == WATER))
			{
				inrun = false;
				i++;
				continue;
			}
			else if(y == h)
				continue;
			
			if(!inrun && state->grid[y*w+x] == WATER)
				continue;
			else if(!inrun && state->grid[y*w+x] != WATER)
			{
				inrun = true;
				runs[i].row = x;
				runs[i].start = y;
				runs[i].len = 0;
				runs[i].horizontal = false;
				runs[i].ships = 0;
			}
			
			if(inrun && state->grid[y*w+x] != WATER)
			{
				runs[i].len++;
			}
			if(inrun && IS_SHIP(state->grid[y*w+x]))
				runs[i].ships++;
		}
	}
	
#if 0
	if (solver_verbose) {
		int d;
		for(d = 0; d < i; d++)
		{
			printf("RUN: row=%i start=%i len=%i ships=%i %s\n",
				runs[d].row, runs[d].start, runs[d].len, runs[d].ships,
				runs[d].horizontal ? "Horizontal" : "Vertical"); 
		}
	}
#endif
	
	return i;
}

static char boats_validate_gridclues(const game_state *state, int *errs)
{
	/*
	 * Check if the given grid clues match the current grid values.
	 * Optionally sets/unsets FE_MISMATCH in the given array.
	 */
	
	int w = state->w;
	int h = state->h;
	int x, y;
	char ret = STATUS_COMPLETE;
	char sleft, sright, sup, sdown;
	bool error;
	
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if(state->grid[y*w+x] == SHIP_VAGUE && ret != STATUS_INVALID)
			ret = STATUS_INCOMPLETE;
		
		if(state->gridclues[y*w+x] == EMPTY)
		{
			if(errs)
				errs[y*w+x] &= ~FE_MISMATCH;
			continue;
		}
		
		error = false;
		if(state->gridclues[y*w+x] != SHIP_VAGUE && state->grid[y*w+x] != SHIP_VAGUE 
			&& state->gridclues[y*w+x] != state->grid[y*w+x])
		{
			error = true;
		}
		else
		{
			sleft = x == 0 ? WATER 
				: state->grid[y*w+(x-1)];
			sright = x == w-1 ? WATER 
				: state->grid[y*w+(x+1)];
			sup = y == 0 ? WATER 
				: state->grid[(y-1)*w+x];
			sdown = y == h-1 ? WATER 
				: state->grid[(y+1)*w+x];
			
			if(state->gridclues[y*w+x] == SHIP_LEFT && sright == WATER)
				error = true;
			else if(state->gridclues[y*w+x] == SHIP_RIGHT && sleft == WATER)
				error = true;
			else if(state->gridclues[y*w+x] == SHIP_TOP && sdown == WATER)
				error = true;
			else if(state->gridclues[y*w+x] == SHIP_BOTTOM && sup == WATER)
				error = true;
			else if(state->gridclues[y*w+x] == SHIP_CENTER &&
				(sleft == WATER || sright == WATER) &&
				(sup == WATER || sdown == WATER))
				error = true;
		}
		
		if(error)
		{
			ret = STATUS_INVALID;
			if(errs)
				errs[y*w+x] |= FE_MISMATCH;
		}
		else if(errs)
			errs[y*w+x] &= ~FE_MISMATCH;
	}
	
	return ret;
}

static char boats_check_dsf(game_state *state, DSF *dsf, int *fleetcount)
{
	/* 
	 * Build a dsf of all unfinished boats. Finished boats, water and empty
	 * spaces are all put into one large set. Index w*h is part of this set.
	 */
	 
	int w = state->w;
	int h = state->h;
	int end = w*h;
	int x, y, i;
	int *tempfleet = snewn(state->fleet, int);
	char ret = STATUS_COMPLETE;
	
	memcpy(tempfleet, fleetcount, sizeof(int)*state->fleet);
	dsf_reinit(dsf);
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i = y*w+x;
		if (state->grid[i] == EMPTY || state->grid[i] == WATER)
		{
			dsf_merge(dsf, i, end);
			continue;
		}
		
		if(x < w-1 && IS_SHIP(state->grid[i]) && IS_SHIP(state->grid[y*w+x+1]))
			dsf_merge(dsf, i, y*w+x+1);
		if(y < h-1 && IS_SHIP(state->grid[i]) && IS_SHIP(state->grid[(y+1)*w+x]))
			dsf_merge(dsf, i, (y+1)*w+x);
		
		if(state->grid[i] == SHIP_SINGLE)
			dsf_merge(dsf, i, end);
		/* The canonical index always points to the first square of a boat */
		else if(state->grid[i] == SHIP_RIGHT && state->grid[dsf_canonify(dsf, i)] == SHIP_LEFT)
			dsf_merge(dsf, i, end);
		else if(state->grid[i] == SHIP_BOTTOM && state->grid[dsf_canonify(dsf, i)] == SHIP_TOP)
			dsf_merge(dsf, i, end);
	}
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i = y*w+x;
		if(dsf_canonify(dsf, i) == dsf_canonify(dsf, end))
			continue;
		
		if(i == dsf_canonify(dsf, i))
		{
			if(ret != STATUS_INVALID) ret = STATUS_INCOMPLETE;
			if(dsf_size(dsf, i) > state->fleet)
				ret = STATUS_INVALID;
			else
				tempfleet[dsf_size(dsf, i) - 1]++;
		}
	}
	
	for(i = state->fleet - 1; i >= 0; i--)
	{
		if(fleetcount[i] < state->fleetdata[i])
			break;
		
		if(tempfleet[i] > state->fleetdata[i])
			ret = STATUS_INVALID;
	}
	
	sfree(tempfleet);
	return ret;
}

static char boats_validate_full_state(game_state *state, int *blankcounts, int *shipcounts, int *fleetcount, DSF *dsf)
{
	/*
	 * Check if the current state is complete, incomplete, or contains errors.
	 * Optionally fills the counts for blank spaces, ships, and the fleet count.
	 * Will only validate unfinished boats if dsf is non-NULL.
	 */
	
	char status, adjuststatus;
	
	status = boats_count_ships(state, blankcounts, shipcounts, NULL);
	if(status == STATUS_INVALID)
		return status;
	if(boats_check_collision(state, NULL))
	{
		return STATUS_INVALID;
	}
	
	adjuststatus = boats_adjust_ships(state);
	
	status = max(status, boats_check_fleet(state, fleetcount, NULL));
	status = max(status, boats_validate_gridclues(state, NULL));
	
	if(status != STATUS_INVALID && dsf)
		status = max(status, boats_check_dsf(state, dsf, fleetcount));
	
	/* When all ships are placed, everything else must be complete */
	if(adjuststatus == STATUS_INVALID || 
		(adjuststatus == STATUS_COMPLETE && status != STATUS_COMPLETE))
		return STATUS_INVALID;
	
	return status;
}

static char boats_validate_state(game_state *state)
{
	/* Short version of boats_validate_full_state */
	return boats_validate_full_state(state, NULL, NULL, NULL, NULL);
}

/* ****** *
 * Solver *
 * ****** */
static int boats_solver_place_water(game_state *state, int x, int y)
{
	/*
	 * Place water in a square. If the square is out of bounds,
	 * or already contains water, this function returns 0.
	 */
	
	int w = state->w;
	int h = state->h;
	int ret = 0;
	
	if(x < 0 || x >= w || y < 0 || y >= h)
		return 0;
	
	if(IS_SHIP(state->grid[y*w+x]))
	{
		ret++;
		state->grid[y*w+x] = CORRUPT;
	}
	else if(state->grid[y*w+x] == EMPTY)
	{
		ret++;
		state->grid[y*w+x] = WATER;
		solver_printf("Place water at %i,%i\n", x, y);
	}
	
	return ret;
}
 
static int boats_solver_place_ship(game_state *state, int x, int y)
{
	/*
	 * Place a ship in a square, and place water in the diagonally
	 * adjacent squares. If there is already a ship in the square,
	 * this function returns 0.
	 */
	
	int w = state->w;
	int h = state->h;
	int ret = 0;
	
	assert(x >= 0 && x < w && y >= 0 && y < h);
	
	if(state->grid[y*w+x] == WATER)
	{
		ret++;
		state->grid[y*w+x] = CORRUPT;
	}
	else if(state->grid[y*w+x] == EMPTY)
	{
		ret++;
		state->grid[y*w+x] = SHIP_VAGUE;
		solver_printf("Place ship at %i,%i\n", x, y);
		
		ret += boats_solver_place_water(state, x-1, y-1);
		ret += boats_solver_place_water(state, x+1, y-1);
		ret += boats_solver_place_water(state, x-1, y+1);
		ret += boats_solver_place_water(state, x+1, y+1);
	}
	
	return ret;
}

static int boats_solver_initial(game_state *state)
{
	/*
	 * Process all given grid clues. SHIP_CENTER is a special case,
	 * which needs to be checked constantly during the solving process.
	 */
	
	int w = state->w;
	int h = state->h;
	int ret = 0;
	int x, y;
	
	memset(state->grid, EMPTY, w*h*sizeof(char));
	
	solver_printf("Processing grid clues\n");
	
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		switch(state->gridclues[y*w+x])
		{
			case WATER:
				ret += boats_solver_place_water(state, x, y);
				break;
			case SHIP_VAGUE:
			case SHIP_CENTER:
				ret += boats_solver_place_ship(state, x, y);
				break;
			case SHIP_TOP:
				ret += boats_solver_place_ship(state, x, y);
				ret += boats_solver_place_ship(state, x, y+1);
				ret += boats_solver_place_water(state, x, y-1);
				break;
			case SHIP_BOTTOM:
				ret += boats_solver_place_ship(state, x, y);
				ret += boats_solver_place_ship(state, x, y-1);
				ret += boats_solver_place_water(state, x, y+1);
				break;
			case SHIP_LEFT:
				ret += boats_solver_place_ship(state, x, y);
				ret += boats_solver_place_ship(state, x+1, y);
				ret += boats_solver_place_water(state, x-1, y);
				break;
			case SHIP_RIGHT:
				ret += boats_solver_place_ship(state, x, y);
				ret += boats_solver_place_ship(state, x-1, y);
				ret += boats_solver_place_water(state, x+1, y);
				break;
			case SHIP_SINGLE:
				ret += boats_solver_place_ship(state, x, y);
				ret += boats_solver_place_water(state, x+1, y);
				ret += boats_solver_place_water(state, x-1, y);
				ret += boats_solver_place_water(state, x, y+1);
				ret += boats_solver_place_water(state, x, y-1);
				break;
		}
	}
	
	return ret;
}

static int boats_solver_fill_row(game_state *state, int sx, int sy, int ex, int ey, char fill)
{
	/*
	 * Fill a row or column with ships or water.
	 */
	
	int ret = 0;
	int w = state->w;
	int x, y;
	
	for(x = sx; x <= ex; x++)
	for(y = sy; y <= ey; y++)
	{
		if(state->grid[y*w+x] == EMPTY)
		{
			if(IS_SHIP(fill))
			{
				ret += boats_solver_place_ship(state, x, y);
			}
			else if(fill == WATER)
			{
				ret += boats_solver_place_water(state, x, y);
			}
		}
	}
	
	return ret;
}

static int boats_solver_check_fill(game_state *state, int *blankcounts)
{
	/* If all water is placed, fill the rest of the grid with ships */
	int i;
	int w = state->w;
	int h = state->h;
	int ret = 0;
	int count = 0;
	
	for(i = 0; i < state->fleet; i++)
		count += state->fleetdata[i] * (i+1);
	
	for(i = 0; i < w; i++)
	{
		count += blankcounts[i];
	}
	
	if(count == w*h)
	{
		solver_printf("Fill the rest of the grid with ships\n");
		
		for(i = 0; i < w*h; i++)
		{
			if(state->grid[i] == EMPTY)
				ret += boats_solver_place_ship(state, i%w, i/w);
		}
	}
	
	return ret;
}

static int boats_solver_check_counts(game_state *state, int *blankcounts, int *shipcounts)
{
	/*
	 * Check if a row/column has enough ships or blank spaces, and fill
	 * the rest of the row/column accordingly.
	 */
	
	int ret = 0;
	int w = state->w;
	int h = state->h;
	int i;
	
	/* Check columns */
	for(i = 0; i < w; i++)
	{
		if(state->borderclues[i] == NO_CLUE)
			continue;
		
		if(shipcounts[i] == state->borderclues[i] && blankcounts[i] != (h - state->borderclues[i]))
		{
			solver_printf("Complete column %i with water\n", i);
			ret += boats_solver_fill_row(state, i, 0, i, h-1, WATER);
		}
		else if(shipcounts[i] != state->borderclues[i] && blankcounts[i] == (h - state->borderclues[i]))
		{
			solver_printf("Complete column %i with ships\n", i);
			ret += boats_solver_fill_row(state, i, 0, i, h-1, SHIP_VAGUE);
		}
	}
	/* Check rows */
	for(i = 0; i < h; i++)
	{
		if(state->borderclues[i+w] == NO_CLUE)
			continue;
		
		if(shipcounts[i+w] == state->borderclues[i+w] && blankcounts[i+w] != (w - state->borderclues[i+w]))
		{
			solver_printf("Complete row %i with water\n", i);
			ret += boats_solver_fill_row(state, 0, i, w-1, i, WATER);
		}
		else if(shipcounts[i+w] != state->borderclues[i+w] && blankcounts[i+w] == (w - state->borderclues[i+w]))
		{
			solver_printf("Complete row %i with ships\n", i);
			ret += boats_solver_fill_row(state, 0, i, w-1, i, SHIP_VAGUE);
		}
	}
	
	return ret;
}

static int boats_solver_remove_singles(game_state *state, int *fleetcount)
{
	/* 
	 * If all boats of size 1 are placed, fill every square surrounded
	 * by water with more water. Single ships adjacent to one empty space
	 * must extend in that direction.
	 */
	int ret = 0;
	int x, y;
	int w = state->w;
	int h = state->h;
	char sleft, sright, sup, sdown;
	
	/* Sanity check */
	if(fleetcount[0] != state->fleetdata[0])
		return 0;
		
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if(state->grid[y*w+x] == WATER)
			continue;
		
		sleft = x == 0 ? WATER 
			: state->grid[y*w+(x-1)];
		sright = x == w-1 ? WATER 
			: state->grid[y*w+(x+1)];
		sup = y == 0 ? WATER 
			: state->grid[(y-1)*w+x];
		sdown = y == h-1 ? WATER 
			: state->grid[(y+1)*w+x];
		
		if( sleft == WATER && sright == WATER &&
			sup == WATER && sdown == WATER &&
			state->grid[y*w+x] == EMPTY)
		{
			solver_printf("Single square at %i,%i cannot contain boat\n", x, y);
			ret += boats_solver_place_water(state, x, y);
		}
		
		if(state->grid[y*w+x] != SHIP_VAGUE)
			continue;
		
		if(sleft == WATER && sright == WATER && sup == WATER && sdown == EMPTY)
		{
			solver_printf("Single ship at %i,%i must extend downward\n", x, y);
			ret += boats_solver_place_ship(state, x, y+1);
		}
		
		else if(sleft == WATER && sright == WATER && sdown == WATER && sup == EMPTY)
		{
			solver_printf("Single ship at %i,%i must extend upward\n", x, y);
			ret += boats_solver_place_ship(state, x, y-1);
		}
		
		else if(sdown == WATER && sright == WATER && sup == WATER && sleft == EMPTY)
		{
			solver_printf("Single ship at %i,%i must extend to the left\n", x, y);
			ret += boats_solver_place_ship(state, x-1, y);
		}
		
		else if(sdown == WATER && sleft == WATER && sup == WATER && sright == EMPTY)
		{
			solver_printf("Single ship at %i,%i must extend to the right\n", x, y);
			ret += boats_solver_place_ship(state, x+1, y);
		}
	}
	
	return ret;
}

static int boats_solver_centers_trivial(game_state *state, bool *hascenters)
{
	/*
	 * Check for each center clue if one of the horizontally/vertically
	 * adjacent squares force the boat in a certain direction.
	 * Also unsets a flag if all center clues are satisfied or no center clues
	 * are found.
	 */
	
	int ret = 0;
	int w = state->w;
	int h = state->h;
	int x, y;
	
	char sleft, sright, sup, sdown;
	bool hc = false;
	
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if(state->gridclues[y*w+x] != SHIP_CENTER)
			continue;
		
		sleft = x == 0 ? WATER : state->grid[y*w+(x-1)];
		sright = x == w-1 ? WATER : state->grid[y*w+(x+1)];
		sup = y == 0 ? WATER : state->grid[(y-1)*w+x];
		sdown = y == h-1 ? WATER : state->grid[(y+1)*w+x];
		
		/* Check if clue is already satisfied */
		if((IS_SHIP(sleft) && IS_SHIP(sright)) || (IS_SHIP(sup) && IS_SHIP(sdown)))
			continue;
		
		hc = true;
		
		if(sleft == WATER || sright == WATER)
		{
			solver_printf("Center clue at %i,%i confirmed vertical\n", x, y);
			ret += boats_solver_place_ship(state, x, y-1);
			ret += boats_solver_place_ship(state, x, y+1);
		}
		else if(sup == WATER || sdown == WATER)
		{
			solver_printf("Center clue at %i,%i confirmed horizontal\n", x, y);
			ret += boats_solver_place_ship(state, x-1, y);
			ret += boats_solver_place_ship(state, x+1, y);
		}
	}
	
	*hascenters = hc;
	return ret;
}

static int boats_solver_centers_normal(game_state *state, int *shipcounts)
{
	/*
	 * Check if a center clue would exceed a border clue if it was set to
	 * a certain direction.
	 */
	
	int ret = 0;
	int w = state->w;
	int h = state->h;
	int x, y;
	
	char sleft, sright, sup, sdown;
	
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if(state->gridclues[y*w+x] != SHIP_CENTER)
			continue;
		
		sleft = x == 0 ? WATER : state->grid[y*w+(x-1)];
		sright = x == w-1 ? WATER : state->grid[y*w+(x+1)];
		sup = y == 0 ? WATER : state->grid[(y-1)*w+x];
		sdown = y == h-1 ? WATER : state->grid[(y+1)*w+x];
		
		/* Check if clue is already satisfied */
		if((IS_SHIP(sleft) && IS_SHIP(sright)) || (IS_SHIP(sup) && IS_SHIP(sdown)))
			continue;
		
		if(state->borderclues[y+w] != NO_CLUE)
		{
			/* The row must support at least 2 more ships */
			if(state->borderclues[y+w] - shipcounts[y+w] < 2)
			{
				solver_printf("Center clue %d,%d: Horizontal ship will violate border clue\n", x, y);
				ret += boats_solver_place_water(state, x+1, y);
			}
		}
		if(state->borderclues[x] != NO_CLUE)
		{
			/* The column must support at least 2 more ships */
			if(state->borderclues[x] - shipcounts[x] < 2)
			{
				solver_printf("Center clue %d,%d: Vertical ship will violate border clue\n", x, y);
				ret += boats_solver_place_water(state, x, y+1);
			}
		}
	}
	
	return ret;
}

static int boats_solver_min_expand_dsf_forward(game_state *state, int *fleetcount, DSF *dsf,
		int sx, int sy, int d, int ship)
{
	/*
	 * See if a boat must expand to the right or down. There must be an edge
	 * located at the first square occupied by this boat (the canonical index).
	 *
	 * Because the dsf must be reconstructed when a new ship is added,
	 * this function can perform at most one action per call.
	 */
	
	int w = state->w;
	int h = state->h;
	int end = dsf_canonify(dsf, w*h);
	int x, y, i1, i2, s;
	for(y = sy; y < h; y++)
	for(x = sx; x < w; x++)
	{
		i1 = y*w+x;
		i2 = i1 - d;
		if(state->grid[i1] != EMPTY || dsf_canonify(dsf, i2) == end)
			continue;
		if(state->grid[dsf_canonify(dsf, i2)] != ship)
			continue;
		
		s = dsf_size(dsf, i2) - 1;
		if(s < 1 || s >= state->fleet || state->fleetdata[s] != fleetcount[s])
			continue;
		
		solver_printf("Boat of size %d must expand to %d,%d\n", s+1, x, y);
		return boats_solver_place_ship(state, x, y);
	}
	
	return 0;
}

static int boats_solver_min_expand_dsf_back(game_state *state, int *fleetcount, DSF *dsf,
		int d, int ship)
{
	/*
	 * See if a boat must expand to the left or up. If an edge pointing right
	 * or down is found, this boat can only expand to the opposite direction.
	 * The cell to expand to is calculated using the canonical index.
	 */
	
	int w = state->w;
	int h = state->h;
	int end = dsf_canonify(dsf, w*h);
	int x, y, i1, c1, i2, s;
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		i1 = y*w+x;
		if(state->grid[i1] != ship)
			continue;
		c1 = dsf_canonify(dsf, i1);
		if(c1 == end)
			continue;
		
		s = dsf_size(dsf, i1) - 1;
		if(s < 1 || s >= state->fleet || state->fleetdata[s] != fleetcount[s])
			continue;
		
		i2 = c1 - d;
		
		solver_printf("Boat of size %d must expand to %d,%d\n", s+1, i2%w, i2/w);
		return boats_solver_place_ship(state, i2%w, i2/w);
	}
	
	return 0;
}

static int boats_solver_min_expand_dsf(game_state *state, int *fleetcount, DSF *dsf)
{
	/*
	 * See if an unfinished boat needs to expand in the last possible direction.
	 * This function does not handle unfinished boats of size 1, this is done
	 * in the function boats_solver_remove_singles.
	 *
	 * Because the dsf must be reconstructed when a new ship is added,
	 * this function can perform at most one action per call.
	 */
	
	int w = state->w;
	
	/* Expand down */
	if(boats_solver_min_expand_dsf_forward(state, fleetcount, dsf, 0, 1, w, SHIP_TOP))
		return 1;
	/* Expand right */
	if(boats_solver_min_expand_dsf_forward(state, fleetcount, dsf, 1, 0, 1, SHIP_LEFT))
		return 1;
	
	/* Expand up */
	if(boats_solver_min_expand_dsf_back(state, fleetcount, dsf, w, SHIP_BOTTOM))
		return 1;
	/* Expand left */
	if(boats_solver_min_expand_dsf_back(state, fleetcount, dsf, 1, SHIP_RIGHT))
		return 1;
	
	return 0;
}

static int boats_solver_max_expand_dsf(game_state *state, int *fleetcount, DSF *dsf)
{
	/* 
	 * See if an unfinished boat becomes too large when expanding into
	 * a certain direction, merging with other boats when necessary.
	 */
	
	int i, x, y;
	int w = state->w;
	int h = state->h;
	int ret = 0;
	int max = -1;
	int count;
	
	/* Determine ship size to find */
	for(i = 0; i < state->fleet; i++)
	{
		if((state->fleetdata[i] - fleetcount[i]) != 0)
			max = i;
	}
	
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		if(state->grid[y*w+x] != EMPTY)
			continue;
		
		count = 1;
		if(x > 0 && state->grid[y*w+x-1] == SHIP_VAGUE)
			count += dsf_size(dsf, y*w+x-1);
		if(x < w-1 && state->grid[y*w+x+1] == SHIP_VAGUE)
			count += dsf_size(dsf, y*w+x+1);
		if(y > 0 && state->grid[(y-1)*w+x] == SHIP_VAGUE)
			count += dsf_size(dsf, (y-1)*w+x);
		if(y < h-1 && state->grid[(y+1)*w+x] == SHIP_VAGUE)
			count += dsf_size(dsf, (y+1)*w+x);
		
		if(count > max+1)
		{
			solver_printf("Ship at %d,%d will result in boat of size %d\n", x, y, count);
			ret += boats_solver_place_water(state, x, y);
		}
	}
	
	return ret;
}

static int boats_solver_find_max_fleet(game_state *state, int *shipcounts, 
	int *fleetcount, struct boats_run *runs, int runcount, bool simple)
{	
	/*
	 * Check if the largest boat/boats need to be placed in a certain run.
	 * If 'simple' is false, the boats can also be placed partially.
	 * If 'simple' is true, this function will return 0 if there is more
	 * than one boat to be placed.
	 */
	
	int ret = 0;
	int i, j;
	int w = state->w;
	int max = -1;
	int bc = -1;
	int r = 0;
	int *idx;
	int start, end;
	
	/* Determine ship size to find */
	for(i = 0; i < state->fleet; i++)
	{
		if((state->fleetdata[i] - fleetcount[i]) != 0)
			max = i;
	}
	
	if(max == -1)
		return 0;
	
	/* Determine the exact amount of candidates required */
	bc = state->fleetdata[max] - fleetcount[max];
	
	if(simple && bc > 1)
		return 0;
	
	idx = snewn(bc, int);
	
	/* Count amount of possible runs to use */
	for(i = 0; i < runcount; i++)
	{
		/* Ignore filled runs */
		if(runs[i].ships == runs[i].len)
			continue;
		
		/* Ignore runs that are too small */
		if(runs[i].len < (max+1))
			continue;
		
		j = runs[i].row + (runs[i].horizontal ? w : 0);
		
		/* Ignore runs that could not fit due to the number clue */
		if(state->borderclues[j] != NO_CLUE && 
			state->borderclues[j] - (shipcounts[j] - runs[i].ships) < (max+1))
			continue;

		/* If there is a run that can fit at least two boats, abort */
		if(runs[i].len >= ((max+1)*2)+1)
			bc = -1;
		
		/* We found a potential position */
		if(r < bc)
			idx[r] = i;
		r++;
	}
	
	/*
	 * If the amount of candidates is the same as the required amount,
	 * check if there are any squares to confirm.
	 */
	if(r == bc)
	{
		for(i = 0; i < r; i++)
		{
			struct boats_run *run = &runs[idx[i]];
			solver_printf("Possible position for ship: row=%i start=%i len=%i ships=%i %s\n",
				run->row, run->start, run->len, run->ships,
				run->horizontal ? "Horizontal" : "Vertical");
						
			/* On lower difficulties, only include runs that fit a boat exactly */
			if(simple && run->len > (max+1))
				continue;
			
			start = run->start + run->len - (max+1);
			end = run->start + (max + 1);
			
			/* Confirm single cell in certain direction */
			if(end - start == 1)
			{
				solver_printf("Required position for ship: row=%i start=%i end=%i Single cell %s\n",
					run->row, start, end,
					run->horizontal ? "Horizontal" : "Vertical"); 
				
				if(run->horizontal)
				{
					ret += boats_solver_place_ship(state, start, run->row);
					ret += boats_solver_place_water(state, start, (run->row)-1);
					ret += boats_solver_place_water(state, start, (run->row)+1);
				}
				else
				{
					ret += boats_solver_place_ship(state, run->row, start);
					ret += boats_solver_place_water(state, (run->row)-1, start);
					ret += boats_solver_place_water(state, (run->row)+1, start);
				}
			}
			else if(end - start > 1)
			{
				solver_printf("Required position for ship: row=%i start=%i end=%i Multiple cells %s\n",
					run->row, start, end,
					run->horizontal ? "Horizontal" : "Vertical"); 
				
				if(run->horizontal)
				{
					ret += boats_solver_fill_row(state, start, run->row, end-1, run->row, SHIP_VAGUE);
				}
				else
				{
					ret += boats_solver_fill_row(state, run->row, start, run->row, end-1, SHIP_VAGUE);
				}
			}
		}
	}
	
	sfree(idx);
	
	return ret;
}

static int boats_solver_split_runs(game_state *state, int *fleetcount, 
		struct boats_run *runs, int runcount)
{
	/*
	 * Look for runs which contain one blank space, and see if filling it would
	 * result in too many boats. If so, fill the last space with water.
	 */
	
	int ret = 0;
	struct boats_run *run;
	int i, len;
	
	for(i = 0; i < runcount; i++)
	{
		run = &runs[i];
		len = run->len;
		if(len < 2 || len > state->fleet)
			continue;
		if(len - run->ships != 1)
			continue;
		
		if(state->fleetdata[len-1] == fleetcount[len-1])
		{
			solver_printf("Run of size %d at %s %d must not be filled\n",
				len, run->horizontal ? "row" : "column", run->row); 
			
			if(run->horizontal)
			{
				ret += boats_solver_fill_row(state, run->start, run->row, run->start+len-1, run->row, WATER);
			}
			else
			{
				ret += boats_solver_fill_row(state, run->row, run->start, run->row, run->start+len-1, WATER);
			}
		}
	}
	
	return ret; 
}

static int boats_solver_shared_diagonals(game_state *state, int *watercounts, int *shipcounts)
{
	/*
	 * In each row and column, find two empty spaces with are separated by
	 * another square. If the row/column needs one more blank square, then
	 * either one or both of these squares must be a ship. They share two
	 * diagonally adjacent squares, which can be filled with water.
	 */
	
	int ret = 0;
	int w = state->w;
	int h = state->h;
	int x, y;
	int target;
	char front, center, back;
	
	/* Rows */
	for(y = 0; y < h; y++)
	{
		if(state->borderclues[y+w] == NO_CLUE) continue;
		
		target = w - (state->borderclues[y+w] + watercounts[y+w]);
		if(target != 1 && target != 2)
			continue;
		
		for(x = 0; x < w; x++)
		{
			front = x > 0 && state->grid[y*w+(x-1)] == EMPTY ? 1 : 0;
			center = state->grid[y*w+x] == EMPTY && 
				state->borderclues[x] - shipcounts[x] == 1 ? 1 : 0;
			back = x < w-1 && state->grid[y*w+(x+1)] == EMPTY ? 1 : 0;
			
			if(front + center + back > target)
			{
				ret += boats_solver_place_water(state, x, y-1);
				ret += boats_solver_place_water(state, x, y+1);
			}
		}
	}
	
	/* Columns */
	for(x = 0; x < w; x++)
	{
		if(state->borderclues[x] == NO_CLUE) continue;
		
		target = h - (state->borderclues[x] + watercounts[x]);
		if(target != 1 && target != 2)
			continue;
		
		for(y = 0; y < h; y++)
		{
			front = y > 0 && state->grid[(y-1)*w+x] == EMPTY ? 1 : 0;
			center = state->grid[y*w+x] == EMPTY && 
				state->borderclues[y+w] - shipcounts[y+w] == 1 ? 1 : 0;
			back = y < h-1 && state->grid[(y+1)*w+x] == EMPTY ? 1 : 0;
			
			if(front + center + back > target)
			{
				ret += boats_solver_place_water(state, x-1, y);
				ret += boats_solver_place_water(state, x+1, y);
			}
		}
	}
	
#ifdef STANDALONE_SOLVER
	if (solver_verbose && ret) {
		printf("%i shared diagonal%s filled with water\n", ret, ret != 1 ? "s" : ""); 
	}
#endif
	
	return ret;
}

static int boats_solver_attempt_ship_rows(game_state *state, char *tmpgrid, int *watercounts)
{
	/*
	 * Look for a row/column which needs one more blank space, then
	 * attempt each position to see if it _immediately_ leads to an
	 * invalid state.
	 */
	
	int w = state->w;
	int h = state->h;
	int ret = 0;
	int x = 0, y = 0;
	
#ifdef STANDALONE_SOLVER
	bool temp_verbose = solver_verbose;
	solver_verbose = false;
#endif
	
	memcpy(tmpgrid, state->grid, w*h*sizeof(char));
	
	/* Rows */
	for(y = 0; y < h; y++)
	{
		if(state->borderclues[y+w] != NO_CLUE && (w - (state->borderclues[y+w] + watercounts[y+w])) == 1)
		{
			for(x = 0; x < w; x++)
			{
				if(state->grid[y*w+x] == EMPTY)
				{
					boats_solver_place_water(state, x, y);
					boats_solver_fill_row(state, 0, y, w-1, y, SHIP_VAGUE);
					/* Also fill the column if this square is at an intersection */
					if(state->borderclues[x] != NO_CLUE && (h - (state->borderclues[x] + watercounts[x])) == 1)
						boats_solver_fill_row(state, x, 0, x, h-1, SHIP_VAGUE);
					
					if(boats_validate_state(state) == STATUS_INVALID)
					{
#ifdef STANDALONE_SOLVER
						if (temp_verbose) {
							printf("Row %i: Water at %i,%i leads to violation\n", y, x, y);
						}
#endif
						memcpy(state->grid, tmpgrid, w*h*sizeof(char));
						ret += boats_solver_place_ship(state, x, y);
						memcpy(tmpgrid, state->grid, w*h*sizeof(char));
					}
					else
					{
						memcpy(state->grid, tmpgrid, w*h*sizeof(char));
					}
				}
			}
		}
	}
	
	/* Columns */
	for(x = 0; x < w; x++)
	{
		if(state->borderclues[x] != NO_CLUE && (h - (state->borderclues[x] + watercounts[x])) == 1)
		{
			for(y = 0; y < h; y++)
			{
				/* Already tried this in the previous loop */
				if(state->borderclues[y+w] != NO_CLUE && (w - (state->borderclues[y+w] + watercounts[y+w])) == 1)
					continue;
				
				if(state->grid[y*w+x] == EMPTY)
				{
					boats_solver_place_water(state, x, y);
					boats_solver_fill_row(state, x, 0, x, h-1, SHIP_VAGUE);
					
					if(boats_validate_state(state) == STATUS_INVALID)
					{
#ifdef STANDALONE_SOLVER
						if (temp_verbose) {
							printf("Column %i: Water at %i,%i leads to violation\n", x, x, y);
						}
#endif
						memcpy(state->grid, tmpgrid, w*h*sizeof(char));
						ret += boats_solver_place_ship(state, x, y);
						memcpy(tmpgrid, state->grid, w*h*sizeof(char));
					}
					else
					{
						memcpy(state->grid, tmpgrid, w*h*sizeof(char));
					}
				}
			}
		}
	}
	
#ifdef STANDALONE_SOLVER
	solver_verbose = temp_verbose;
#endif

	memcpy(state->grid, tmpgrid, w*h*sizeof(char));
	return ret;
}

static int boats_solver_attempt_water_rows(game_state *state, char *tmpgrid, int *shipcounts)
{
	/*
	 * Look for a row/column which needs one more ship, then
	 * attempt each position to see if it immediately leads to an
	 * invalid state.
	 */
	
	int w = state->w;
	int h = state->h;
	int ret = 0;
	int x = 0, y = 0;
	
#ifdef STANDALONE_SOLVER
	bool temp_verbose = solver_verbose;
	solver_verbose = false;
#endif
	
	memcpy(tmpgrid, state->grid, w*h*sizeof(char));
	
	/* Rows */
	for(y = 0; y < h; y++)
	{
		if(state->borderclues[y+w] != NO_CLUE && state->borderclues[y+w] - shipcounts[y+w] == 1)
		{
			for(x = 0; x < w; x++)
			{
				if(state->grid[y*w+x] == EMPTY)
				{
					boats_solver_place_ship(state, x, y);
					boats_solver_fill_row(state, 0, y, w-1, y, WATER);
					if(state->borderclues[x] != NO_CLUE && state->borderclues[x] - shipcounts[x] == 1)
						boats_solver_fill_row(state, x, 0, x, h-1, WATER);
					
					if(boats_validate_state(state) == STATUS_INVALID)
					{
#ifdef STANDALONE_SOLVER
						if (temp_verbose) {
							printf("Row %i: Ship at %i,%i leads to violation\n", y, x, y);
						}
#endif
						memcpy(state->grid, tmpgrid, w*h*sizeof(char));
						ret += boats_solver_place_water(state, x, y);
						memcpy(tmpgrid, state->grid, w*h*sizeof(char));
					}
					else
					{
						memcpy(state->grid, tmpgrid, w*h*sizeof(char));
					}
				}
			}
		}
	}
	
	/* Columns */
	for(x = 0; x < w; x++)
	{
		if(state->borderclues[x] != NO_CLUE && state->borderclues[x] - shipcounts[x] == 1)
		{
			for(y = 0; y < h; y++)
			{
				if(state->borderclues[y+w] != NO_CLUE && state->borderclues[y+w] - shipcounts[y+w] == 1)
					continue;
				
				if(state->grid[y*w+x] == EMPTY)
				{
					boats_solver_place_ship(state, x, y);
					boats_solver_fill_row(state, x, 0, x, h-1, WATER);
					
					if(boats_validate_state(state) == STATUS_INVALID)
					{
#ifdef STANDALONE_SOLVER
						if (temp_verbose) {
							printf("Column %i: Ship at %i,%i leads to violation\n", x, x, y);
						}
#endif
						memcpy(state->grid, tmpgrid, w*h*sizeof(char));
						ret += boats_solver_place_water(state, x, y);
						memcpy(tmpgrid, state->grid, w*h*sizeof(char));
					}
					else
					{
						memcpy(state->grid, tmpgrid, w*h*sizeof(char));
					}
				}
			}
		}
	}
	
#ifdef STANDALONE_SOLVER
	solver_verbose = temp_verbose;
#endif

	memcpy(state->grid, tmpgrid, w*h*sizeof(char));
	return ret;
}

static int boats_solver_centers_attempt(game_state *state, char *tmpgrid)
{
	/*
	 * Attempt each possible direction on a center clue, and check if it
	 * immediately leads to an invalid state.
	 */
	
	int ret = 0;
	int w = state->w;
	int h = state->h;
	int x, y;
	
	char sleft, sright, sup, sdown;

#ifdef STANDALONE_SOLVER
	bool temp_verbose = solver_verbose;
	solver_verbose = false;
#endif
	
	memcpy(tmpgrid, state->grid, w*h*sizeof(char));
	
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if(state->gridclues[y*w+x] != SHIP_CENTER)
			continue;
		
		sleft = x == 0 ? WATER : state->grid[y*w+(x-1)];
		sright = x == w-1 ? WATER : state->grid[y*w+(x+1)];
		sup = y == 0 ? WATER : state->grid[(y-1)*w+x];
		sdown = y == h-1 ? WATER : state->grid[(y+1)*w+x];
		
		if((IS_SHIP(sleft) && IS_SHIP(sright)) || (IS_SHIP(sup) && IS_SHIP(sdown)))
			continue;
		
		boats_solver_place_ship(state, x-1, y);
		boats_solver_place_ship(state, x+1, y);
		
		if(boats_validate_state(state) == STATUS_INVALID)
		{
#ifdef STANDALONE_SOLVER
			if (temp_verbose) {
				printf("Horizontal ship at %i,%i leads to violation\n", x, y);
			}
#endif
			memcpy(state->grid, tmpgrid, w*h*sizeof(char));
			ret += boats_solver_place_water(state, x+1, y);
			memcpy(tmpgrid, state->grid, w*h*sizeof(char));
			
			continue;
		}
		else
		{
			memcpy(state->grid, tmpgrid, w*h*sizeof(char));
		}
		
		boats_solver_place_ship(state, x, y-1);
		boats_solver_place_ship(state, x, y+1);
		
		if(boats_validate_state(state) == STATUS_INVALID)
		{
#ifdef STANDALONE_SOLVER
			if (temp_verbose) {
				printf("Vertical ship at %i,%i leads to violation\n", x, y);
			}
#endif
			memcpy(state->grid, tmpgrid, w*h*sizeof(char));
			ret += boats_solver_place_water(state, x, y+1);
			memcpy(tmpgrid, state->grid, w*h*sizeof(char));
			
			continue;
		}
		else
		{
			memcpy(state->grid, tmpgrid, w*h*sizeof(char));
		}
	}
	
#ifdef STANDALONE_SOLVER
	solver_verbose = temp_verbose;
#endif
	
	return ret;
}

static bool boats_solver_borderclues_fill(game_state *state, int *blankcounts, int *shipcounts)
{
	/*
	 * When a row or column is filled completely, add the corresponding clue.
	 */
	int w = state->w;
	int h = state->h;
	int i;
	bool found = false;
	
	for(i = 0; i < w; i++)
	{
		if(state->borderclues[i] != NO_CLUE)
			continue;
		
		found = true;
		if(shipcounts[i] + blankcounts[i] == h)
		{
			solver_printf("Column %d is finished, add clue %d\n", i, shipcounts[i]);
			state->borderclues[i] = shipcounts[i];
		}
	}
	
	for(i = 0; i < h; i++)
	{
		if(state->borderclues[i+w] != NO_CLUE)
			continue;
		
		found = true;
		if(shipcounts[i+w] + blankcounts[i+w] == w)
		{
			solver_printf("Row %d is finished, add clue %d\n", i, shipcounts[i+w]);
			state->borderclues[i+w] = shipcounts[i+w];
		}
	}
	
	return found;
}

static int boats_solver_borderclues_last(game_state *state)
{
	/*
	 * If all but one border clues are placed, calculate the last one.
	 */
	int maxships = 0;
	int ret = 0;
	int w = state->w;
	int h = state->h;
	int i, found, shipcount;
	
	for(i = 0; i < state->fleet; i++)
		maxships += state->fleetdata[i] * (i+1);
	
	/* Column clues */
	found = -1, shipcount = 0;
	for(i = 0; i < w; i++)
	{
		if(state->borderclues[i] != NO_CLUE)
		{
			shipcount += state->borderclues[i];
			continue;
		}
		if(found == -1)
			found = i;
		else
			found = -2;
	}
	
	if(found >= 0)
	{
		solver_printf("Last Column clue is at %d, add clue %d\n", found, maxships - shipcount);
		state->borderclues[found] = maxships - shipcount;
		ret++;
	}
	
	found = -1, shipcount = 0;
	for(i = 0; i < h; i++)
	{
		if(state->borderclues[i+w] != NO_CLUE)
		{
			shipcount += state->borderclues[i+w];
			continue;
		}
		if(found == -1)
			found = i;
		else
			found = -2;
	}
	
	if(found >= 0)
	{
		solver_printf("Last Row clue is at %d, add clue %d\n", found, maxships - shipcount);
		state->borderclues[found+w] = maxships - shipcount;
		ret++;
	}
	
	return ret;
}

static int boats_solve_game(game_state *state, int maxdiff)
{
	int w = state->w;
	int h = state->h;
	int i;
	
#ifdef STANDALONE_SOLVER
	char *debug;
#endif
	
	struct boats_run *runs = NULL;
	char *tmpgrid = NULL;
	DSF *dsf = NULL;
	int runcount = 0;
	int *borderclues = NULL;
	int diff = DIFF_EASY;
	
	/*
	 * This flag is for solver optimization. If all CENTER clues are satisfied,
	 * don't try techniques which only relate to CENTER clues.
	 */
	bool hascenters = true;
	/*
	 * This optimization flag is for finding missing border numbers.
	 */
	bool hasnoclue = false;
	int status;
	
	int *blankcounts = snewn(w+h, int);
	int *shipcounts = snewn(w+h, int);
	int *fleetcount = snewn(state->fleet, int);
	
	if(maxdiff >= DIFF_NORMAL)
	{
		runs = snewn(w*h*2, struct boats_run);
		
		dsf = dsf_new((w*h)+1);
	}
	
	for(i = 0; i < w+h && !hasnoclue; i++)
	{
		if(state->borderclues[i] == NO_CLUE)
			hasnoclue = true;
	}
	
	if(maxdiff >= DIFF_TRICKY && hasnoclue)
	{
		/*
		 * When a puzzle contains missing border clues, try to fill in the missing
		 * numbers. This is done in the state itself. When the solver finishes,
		 * the puzzle must be restored to its original state.
		 */
		borderclues = snewn(w+h, int);
		memcpy(borderclues, state->borderclues, (w+h)*sizeof(int));
	}
	if(maxdiff >= DIFF_HARD)
		tmpgrid = snewn(w*h, char);
	boats_solver_initial(state);
	
	while(true)
	{
		/* Validation */
		if(boats_validate_full_state(state, blankcounts, shipcounts, fleetcount, dsf) != STATUS_INCOMPLETE)
			break;
		
#ifdef STANDALONE_SOLVER
		if (solver_steps) {
			/* Show the intermediate status, and pause the solver */
			debug = game_text_format(state);
			fputs(debug, stdout);
			sfree(debug);
			printf("Press any key to continue...\n");
			fflush(stdout);
			getchar();
		}
#endif
		
		/* Easy techniques */
		if(hasnoclue && boats_solver_check_fill(state, blankcounts))
			continue;
		
		if(boats_solver_check_counts(state, blankcounts, shipcounts))
			continue;
		
		if(hascenters && boats_solver_centers_trivial(state, &hascenters))
			continue;
		
		if(fleetcount[0] == state->fleetdata[0] && 
			boats_solver_remove_singles(state, fleetcount))
			continue;
		
		/* Normal techniques */
		if(maxdiff < DIFF_NORMAL) break;
		diff = max(diff, DIFF_NORMAL);
		
		if(hascenters && boats_solver_centers_normal(state, shipcounts))
			continue;
		
		if(boats_solver_max_expand_dsf(state, fleetcount, dsf))
			continue;
		
		if(boats_solver_min_expand_dsf(state, fleetcount, dsf))
			continue;
		
		runcount = boats_collect_runs(state, runs);
		
		/* Don't run this function twice per cycle on DIFF_TRICKY and up */
		if(diff < DIFF_TRICKY &&
			boats_solver_find_max_fleet(state, shipcounts, fleetcount,
			runs, runcount, true))
			continue;
		
		if(boats_solver_split_runs(state, fleetcount, runs, runcount))
			continue;
		
		/* Tricky techniques */
		if(maxdiff < DIFF_TRICKY) break;
		diff = max(diff, DIFF_TRICKY);
		
		if(hasnoclue)
		{
			hasnoclue = boats_solver_borderclues_fill(state, blankcounts, shipcounts);
			
			if(boats_solver_borderclues_last(state))
				continue;
		}
		
		if(boats_solver_shared_diagonals(state, blankcounts, shipcounts))
			continue;
		
		if(boats_solver_find_max_fleet(state, shipcounts, fleetcount,
			runs, runcount, false))
			continue;
		
		/* Hard techniques */
		if(maxdiff < DIFF_HARD) break;
		diff = max(diff, DIFF_HARD);
		
		if(hascenters && boats_solver_centers_attempt(state, tmpgrid))
			continue;
			
		if(boats_solver_attempt_ship_rows(state, tmpgrid, blankcounts))
			continue;
		
		if(boats_solver_attempt_water_rows(state, tmpgrid, shipcounts))
			continue;
		
		break;
	}
	
	status = boats_validate_full_state(state, blankcounts, shipcounts, fleetcount, NULL);
	
	if(status == STATUS_INCOMPLETE)
		diff = -1;
	if(status == STATUS_INVALID)
		diff = -2;
	
	if(borderclues)
		memcpy(state->borderclues, borderclues, (w+h)*sizeof(int));
	
	sfree(blankcounts);
	sfree(shipcounts);
	sfree(fleetcount);
	sfree(runs);
	sfree(tmpgrid);
	sfree(borderclues);
	dsf_free(dsf);
	
	return diff;
}

static char *solve_game(const game_state *state, const game_state *currstate,
			const char *aux, const char **error)
{
	int w = state->w;
	int h = state->h;
	char *ret, *p;
	int diff;
	int i;
	
	game_state *solved = dup_game(state);
	
	diff = boats_solve_game(solved, DIFFCOUNT);
	
	if(diff == -2)
	{
		*error = "Puzzle is invalid.";
		return NULL;
	}
	else if(diff != -1)
	{
		for(i = 0; i < w*h; i++)
		{
			if(solved->grid[i] == EMPTY)
				solved->grid[i] = WATER;
		}
	}
	
	ret = snewn((w*h) + 2, char);
	p = ret;
	
	*p++ = 'S';
	
	for(i = 0; i < w*h; i++)
	{
		*p++ = IS_SHIP(solved->grid[i]) ? 'B' : solved->grid[i] == WATER ? 'W' : '-';
	}
	
	*p++ = '\0';
	
	free_game(solved);
	return ret;
}

/* ********* *
 * Generator *
 * ********* */
static bool boats_generate_fleet(game_state *state, random_state *rs, struct boats_run *runs, int *spaces)
{
	/*
	 * Attempt to place each boat in the grid. If no random_state is given,
	 * boats will always be placed in the first possible run in the first
	 * position.
	 */
	
	int w = state->w;
	int h = state->h;
	int fleet = state->fleet;
	int i, j, f, pos;
	int runcount;
	struct boats_run *run;
	
	for(f = fleet-1; f >= 0; f--)
	{
		for(j = 0; j < state->fleetdata[f]; j++)
		{
#ifdef STANDALONE_SOLVER
			if(solver_steps)
			{
				char *debug;
				debug = game_text_format(state);
				fputs(debug, stdout);
				sfree(debug);
				printf("Press any key to continue...\n");
				fflush(stdout);
				getchar();
			}
#endif
			runcount = boats_collect_runs(state, runs);
			for(i = 0; i < runcount; i++)
				spaces[i] = i;
			if(rs)
				shuffle(spaces, runcount, sizeof(*spaces), rs);
			
			for(i = 0; i < runcount; i++)
			{
				run = &runs[spaces[i]];
				
				/* Ignore filled runs */
				if(run->ships > 0)
					continue;
				
				/* Ignore runs that are too small */
				if(run->len < (f+1))
					continue;
				
				pos = run->start + (rs ? random_upto(rs, run->len - f) : 0);
				
				if(run->horizontal)
				{
					boats_solver_fill_row(state, pos, run->row, pos+f, run->row, SHIP_VAGUE);
					
					/* Put water on either side */
					boats_solver_place_water(state, pos-1, run->row);
					boats_solver_place_water(state, pos+f+1, run->row);
					
					/* If the ship is single, surround it with water */
					if(f == 0)
					{
						boats_solver_place_water(state, pos, run->row-1);
						boats_solver_place_water(state, pos, run->row+1);
					}
				}
				if(!run->horizontal)
				{
					boats_solver_fill_row(state, run->row, pos, run->row, pos+f, SHIP_VAGUE);
					boats_solver_place_water(state, run->row, pos-1);
					boats_solver_place_water(state, run->row, pos+f+1);
					if(f == 0)
					{
						boats_solver_place_water(state, run->row-1, pos);
						boats_solver_place_water(state, run->row+1, pos);
					}
				}
				
				boats_adjust_ships(state);
				break;
			}
			
			/* Failed to fit all ships */
			if(i == runcount)
				return false;
		}
	}
	
	for(i = 0; i < w*h; i++)
	{
		if(state->grid[i] == EMPTY)
			state->grid[i] = WATER;
	}
	
	return true;
}

static const char *validate_params(const game_params *params, bool full)
{
	int w = params->w;
	int h = params->h;
	int i;
	int fleet = params->fleet;
	
	int *spaces;
	struct boats_run *runs;
	game_state *state;
	char *ret = NULL;
	
	if(full && params->diff >= DIFFCOUNT)
		return "Unknown difficulty level";
	if(w > 99)
		return "Width is too high";
	if(h > 99)
		return "Height is too high";
	if(fleet < 1)
		return "Fleet size must be at least 1";
	if(fleet > w && fleet > h)
		return "Fleet size must be smaller than the width and height";
	if(fleet > 9)
		return "Fleet size must be no more than 9";
		
	for(i = 0; i < fleet; i++)
		if(params->fleetdata[i]) break;
	if(i == fleet)
		return "Fleet must contain at least 1 boat";
	
	if(w < 2)
		return "Width must be at least 2";
	if(h < 2)
		return "Height must be at least 2";
	
	/* Attempt to fit the fleet in the current parameters */
	state = blank_game(min(w,h), max(w,h), fleet, params->fleetdata);
	runs = snewn(w*h*2, struct boats_run);
	spaces = snewn(w*h*2, int);
	
	memset(state->grid, EMPTY, w*h*sizeof(char));

#ifdef STANDALONE_SOLVER
	bool temp_steps = solver_steps;
	bool temp_verbose = solver_verbose;
	solver_verbose = false;
	solver_steps = false;
#endif
	
	if(!boats_generate_fleet(state, NULL, runs, spaces))
		ret = "Fleet does not fit into the grid";

#ifdef STANDALONE_SOLVER
	solver_verbose = temp_verbose;
	solver_steps = temp_steps;
#endif
		
	free_game(state);
	sfree(runs);
	sfree(spaces);
	
	return ret;
}

static void boats_create_borderclues(game_state *state)
{
	int w = state->w;
	int h = state->h;
	int x, y;
	
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if(IS_SHIP(state->grid[y*w+x]))
		{
			state->borderclues[x]++;
			state->borderclues[y+w]++;
		}
	}
}
 
#define MAX_ATTEMPTS 1000
 
static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
	game_state *state;
	int diff = params->diff;
	bool strip = params->strip;
	char *ret = NULL, *p;
	char *grid;
	char tempg;
	int tempb;
	int w = params->w;
	int h = params->h;
	int i, j, run;
	int *spaces;
	int attempts = 0;
	struct boats_run *runs = NULL;
	
	state = blank_game(w, h, params->fleet, params->fleetdata);
	runs = snewn(w*h*2, struct boats_run);
	spaces = snewn(w*h*2, int);
	grid = snewn(w*h, char);
	
restart:	
	attempts++;
	if(attempts > MAX_ATTEMPTS)
	{
		attempts = 0;
		if(strip)
			strip = false;
		else
			diff--;
		assert(diff >= 0);
	}
	
	memset(state->gridclues, EMPTY, w*h*sizeof(char));
	memset(state->grid, EMPTY, w*h*sizeof(char));
	memset(state->borderclues, 0, (w+h)*sizeof(int));
	
	/* Fill grid */
	while(!boats_generate_fleet(state, rs, runs, spaces))
	{
		memset(state->grid, EMPTY, w*h*sizeof(char));
	}
	
	/* Create border clues */
	boats_create_borderclues(state);
	memcpy(grid, state->grid, w*h*sizeof(char));
	
	/*
	 * The grid clues are first determined by solving the grid on the lowest
	 * difficulty setting without grid clues, then adding a random clue when it
	 * gets stuck.
	 */
	for(i = 0; i < w*h; i++)
		spaces[i] = i;
	shuffle(spaces, w*h, sizeof(*spaces), rs);
	while(true)
	{
		if(boats_solve_game(state, DIFF_EASY) != -1)
			break;
		
		for(i = 0; i < w*h; i++)
		{
			j = spaces[i];
			if(state->grid[j] != EMPTY)
				continue;
			
			state->gridclues[j] = grid[j];
			break;
		}
	}
	
	/*
	 * Now attempt to remove each grid clue, and see if the puzzle is still
	 * solvable on the proper difficulty level.
	 */
	shuffle(spaces, w*h, sizeof(*spaces), rs);
	for(i = 0; i < w*h; i++)
	{
		j = spaces[i];
		if(state->gridclues[j] == EMPTY)
			continue;
		
		tempg = state->gridclues[j];
		state->gridclues[j] = EMPTY;
		
		if(boats_solve_game(state, diff) == -1)
		{
			state->gridclues[j] = tempg;
		}
	}
	
	/* Remove border clues */
	if(strip)
	{
		for(i = 0; i < w+h; i++)
			spaces[i] = i;
		shuffle(spaces, w+h, sizeof(*spaces), rs);
		
		for(i = 0; i < w+h; i++)
		{
			j = spaces[i];
			if(state->borderclues[j] == NO_CLUE)
				continue;
			
			tempb = state->borderclues[j];
			state->borderclues[j] = NO_CLUE;
			
			if(boats_solve_game(state, diff) == -1)
			{
				state->borderclues[j] = tempb;
			}
		}
		
		/* Don't generate puzzles with only one clue missing */
		boats_solver_borderclues_last(state);
		
		for(i = 0; i < w+h; i++)
		{
			if(state->borderclues[i] == NO_CLUE)
				break;
		}
		if(i == w+h) /* No empty clue found */
			goto restart;
	}
	
	/* 
	 * Ensure difficulty by making sure the puzzle is not solvable
	 * at a lower difficulty level. 
	 */
	if(boats_solve_game(state, diff) != diff)
		goto restart;
	
	/* Serialize border clues */
	ret = snewn(((w+h)*3)+(w*h)+1, char);
	p = ret;
	for(i = 0; i < w+h; i++)
	{
		if(state->borderclues[i] == NO_CLUE)
			p += sprintf(p, "-,");
		else
			p += sprintf(p, "%d,", state->borderclues[i]);
	}
	
	/* Serialize grid clues */
	run = 0;
	for(i = 0; i < w*h; i++)
	{
		if(state->gridclues[i] == EMPTY)
			run++;
		if(run && (run == 26 || state->gridclues[i] != EMPTY))
		{
			*p++ = 'a' + (run - 1);
			run = 0;
		}
		
		switch(state->gridclues[i])
		{
			case WATER:
				*p++ = 'W';
				break;
			case SHIP_TOP:
				*p++ = 'T';
				break;
			case SHIP_BOTTOM:
				*p++ = 'B';
				break;
			case SHIP_LEFT:
				*p++ = 'L';
				break;
			case SHIP_RIGHT:
				*p++ = 'R';
				break;
			case SHIP_VAGUE:
				*p++ = 'V';
				break;
			case SHIP_CENTER:
				*p++ = 'C';
				break;
			case SHIP_SINGLE:
				*p++ = 'S';
				break;
		}
	}
	
	*p++ = '\0';
	free_game(state);
	sfree(runs);
	sfree(spaces);
	sfree(grid);
	
	return ret;
}

/* ************** *
 * User interface *
 * ************** */
struct game_ui {
	int cx, cy;
	bool cursor;
	
	char drag_from, drag_to;
	bool drag_ok;
	int dsx, dex, dsy, dey;
};

static game_ui *new_ui(const game_state *state)
{
	game_ui *ret = snew(game_ui);
	
	ret->drag_from = 0;
	ret->drag_to = 0;
	ret->dsx = ret->dex = ret->dsy = ret->dey = -1;
	ret->cx = ret->cy = 0;
	ret->cursor = false;
	ret->drag_ok = false;
	
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

struct game_drawstate {
	int tilesize;
	int fleeth; /* Actual height of fleet data */
	int *border;
	int *fleetcount;
	int *gridfs;
	
	bool redraw;

	char oldflash;
	int *oldgridfs;
	int *oldfleetcount;
	int *oldborder;
	int *grid;
};

#define FROMCOORD(x) ( ((x)-(ds->tilesize/2)) / ds->tilesize ) 

static bool boats_validate_move(const game_state *state, int sx, int sy, int ex, int ey, char from, char to)
{
	/*
	 * Check if the given move will actually make a change to the grid.
	 */
	
	int x, y;
	int w = state->w;
	
	if(from == to)
		return false;
	
	for(x = sx; x <= ex; x++)
	for(y = sy; y <= ey; y++)
	{
		if(state->gridclues[y*w+x] != EMPTY)
			continue;
		
		if(from == 'B' && !IS_SHIP(state->grid[y*w+x]))
			continue;
		
		if(from == 'W' && state->grid[y*w+x] != WATER)
			continue;
		
		if(from == '-' && state->grid[y*w+x] != EMPTY)
			continue;
		
		if(to == 'B' && IS_SHIP(state->grid[y*w+x]))
			continue;
		
		if(to == 'W' && state->grid[y*w+x] == WATER)
			continue;
		
		if(to == '-' && state->grid[y*w+x] == EMPTY)
			continue;
		
		return true;
	}
	
	return false;
}

static char *interpret_move(const game_state *state, game_ui *ui, const game_drawstate *ds,
				int ox, int oy, int button)
{
	char buf[80];
	char from, to;
	int w = state->w;
	int h = state->h;
	int gx = FROMCOORD(ox);
	int gy = FROMCOORD(oy);
	
	/* 
	 * Since users will often want to fill an entire line, increase
	 * the click target around the edges.
	 */
	if(gx == w) gx = w-1;
	if(gy == h) gy = h-1;
	
	if(button == LEFT_BUTTON || button == MIDDLE_BUTTON || button == RIGHT_BUTTON)
	{
		if(gx >= 0 && gy >= 0 && gx < w && gy < h)
		{
			from = IS_SHIP(state->grid[gy*w+gx]) ? 'B' : 
				state->grid[gy*w+gx] == WATER ? 'W' : '-';
			to = '-';
			
			if(button == LEFT_BUTTON)
				to = from == 'B' ? 'W' : from == '-' ? 'B' : '-';
			if(button == LEFT_BUTTON && to == 'W')
				from = '*';
			if(button == RIGHT_BUTTON)
				to = from == '-' ? 'W' : '-';
			
			if(button == MIDDLE_BUTTON)
				from = '*';
			
			ui->drag_from = from;
			ui->drag_to = to;
			ui->drag_ok = true;
			ui->dsx = ui->dex = gx;
			ui->dsy = ui->dey = gy;
			ui->cursor = false;
			
			return MOVE_UI_UPDATE;
		}
	}
	
	if ((IS_MOUSE_DRAG(button) || IS_MOUSE_RELEASE(button)) &&
		ui->drag_to != 0)
	{
		if (gx < 0 || gy < 0 || gx >= w || gy >= h)
            ui->drag_ok = false;
		else
		{
			/*
             * Drags are limited to one row or column. Hence, we
             * work out which coordinate is closer to the drag
             * start, and move it _to_ the drag start.
             */
            if (abs(gx - ui->dsx) < abs(gy - ui->dsy))
                gx = ui->dsx;
            else
                gy = ui->dsy;

            ui->dex = gx;
            ui->dey = gy;

            ui->drag_ok = true;
		}
		
		if(IS_MOUSE_RELEASE(button) && ui->drag_ok)
		{
			int xmin, xmax, ymin, ymax;
			
			from = ui->drag_from;
			to = ui->drag_to;
			
			xmin = min(ui->dsx, ui->dex);
			xmax = max(ui->dsx, ui->dex);
			ymin = min(ui->dsy, ui->dey);
			ymax = max(ui->dsy, ui->dey);
			
			ui->drag_ok = false;
			
			if(boats_validate_move(state, xmin, ymin, xmax, ymax, from, to))
			{
				sprintf(buf, "P%d,%d,%d,%d,%c,%c", xmin, ymin, xmax, ymax, from, to);
					
				return dupstr(buf);
			}
		}
		return MOVE_UI_UPDATE;
	}
	
	if (IS_CURSOR_MOVE(button & ~MOD_MASK))
	{
		int cx = ui->cx, cy = ui->cy;
		move_cursor(button & ~MOD_MASK, &ui->cx, &ui->cy, w, h, 0, &ui->cursor);
		
		/* Place boats or water by holding Shift or Ctrl while moving */
		if(button & (MOD_CTRL|MOD_SHFT))
		{
			int xmin = min(cx, ui->cx);
			int xmax = max(cx, ui->cx);
			int ymin = min(cy, ui->cy);
			int ymax = max(cy, ui->cy);
			to = button & MOD_CTRL ? button & MOD_SHFT ? '-' : 'B' : 'W';
			from = to == '-' ? '*' : '-';
			
			if(boats_validate_move(state, xmin, ymin, xmax, ymax, from, to))
			{
				sprintf(buf, "P%d,%d,%d,%d,%c,%c", xmin, ymin, xmax, ymax, from, to);
				return dupstr(buf);
			}
		}
		
		return MOVE_UI_UPDATE;
	}
	
	if(ui->cursor && (button == CURSOR_SELECT ||
		button == CURSOR_SELECT2 || button == '\b'))
	{
		gx = ui->cx;
		gy = ui->cy;
		
		from = IS_SHIP(state->grid[gy*w+gx]) ? 'B' : 
			state->grid[gy*w+gx] == WATER ? 'W' : '-';
		to = '-';
		
		if(button == CURSOR_SELECT && from == '-')
			to = 'B';
		if(button == CURSOR_SELECT2 && from == '-')
			to = 'W';
			
		if(boats_validate_move(state, gx, gy, gx, gy, from, to))
		{
			sprintf(buf, "P%d,%d,%d,%d,%c,%c", gx, gy, gx, gy, from, to);
			return dupstr(buf);
		}
	}
	
	return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
	int sx, sy, ex, ey, x, y;
	int w = state->w;
	int h = state->h;
	char from, to;
	game_state *ret;
	
	if(move[0] == 'P' && sscanf(move+1, "%d,%d,%d,%d,%c,%c", &sx, &sy, &ex, &ey, &from, &to) == 6)
	{
		ret = dup_game(state);
		
		for(x = sx; x <= ex; x++)
		for(y = sy; y <= ey; y++)
		{
			if(state->gridclues[y*w+x] == EMPTY)
			{
				if(IS_SHIP(ret->grid[y*w+x]) && from != 'B' && from != '*')
					continue;
				if(ret->grid[y*w+x] == EMPTY && from != '-' && from != '*')
					continue;
				if(ret->grid[y*w+x] == WATER && from != 'W' && from != '*')
					continue;
				
				ret->grid[y*w+x] = (to == 'B' ? SHIP_VAGUE : 
					to == 'W' ? WATER : EMPTY);
			}
		}
		
		boats_adjust_ships(ret);
		if(boats_validate_state(ret) == STATUS_COMPLETE)
			ret->completed = true;
		
		return ret;
	}
	
	else if(move[0] == 'S')
	{
		const char *p;
		int i;
		ret = dup_game(state);
		
		p = move+1;
		
		for(i = 0; i < w*h; i++)
		{
			if (!*p || !(*p == 'W' || *p == 'B' || *p == '-')) {
				free_game(ret);
				return NULL;
			}

			ret->grid[i] = (*p == 'B' ? SHIP_VAGUE : *p == 'W' ? WATER : EMPTY);
			p++;
		}
		
		boats_adjust_ships(ret);
		
		if(boats_validate_state(ret) == STATUS_COMPLETE)
			ret->completed = true;
		
		/* 
		 * If the solve move did not actually finish the grid,
		 * do not set the cheated flag.
		 */
		ret->cheated = ret->completed;
		
		return ret;
	}
	return NULL;
}

/* **************** *
 * Drawing routines *
 * **************** */

static float *game_colours(frontend *fe, int *ncolours)
{
	float *ret = snewn(3 * NCOLOURS, float);

	frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);
	
	ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;
	
	ret[COL_WATER * 3 + 0] = 0.5F;
    ret[COL_WATER * 3 + 1] = 0.7F;
    ret[COL_WATER * 3 + 2] = 1.0F;
	
	ret[COL_CURSOR_A * 3 + 0] = 0.0F;
    ret[COL_CURSOR_A * 3 + 1] = 0.0F;
    ret[COL_CURSOR_A * 3 + 2] = 0.0F;
	
	ret[COL_CURSOR_B * 3 + 0] = 1.0F;
    ret[COL_CURSOR_B * 3 + 1] = 1.0F;
    ret[COL_CURSOR_B * 3 + 2] = 1.0F;
	
	ret[COL_SHIP_CLUE * 3 + 0] = 0.1F;
    ret[COL_SHIP_CLUE * 3 + 1] = 0.1F;
    ret[COL_SHIP_CLUE * 3 + 2] = 0.1F;
	
	ret[COL_SHIP_GUESS * 3 + 0] = 0.0F;
    ret[COL_SHIP_GUESS * 3 + 1] = 0.0F;
    ret[COL_SHIP_GUESS * 3 + 2] = 0.0F;
	
	ret[COL_SHIP_ERROR * 3 + 0] = 0.8F;
    ret[COL_SHIP_ERROR * 3 + 1] = 0.0F;
    ret[COL_SHIP_ERROR * 3 + 2] = 0.0F;
	
	ret[COL_SHIP_FLEET * 3 + 0] = 0.0F;
    ret[COL_SHIP_FLEET * 3 + 1] = 0.5F;
    ret[COL_SHIP_FLEET * 3 + 2] = 0.0F;
	
	ret[COL_SHIP_FLEET_DONE * 3 + 0] = 0.7F;
    ret[COL_SHIP_FLEET_DONE * 3 + 1] = 0.7F;
    ret[COL_SHIP_FLEET_DONE * 3 + 2] = 0.7F;
	
	ret[COL_SHIP_FLEET_STRIPE * 3 + 0] = 0.0F;
    ret[COL_SHIP_FLEET_STRIPE * 3 + 1] = 0.0F;
    ret[COL_SHIP_FLEET_STRIPE * 3 + 2] = 0.0F;
	
	ret[COL_COUNT * 3 + 0] = 0.0F;
    ret[COL_COUNT * 3 + 1] = 0.0F;
    ret[COL_COUNT * 3 + 2] = 0.0F;
	
	ret[COL_COUNT_ERROR * 3 + 0] = 1.0F;
    ret[COL_COUNT_ERROR * 3 + 1] = 0.0F;
    ret[COL_COUNT_ERROR * 3 + 2] = 0.0F;
	
	ret[COL_COLLISION_ERROR * 3 + 0] = 1.0F;
    ret[COL_COLLISION_ERROR * 3 + 1] = 0.0F;
    ret[COL_COLLISION_ERROR * 3 + 2] = 0.0F;
	
	ret[COL_COLLISION_TEXT * 3 + 0] = 1.0F;
    ret[COL_COLLISION_TEXT * 3 + 1] = 1.0F;
    ret[COL_COLLISION_TEXT * 3 + 2] = 1.0F;

	*ncolours = NCOLOURS;
	return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
	struct game_drawstate *ds = snew(struct game_drawstate);

	int w = state->w;
	int h = state->h;
	int fleet = state->fleet;
	
	ds->tilesize = 0;
	ds->fleeth = 0;
	ds->border = snewn(w + h, int);
	ds->fleetcount = snewn(fleet, int);
	ds->gridfs = snewn(w * h, int);
	ds->oldgridfs = snewn(w * h, int);
	ds->oldfleetcount = snewn(fleet, int);
	ds->oldborder = snewn(w + h, int);
	ds->grid = snewn(w * h, int);
	
	ds->redraw = true;
	ds->oldflash = false;
	
	memset(ds->grid, 0, w*h * sizeof(int));
	memset(ds->gridfs, 0, w*h * sizeof(int));
	memset(ds->oldgridfs, 0, w*h * sizeof(int));
	memset(ds->oldfleetcount, 0, fleet * sizeof(int));
	memset(ds->oldborder, 0, w + h * sizeof(int));

	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->border);
	sfree(ds->fleetcount);
	sfree(ds->gridfs);
	sfree(ds->oldgridfs);
	sfree(ds->oldfleetcount);
	sfree(ds->oldborder);
	sfree(ds->grid);
	sfree(ds);
}

static void boats_draw_ship(drawing *dr, int tx, int ty, double tilesize, char ship, int color)
{
	double cx, cy, r;
	int coords[8];
	double off = tilesize / 20;
	
	assert(IS_SHIP(ship));
	
	cx = tx + (tilesize/2);
	cy = ty + (tilesize/2);
	r = (tilesize/2) - (off * 2);
	
	/* Draw a circle */
	if(ship != SHIP_CENTER && ship != SHIP_VAGUE)
	{
		draw_circle(dr, cx, cy, r, color, color);
	}
	
	if(ship == SHIP_VAGUE) /* Smaller square */
		r *= 0.7;
	
	if(ship == SHIP_CENTER || ship == SHIP_VAGUE) /* Square */
	{
		coords[0] = cx - r; 
		coords[1] = cy - r;
		coords[2] = cx + r;
		coords[5] = cy + r;
	}
	
	if(ship == SHIP_TOP) /* Rectangle on bottom */
	{
		coords[0] = cx - r; 
		coords[1] = cy;
		coords[2] = cx + r;
		coords[5] = cy + r;
	}
	
	if(ship == SHIP_BOTTOM) /* Rectangle on top */
	{
		coords[0] = cx - r; 
		coords[1] = cy - r;
		coords[2] = cx + r;
		coords[5] = cy;
	}
	
	if(ship == SHIP_LEFT) /* Rectangle on right */
	{
		coords[0] = cx; 
		coords[1] = cy - r;
		coords[2] = cx + r;
		coords[5] = cy + r;
	}
	
	if(ship == SHIP_RIGHT) /* Rectangle on left */
	{
		coords[0] = cx - r; 
		coords[1] = cy - r;
		coords[2] = cx;
		coords[5] = cy + r;
	}
	
	/* Draw the rectangle */
	if(ship != SHIP_SINGLE) 
	{
		coords[3] = coords[1];
		coords[4] = coords[2]; 
		coords[6] = coords[0];
		coords[7] = coords[5];
		
		draw_polygon(dr, coords, 4, color, color);
	}
}

/* Copied from tents.c */
static void boats_draw_collision(drawing *dr, int tilesize, int x, int y)
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
    draw_polygon(dr, coords, 4, COL_COLLISION_ERROR, COL_GRID);

    /*
     * Draw an exclamation mark in the diamond. This turns out to
     * look unpleasantly off-centre if done via draw_text, so I do
     * it by hand on the basis that exclamation marks aren't that
     * difficult to draw...
     */
    xext = tilesize/16;
    yext = tilesize*2/5 - (xext*2+2);
    draw_rect(dr, x-xext, y-yext, xext*2+1, yext*2+1 - (xext*3),
	      COL_COLLISION_TEXT);
    draw_rect(dr, x-xext, y+yext-xext*2+1, xext*2+1, xext*2, COL_COLLISION_TEXT);
}

/* Starting position */
#define FLEET_X (0.5F)
/* Size of a single ship segment */
#define FLEET_SIZE (0.75F)
/* Space between two boats */
#define FLEET_MARGIN (0.25F)

static int boats_draw_fleet(drawing *dr, int w, int y, int fleet, 
	int *fleetdata, int *fleetcount, int *oldfc, bool redraw, double tilesize, int print)
{
	/*
	 * This function can draw the fleet data, or measure the height needed
	 * to draw the fleet data.
	 * If no drawing is given, this function only returns the height needed.
	 * If a print color is given, the drawing is not manually updated.
	 */
	
	int i, j, k, bgcol;
	float fx, ofx, nfx;
	char ship;
	
	fx = FLEET_X;
	for(i = 0; i < fleet; i++)
	{
		float fw;
		fw = fleetdata[i] * (((i+1) * FLEET_SIZE) + FLEET_MARGIN);
		
		/* If the next batch of boats doesn't fit, move to the next line */
		if(fx + fw > w+2 && fx != FLEET_X)
		{
			fx = FLEET_X;
			y++;
		}
		
		for(j = 0; j < fleetdata[i]; j++)
		{
			nfx = ((i+1) * FLEET_SIZE) + FLEET_MARGIN;
			
			/* See if this boat needs to be updated */
			if(!dr || (!redraw && fleetcount && oldfc && 
				fleetcount[i] == oldfc[i]) )
			{
				fx += nfx;
				continue;
			}
			
			ofx = fx;
			if(print == -1)
			{
				draw_update(dr, ofx * tilesize, y * tilesize,
					nfx * tilesize, FLEET_SIZE * tilesize);
				draw_rect(dr, ofx * tilesize, y * tilesize,
					nfx * tilesize, FLEET_SIZE * tilesize, COL_BACKGROUND);
			}
			
			if(print != -1)
				bgcol = print;
			else if(fleetcount)
				bgcol = j < fleetcount[i] ? 
					COL_SHIP_FLEET_DONE : COL_SHIP_FLEET;
			else
				bgcol = COL_SHIP_FLEET;
			
			/* Draw each segment */
			for(k = 0; k <= i; k++)
			{
				ship = (i == 0 ? SHIP_SINGLE : k == 0 ? SHIP_LEFT :
					k == i ? SHIP_RIGHT : SHIP_CENTER);
				
				boats_draw_ship(dr, fx * tilesize, y * tilesize, 
					tilesize * FLEET_SIZE, ship, bgcol);
				
				fx += FLEET_SIZE;
			}
			
			/* Draw a stripe through boats that have been placed already */
			if(print == -1 && fleetcount && j < fleetcount[i])
			{
				bgcol = (fleetdata[i] >= fleetcount[i] ? COL_SHIP_FLEET_STRIPE : COL_COUNT_ERROR);
				
				draw_thick_line(dr, 2,
					ofx * tilesize + 2, (y + FLEET_SIZE) * tilesize - 2,
					fx * tilesize - 2, y * tilesize + 2, bgcol);
			}
			
			fx += FLEET_MARGIN;
		}
		if(fleetcount && oldfc)
			oldfc[i] = fleetcount[i];
	}
	
	return y + 1;
}

#define FLASH_FRAME 0.12F
#define FLASH_TIME (FLASH_FRAME * 5)

static void game_redraw(drawing *dr, game_drawstate *ds, const game_state *oldstate,
			const game_state *state, int dir, const game_ui *ui,
			float animtime, float flashtime)
{
	char buf[80];
	int tilesize = ds->tilesize;
	int w = state->w;
	int h = state->h;
	int x, y, tx, ty, bgcol;
	int xmin = min(ui->dsx, ui->dex);
	int xmax = max(ui->dsx, ui->dex);
	int ymin = min(ui->dsy, ui->dey);
	int ymax = max(ui->dsy, ui->dey);
	char ship;
	bool redraw = ds->redraw;
	bool flash = false;
	
	if(flashtime > 0)
		flash = (int)(flashtime/FLASH_FRAME) & 1;
	
	if(redraw)
	{
		draw_rect(dr, 0, 0, (w+2)*tilesize, (h+2)*tilesize + ds->fleeth, COL_BACKGROUND);
		draw_update(dr, 0, 0, (w+2)*tilesize, (h+2)*tilesize + ds->fleeth);
	}
	
	boats_count_ships(state, NULL, NULL, ds->border);
	boats_check_fleet(state, ds->fleetcount, ds->gridfs);
	
	/* Draw column numbers */
	ty = (h+1)*tilesize + (0.5 * tilesize);
	for(x = 0; x < w; x++)
	{
		if(state->borderclues[x] == NO_CLUE || 
			(!redraw && (ds->border[x] == ds->oldborder[x])))
			continue;
		
		tx = (x+1)*tilesize;
		sprintf(buf, "%d", state->borderclues[x]);
		bgcol = ds->border[x] == STATUS_INVALID ? COL_COUNT_ERROR : COL_COUNT;
		
		draw_rect(dr, tx - (tilesize/2), ty - (tilesize/2), 
			tilesize, tilesize, COL_BACKGROUND);
		draw_update(dr, tx - (tilesize/2), ty - (tilesize/2),
			tilesize, tilesize);
		draw_text(dr, tx, ty,
		      FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VNORMAL,
		      bgcol, buf);
		
		ds->oldborder[x] = ds->border[x];
	}
	
	/* Draw row numbers */
	tx = (w+1)*tilesize + (0.5 * tilesize);
	for(y = 0; y < h; y++)
	{
		if(state->borderclues[y+w] == NO_CLUE || 
			(!redraw && (ds->border[y+w] == ds->oldborder[y+w])))
			continue;
		
		ty = (y+1)*tilesize;
		sprintf(buf, "%d", state->borderclues[y+w]);
		bgcol = ds->border[y+w] == STATUS_INVALID ? COL_COUNT_ERROR : COL_COUNT;
		
		draw_rect(dr, tx - (tilesize/2), ty - (tilesize/2), 
			tilesize, tilesize, COL_BACKGROUND);
		draw_update(dr, tx - (tilesize/2), ty - (tilesize/2),
			tilesize, tilesize);
		draw_text(dr, tx, ty,
		      FONT_VARIABLE, tilesize/2, ALIGN_VCENTRE|ALIGN_HRIGHT,
		      bgcol, buf);
		
		ds->oldborder[y+w] = ds->border[y+w];
	}
	
	boats_validate_gridclues(state, ds->gridfs);
	boats_check_collision(state, ds->gridfs);
	
	/* Invalidate squares near change in collision */
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		if((ds->oldgridfs[y*w+x] & FE_COLLISION) != (ds->gridfs[y*w+x] & FE_COLLISION))
		{
			ds->grid[(y+1)*w+x] = -1;
			ds->grid[y*w+(x+1)] = -1;
			ds->grid[(y+1)*w+(x+1)] = -1;
		}
	}
	
	/* Draw ships and water clues */
	for(x = 0; x < w; x++)
	for(y = 0; y < h; y++)
	{
		tx = x*tilesize + (0.5 * tilesize);
		ty = y*tilesize + (0.5 * tilesize);
		
		if(flashtime == 0 && ui->cursor && ui->cx == x && ui->cy == y)
			ds->gridfs[y*w+x] |= FD_CURSOR;
		else
			ds->gridfs[y*w+x] &= ~FD_CURSOR;
		
		ship = state->gridclues[y*w+x] != EMPTY ? state->gridclues[y*w+x]
			: state->grid[y*w+x];
		
		/* Check if this square is being changed by a drag */
		if(ui->drag_ok && x >= xmin && x <= xmax &&
			y >= ymin && y <= ymax && state->gridclues[y*w+x] == EMPTY &&
				(ui->drag_from == '*' || 
					(ui->drag_from == '-' && ship == EMPTY) ||
					(ui->drag_from == 'W' && ship == WATER) ||
					(ui->drag_from == 'B' && IS_SHIP(ship))
				)
			)
		{	
			ship = ui->drag_to == 'B' ? SHIP_VAGUE : 
				ui->drag_to == 'W' ? WATER : EMPTY;
		}

		if(redraw || flash != ds->oldflash || ds->oldgridfs[y*w+x] 
			!= ds->gridfs[y*w+x] || ds->grid[y*w+x] != ship)
		{
			draw_update(dr, tx, ty, tilesize + 1, tilesize + 1);
			ds->oldgridfs[y*w+x] = ds->gridfs[y*w+x];
			ds->grid[y*w+x] = ship;
			
			bgcol = ship != EMPTY ? COL_WATER : COL_BACKGROUND;
			
			draw_rect(dr, tx, ty, tilesize, tilesize, bgcol);
			draw_rect_outline(dr, tx, ty, tilesize+1, tilesize+1, COL_GRID);
			
			if(!flash && IS_SHIP(ship))
			{
				bgcol = ds->gridfs[y*w+x] & FE_MISMATCH ? COL_SHIP_ERROR :
					state->gridclues[y*w+x] == EMPTY ? COL_SHIP_GUESS :
					COL_SHIP_CLUE;
				boats_draw_ship(dr, tx, ty, tilesize + 1, ship, bgcol);
			}
			else if(!flash && state->gridclues[y*w+x] == WATER)
			{
				draw_text(dr, tx + tilesize/2, ty + (tilesize * 0.42F),
				  FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
				  COL_GRID, "~");
				  
				draw_text(dr, tx + tilesize/2, ty + (tilesize * 0.58F),
				  FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
				  COL_GRID, "~");
			}
			if(IS_SHIP(ship) && ds->gridfs[y*w+x] & FE_FLEET)
			{
				draw_text(dr, tx + tilesize/2, ty + tilesize/2,
				  FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
				  COL_COUNT_ERROR, "?");
			}
			
			if(ds->gridfs[y*w+x] & FD_CURSOR)
			{
				int coff = tilesize/8;
				bgcol = state->grid[y*w+x] == EMPTY ? 
					COL_CURSOR_A : COL_CURSOR_B;
				draw_rect_outline(dr, tx + coff, ty + coff,
					tilesize - coff*2 + 1, tilesize - coff*2 + 1, bgcol);
			}
		}
	}
	
	/* Draw collisions */
	for(x = 0; x < w - 1; x++)
	for(y = 0; y < h - 1; y++)
	{
		if(ds->gridfs[y*w+x] & FE_COLLISION)
		{
			boats_draw_collision(dr, tilesize, (x+1.5F)*tilesize,
			(y+1.5F)*tilesize);
		}
	}
	
	/* Draw fleet */
	boats_draw_fleet(dr, w, h+2, state->fleet, state->fleetdata, 
		ds->fleetcount, ds->oldfleetcount, redraw, tilesize, -1);
	
	ds->redraw = false;
	ds->oldflash = flash;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
	if(ui->cursor) {
		*x = (ui->cx+0.5) * ds->tilesize;
		*y = (ui->cy+0.5) * ds->tilesize;
		*w = *h = ds->tilesize;
	}
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  const game_params *params, int tilesize)
{
	ds->tilesize = tilesize;
	ds->fleeth = boats_draw_fleet(NULL, params->w, 0, params->fleet,
		params->fleetdata, NULL, NULL, false, tilesize, -1) * tilesize;
	ds->redraw = true;
}

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
	int fh;
	*x = (params->w+2) * tilesize;
	
	fh = boats_draw_fleet(NULL, params->w, 0, params->fleet, params->fleetdata,
		NULL, NULL, false, tilesize, -1);
	
	*y = (params->h+2+fh) * tilesize;
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

static void game_print_size(const game_params *params, const game_ui *ui,
                            float *x, float *y)
{
    int pw, ph;

    /* Using 7mm squares */
    game_compute_size(params, 700, ui, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, const game_ui *ui,
                       int tilesize)
{
	int x, y;
	int w = state->w;
	int h = state->h;
	int tx, ty;
	char ship;
	char buf[80];
	bool solution = false;
	
	int ink = print_mono_colour(dr, 0);
	
	/* Grid */
	for(y = 0; y < h; y++)
	for(x = 0; x < w; x++)
	{
		tx = x * tilesize + (tilesize / 2);
		ty = y * tilesize + (tilesize / 2);
		
		draw_rect_outline(dr, tx, ty, tilesize+1, tilesize+1, ink);
		
		if(state->gridclues[y*w+x] == WATER)
		{
			draw_text(dr, tx + tilesize/2, ty + (tilesize * 0.42F),
		      FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
		      ink, "~");
			  
			draw_text(dr, tx + tilesize/2, ty + (tilesize * 0.58F),
		      FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VCENTRE,
		      ink, "~");
		}
		else if(IS_SHIP(state->grid[y*w+x]))
		{
			ship = state->gridclues[y*w+x] != EMPTY ? 
				state->gridclues[y*w+x] : state->grid[y*w+x];
			
			boats_draw_ship(dr, tx, ty, tilesize + 1, ship, ink);
		}
		
		if(state->gridclues[y*w+x] == EMPTY && IS_SHIP(state->grid[y*w+x]))
			solution = true;
	}
	
	/* Row numbers */
	tx = (w+1)*tilesize + (0.5 * tilesize);
	for(y = 0; y < h; y++)
	{
		if(state->borderclues[y+w] == NO_CLUE)
			continue;
		
		ty = (y+1)*tilesize;
		sprintf(buf, "%d", state->borderclues[y+w]);
		
		draw_text(dr, tx, ty,
		      FONT_VARIABLE, tilesize/2, ALIGN_VCENTRE|ALIGN_HRIGHT,
		      ink, buf);
	}
	
	/* Column numbers */
	ty = (h+1)*tilesize + (0.5 * tilesize);
	for(x = 0; x < w; x++)
	{
		if(state->borderclues[x] == NO_CLUE)
			continue;
		
		tx = (x+1)*tilesize;
		sprintf(buf, "%d", state->borderclues[x]);
		
		draw_text(dr, tx, ty,
		      FONT_VARIABLE, tilesize/2, ALIGN_HCENTRE|ALIGN_VNORMAL,
		      ink, buf);
	}
	
	if(!solution)
	{
		boats_draw_fleet(dr, w, h+2, state->fleet, state->fleetdata, NULL,
			NULL, false, tilesize, ink);
	}
}

#ifdef COMBINED
#define thegame boats
#endif

const struct game thegame = {
	"Boats", NULL, NULL,
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
	NULL, /* game_request_keys */
	game_changed_state,
	NULL, /* current_key_label */
	interpret_move,
	execute_move,
	32, game_compute_size, game_set_size,
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
			"Usage: %s [-v | -s] [--seed SEED] <params> | [game_id [game_id ...]]\n",
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
		else if (!strcmp(p, "-s"))
		{
			solver_verbose = true;
			solver_steps = true;
		}
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
		char *desc_gen, *aux, *fmt;
		solver_steps = false;
		rs = random_new((void *) &seed, sizeof(time_t));
		if (!params)
			params = default_params();
		printf("Generating puzzle with parameters %s\n",
			   encode_params(params, true));
		desc_gen = new_game_desc(params, rs, &aux, false);

		fmt = game_text_format(new_game(NULL, params, desc_gen));
		fputs(fmt, stdout);
		sfree(fmt);

		printf("Game ID: %s\n", desc_gen);
	} else {
		game_state *input;
		int maxdiff;

		err = validate_desc(params, desc);
		if (err) {
			fprintf(stderr, "Description is invalid\n");
			fprintf(stderr, "%s", err);
			exit(1);
		}

		input = new_game(NULL, params, desc);

		maxdiff = boats_solve_game(input, DIFFCOUNT);
		
		if (maxdiff != -2) {
			char *fmt = game_text_format(input);
			fputs(fmt, stdout);
			sfree(fmt);
			if (maxdiff >= 0)
				printf("Difficulty: %s\n", boats_diffnames[maxdiff]);
		}
		if (maxdiff == -1)
			printf("No solution found.\n");
		if (maxdiff == -2)
			printf("Puzzle is invalid.\n");

		free_game(input);
	}

	return 0;
}
#endif
