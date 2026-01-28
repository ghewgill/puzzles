/*
 * subsets.c: Implementation of Subsets puzzles.
 * (C) 2025 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective: Place every given set into the grid exactly once.
 * - A horseshoe symbol points from a superset to a subset.
 * - All possible horseshoe symbols are given.
 *
 * This puzzle type was invented by Inaba Naoki.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#ifdef NO_TGMATH_H
#include <math.h>
#else
#include <tgmath.h>
#endif

#include "puzzles.h"

#ifdef STANDALONE_SOLVER
#include <stdarg.h>
int solver_verbose = false;

void solver_printf(char *fmt, ...)
{
    if (!solver_verbose)
        return;
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

enum
{
    COL_OUTERBG,
    COL_INNERBG,
    COL_GRID,
    COL_HIGHLIGHT,
    COL_LOWLIGHT,

    COL_FIXED,
    COL_GUESS,
    COL_ERROR,

    COL_CURSOR,
    NCOLOURS
};

#define ALL_BITS(n) ((1 << (n)) - 1)
#define F_ADJ_UP 1
#define F_ADJ_RIGHT 2
#define F_ADJ_DOWN 4
#define F_ADJ_LEFT 8

static const struct
{
    unsigned int f, fo;
    int dx, dy;
    char c, enc;
} adjthan[] = {
    {F_ADJ_UP, F_ADJ_DOWN, 0, -1, '^', 'U'},
    {F_ADJ_RIGHT, F_ADJ_LEFT, 1, 0, '>', 'R'},
    {F_ADJ_DOWN, F_ADJ_UP, 0, 1, 'v', 'D'},
    {F_ADJ_LEFT, F_ADJ_RIGHT, -1, 0, '<', 'L'}};

/* TODO: When other sizes are supported, read n instead of returning a constant */
#define CELL_WIDTH(n) (2)
#define CELL_HEIGHT(n) (2)

struct game_params
{
    int w, h, n;
};

struct game_state
{
    int w, h, n;

    unsigned int *clues;

    unsigned int *immutable;
    unsigned int *known;
    unsigned int *mask;

    bool completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = ret->n = 4;

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    if (i != 0)
        return false;

    *name = dupstr("4x4 Size 4");
    *params = default_params();
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
    params->w = params->h = atoi(string);
    while (*string && isdigit((unsigned char)*string))
        ++string;

    if (*string == 'x')
    {
        ++string;
        params->h = atoi(string);
    }
    while (*string && isdigit((unsigned char)*string))
        ++string;

    if (*string == 'n')
    {
        ++string;
        params->n = atoi(string);
    }
    while (*string && isdigit((unsigned char)*string))
        ++string;
}

static char *encode_params(const game_params *params, bool full)
{
    char data[80];
    sprintf(data, "%dx%dn%d", params->w, params->h, params->n);

    return dupstr(data);
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w != 4 || params->h != 4 || params->n != 4)
        return "Currently only 4x4 puzzles are supported";

    return NULL;
}

static void free_game(game_state *state)
{
    sfree(state->clues);
    sfree(state->immutable);
    sfree(state->known);
    sfree(state->mask);
    sfree(state);
}

static const char *attempt_load_game(game_state *state, const char *desc)
{
    const char *p = desc;
    int w = state->w, h = state->w, n = state->n;
    int i = 0, x, y;
    unsigned int num;

    while (*p)
    {
        if (i >= w * h)
            return "Too much data to fill grid";

        if (*p >= '0' && *p <= '9')
        {
            num = (unsigned int)atoi(p);
            if (num < 0 || num > ALL_BITS(n))
                return "Out-of-range number in game description";

            state->known[i] = num;
            state->mask[i] = num;
            state->immutable[i] = ALL_BITS(n);

            while (*p >= '0' && *p <= '9')
                p++; /* skip number */
        }
        else if (*p == '_')
            p++;
        else
            return "Expecting number in game description";

        while (*p == 'U' || *p == 'R' || *p == 'D' || *p == 'L')
        {
            switch (*p)
            {
            case 'U':
                state->clues[i] |= F_ADJ_UP;
                break;
            case 'R':
                state->clues[i] |= F_ADJ_RIGHT;
                break;
            case 'D':
                state->clues[i] |= F_ADJ_DOWN;
                break;
            case 'L':
                state->clues[i] |= F_ADJ_LEFT;
                break;
            default:
                return "Expecting flag URDL in game description";
            }
            p++;
        }
        i++;
        if (i < w * h && *p != ',')
            return "Missing separator";
        if (*p == ',')
            p++;
    }
    if (i < w * h)
        return "Not enough data to fill grid";

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            for (i = 0; i < 4; i++)
            {
                if (state->clues[y * w + x] & adjthan[i].f)
                {
                    int nx = x + adjthan[i].dx;
                    int ny = y + adjthan[i].dy;
                    /* a flag must not point us off the grid. */
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        return "Flags go off grid";

                    if (state->clues[ny * w + nx] & adjthan[i].fo)
                        return "Flags contradicting each other";
                }
            }
        }
    }

    return NULL;
}

