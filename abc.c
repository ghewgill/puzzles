/*
 * abc.c - an implementation of Tetsuya Nishio's & Naoki Inaba's 'ABC' puzzle
 *
 * Currently there is two function versions for generating game id:
 * 1st func generates random edges, does some 're-arranging' and checks for unique solution
 * 2nd func generates a board with _AT LEAST one solution and checks for uniqueness
 * 
 * I had expected 2nd func to work bettter but it is only faster for small grids and then gets much slower
 * 
 * Unforunatelly both versions get too slow for grid sizes above 12
 *
 * There also exists an interesting variation of this puzzle caleed 'Blood' which
 * can be found on Naoki Inaba's puzzles webpage
 *
 * TODO: - add printing functions
 *       - 'grid' and 'pencil' could be packed into a single byte (see struct cell)
 *
 * BUGS: - 'aux' in solve function doesn't display correct solution unless the same id is entered manually again 
 *         (using #if 0 to ignore this piece of code for now)
 *		 - 'game id' has some extra symbols when reducing the board size (this doesn't affect gameplay)
 *	     - when 'game id' has too few parameters correct error message is displayed initially 
 *         but then the wrong error message is displayed subsequently
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>

#include "puzzles.h"

typedef unsigned char byte;

#ifdef STANDALONE_SOLVER
int solver_show_working, solver_show_elimination;
#endif

#define NEW_GAME_DESC_VERSION_1

enum {
	COL_BACKGROUND,
	COL_GRID,
	COL_LETTER,
	COL_HIGHLIGHT,
	COL_PENCIL,
	COL_XMARK,
	COL_EDGE,
	NCOLOURS
};

struct game_params {
	int wh;
};

struct edges {
	byte *top, *bottom, *left, *right;
};


#if 0 
struct cell /* we could pack 'grid' and 'pencil' in game_state and _drawstate */
{
	unsigned int grid: 3;
	unsigned int pencil: 3;
	unsigned int hl: 2;
};
#endif

struct game_state {
	int wh;
	struct edges edges;
	byte *grid;
	byte *pencil;
	bool completed, cheated;
};

static game_params *default_params(void)
{
	game_params *ret = snew(game_params);
	ret->wh = 5;
	return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    return FALSE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
	params->wh = atoi(string);
}

static char *encode_params(const game_params *params, int full)
{
    char str[4];
	sprintf(str, "%d", params->wh);
    return dupstr(str);
}

static config_item *game_configure(const game_params *params)
{
	config_item *ret;
	char buf[8];
	
	ret = snewn(2, config_item);
	
	ret[0].name = "Square size";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->wh);
    ret[0].sval = dupstr(buf);
	
	ret[1].name = NULL;
    ret[1].type = C_END;
	
	return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->wh = atoi(cfg[0].sval);

    return ret;
}

static char *validate_params(const game_params *params, int full)
{
    if (params->wh < 4)
		return "Square size must be at least 4.";

	return NULL; 
}
 
enum {
	MASK_A = 0x01, 
	MASK_B = 0x02, 
	MASK_C = 0x04, 
	MASK_ABC = 0x07,
	MASK_X = 0x08,
	MASK_CURSOR = 0x10,
	MASK_PENCIL = 0x20
};

struct solver_usage {
	int wh, area;
	/* Final deductions */
	byte *grid;
	/* Keeping track of possibilities for each cell (1 bit for each A, B or C) */
	byte *values;
	/* Values still left to be allocated in each row and column */
    byte *row, *col;
};

static struct solver_usage *new_solver_usage(const game_params *params) 
{	
	struct solver_usage *ret = snew(struct solver_usage);
	
	ret->wh = params->wh;
	ret->area = ret->wh*ret->wh;
	ret->grid = snewn(ret->area, byte);
	ret->values = snewn(ret->area, byte);
	ret->row = snewn(ret->wh, byte);
	ret->col = snewn(ret->wh, byte);
	
	memset(ret->grid, 0, ret->area);
	memset(ret->values, MASK_ABC, ret->area);
	memset(ret->row, MASK_ABC, ret->wh);
	memset(ret->col, MASK_ABC, ret->wh);
	
	return ret;
}

static void free_solver_usage(struct solver_usage *usage) {
	sfree(usage->grid);
	sfree(usage->values);
	sfree(usage->row);
	sfree(usage->col);
	sfree(usage);
}

/* Helper macros for the solver, the program uses bit masks to encode possible values for each cell */
#define solver_removeval(idx, val) (usage->values[idx] &= ~(val))
#define solver_isunique(val) ((val & (val - 1)) == 0)
#define solver_ispossible(val, invalues) (((invalues) & (val)) > 0) /* at least one value is possible */
#define solver_isassigned(idx) (usage->grid[idx] > 0)

/* Place values in a final grid and eliminate the possible value in the according row and column */
static void solver_place(struct solver_usage *usage, int idx, byte val) {
	int row = idx/usage->wh;
	int col = idx%usage->wh;
	
	usage->grid[idx] = val;
	usage->values[idx] = 0;
	
	/* Remove possible values from a row */
	for(int i = idx-col; i<idx-col+usage->wh; i++)
		solver_removeval(i, val);
	/* Remove possible values from a column */
	for(int i = col; i<usage->area; i+=usage->wh)
		solver_removeval(i, val);
	/* Remove value from to allocate list */
	usage->row[row] &= ~val;
	usage->col[col] &= ~val;
}

/* Determine if there is a single slot for a value in the whole row or column
 * (first avaliable slot is saved in the idx parameter) */
