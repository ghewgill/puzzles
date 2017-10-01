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
	NCOLOURS
};

typedef int number;
typedef int cell;
typedef unsigned char bitmap;

#define IS_NEAR(a,b,w) ( ( abs(((a)/(w)) - ((b)/(w))) | abs(((a)%(w)) - ((b)%(w)))) == 1 )
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

	int diff;
	char removeends;
};

#define DIFFLIST(A)                             \
	A(EASY,Easy, e)                             \
	A(NORMAL,Normal, n)                         \
	A(TRICKY,Tricky, t)                         \
	A(HARD,Hard, h)                             \

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const ascent_diffnames[] = { DIFFLIST(TITLE) };

static char const ascent_diffchars[] = DIFFLIST(ENCODE);

const static struct game_params ascent_presets[] = {
	{ 7,  6, DIFF_EASY, FALSE },
	{ 7,  6, DIFF_NORMAL, FALSE },
	{ 7,  6, DIFF_TRICKY, FALSE },
	{ 7,  6, DIFF_HARD, FALSE },
	{ 10, 8, DIFF_EASY, FALSE },
	{ 10, 8, DIFF_NORMAL, FALSE },
	{ 10, 8, DIFF_TRICKY, FALSE },
	{ 10, 8, DIFF_HARD, FALSE },
#ifndef SMALL_SCREEN
	{ 14, 11, DIFF_EASY, FALSE },
	{ 14, 11, DIFF_NORMAL, FALSE },
	{ 14, 11, DIFF_TRICKY, FALSE },
	{ 14, 11, DIFF_HARD, FALSE },
#endif
};

#define DEFAULT_PRESET 0

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

	*ret = ascent_presets[DEFAULT_PRESET];        /* structure copy */
	
	return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
	game_params *ret;
	char buf[80];

	if (i < 0 || i >= lenof(ascent_presets))
		return FALSE;

	ret = snew(game_params);
	*ret = ascent_presets[i];     /* structure copy */

	sprintf(buf, "%dx%d %s", ret->w, ret->h, ascent_diffnames[ret->diff]);

	*name = dupstr(buf);
	*params = ret;
	return TRUE;
}

static void free_params(game_params *params)
{
	sfree(params);
}

static game_params *dup_params(const game_params *params)
{
	game_params *ret = snew(game_params);
	*ret = *params;               /* structure copy */
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
	p += sprintf(p, "%dx%d", params->w, params->h);
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
	
	ret = snewn(5, config_item);
	
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
	game_params *ret = snew(game_params);
	
	ret->w = atoi(cfg[0].u.string.sval);
	ret->h = atoi(cfg[1].u.string.sval);
	ret->removeends = !cfg[2].u.boolean.bval;
	ret->diff = cfg[3].u.choices.selected;
	
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
		if(grid[i] < -1)
			last--;
	}
	if(x == -1)
		return FALSE;
	
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
	return (random_upto(rs, 2) ? backbite_left : backbite_right)(random_upto(rs, 8), n, pathx, pathy, w, h);
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
		if(n < 0 || n >= s) continue;
		positions[n] = (positions[n] == -1 ? i : -2);
	}
}

struct solver_scratch {
	int w, h;
	
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

static struct solver_scratch *new_scratch(int w, int h, number last)
{
	int i, n = w*h;
	struct solver_scratch *ret = snew(struct solver_scratch);
	ret->w = w;
	ret->h = h;
	ret->end = last;
	ret->positions = snewn(n, cell);
	ret->grid = snewn(n, number);
	ret->path = snewn(n, int);
	ret->found_endpoints = FALSE;
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
	int w = scratch->w, s = w*scratch->h;
	cell i; number n;
	
	/* Place the number and update the positions array */
	scratch->grid[pos] = num;
	scratch->positions[num] = (scratch->positions[num] == -1 ? pos : -2);
	
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
		if(scratch->grid[i] != -1) continue;
		found = -1;
		for(n = 0; n <= scratch->end; n++)
		{
			if(!GET_BIT(scratch->marks, i*s+n)) continue;
			if(found == -1)
				found = n;
			else
				found = -2;
		}
		assert(found != -1);
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
		hdist = abs((i%w) - (near%w));
		vdist = abs((i/w) - (near/w));
		if(max(hdist, vdist) <= distance) continue;
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
		