static game_state *blank_game(const game_params *params)
{
    game_state *state = snew(game_state);

    int w = params->w, h = params->h, n = params->n;
    state->w = w;
    state->h = h;
    state->n = n;
    int s = w * h;
    int i;

    state->clues = snewn(s, unsigned int);
    state->immutable = snewn(s, unsigned int);
    state->known = snewn(s, unsigned int);
    state->mask = snewn(s, unsigned int);

    state->completed = state->cheated = false;

    for (i = 0; i < s; i++)
    {
        state->clues[i] = 0;
        state->immutable[i] = 0;
        state->known[i] = 0;
        state->mask[i] = ALL_BITS(n);
    }
    return state;
}

static game_state *load_game(const game_params *params,
                             const char *desc, const char **failure)
{
    const char *result = NULL;
    game_state *state = blank_game(params);

    result = attempt_load_game(state, desc);
    if (result)
    {
        if (failure)
            *failure = result;
        free_game(state);
        return NULL;
    }

    return state;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = load_game(params, desc, NULL);
    if (!state)
    {
        assert(!"Unable to load validated game.");
        return NULL;
    }
    return state;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    const char *failure = NULL;
    game_state *dummy = load_game(params, desc, &failure);
    if (dummy)
    {
        free_game(dummy);
        assert(!failure);
    }
    else
        assert(failure);
    return failure;
}

static game_state *dup_game(const game_state *state)
{
    int s = state->w * state->h;
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->n = state->n;

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    ret->clues = snewn(s, unsigned int);
    ret->immutable = snewn(s, unsigned int);
    ret->known = snewn(s, unsigned int);
    ret->mask = snewn(s, unsigned int);

    memcpy(ret->clues, state->clues, s * sizeof(unsigned int));
    memcpy(ret->immutable, state->immutable, s * sizeof(unsigned int));
    memcpy(ret->known, state->known, s * sizeof(unsigned int));
    memcpy(ret->mask, state->mask, s * sizeof(unsigned int));

    return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->w;
    int h = state->h;
    int n = state->n;
    int x, y, cx, cy, cn;

    int cw = CELL_WIDTH(n);
    int ch = CELL_HEIGHT(n);

    char *ret = snewn(((w * (cw + 1)) * (((ch + 1) * h) - 1)) + 1, char);
    char *p = ret;

    for (y = 0; y < h; y++)
    {
        for (cy = 0; cy < ch; cy++)
        {
            for (x = 0; x < w; x++)
            {
                for (cx = 0; cx < cw; cx++)
                {
                    cn = cy * cw + cx;

                    if (cn >= n)
                        *p++ = ' ';
                    else if (state->known[y * w + x] & (1 << cn))
                        *p++ = 'A' + cn;
                    else if (!(state->mask[y * w + x] & (1 << cn)))
                        *p++ = '.';
                    else
                        *p++ = '?';
                }
                if (x < w - 1)
                {
                    *p++ = cy != 0 ? ' ' : state->clues[y * w + x] & F_ADJ_RIGHT  ? '>'
                                       : state->clues[y * w + x + 1] & F_ADJ_LEFT ? '<'
                                                                                  : ' ';
                }
            }

            *p++ = '\n';
        }
        if (y < h - 1)
        {
            for (x = 0; x < w; x++)
            {
                *p++ = state->clues[y * w + x] & F_ADJ_DOWN       ? 'v'
                       : state->clues[(y + 1) * w + x] & F_ADJ_UP ? '^'
                                                                  : ' ';

                for (cx = 1; cx < cw; cx++)
                {
                    *p++ = ' ';
                }
                if (x < w - 1)
                    *p++ = ' ';
            }

            *p++ = '\n';
        }
    }

    *p++ = '\0';

    return ret;
}

enum
{
    STATUS_COMPLETE,
    STATUS_UNFINISHED,
    STATUS_INVALID
};
static char subsets_validate(const game_state *state, unsigned int *flags, int *counts)
{
    int w = state->w;
    int h = state->h;
    int x, y, i, x2, y2, i2, dir, intersect;
    bool hascounts = counts != NULL;

    char ret = STATUS_COMPLETE;

    for (i = 0; i < w * h; i++)
    {
        if (state->known[i] != state->mask[i])
        {
            if (!flags && !hascounts)
                return STATUS_UNFINISHED;
            ret = STATUS_UNFINISHED;
        }
    }

    if (flags)
        memset(flags, 0, w * h * sizeof(int));
    if (!hascounts)
        counts = snewn(w * h, int);
    memset(counts, 0, w * h * sizeof(int));

    /* Validate counts */
    for (i = 0; i < w * h; i++)
    {
        if (state->known[i] == state->mask[i])
        {
            counts[state->known[i]]++;

            if (counts[state->known[i]] > 1)
                ret = STATUS_INVALID;
            if (!flags && ret == STATUS_INVALID)
                break;
        }
    }

    /* Validate arrows */
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            if (ret == STATUS_INVALID && !flags)
                break;

            i = y * w + x;

            if (state->known[i] != state->mask[i])
                continue;

            for (dir = 0; dir < 4; dir++)
            {
                x2 = x + adjthan[dir].dx;
                y2 = y + adjthan[dir].dy;

                if (x2 < 0 || x2 >= w || y2 < 0 || y2 >= h)
                    continue;

                i2 = y2 * w + x2;
                if (state->known[i2] != state->mask[i2])
                    continue;

                /* Validate disjoint pairs only once */
                if (!(state->clues[i] & adjthan[dir].f) && (x2 < x || y2 < y))
                    continue;

                intersect = state->known[i] & state->known[i2];

                if (state->clues[i] & adjthan[dir].f)
                {
                    if (intersect != state->known[i2])
                    {
                        ret = STATUS_INVALID;
                        if (flags)
                            flags[i] |= adjthan[dir].f;
                    }
                }
                else if (!(state->clues[i2] & adjthan[dir].fo))
                {
                    if (intersect == state->known[i2] || intersect == state->known[i])
                    {
                        ret = STATUS_INVALID;
                        if (flags)
                            flags[i] |= adjthan[dir].f;
                    }
                }
            }
        }
    }

    if (!hascounts)
        sfree(counts);

    return ret;
}