static bool solver_isunique_inrow(struct solver_usage *usage, int row, byte val, int *idx) {
	byte count = 0;
	for(int i = row*usage->wh; i < (row+1)*usage->wh; i++)
	if(solver_ispossible(val, usage->values[i]))
	{
		if(++count > 1)
			return FALSE;
		*idx = i;

	}
	
	return (count == 1);
}
	
static bool solver_isunique_incol(struct solver_usage *usage, int col, byte val, int *idx) {
	byte count = 0;
	for(int i = col; i < usage->area; i+=usage->wh)
	if(solver_ispossible(val, usage->values[i]))
	{
		if(++count > 1)
			return FALSE;
		*idx = i;
	}
	
	return (count == 1);
}

/* Eliminate edge value past(>=) the closest of the others' furthest posibble cells,
 * otherwise that other value would be the closest to the edge */
static bool solver_elim_farpos_inrow(struct solver_usage *usage, const struct edges *edges, int row) {
	bool ret = FALSE;
	
	byte val_elim;
	byte val_other;
	
	int row_beg = row*usage->wh;
	int row_end = (row+1)*usage->wh-1;
	int j;
	
	/* Left cell */
	if(solver_ispossible(edges->left[row], usage->row[row])) { /* If value had been placed already it would have been eliminated also */ 
	val_elim = edges->left[row];
	val_other = MASK_ABC ^ val_elim;
	j = row_end;
	/* Begin the search from the opposite end */
	while((val_other &= ~(usage->values[j] | usage->grid[j])) && j > row_beg)
		j--;
	for(j; j <= row_end; j++)
	if(solver_ispossible(val_elim, usage->values[j])) {
		#ifdef STANDALONE_SOLVER
		if(solver_show_working && solver_show_elimination)
			printf("far pos elim(from left): row %d \n"
				   "\telim val %d idx %d\n", row, val_elim, j);
		#endif
		solver_removeval(j, val_elim);
		ret = TRUE;
	}
	}
	
	/* Right cell */
	if(solver_ispossible(edges->right[row], usage->row[row])) {
	val_elim = edges->right[row];
	val_other = MASK_ABC ^ val_elim;
	j = row_beg;
	while((val_other &= ~(usage->values[j] | usage->grid[j])) && j < row_end)
		j++;
	for(j; j >= row_beg; j--)
	if(solver_ispossible(val_elim, usage->values[j])) {
		#ifdef STANDALONE_SOLVER
		if(solver_show_working && solver_show_elimination)
			printf("far pos elim(from right): row %d \n"
				   "\telim val %d idx %d\n", row, val_elim, j);
		#endif
		solver_removeval(j, val_elim);
		ret = TRUE;
	}
	}
	
	return FALSE;
}

static bool solver_elim_farpos_incol(struct solver_usage *usage, const struct edges *edges, int col) {
	bool ret = 0;
	
	byte val_elim;
	byte val_other;
	
	int col_beg = col;
	int col_end = (usage->wh-1)*usage->wh + col;
	int j;
	
	/* Make sure top edge letter isn't the furthest */
	if(solver_ispossible(edges->top[col], usage->col[col])) {
	val_elim = edges->top[col];
	val_other = MASK_ABC ^ val_elim;
	j = col_end;
	while((val_other &= ~(usage->values[j] | usage->grid[j])) && j > col_beg)
		j-=usage->wh;
	for(j; j <= col_end; j+=usage->wh)
	if(solver_ispossible(val_elim, usage->values[j]))
	{
		#ifdef STANDALONE_SOLVER
		if(solver_show_working && solver_show_elimination)
			printf("far pos elim(from top): col %d \n"
				   "\telim val %d idx %d\n", col, val_elim, j);
		#endif
		solver_removeval(j, val_elim);
		ret = TRUE;
	}
	}
	
	/* Bottom not the furthest */
	if(solver_ispossible(edges->bottom[col], usage->col[col])) {
	val_elim = edges->bottom[col];
	val_other = MASK_ABC ^ val_elim;
	j = col_beg;
	while((val_other &= ~(usage->values[j] | usage->grid[j])) && j < col_end)
		j+=usage->wh;
	for(j; j >= col_beg; j-=usage->wh)
	if(solver_ispossible(val_elim, usage->values[j]))
	{
		#ifdef STANDALONE_SOLVER
		if(solver_show_working && solver_show_elimination)
			printf("far pos elim(from bottom): col %d \n"
				   "\telim val %d idx %d\n", col, val_elim, j);
		#endif
		solver_removeval(j, val_elim);
		ret = TRUE;
	}
	}
	
	return ret;
}

/* Eliminate other values before the first possible edge cell value 
 * i.e. if edge cell is B, eliminate A and C before(<=) the first possible B */
static bool solver_elim_closepos_inrow(struct solver_usage *usage, const struct edges *edges, int row) {
	bool ret = FALSE;
	byte val_elim, val_other;
	int row_beg = row*usage->wh;
	int row_end = row*usage->wh + usage->wh-1;
	int j;
	
	/* Make sure left edge letter isn't the furthest */
	val_elim = MASK_ABC ^ edges->left[row];
	val_other = edges->left[row];
	j=row_beg;
	while(!(val_other & (usage->values[j] | usage->grid[j])) && j < row_end)  // FIX FIRST LOOP COND
		j++;
	for(j; j>=row_beg; j--)
	if(solver_ispossible(val_elim, usage->values[j])) {
		#ifdef STANDALONE_SOLVER
		if(solver_show_working && solver_show_elimination)
			printf("closest pos elimination at row %d\n"
				   "\tremoving %d at idx %d\n", row, val_elim, j);
		#endif
		solver_removeval(j, val_elim);
		ret = TRUE;
	}
	
	/* Make sure right edge letter isn't the furthest */
	val_elim = MASK_ABC ^ edges->right[row];
	val_other = edges->right[row];
	j=row_end;
	while(!(val_other & (usage->values[j] | usage->grid[j])) && j > row_beg)
		j--;
	for(j; j<=row_end; j++)
	if(solver_ispossible(val_elim, usage->values[j])) {
		#ifdef STANDALONE_SOLVER
		if(solver_show_working && solver_show_elimination)
			printf("closest pos elimination at row %d\n"
				   "\tremoving %d at idx %d\n", row, val_elim, j);
		#endif
		solver_removeval(j, val_elim);
		ret = TRUE;
	}
	
	return ret;
}