		if(n > 0 && scratch->positions[n-1] == -1)
			ret += solver_near(scratch, i, n-1, 1);
		if(n < end-1 && scratch->positions[n+1] == -1)
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
		while(n2 >= 0 && scratch->positions[n2] == -1)
		{
			ret += solver_near(scratch, i, n2, abs(n-n2));
			n2--;
		}
		n2 = n+1;
		while(n2 <= end-1 && scratch->positions[n2] == -1)
		{
			ret += solver_near(scratch, i, n2, abs(n-n2));
			n2++;
		}
	}
	
	return ret;
}

static int ascent_find_direction(cell i1, cell i2, int w)
{
	int dir;
	for (dir = 0; dir < 8; dir++)
	{
		if (i2 - i1 == (dir_y[dir] * w + dir_x[dir]))
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
	if (!scratch->found_endpoints && ib != -1 && ic != -1)
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
	if (i != -1 && ib != -1 && !(scratch->path[ib] & FLAG_COMPLETE))
	{
		scratch->path[ib] = (1 << ascent_find_direction(ib, i, w)) | FLAG_ENDPOINT;
	}
	/* Do the same for the last number pointing to the penultimate number. */
	i = scratch->positions[end - 1];
	if (i != -1 && ic != -1 && !(scratch->path[ic] & FLAG_COMPLETE))
	{
		scratch->path[ic] = (1 << ascent_find_direction(ic, i, w)) | FLAG_ENDPOINT;
	}

	/* 
	 * For all numbers in the middle, set the path 
	 * if the next and previous numbers are known. 
	 */
	for (n = 1; n <= end-1; n++)
	{
		i = scratch->positions[n];
		if (i == -1 || scratch->path[i] & FLAG_COMPLETE) continue;

		ib = scratch->positions[n-1];
		ic = scratch->positions[n+1];
		if (ib == -1 || ic == -1) continue;

		scratch->path[i] = 1 << ascent_find_direction(i, ib, w);
		scratch->path[i] |= 1 << ascent_find_direction(i, ic, w);
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
		if (scratch->path[i] & FLAG_COMPLETE && scratch->grid[i] == -1)
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
		if(scratch->grid[i1] >= -1) continue;
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
			if(scratch->grid[i] == -1)
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
	struct solver_scratch *scratch = new_scratch(w, h, (w*h)-1);
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
		if(!params->removeends && (temp == 0 || temp == w*h-1)) continue;
		grid[i] = -1;
		
		ascent_solve(grid, params->diff, scratch);
		
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
		else if(*p >= 'A' && *p <= 'Z')
		{
			walls = ((*p++) - 'A') + 1;
			for(j = i; j < walls + i; j++)
			{
				state->grid[j] = -2;
				SET_BIT(state->immutable, j);
			}
			
			state->last -= walls;
			i += walls;
		}
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
	struct solver_scratch *scratch = new_scratch(w, h, state->last);
	
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
	return TRUE;
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
	/* Deselect the current number */
	ui->held = ui->select = ui->target = -1;
	ui->dir = 0;
}

static void ui_seek(game_ui *ui, number last)
{
	/* Move the selection forward until an unplaced number is found */
	if(ui->held == -1 || ui->select < 0 || ui->select > last)
	{
		ui->select = -1;
		ui->target = -1;
	}
	else
	{
		number n = ui->select;
		while(n + ui->dir >= 0 && n + ui->dir <= last && ui->positions[n] == -1)
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
	while(n > 0 && n < last && ui->held == -1);
	
	ui->select = n + ui->dir;
	ui_seek(ui, last);
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
	
	if (IS_MOUSE_DOWN(button) && (ox < tilesize/2 || oy < tilesize/2 || gx >= w || gy >= h))
	{
		ui_clear(ui);
		return "";
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
			
			if(n < -1)
			{
				ui_clear(ui);
				return "";
			}
			if(n >= 0)
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
			if(n >= 0 && ui->select == n && ui->select + ui->dir <= state->last && ui->select + ui->dir >= 0)
			{
				ui->held = i;
				ui->select += ui->dir;
				ui_seek(ui, state->last);
				return "";
			}
			if(n == -1 && ui->held != -1 && ui->positions[ui->select] == -1 && IS_NEAR(ui->held, i, w))
			{
				sprintf(buf, "P%d,%d", i, ui->select);
				
				ui->held = i;
				if(ui->select + ui->dir <= state->last)
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
			i >= 0 && i < w*h && n >= 0 && n <= state->last
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
				if(!GET_BIT(ret->immutable, i)) ret->grid[i] = -1;
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
	
	return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
	sfree(ds->colours);
	sfree(ds->oldgrid);
	sfree(ds->oldpositions);
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
	
	if(ds->redraw)
	{
		draw_rect(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize, COL_MIDLIGHT);
		draw_rect(dr, (tilesize/2), (tilesize/2)-1, w*tilesize+1, h*tilesize+1, COL_BORDER);
		draw_update(dr, 0, 0, (w+1)*tilesize, (h+1)*tilesize);
	}
	else
	{
		char dirty;
		
		/* Invalidate squares */
		for(i = 0; i < w*h; i++)
		{
			dirty = FALSE;
			n = state->grid[i];
			if(n == -1 && ui->held != -1 && IS_NEAR(i, ui->held, w) && positions[ui->select] < 0)
				n = ui->select;
			
			if(ds->oldgrid[i] != n)
				dirty = TRUE;
			
			if(ui->held != ds->oldheld || ui->target != ds->oldtarget)
			{
				if(IS_NEAR(i, ui->held, w))
					dirty = TRUE;
				else if(IS_NEAR(i, ds->oldheld, w))
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
		
		memcpy(ds->oldgrid, state->grid, w*h*sizeof(number));
		memcpy(ds->oldpositions, ui->positions, w*h*sizeof(cell));
	}
	
	ds->redraw = FALSE;
	ds->oldheld = ui->held;
	ds->oldtarget = ui->target;
	
	/* Draw squares */
	for(i = 0; i < w*h; i++)
	{
		tx = TOCOORD(i%w), ty = TOCOORD(i/w);
		tx1 = tx + (tilesize/2), ty1 = ty + (tilesize/2);
		n = state->grid[i];
		error = FALSE;
		
		colour = n == -2 ? COL_BORDER :
			flash >= n && flash <= n + FLASH_SIZE ? COL_LOWLIGHT :
			ui->held == i ? COL_LOWLIGHT : 
			ui->target >= 0 && positions[ui->target] == i ? COL_HIGHLIGHT :
			COL_MIDLIGHT;
		
		if(ds->colours[i] == colour) continue;
		
		/* Draw tile background */
		clip(dr, tx, ty, tilesize, tilesize);
		draw_update(dr, tx, ty, tilesize, tilesize);
		draw_rect(dr, tx, ty, tilesize, tilesize, colour);
		ds->colours[i] = colour;
		
		/* Draw a circle on the beginning and the end of the path */
		if(n == 0 || n == state->last)
		{
			draw_circle(dr, tx+(tilesize/2), ty+(tilesize/2),
				tilesize/3, COL_HIGHLIGHT, COL_HIGHLIGHT);
		}
		
		/* Draw path lines */
		if(n > 0 && positions[n-1] >= 0)
		{
			i2 = positions[n-1];
			tx2 = (i2%w)*tilesize + (tilesize);
			ty2 = (i2/w)*tilesize + (tilesize);
			if(IS_NEAR(i, i2, w))
				draw_thick_line(dr, 5.0, tx1, ty1, tx2, ty2, COL_HIGHLIGHT);
			else
				error = TRUE;
		}
		if(n >= 0 && n < state->last && positions[n+1] >= 0)
		{
			i2 = positions[n+1];
			tx2 = (i2%w)*tilesize + (tilesize);
			ty2 = (i2/w)*tilesize + (tilesize);
			if(IS_NEAR(i, i2, w))
				draw_thick_line(dr, 5.0, tx1, ty1, tx2, ty2, COL_HIGHLIGHT);
			else
				error = TRUE;
		}
		
		/* Draw square border */
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

		if(n == -1 && ui->held != -1 && IS_NEAR(i, ui->held, w) && positions[ui->select] < 0)
			n = ui->select;
		
		/* Draw a light circle on possible endpoints */
		if(state->grid[i] == -1 && (n == 0 || n == state->last))
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
					positions[n] == -2 ? COL_ERROR :
					GET_BIT(state->immutable, i) ? COL_IMMUTABLE : 
					state->grid[i] == -1 ? COL_LOWLIGHT : COL_BORDER, buf);
			
			if(error)
			{
				draw_thick_line(dr, 2, tx+margin, ty+margin,
					(tx+tilesize)-margin, (ty+tilesize)-margin, COL_ERROR);
			}
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
	game_fetch_preset, NULL,
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

	char *id = NULL, *desc = NULL, *err;

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
		scratch = new_scratch(w, h, input->last);
		
		ascent_solve(input->grid, DIFFCOUNT, scratch);
		
		free_scratch(scratch);
		free_game(input);
	}

	return 0;
}
#endif