/* ****** *
 * Solver *
 * ****** */
static void subsets_sync_cube(const game_state *state, bool *cube)
{
    const int s = state->w * state->h, n = state->n, n2 = 1 << n;
    int i, nj;

    for (i = 0; i < s; i++)
    {
        for (nj = 0; nj < n2; nj++)
        {
            if (!cube[i * n2 + nj])
                continue;

            if ((state->mask[i] & nj) != nj)
            {
                solver_printf("\x1B[0;36mRemoving possibility %d from space %d due to missing mask\x1B[0m\n", nj, i);
                cube[i * n2 + nj] = false;
            }

            if ((state->known[i] & nj) != state->known[i])
            {
                solver_printf("\x1B[0;36mRemoving possibility %d from space %d due to confirmed bits\x1B[0m\n", nj, i);
                cube[i * n2 + nj] = false;
            }
        }
    }
}

static void subsets_cube_single_count(const game_state *state, const int *counts, bool *cube)
{
    const int s = state->w * state->h, n = state->n, n2 = 1 << n;
    int j, ni;

    for (ni = 0; ni < n2; ni++)
    {
        if (counts[ni] != 1)
            continue;

        for (j = 0; j < s; j++)
        {
            if (state->mask[j] == state->known[j])
                continue;

            if (!cube[j * n2 + ni])
                continue;

            solver_printf("\x1B[0;36mRemoving possibility %d from space %d due to being located elsewhere\x1B[0m\n", ni, j);
            cube[j * n2 + ni] = false;
        }
    }
}

static int subsets_solve_apply_arrows(game_state *state)
{
    const int w = state->w, h = state->h, n = state->n;
    int x, y, d, i1, i2, prev;
    int ret = 0;

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            for (d = 0; d < 4; d++)
            {
                i1 = y * w + x;
                if (!(state->clues[i1] & adjthan[d].f))
                    continue;
                i2 = i1 + (adjthan[d].dy * w) + adjthan[d].dx;

                prev = state->known[i1];
                state->known[i1] |= state->known[i2];
                if (prev != state->known[i1])
                {
                    solver_printf("\x1B[0;33mArrow pointing to %d confirms bits at %d\x1B[0m\n", i2, i1);
                    ret++;
                }

                prev = state->mask[i2];
                state->mask[i2] &= state->mask[i1];
                if (prev != state->mask[i2])
                {
                    solver_printf("\x1B[0;33mArrow pointing from %d removes bits at %d\x1B[0m\n", i1, i2);
                    ret++;
                }
            }
        }
    }

    return ret;
}

static int subsets_solve_single_position(game_state *state, const int *counts, const bool *cube)
{
    const int s = state->w * state->h, n = state->n, n2 = 1 << n;
    int i, nj, found;
    int ret = 0;

    for (nj = 0; nj < n2; nj++)
    {
        if (counts[nj] != 0)
            continue;

        found = -1;
        for (i = 0; i < s && found != -2; i++)
        {
            if (!cube[i * n2 + nj])
                continue;
            found = found == -1 ? i : -2;
        }

        if (found < 0)
            continue;

        solver_printf("\x1B[0;33mSpace %d must be %d\x1B[0m\n", found, nj);
        state->known[found] = nj;
        state->mask[found] = nj;
        ret++;
    }
    return ret;
}

static int subsets_bits_from_cube(game_state *state, const bool *cube)
{
    const int s = state->w * state->h, n = state->n, n2 = 1 << n;
    int i, prev, nj, newmask, newknown;
    int ret = 0;

    for (i = 0; i < s; i++)
    {
        newmask = 0;
        newknown = ~0;

        for (nj = 0; nj < n2; nj++)
        {
            if (cube[i * n2 + nj])
            {
                newmask |= nj;
                newknown &= nj;
            }
        }

        prev = state->known[i];
        state->known[i] |= newknown;
        if (prev != state->known[i])
        {
            solver_printf("\x1B[0;33mPossibilities at %d confirms bits\x1B[0m\n", i);
            ret++;
        }

        prev = state->mask[i];
        state->mask[i] &= newmask;
        if (prev != state->mask[i])
        {
            solver_printf("\x1B[0;33mPossibilities at %d removes bits\x1B[0m\n", i);
            ret++;
        }
    }
    return ret;
}