static bool solver_elim_closepos_incol(struct solver_usage *usage, const struct edges *edges, int col) {
	bool ret = 0;
	
	byte val_elim;
	byte val_other;
	
	int col_beg = col;
	int col_end = (usage->wh-1)*usage->wh + col;
	int j;
	
	/* Make sure top edge letter isn't the furthest */
	val_elim = MASK_ABC ^ edges->top[col];
	val_other = edges->top[col];
	j=col_beg;
	while(!(val_other & (usage->values[j] | usage->grid[j])) && j < col_end)
		j+=usage->wh;
	for(j; j>=col_beg; j-=usage->wh)
	if(solver_ispossible(val_elim, usage->values[j])) {
		#ifdef STANDALONE_SOLVER
		if(solver_show_working && solver_show_elimination)
		   printf("closest pos elimination at col %d\n"
				  "\tremoving %d at idx %d\n", col, val_elim, j);
		#endif
		solver_removeval(j, val_elim);
		ret = TRUE;
	}
	
	/* Make sure bottom edge letter isn't the furthest */
	val_elim = MASK_ABC ^ edges->bottom[col];
	val_other = edges->bottom[col];
	j=col_end;
	while(!(val_other & (usage->values[j] | usage->grid[j])) && j > col_beg)
		j-=usage->wh;
	for(j; j<=col_end; j+=usage->wh)
	if(solver_ispossible(val_elim, usage->values[j]))
	{
		#ifdef STANDALONE_SOLVER
		if(solver_show_working && solver_show_elimination)
			printf("closest pos elimination at col %d\n"
				   "\tremoving %d at idx %d\n", col, val_elim, j);
		#endif
		solver_removeval(j, val_elim);
		ret = TRUE;
	}
	
	return ret;
}

#ifdef STANDALONE_SOLVER
static void printvalues(struct solver_usage *usage) {
	printf("\n");
	for(int r = 0; r<usage->wh; r++) 
	{
		for(int c = 0; c<usage->wh; c++)
		printf("%2d", usage->values[r*usage->wh+c]);
	printf("\n");
	}
}

static void printresult(struct solver_usage *usage) {
	printf("\n");
	for(int r = 0; r<usage->wh; r++) 
	{
		for(int c = 0; c<usage->wh; c++)
		printf("%2d", usage->grid[r*usage->wh+c]);
	printf("\n");
	}	
}
#endif

/* Check if the grid corresponds to the edges */
static bool check_valid(byte *grid, const struct edges *edges, int wh)
{
	struct edges from_grid;
	from_grid.top = snewn(4*wh,byte);
	from_grid.bottom = from_grid.top + wh;
	from_grid.left = from_grid.top + 2*wh;
	from_grid.right = from_grid.top + 3*wh;
	
	/* Read the closest values to each edge */
	byte vals[3];
	int i;
	
 	for(int r = 0; r < wh; r++)
	{
		for(int c = i = 0; c < wh; c++)
		if(grid[r*wh+c] > 0)
			vals[i++] = grid[r*wh+c];
		
		if(i != 3)
		{
			sfree(from_grid.top);
			return FALSE;
		}
		
		from_grid.left[r] = vals[0];
		from_grid.right[r] = vals[2];		
	}
	
	for(int c = 0; c < wh; c++)
	{
		for(int r = i = 0; r < wh; r++)
		if(grid[r*wh+c] > 0)
			vals[i++] = grid[r*wh+c];
		
		if(i != 3)
		{
			sfree(from_grid.top);
			return FALSE;
		}
		
		from_grid.top[c] = vals[0];
		from_grid.bottom[c] = vals[2];		
	}
	
	for(int i = 0; i < 4*wh; i++)
	if(from_grid.top[i] != edges->top[i]) 
	{
		sfree(from_grid.top);
		return FALSE;
	}
	
	sfree(from_grid.top);
	return TRUE;
}

static bool solver(struct solver_usage *usage, const struct edges *edges)
{	
	bool res;
	
	do {
	res = FALSE;
	
	/* Eliminating values which would contradict game's definition */
	for(int i = 0; i<usage->wh; i++)
	res |= solver_elim_closepos_inrow(usage, edges, i) ||
		solver_elim_farpos_inrow(usage, edges, i) ||
		solver_elim_closepos_incol(usage, edges, i) ||
		solver_elim_farpos_incol(usage, edges, i);
	
	/* Place unique values in rows and columns */
	for(int idx, i = 0; i<usage->wh; i++)
	for(int val = 1; val <= MASK_ABC; val<<=1)
	{
		if(solver_ispossible(val, usage->row[i]) && // Check if the value is required in this row
			solver_isunique_inrow(usage, i, val, &idx))
		{
			#ifdef STANDALONE_SOLVER
			if(solver_show_working)
				printf("unique val in row %d\n"
			           "\tplacing %d at idx %d\n", i, val, idx);
			#endif
			solver_place(usage, idx, val);
			res = TRUE;
		}
	
		if(solver_ispossible(val, usage->col[i]) &&
		   solver_isunique_incol(usage, i, val, &idx))
		{
			#ifdef STANDALONE_SOLVER
			if(solver_show_working)
				printf("unique val in col %d\n"
					   "\tplacing %d at idx %d\n", i, val, idx);
			#endif
			solver_place(usage, idx, val);
			res = TRUE;
		}
	}
	}while(res);
	
	res = TRUE;
	
	/* Final check for board validity */
	if(!check_valid(usage->grid, edges, usage->wh))
		res = FALSE;
	
	for(int i = 0; i < usage->wh; i++)
	if(usage->row[i] > 0 || usage->col[i])
		res = FALSE;
	
	#ifdef STANDALONE_SOLVER
	if(solver_show_working) {
		if(solver_show_elimination)
		{
			printvalues(usage);
			printf("\n");
		}
		if(!res)
			printf("solution not found.\n");
		printresult(usage);
	}
	#endif
	
	return res;
}

#ifdef NEW_GAME_DESC_VERSION_1
/* Generate random edges
 * first ensure that at least one of each A, B and C is present,
 * then fill the rest of the cells in */
 
int cmpfunc (const void * a, const void * b) {
   return (*(int*)a - *(int*)b);
}

static void random_edges(const game_params *params, random_state *rs, 
						struct edges *edges) {
	static byte vals_arr[] = {MASK_A, MASK_B, MASK_C}; // Shuffle ABC for random generation
	static int shuf_idx[3];
	 
	shuffle(vals_arr, 3, sizeof(byte), rs);
	shuf_idx[0] = random_upto(rs, params->wh);
	while((shuf_idx[1] = random_upto(rs, params->wh)) == shuf_idx[0])
		;
	while((shuf_idx[2] = random_upto(rs, params->wh)) == shuf_idx[1] || shuf_idx[2] == shuf_idx[0])
		;
	for(int i = 0; i < 3; i++)
		edges->top[shuf_idx[i]] = vals_arr[i];
	
	shuffle(vals_arr, 3, sizeof(byte), rs);
	shuf_idx[0] = random_upto(rs, params->wh);
	while(vals_arr[0] == edges->top[shuf_idx[0]])
		shuf_idx[0] = random_upto(rs, params->wh);
	while((shuf_idx[1] = random_upto(rs, params->wh)) == shuf_idx[0] || \
		   vals_arr[1] == edges->top[shuf_idx[1]])
		shuf_idx[1] = random_upto(rs, params->wh);
	while((shuf_idx[2] = random_upto(rs, params->wh)) == shuf_idx[1] || shuf_idx[2] == shuf_idx[0]
		  || vals_arr[2] == edges->top[shuf_idx[2]])
		shuf_idx[2] = random_upto(rs, params->wh);
	
	for(int i = 0; i < 3; i++)
	edges->bottom[shuf_idx[i]] = vals_arr[i];
	
	for(int i = 0; i < params->wh; i++)
	if(edges->top[i] == 0)
	{
		shuffle(vals_arr, 3, sizeof(byte), rs);
		if((edges->top[i] = vals_arr[0]) == edges->bottom[i])
				edges->top[i] = vals_arr[1];
	}
	
	for(int i = 0; i < params->wh; i++)
	if(edges->bottom[i] == 0)
	{
		shuffle(vals_arr, 3, sizeof(byte), rs);
		if((edges->bottom[i] = vals_arr[0]) == edges->top[i])
			edges->bottom[i] = vals_arr[1];
	}
	
	vals_arr[0] = edges->top[0];
	vals_arr[2] = edges->bottom[0];
	vals_arr[1] = MASK_ABC ^ (vals_arr[0] | vals_arr[2]);
	
	shuf_idx[0] = random_upto(rs, params->wh);
	while((shuf_idx[1] = random_upto(rs, params->wh)) == shuf_idx[0])
		;
	while((shuf_idx[2] = random_upto(rs, params->wh)) == shuf_idx[1] || shuf_idx[2] == shuf_idx[0])
		;
	
	qsort(shuf_idx, 3, sizeof(int), cmpfunc);
	for(int i = 0; i < 3; i++)
		edges->left[shuf_idx[i]] = vals_arr[i];
	for(int i = 0; i < params->wh; i++)
	{
		if(edges->left[i] == 0)
		{
			shuffle(vals_arr, 3, sizeof(byte), rs);
			edges->left[i] = vals_arr[0];
		}
	}

	for(int i = 0; i < params->wh; i++)
	{
		if(edges->right[i] == 0)
		{
			shuffle(vals_arr, 3, sizeof(byte), rs);
			if((edges->right[i] = vals_arr[0]) == edges->left[i])
				edges->right[i] = vals_arr[1];
		}
	}
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive) {
	int wh = params->wh;
	int area = params->wh*params->wh;
	
	int	desc_len = 4*params->wh;
	char *desc = snewn(desc_len, char);
	
	struct edges edges;
	edges.top = desc;
	edges.bottom = edges.top + wh;
	edges.left = edges.top + 2*wh;
	edges.right = edges.top + 3*wh;
	
	#ifdef STANDALONE_SOLVER
	int idcounter = 0;
	#endif
	
	struct solver_usage *usage = new_solver_usage(params);
	while(1)
	{
		#ifdef STANDALONE_SOLVER
		idcounter++;
		#endif
		memset(desc, 0, desc_len);
		random_edges(params, rs, &edges);
		if(solver(usage, &edges))
			break;
		memset(usage->grid, 0, area);
		memset(usage->values, MASK_ABC, area);
		memset(usage->row, MASK_ABC, wh);
		memset(usage->col, MASK_ABC, wh);
	}
	
	#if 0 // BUG: solve doesn't display correctly but works after entering the same game id
	if (*aux)
		sfree(*aux);
	*aux = snewn(area+2, char);
	**aux = 'S';
	memcpy((*aux)+1, usage->grid, area);
	(*aux)[area+1] = '\0'; 
	#endif 
	
	free_solver_usage(usage);
	
	for(int i = 0; i < desc_len; i++)
		desc[i] += 'A' - 1 - ((desc[i] == MASK_C) ? 1 : 0);

	#ifdef STANDALONE_SOLVER
	printf("idcounter:%d\n", idcounter);
	#endif
	
	return desc;
}
#endif