static int subsets_solve_apply_arrows_advanced(const game_state *state, bool *cube)
{
    const int w = state->w, h = state->h, n = state->n, n2 = 1 << n;
    int x, y, d, i1, i2, super, sub;
    bool found;
    int ret = 0;

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            for (d = 0; d < 4; d++)
            {
                i1 = y * w + x;
                if (!(state->clues[i1] & adjthan[d].f))
                    continue;
                i2 = i1 + (adjthan[d].dy * w) + adjthan[d].dx;

                /* Remove options that don't contain the smaller set */
                for (super = 0; super < n2; super++)
                {
                    if (!(cube[i1 * n2 + super]))
                        continue;
                    found = false;
                    for (sub = 0; sub < super && !found; sub++)
                    {
                        if ((super & sub) != sub || !(cube[i2 * n2 + sub]))
                            continue;
                        found = true;
                    }

                    if (!found)
                    {
                        solver_printf("\x1B[0;36mRemoving possibility %d from space %d due to not fitting subset at %d\x1B[0m\n", super, i1, i2);
                        cube[i1 * n2 + super] = false;
                        ret++;
                    }
                }

                // TODO repair this

                /* Remove options that don't fit the larger set */
                // for (sub = 0; sub < n2; sub++)
                // {
                //     if (!(cube[i2 * n2 + sub]))
                //         continue;
                //     found = false;
                //     for (super = sub + 1; super < n2 && !found; super++)
                //     {
                //         if ((super & sub) != sub || !(cube[i1 * n2 + super]))
                //             continue;
                //         found = true;
                //     }

                //     if (!found)
                //     {
                //         solver_printf("\x1B[0;36mRemoving possibility %d from space %d due to not fitting superset at %d\x1B[0m\n", sub, i2, i1);
                //         cube[i2 * n2 + sub] = false;
                //         ret++;
                //     }
                // }
            }
        }
    }

    return ret;
}

static int subsets_disjoint(const game_state *state, bool *cube)
{
    const int w = state->w, h = state->h, n = state->n, n2 = 1 << n;
    int x, y, d, i1, i2, opt;
    int ret = 0;

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            for (d = 0; d < 4; d++)
            {
                i1 = y * w + x;

                if (state->clues[i1] & adjthan[d].f)
                    continue;

                if ((x + adjthan[d].dx < 0) || (x + adjthan[d].dx >= w) || (y + adjthan[d].dy < 0) || (y + adjthan[d].dy >= h))
                    continue;

                i2 = i1 + (adjthan[d].dy * w) + adjthan[d].dx;
                if (state->clues[i2] & adjthan[d].fo)
                    continue;

                if (state->known[i1] != state->mask[i1])
                {
                    /* Remove the minimum and maximum sets */
                    if (cube[i1 * n2] || cube[i1 * n2 + (n2 - 1)])
                    {
                        solver_printf("\x1B[0;33m%d is disjoint from %d, removing possibilities 0 and %d\x1B[0m\n", i1, i2, n2 - 1);
                        cube[i1 * n2] = false;
                        cube[i1 * n2 + (n2 - 1)] = false;
                        ret++;
                    }
                }
                else if (state->known[i2] != state->mask[i2])
                {
                    /* Rule out every set in i2 that is not disjoint with the set at i1 */

                    for (opt = 0; opt < n2; opt++)
                    {
                        if (!cube[i2 * n2 + opt])
                            continue;

                        if ((state->known[i1] & opt) != opt && (state->known[i1] & opt) != state->known[i1])
                            continue;

                        solver_printf("\x1B[0;33mRemoving possibility %d from %d because it overlaps the set at %d \x1B[0m\n", opt, i2, i1);
                        cube[i2 * n2 + opt] = false;
                        ret++;
                    }
                }
            }
        }
    }

    return ret;
}

static char subsets_solve_game(game_state *state)
{
    const int s = state->w * state->h, n = state->n, n2 = 1 << n;
    int i;
    char ret = STATUS_UNFINISHED;
    int *counts = snewn(s, int);
    bool *cube = snewn(s * n2, bool);

    for (i = 0; i < s; i++)
    {
        if (state->immutable[i])
            continue;

        state->known[i] = 0;
        state->mask[i] = ALL_BITS(n);
    }
    for (i = 0; i < s * n2; i++)
        cube[i] = true;

    while ((ret = subsets_validate(state, NULL, counts)) == STATUS_UNFINISHED)
    {
        subsets_sync_cube(state, cube);
        subsets_cube_single_count(state, counts, cube);

        if (subsets_solve_apply_arrows(state))
            continue;

        if (subsets_disjoint(state, cube))
            continue;

        if (subsets_bits_from_cube(state, cube))
            continue;

        if (subsets_solve_single_position(state, counts, cube))
            continue;

        if (subsets_solve_apply_arrows_advanced(state, cube))
            continue;

        break;
    }

    sfree(counts);
    sfree(cube);

    return ret;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    game_state *state = blank_game(params), *solved;
    const int n = state->n, n2 = 1 << n, w = state->w, h = state->h;
    int *spaces = snewn(w * h, int);
    int i, x, y, i2, value, d;
    char *ret, *p;

    for (i = 0; i < n2; i++)
    {
        state->known[i] = i;
        state->immutable[i] = true;
    }
    for (i = 0; i < w * h; i++)
        spaces[i] = i;

    shuffle(state->known, n2, sizeof(int), rs);
    for (i = 0; i < n2; i++)
        state->mask[i] = state->known[i];

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            for (d = 0; d < 4; d++)
            {
                if ((x + adjthan[d].dx < 0) || (x + adjthan[d].dx >= w) || (y + adjthan[d].dy < 0) || (y + adjthan[d].dy >= h))
                    continue;

                i = y * w + x;
                i2 = i + (adjthan[d].dy * w) + adjthan[d].dx;

                if ((state->known[i] & state->known[i2]) == state->known[i2])
                    state->clues[i] |= adjthan[d].f;
            }
        }
    }

    shuffle(spaces, w * h, sizeof(int), rs);
    for (i2 = 0; i2 < w * h; i2++)
    {
        i = spaces[i2];
        value = state->known[i];

        state->immutable[i] = false;

        solved = dup_game(state);

        if (subsets_solve_game(solved) != STATUS_COMPLETE)
            state->immutable[i] = true;

        free_game(solved);
    }

    ret = snewn(w * h * 6, char);
    p = ret;
    for (i = 0; i < w * h; i++)
    {
        if (state->immutable[i])
            p += sprintf(p, "%d", state->known[i]);
        else
            *p++ = '_';

        for (d = 0; d < 4; d++)
        {
            if (state->clues[i] & adjthan[d].f)
                *p++ = adjthan[d].enc;
        }
        *p++ = ',';
    }

    p[-1] = '\0';

    free_game(state);
    sfree(spaces);

    ret = srealloc(ret, p - ret);
    return ret;
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    game_state *solved = dup_game(state);
    char *ret = NULL;
    int result = subsets_solve_game(solved);

    if (result != STATUS_INVALID)
    {
        int s = solved->w * solved->h;
        char *p;
        int i;

        ret = snewn((s * 6) + 1, char);
        p = ret;
        *p++ = 'S';

        for (i = 0; i < s; i++)
        {
            p += sprintf(p, "%d,%d", solved->known[i], solved->mask[i]);
            *p++ = i == s - 1 ? '\0' : ',';
        }
    }
    else
        *error = "Puzzle is invalid.";

    free_game(solved);
    return ret;
}

struct game_ui
{
    bool cshow;
    int cx, cy;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->cshow = false;
    ui->cx = ui->cy = 0;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

struct game_drawstate
{
    int tilesize;

    unsigned int *flags;
    unsigned int *oldflags;
    int *counts;
    int *oldcounts;
    bool oldflash;

    unsigned int *oldknown;
    unsigned int *oldmask;

    bool started;