#ifdef NEW_GAME_DESC_VERSION_2
#include "latin.h"
static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, int interactive)
{
	int wh = params->wh;
	int area = wh*wh;
	int	desc_len = 4*params->wh;
	
	char *desc = snewn(desc_len, char);
	byte *grid = NULL;
	
	struct edges edges;
	edges.top = desc;
	edges.bottom = edges.top + wh;
	edges.left = edges.top + 2*wh;
	edges.right = edges.top+ 3*wh;
	
	struct solver_usage *usage = new_solver_usage(params);
	int idcounter = 0;
	while(1)
	{
		idcounter++;

		if(!grid)
			sfree(grid);
		grid = latin_generate(wh, rs);
	
		for(int i = 0; i < wh*wh; i++)
		if(grid[i] != MASK_A && grid[i] != MASK_B && grid[i] != MASK_C)
		grid[i] = 0;
	
		
		/* Read the closest values to each edge */
		byte enc, val, valc; /* values encounter in the row or col so far */
 		for(int r = 0; r < wh; r++)
		{
			enc = 0, val = 0, valc = 0;
			int c = 0;
			while(valc < 2)
			{
				if((val = grid[r*wh+c]) > 0)
				{
					enc |= val;
					if(valc == 0)
						edges.left[r] = val;
					valc++;
				}
				c++;
			}
			edges.right[r] = MASK_ABC ^ enc;
		}
		
		for(int c = 0; c < wh; c++)
		{
			enc = 0, val = 0, valc = 0;
			int r = 0;
			while(valc < 2)
			{
				if((val = grid[r*wh+c]) > 0)
				{
					enc |= val;
					if(valc == 0)
						edges.top[c] = val;
					valc++;
				}
				r++;
			}
			edges.bottom[c] = MASK_ABC ^ enc;
		}
		
		if(solver(usage, &edges))
		{
			printf("idcounter:%d\n", idcounter);
			break;	/* the solution is unique */
		}
		
		memset(usage->grid, 0, wh*wh);
		memset(usage->values, MASK_ABC, wh*wh);
		memset(usage->row, MASK_ABC, params->wh);
		memset(usage->col, MASK_ABC, params->wh);
	}
	
	#if 0 // BUG: solve doesn't display correctly but works after entering the same game id
	if (*aux)
		sfree(*aux);
	*aux = snewn(area+2, char);
	**aux = 'S';
	memcpy((*aux)+1, usage->grid, area);
	(*aux)[area+1] = '\0'; 
	#endif 

	free_solver_usage(usage);
		
	for(int i = 0; i < desc_len; i++)
		desc[i] += 'A' - 1 - ((desc[i] == MASK_C) ? 1 : 0);
	
	return desc;
}
#endif

static char *validate_desc(const game_params *params, const char *desc)
{
	int i = 0;
	for(i = 0; (i < 4*params->wh) && desc[i] != '\0'; i++)
	if(desc[i] != 'A' && desc[i] != 'B' && desc[i] != 'C')
		return "Only As, Bs and Cs are allowed in game description.";
	
	if(i < 4*params->wh)
		return "Game description is too short.";
	
	return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
	game_state *state = snew(game_state);
	
	int wh, area;
	state->wh = wh = params->wh;
	area = wh*wh;
	
	state->edges.top = snewn(4*wh, byte);
	state->edges.bottom = state->edges.top + wh;
	state->edges.left = state->edges.top + 2*wh;
	state->edges.right = state->edges.top + 3*wh;
	
	for(int i = 0; i < 4*wh; i++)
		state->edges.top[i] = desc[i] - ('A' - 1) + ((desc[i] == 'C') ? 1 : 0);
	
	state->grid = snewn(area, byte);
	memset(state->grid, 0, area);
	state->pencil = snewn(area, byte);
	memset(state->pencil, 0, area);
	
	state->completed = state->cheated = FALSE;
	
	return state;
}

static game_state *dup_game(const game_state *state)
{
	game_state *ret = snew(game_state);
	
	int wh, area;
	ret->wh = wh = state->wh;
	area = wh*wh;
	
	ret->edges.top = snewn(4*wh, byte);
	ret->edges.bottom = state->edges.top + wh;
	ret->edges.left = state->edges.top + 2*wh;
	ret->edges.right = state->edges.top + 3*wh;
	memcpy(ret->edges.top, state->edges.top, 4*wh);
	
	ret->grid = snewn(area, byte);
	memcpy(ret->grid, state->grid, area);
	ret->pencil = snewn(area, byte);
	memcpy(ret->pencil, state->pencil, area);
	
	ret->completed = state->completed;
	ret->cheated = state->cheated;
	
	return ret;
}

static void free_game(game_state *state)
{
	sfree(state->edges.top);
	sfree(state->grid);
	sfree(state->pencil);
	sfree(state);
}
		
static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, char **error)
{
	char *ret;
	int wh = state->wh;
	
	if (aux)
       return dupstr(aux);
	
	ret	= snewn(wh*wh+2, char);
	ret[0] = 'S';
	ret[wh*wh+1] = '\0';
	
	*error = NULL;
	
	game_params params; // faking params for solver_usage
	params.wh = wh;
	
	struct solver_usage *usage = new_solver_usage(&params);
	
	if(!solver(usage, &state->edges))
	{
		*error = "Solution not found.";
		free_solver_usage(usage);
		return NULL;
	}
	
	memcpy(ret+1, usage->grid, wh*wh);
	
	free_solver_usage(usage);
	
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

struct game_ui {
    int hx, hy;
    bool hshow, hpencil, hcursor;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->hx = ui->hy = -1;
    ui->hpencil = ui->hshow = ui->hcursor = 0;

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
	int wh = newstate->wh;
    /* Prevent pencil-mode highlighting */
    if (ui->hshow && ui->hpencil && !ui->hcursor &&
        newstate->grid[ui->hy * wh + ui->hx] != 0) {
        ui->hshow = 0;
    }
}

#define PREFERRED_TILE_SIZE 48
#define FLASH_TIME 0.2F
#define TILE_SIZE (ds->tilesize)
#define BORDER (TILE_SIZE / 2)
#define GRIDEXTRA max((TILE_SIZE / 32),1)
struct game_drawstate {
    bool started;
    int wh;
    int tilesize;
	struct edges edges;
    byte *grid;
    byte *pencil;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int wh = state->wh;
    int tx, ty;
    char buf[80];
	
	button &= ~MOD_MASK;

    tx = (x + TILE_SIZE - BORDER) / TILE_SIZE - 2;
    ty = (y + TILE_SIZE - BORDER) / TILE_SIZE - 2;
	
    if (tx >= 0 && tx < wh && ty >= 0 && ty < wh) {
        if (button == LEFT_BUTTON) {
			if (tx == ui->hx && ty == ui->hy &&
                       ui->hshow && ui->hpencil == 0) {
                ui->hshow = 0;
            } else {
                ui->hx = tx;
                ui->hy = ty;
                ui->hshow = 1;
                ui->hpencil = 0;
            }
            ui->hcursor = 0;
            return "";		       /* UI activity occurred */
        }
        if (button == RIGHT_BUTTON) {
            /*
             * Pencil-mode highlighting for non filled squares.
             */
            if (state->grid[ty*wh+tx] == 0) {
                if (tx == ui->hx && ty == ui->hy &&
                    ui->hshow && ui->hpencil) {
                    ui->hshow = 0;
                } else {
                    ui->hpencil = 1;
                    ui->hx = tx;
                    ui->hy = ty;
                    ui->hshow = 1;
                }
            } else {
                ui->hshow = 0;
            }
            ui->hcursor = 0;
            return "";		       /* UI activity occurred */
        }
    }
    
	if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->hx, &ui->hy, wh, wh, 0);
        ui->hshow = ui->hcursor = 1;
        return "";
    }
	
    if (ui->hshow &&
        (button == CURSOR_SELECT)) {
        ui->hpencil = 1 - ui->hpencil;
        ui->hcursor = 1;
        return "";
    }

    if (ui->hshow &&
	   ((button >= 'A' && button <= 'C')||
	    (button >= 'a' && button <= 'c')||
	     button == CURSOR_SELECT2 || button == '\b')) {
		int n;
		if (button >= 'A' && button <= 'C')
			n = button - 'A'+1;
		if (button >= 'a' && button <= 'c')
			n = button - 'a'+1;
		if (button == CURSOR_SELECT2 || button == '\b')
			n = 0;

        /*
         * Can't make pencil marks in a filled square. Again, this
         * can only become highlighted if we're using cursor keys.
         */
        if (ui->hpencil && state->grid[ui->hy*wh+ui->hx])
            return NULL;

		sprintf(buf, "%c%d,%d,%d",
			    (char)(ui->hpencil && n > 0 ? 'P' : 'R'), ui->hx, ui->hy, n);

        if (!ui->hcursor && !ui->hpencil) 
			ui->hshow = 0;
	

		return dupstr(buf);
    }
	
	if(ui->hshow && (button == 'X' || button == 'x'))
	{
		sprintf(buf, "%c%d,%d", (char)('X'), ui->hx, ui->hy);
		return dupstr(buf);
	}

    if (button == 'M' || button == 'm')
        return dupstr("M");

    return NULL;
}

static game_state *execute_move(const game_state *from, const char *move)
{
    int wh = from->wh;
    game_state *ret;
    int x, y, n;

    if (move[0] == 'S') {
		const char *p;
		
		ret = dup_game(from);
		ret->completed = ret->cheated = TRUE;
		
		p = move+1;
	
		memcpy(ret->grid, p, wh*wh);

		return ret;
    } else if ((move[0] == 'P' || move[0] == 'R') &&
				sscanf(move+1, "%d,%d,%d", &x, &y, &n) == 3 &&
				x >= 0 && x < wh && y >= 0 && y < wh && n >= 0 && n <= 3) {
		ret = dup_game(from);
        if (move[0] == 'P' && n > 0)
            ret->pencil[y*wh+x] ^= 1 << (n-1);
        else {
            ret->grid[y*wh+x] = 1 << (n-1);
            ret->pencil[y*wh+x] = 0;
			
            if (!ret->completed && check_valid(ret->grid, &ret->edges, wh))
                ret->completed = TRUE;
		}
		
		return ret;
    } else if(move[0] == 'X' && sscanf(move+1, "%d,%d", &x, &y) == 2 &&
			x >= 0 && x < wh && y >= 0 && y < wh) {
		ret = dup_game(from);
		ret->grid[y*wh+x] = 0;
		ret->pencil[y*wh+x] = 8;
		
		return ret;
	} else if(move[0] == 'M') {
		ret = dup_game(from);
		for(int i = 0; i < wh*wh; i++)
		if(!ret->grid[i] && !(ret->pencil[i] & MASK_X))
			ret->pencil[i] = MASK_ABC;
		
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
	*x = *y = (params->wh + 3) * tilesize;
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
	
	ret[COL_EDGE * 3 + 0] = 1.0F;
    ret[COL_EDGE * 3 + 1] = 0.8F;
    ret[COL_EDGE * 3 + 2] = 0.0F;

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_LETTER * 3 + 0] = 0.0F;
    ret[COL_LETTER * 3 + 1] = 0.0F;
    ret[COL_LETTER * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.78F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.78F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.78F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_PENCIL * 3 + 2] = ret[COL_BACKGROUND * 3 + 2];
	
	ret[COL_XMARK * 3 + 0] = 0.5F;
	ret[COL_XMARK * 3 + 1] = 0.5F;
	ret[COL_XMARK * 3 + 2] = 0.5F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
	int wh = state->wh;

	ds->tilesize = 0;
	ds->started = FALSE;
    ds->wh = wh;
    ds->grid = snewn(wh*wh, byte);
	ds->pencil = snewn(wh*wh, byte);
    memset(ds->grid, MASK_ABC+1, wh*wh); /* Force redraw */
	memset(ds->pencil, 0, wh*wh);
	
	ds->edges.top = snewn(4*wh, byte);
	ds->edges.bottom = ds->edges.top + wh;
	ds->edges.left = ds->edges.top + 2*wh;
	ds->edges.right = ds->edges.top + 3*wh;
	memset(ds->edges.top, 0, 4*wh);
	
    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
	sfree(ds->pencil);
	sfree(ds->edges.top);
    sfree(ds);
}