    /* Blitter for the background of the keyboard cursor */
    blitter *bl;
    bool bl_on;
    /* Position of the center of the blitter */
    int blx, bly;
    /* Radius of the keyboard cursor */
    int blr;
    /* Size of the blitter */
    int bls;
};

#define FROM_COORD(x) (((x) - (tilesize / 2)) / tilesize)

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int ox, int oy, int button)
{
    int w = state->w, h = state->h, n = state->n;
    int gx, gy;
    int cw = CELL_WIDTH(n);
    int ch = CELL_HEIGHT(n);
    int tilesize = ds->tilesize;

    if (IS_CURSOR_MOVE(button))
    {
        char *ret = NULL;
        do
        {
            char *result = move_cursor(button, &ui->cx, &ui->cy,
                                       w * (cw + 1) - 1, h * (ch + 1) - 1,
                                       false, &ui->cshow);

            if (!ret)
                ret = result;
        } while (ui->cx % (cw + 1) == cw || ui->cy % (ch + 1) == ch);

        return ret;
    }

    int num, pos;
    char oldtype, newtype;

    if ((IS_CURSOR_SELECT(button) || button == '\b') && ui->cshow)
    {
        gx = ui->cx;
        gy = ui->cy;
    }
    else if (!IS_MOUSE_DOWN(button) || ox < tilesize / 2 || oy < tilesize / 2)
        return NULL;
    else
    {
        gx = FROM_COORD(ox);
        gy = FROM_COORD(oy);
    }
    int cellx = gx / (cw + 1);
    int celly = gy / (ch + 1);
    int numx = gx % (cw + 1);
    int numy = gy % (ch + 1);

    if (cellx >= state->w || celly >= state->h)
        return NULL;
    if (numx >= cw || numy >= ch)
        return NULL;

    pos = celly * state->w + cellx;
    num = numy * cw + numx;

    if (state->immutable[pos] & (1 << num))
        return MOVE_NO_EFFECT;

    oldtype = state->known[pos] & (1 << num)  ? 'K'
              : state->mask[pos] & (1 << num) ? 'U'
                                              : 'C';

    switch (button)
    {
    case LEFT_BUTTON:
    case CURSOR_SELECT:
        newtype = oldtype == 'U'   ? 'K'
                  : oldtype == 'K' ? 'C'
                                   : 'U';
        break;
    case RIGHT_BUTTON:
    case CURSOR_SELECT2:
        newtype = oldtype == 'U'   ? 'C'
                  : oldtype == 'C' ? 'K'
                                   : 'U';
        break;
    case MIDDLE_BUTTON:
    case '\b':
        newtype = 'U';
        break;
    default:
        newtype = oldtype;
        break;
    }

    if (oldtype == newtype)
        return MOVE_NO_EFFECT;

    if (IS_MOUSE_DOWN(button))
        ui->cshow = false;

    char buf[80];
    sprintf(buf, "%c%d,%d", newtype, pos, num);
    return dupstr(buf);
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret = NULL;
    int pos, n;
    const char *p;
    char type;

    if (move[0] == 'S')
    {
        p = move + 1;
        ret = dup_game(state);
        pos = 0;

        while (pos < state->w * state->h && *p)
        {
            n = (unsigned int)atoi(p);

            while (*p >= '0' && *p <= '9')
                p++; /* skip number */

            p++; /* comma */

            ret->known[pos] = n;

            n = (unsigned int)atoi(p);

            while (*p >= '0' && *p <= '9')
                p++; /* skip number */
            p++;     /* comma */

            ret->mask[pos] = n;

            pos++;
        }
        return ret;
    }

    if (sscanf(move, "%c%d,%d", &type, &pos, &n) != 3)
        return NULL;
    if (pos < 0 || pos >= state->w * state->h)
        return NULL;
    if (n < 0 || n >= state->n)
        return NULL;
    if (state->immutable[pos] & (1 << n))
        return NULL;

    ret = dup_game(state);

    switch (type)
    {
    case 'K':
        ret->known[pos] |= (1 << n);
        ret->mask[pos] |= (1 << n);
        break;
    case 'C':
        ret->known[pos] &= ~(1 << n);
        ret->mask[pos] &= ~(1 << n);
        break;
    case 'U':
        ret->known[pos] &= ~(1 << n);
        ret->mask[pos] |= (1 << n);
        break;
    }

    if (subsets_validate(ret, NULL, NULL) == STATUS_COMPLETE)
        ret->completed = true;

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              const game_ui *ui, int *x, int *y)
{
    int w = params->w, h = params->h, n = params->n;

    *x = w * (CELL_WIDTH(n) + 1) * tilesize;
    *y = h * (CELL_HEIGHT(n) + 1) * tilesize;

    *y += tilesize * (CELL_HEIGHT(n) + 1);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    ds->blr = tilesize * 0.4;
    ds->bls = ds->blr * 2;
    assert(!ds->bl);
    ds->bl = blitter_new(dr, ds->bls, ds->bls);
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    game_mkhighlight(fe, ret, COL_INNERBG, COL_HIGHLIGHT, COL_LOWLIGHT);
    frontend_default_colour(fe, &ret[COL_OUTERBG * 3]);

    int i;
    for (i = 0; i < 3; i++)
    {
        ret[COL_FIXED * 3 + i] = 0.0F;
        ret[COL_GRID * 3 + i] = 0.5F;
    }

    ret[COL_GUESS * 3 + 0] = 0.0F;
    ret[COL_GUESS * 3 + 1] = 0.5F;
    ret[COL_GUESS * 3 + 2] = 0.0F;

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
    int s = state->w * state->h;

    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->oldflash = false;
    ds->started = false;
    ds->counts = snewn(s, int);
    ds->oldcounts = snewn(s, int);
    ds->flags = snewn(s, unsigned int);
    ds->oldflags = snewn(s, int);
    ds->oldknown = snewn(s, unsigned int);
    ds->oldmask = snewn(s, unsigned int);

    subsets_validate(state, ds->flags, ds->counts);
    memset(ds->oldflags, 0, s * sizeof(unsigned int));
    memset(ds->oldcounts, 0, s * sizeof(int));
    memset(ds->oldknown, 0, s * sizeof(unsigned int));
    memset(ds->oldmask, 0, s * sizeof(unsigned int));

    ds->bl = NULL;
    ds->bl_on = false;
    ds->blx = -1;
    ds->bly = -1;
    ds->blr = -1;
    ds->bls = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->counts);
    sfree(ds->flags);
    sfree(ds->oldflags);
    sfree(ds->oldcounts);
    sfree(ds->oldknown);
    sfree(ds->oldmask);
    if (ds->bl)
        blitter_free(dr, ds->bl);
    sfree(ds);
}

#define FLASH_FRAME 0.12F
#define FLASH_TIME (FLASH_FRAME * 5)
static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->w;
    int h = state->h;
    int n = state->n;
    int cw = CELL_WIDTH(n);
    int ch = CELL_HEIGHT(n);

    int x, y, cx, cy, cn, tx, ty, d, i1, i2, color;

    int tilesize = ds->tilesize;
    int fontsize = ds->tilesize * 3 / 4;
    int diameter = (int)(tilesize * 0.7f) | 1;
    int radius = diameter / 2;
    bool unknown;

    bool flash = false;
    bool cshow = ui->cshow && !flashtime;

    if (flashtime > 0)
        flash = (int)(flashtime / FLASH_FRAME) & 1;

    if (oldstate != state)
        subsets_validate(state, ds->flags, ds->counts);

    if (ds->bl_on)
    {
        blitter_load(dr, ds->bl,
                     ds->blx - ds->blr, ds->bly - ds->blr);
        draw_update(dr,
                    ds->blx - ds->blr, ds->bly - ds->blr,
                    ds->bls, ds->bls);
        ds->bl_on = false;
    }

    char buf[16];
    buf[1] = '\0';

    if (!ds->started)
    {
        for (y = 0; y < h; y++)
        {
            for (x = 0; x < w; x++)
            {
                tx = ((x * (cw + 1)) + 0.5f) * tilesize;
                ty = ((y * (ch + 1)) + 0.5f) * tilesize;

                draw_rect(dr, tx - 1, ty - 1,
                          tilesize * cw + 1,
                          tilesize * ch + 1, COL_GRID);
                draw_update(dr, tx - 1, ty - 1,
                            tilesize * cw + 1,
                            tilesize * ch + 1);
            }
        }
    }

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            if (ds->started &&
                ds->oldflash == flash &&
                ds->oldknown[y * w + x] == state->known[y * w + x] &&
                ds->oldmask[y * w + x] == state->mask[y * w + x])
                continue;

            for (cy = 0; cy < ch; cy++)
            {
                for (cx = 0; cx < cw; cx++)
                {
                    cn = cy * cw + cx;
                    if (cn >= n)
                        continue;

                    tx = (((x * (cw + 1)) + cx) + 0.5f) * tilesize;
                    ty = (((y * (ch + 1)) + cy) + 0.5f) * tilesize;

                    unknown = (state->known[y * w + x] ^ state->mask[y * w + x]) & (1 << cn);
                    draw_rect(dr, tx, ty,
                              tilesize - 1, tilesize - 1,
                              flash || unknown ? COL_INNERBG : COL_HIGHLIGHT);

                    if (state->known[y * w + x] & (1 << cn))
                    {
                        buf[0] = 'A' + cn;

                        draw_text(dr, tx + tilesize / 2, ty + tilesize / 2,
                                  FONT_VARIABLE, fontsize, ALIGN_HCENTRE | ALIGN_VCENTRE,
                                  state->immutable[y * w + x] & (1 << cn) ? COL_FIXED : COL_GUESS,
                                  buf);
                    }

                    draw_update(dr, tx, ty, tilesize - 1, tilesize - 1);
                }
            }

            ds->oldknown[y * w + x] = state->known[y * w + x];
            ds->oldmask[y * w + x] = state->mask[y * w + x];
        }
    }

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            i1 = y * w + x;
            for (d = 0; d < 4; d++)
            {
                if ((x + adjthan[d].dx < 0) || (x + adjthan[d].dx >= w) || (y + adjthan[d].dy < 0) || (y + adjthan[d].dy >= h))
                    continue;

                i2 = i1 + (adjthan[d].dy * w) + adjthan[d].dx;

                tx = (((x * (cw + 1)) + (cw * 0.5f)) + 0.5f) * tilesize;
                ty = (((y * (ch + 1)) + (ch * 0.5f)) + 0.5f) * tilesize;
                tx += adjthan[d].dx * (cw - 0.5f) * tilesize;
                ty += adjthan[d].dy * (ch - 0.5f) * tilesize;

                clip(dr, tx - radius - 1, ty - radius - 1, diameter + 2, diameter + 2);

                if (state->clues[i1] & adjthan[d].f)
                {
                    if (!ds->started || (ds->flags[i1] & adjthan[d].f) != (ds->oldflags[i1] & adjthan[d].f))
                    {
                        color = (ds->flags[i1] & adjthan[d].f) ? COL_ERROR : COL_FIXED;
                        draw_rect(dr, tx - radius - 1, ty - radius - 1, diameter + 2, diameter + 2, COL_OUTERBG);
                        draw_circle(dr, tx, ty, radius, color, color);
                        draw_circle(dr, tx, ty, radius - 2, COL_OUTERBG, COL_OUTERBG);

                        if (adjthan[d].f & (F_ADJ_UP | F_ADJ_DOWN))
                        {
                            if (adjthan[d].dy > 0)
                                ty -= radius;
                            else
                                ty += 1;
                            draw_rect(dr, tx - radius, ty, diameter, radius, color);
                            draw_rect(dr, 2 + tx - radius, ty, diameter - 4, radius, COL_OUTERBG);
                        }
                        else
                        {
                            if (adjthan[d].dx > 0)
                                tx -= radius;
                            else
                                tx += 1;
                            draw_rect(dr, tx, ty - radius, radius, diameter, color);
                            draw_rect(dr, tx, 2 + ty - radius, radius, diameter - 4, COL_OUTERBG);
                        }

                        draw_update(dr, tx - radius - 1, ty - radius - 1, diameter + 2, diameter + 2);

                        if (color == COL_ERROR)
                            ds->oldflags[i1] |= adjthan[d].f;
                        else
                            ds->oldflags[i1] &= ~adjthan[d].f;
                    }
                }
                else if (i1 < i2 && !(state->clues[i2] & adjthan[d].fo))
                {
                    if (ds->started && (ds->flags[i1] & adjthan[d].f) == (ds->oldflags[i1] & adjthan[d].f))
                    {
                        /* Do nothing */
                    }
                    else if (ds->flags[i1] & adjthan[d].f)
                    {
                        draw_thick_line(dr, 2, tx - radius, ty - radius, tx + radius, ty + radius, COL_ERROR);
                        draw_thick_line(dr, 2, tx - radius, ty + radius, tx + radius, ty - radius, COL_ERROR);
                        draw_update(dr, tx - radius - 1, ty - radius - 1, diameter + 2, diameter + 2);
                        ds->oldflags[i1] |= adjthan[d].f;
                    }
                    else
                    {
                        draw_rect(dr, tx - radius - 1, ty - radius - 1, diameter + 2, diameter + 2, COL_OUTERBG);
                        draw_update(dr, tx - radius - 1, ty - radius - 1, diameter + 2, diameter + 2);
                        ds->oldflags[i1] &= ~adjthan[d].f;
                    }
                }

                unclip(dr);
            }
        }
    }

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            cn = x * h + y;

            if (ds->started && ds->counts[cn] == ds->oldcounts[cn])
                continue;

            tx = x * (cw + 1) * tilesize;
            ty = y * 0.75 * tilesize;
            tx += cw * tilesize * 0.75;
            ty += (h + 2) * ch * tilesize;

            for (cx = 0; cx < n; cx++)
                buf[cx] = cn & (1 << cx) ? 'A' + cx : '_';

            buf[n] = '\0';

            color = ds->counts[cn] > 1 ? COL_ERROR : ds->counts[cn] == 1 ? COL_LOWLIGHT
                                                                         : COL_FIXED;

            draw_rect(dr, tx - tilesize, ty - (tilesize * 0.375),
                      tilesize * 2, tilesize * 0.75, COL_OUTERBG);

            draw_text(dr, tx, ty, FONT_FIXED, tilesize / 2, ALIGN_HCENTRE | ALIGN_VCENTRE, color, buf);

            draw_update(dr, tx - tilesize, ty - (tilesize * 0.375), tilesize * 2, tilesize * 0.75);

            ds->oldcounts[cn] = ds->counts[cn];
        }
    }

    ds->started = true;
    ds->oldflash = flash;

    if (cshow)
    {
        ds->blx = ((ui->cx + 1) * tilesize) - 1;
        ds->bly = ((ui->cy + 1) * tilesize) - 1;

        blitter_save(dr, ds->bl, ds->blx - ds->blr, ds->bly - ds->blr);
        ds->bl_on = true;

        draw_rect_corners(dr, ds->blx, ds->bly, ds->blr - 1, COL_CURSOR);
        draw_update(dr, ds->blx - ds->blr, ds->bly - ds->blr, ds->bls, ds->bls);
    }
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if (ui->cshow)
    {
        int tilesize = ds->tilesize;
        *x = ui->cx * tilesize + (tilesize / 2);
        *y = ui->cy * tilesize + (tilesize / 2);
        *w = *h = tilesize;
    }
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