static void draw_edges(drawing *dr, game_drawstate *ds,
                        const game_state *state)
{
	int wh = state->wh;
    int tx, ty, tw, th;
	tw = th = TILE_SIZE-1;
	
	char str[2];
	str[1] = '\0';
	for(int i=0; i<wh; i++)
	{
		tx = BORDER + (i+1)*TILE_SIZE + 1;
		
		/* Top */
		ty = BORDER - GRIDEXTRA + 1;
		clip(dr, tx, ty, tw, th);
		draw_rect(dr, tx, ty, tw, th, COL_EDGE);
		str[0] = state->edges.top[i] + 'A'-1;
		if(str[0] == 'D')
			str[0] = 'C';
		draw_text(dr, tx + tw/2, ty + th/2,
		  FONT_VARIABLE, TILE_SIZE*2/3, ALIGN_HCENTRE | ALIGN_VCENTRE,
		  COL_LETTER, str);
		unclip(dr);
		
		/* Bottom */
		ty = BORDER + (wh+1)*TILE_SIZE + GRIDEXTRA + 1;
		clip(dr, tx, ty, tw, th);
		draw_rect(dr, tx, ty, tw, th, COL_EDGE);
		str[0] = state->edges.bottom[i] + 'A'-1;
		if(str[0] == 'D')
			str[0] = 'C';
		draw_text(dr, tx + tw/2, ty + th/2,
		  FONT_VARIABLE, TILE_SIZE*2/3, ALIGN_HCENTRE | ALIGN_VCENTRE,
		  COL_LETTER, str);
		unclip(dr);

		ty = BORDER + (i+1)*TILE_SIZE + 1;
		
		/* Left */
		tx = BORDER - GRIDEXTRA +1 ;
		clip(dr, tx, ty, tw, th);
		draw_rect(dr, tx, ty, tw, th, COL_EDGE);
		str[0] = state->edges.left[i] + 'A'-1;
				if(str[0] == 'D')
			str[0] = 'C';
		draw_text(dr, tx + tw/2, ty + th/2,
		  FONT_VARIABLE, TILE_SIZE*2/3, ALIGN_HCENTRE | ALIGN_VCENTRE,
		  COL_LETTER, str);
		unclip(dr);
		
		/* Right */
		tx = BORDER + (wh+1)*TILE_SIZE + GRIDEXTRA + 1;
		clip(dr, tx, ty, tw, th);
		draw_rect(dr, tx, ty, tw, th, COL_EDGE);
				str[0] = state->edges.right[i] + 'A'-1;
						if(str[0] == 'D')
			str[0] = 'C';
		draw_text(dr, tx + tw/2, ty + th/2,
		  FONT_VARIABLE, TILE_SIZE*2/3, ALIGN_HCENTRE | ALIGN_VCENTRE,
		  COL_LETTER, str);
		unclip(dr);
	}
}