#ifdef COMBINED
#define thegame subsets
#endif

const struct game thegame = {
    "Subsets", "games.subsets", "subsets",
    default_params,
    game_fetch_preset, NULL,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    false, NULL, NULL, /* configure, custom_params */
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    NULL, /* set_public_desc */
    dup_game,
    free_game,
    true, solve_game,
    true, game_can_format_as_text_now, game_text_format,
    NULL, NULL, /* get_prefs, set_prefs */
    new_ui,
    free_ui,
    NULL, /* encode_ui */
    NULL, /* decode_ui */
    NULL, /* game_request_keys */
    game_changed_state,
    NULL, /* current_key_label */
    interpret_move,
    execute_move,
    36, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    false, false, NULL, NULL, /* print_size, print */
    false,                    /* wants_statusbar */
    false, NULL,              /* timing_state */
    0,                        /* flags */
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
        else if (!strcmp(p, "-v"))
            solver_verbose = true;
        else if (*p == '-')
            usage_exit("unrecognised option");
        else
            id = p;
    }

    if (id)
    {
        desc = strchr(id, ':');
        if (desc)
            *desc++ = '\0';

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
        rs = random_new((void *)&seed, sizeof(time_t));
        if (!params)
            params = default_params();
        char *desc_gen, *aux;
        printf("Generating puzzle with parameters %s\n", encode_params(params, true));
        desc_gen = new_game_desc(params, rs, &aux, false);
        printf("Game ID: %s", desc_gen);
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

        int errcode = subsets_solve_game(solved);

        if (errcode == STATUS_INVALID)
            printf("Puzzle is INVALID.\n");
        char *fmt = game_text_format(solved);
        printf("%s", fmt);
        sfree(fmt);
        if (errcode == STATUS_UNFINISHED)
            printf("Solution not found.\n");

        free_game(input);
        free_game(solved);
    }

    return 0;
}
#endif