static void draw_user_letter(drawing *dr, game_drawstate *ds,
                        const game_state *state, int x, int y)
{
	int wh = state->wh;
    int tx, ty, tw, th;
    char str[4];
	
	tx = BORDER + (x+1)*TILE_SIZE+1;// + GRIDEXTRA;
    ty = BORDER + (y+1)*TILE_SIZE+1;// + GRIDEXTRA;
	tw = TILE_SIZE-1;
    th = TILE_SIZE-1;
	
	clip(dr, tx, ty, tw, th);
	
	/* background needs erasing */
    draw_rect(dr, tx, ty, tw, th, (ds->grid[y*wh+x] & MASK_CURSOR) ? COL_HIGHLIGHT : COL_BACKGROUND);
	
	/* pencil-mode highlight */
    if (ds->grid[y*wh+x] & MASK_PENCIL) {
        int coords[6];
        coords[0] = tx;
        coords[1] = ty;
        coords[2] = tx+tw/2;
        coords[3] = ty;
        coords[4] = tx;
        coords[5] = ty+th/2;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    } 
	
	if (state->grid[y*wh+x]) {
		str[1] = '\0';
		str[0] = state->grid[y*wh+x] + 'A' - 1;
		if(str[0] == 'D')
			str[0] = 'C';
		draw_text(dr, tx + tw/2, ty + th/2,
		  FONT_VARIABLE, TILE_SIZE*2/3, ALIGN_VCENTRE | ALIGN_HCENTRE,
		  COL_LETTER, str);
    } else if(state->pencil[y*wh+x] & MASK_X) {
			draw_text(dr, tx + tw/2, ty + th/2,
				FONT_VARIABLE, TILE_SIZE*2/3, ALIGN_VCENTRE | ALIGN_HCENTRE,
				COL_XMARK, "X");
	} else {
        /* Count the pencil marks required */
		byte npencil = 0;
		byte pencil = state->pencil[y*wh+x];
	
		if(pencil & MASK_A)
		str[npencil++] = 'A';
		
		if(pencil & MASK_B)
		str[npencil++] = 'B';
		
		if(pencil & MASK_C)
		str[npencil++] = 'C';
		
		str[npencil] = '\0';
		
		if (npencil)
		draw_text(dr, tx + tw/8, ty + th/4,
			FONT_VARIABLE, TILE_SIZE/4, ALIGN_HLEFT | ALIGN_VCENTRE,
			COL_PENCIL, str);
    }
	
    unclip(dr);

    draw_update(dr, tx, ty, tw, th);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int wh = state->wh;
    int x, y;

    if (!ds->started) {
		draw_rect(dr, 0, 0, TILE_SIZE*(wh+3), TILE_SIZE*(wh+3), COL_BACKGROUND);

		draw_rect(dr, BORDER-2*GRIDEXTRA, BORDER-2*GRIDEXTRA,
			(wh+2)*TILE_SIZE+4*GRIDEXTRA+1, (wh+2)*TILE_SIZE+4*GRIDEXTRA+1,
			COL_GRID);
		
		draw_rect(dr, BORDER-2*GRIDEXTRA, BORDER-2*GRIDEXTRA, TILE_SIZE+GRIDEXTRA, TILE_SIZE+GRIDEXTRA, COL_BACKGROUND);
		draw_rect(dr, BORDER-2*GRIDEXTRA, BORDER+GRIDEXTRA+TILE_SIZE*(wh+1)+1, TILE_SIZE+GRIDEXTRA, TILE_SIZE+GRIDEXTRA, COL_BACKGROUND);
		draw_rect(dr, BORDER+GRIDEXTRA+TILE_SIZE*(wh+1)+1, BORDER-2*GRIDEXTRA, TILE_SIZE+GRIDEXTRA, TILE_SIZE+GRIDEXTRA, COL_BACKGROUND);
		draw_rect(dr, BORDER+GRIDEXTRA+TILE_SIZE*(wh+1)+1, BORDER+GRIDEXTRA+TILE_SIZE*(wh+1)+1, TILE_SIZE+GRIDEXTRA, TILE_SIZE+GRIDEXTRA, COL_BACKGROUND);
			
		draw_edges(dr, ds, state);
		  
		draw_update(dr, 0, 0,
                    TILE_SIZE * (wh+3), TILE_SIZE * (wh+3));
		
		ds->started = TRUE;
	}
	
    for (x = 0; x < wh; x++) {
	for (y = 0; y < wh; y++) {
        byte cell = state->grid[y*wh+x];
			
		if (x == ui->hx && y == ui->hy)
			cell |= ui->hpencil ? MASK_PENCIL : MASK_CURSOR; // store highlight information in a grid cell
		
		if (flashtime > 0 && 
			(flashtime <= FLASH_TIME/3 ||
			 flashtime >= FLASH_TIME*2/3))
            cell |= MASK_CURSOR;

		if (ds->grid[y*wh+x] == cell &&
			ds->pencil[y*wh+x] == state->pencil[y*wh+x])
		continue;

		ds->grid[y*wh+x] = cell;
		ds->pencil[y*wh+x] = state->pencil[y*wh+x];
			
		draw_user_letter(dr, ds, state, x, y);
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

static int game_timing_state(const game_state *state, game_ui *ui)
{
    if (state->completed)
		return FALSE;
    return TRUE;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
}

#ifdef COMBINED
#define thegame abc
#endif

const struct game thegame = {
    "ABC", "games.abc", "abc",
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
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
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
    REQUIRE_RBUTTON
};

#ifdef STANDALONE_SOLVER

#include <time.h>

int main(int argc, char **argv)
{
	bool random = false;
	
	time_t seed = time(NULL);
		
    game_params *params = NULL;
    game_state *state = NULL;
	
    char *id = NULL, *desc = NULL;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v"))
            solver_show_working = TRUE;
		else if (!strcmp(p, "-e"))
            solver_show_elimination = TRUE;
		else if(!strcmp(p, "-g"))
			random = true;
		else if (*p == '-') {
            printf("%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }
	
	if (!id && !random) {
        printf("usage: %s [-v | -e] <game_id> OR -g [<game_params>]\n", argv[0]);
        return 1;
    }
	
	params = default_params();
	decode_params(params, id);
	
	if(random) {
		random_state *rs = random_new((void *)&seed, sizeof(time_t));
		desc = new_game_desc(params, rs, NULL, 0);
		
		printf("game id: ");
		printf("%d:%.*s\n",params->wh, 4*params->wh, desc);
		return 0;
	} else {
		desc = strchr(id, ':');
		if (!desc) {
			printf("%s: game id expects a colon in it\n", argv[0]);
			return 1;
		}
		
		desc++;
		char *vd = validate_desc(params, desc);	
		if(vd)
		{
			printf(vd);
			return 1;
		}
	}
	state = new_game(NULL, params, desc);
	
	struct solver_usage *usage = new_solver_usage(params);
	solver(usage, &state->edges);
	
	free_solver_usage(usage);
	free_params(params);
	free_game(state);
	
	return 0;
}
#endif
