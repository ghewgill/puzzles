//
//  GameView.m
//  Puzzles
//
//  Created by Greg Hewgill on 7/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameView.h"

#import <AudioToolbox/AudioToolbox.h>
#import <CoreText/CoreText.h>

#import "GameMenuController.h"
#import "GameTypeController.h"

#include "puzzles.h"

typedef float rgb[3];

struct frontend {
    void *gv;
    rgb *colours;
    int ncolours;
    BOOL clipping;
};

extern const struct drawing_api ios_drawing;

// Game instances we will want to refer to
extern const game filling;
extern const game keen;
extern const game map;
extern const game net;
extern const game pattern;
extern const game solo;
extern const game towers;
extern const game undead;
extern const game unequal;

const int ButtonDown[3] = {LEFT_BUTTON,  RIGHT_BUTTON,  MIDDLE_BUTTON};
const int ButtonDrag[3] = {LEFT_DRAG,    RIGHT_DRAG,    MIDDLE_DRAG};
const int ButtonUp[3]   = {LEFT_RELEASE, RIGHT_RELEASE, MIDDLE_RELEASE};

const int NBUTTONS = 10;

@implementation GameView {
    UINavigationController *navigationController;
    const game *ourgame;
    midend *me;
    frontend fe;
    CGRect usable_frame;
    CGRect game_rect;
    NSTimer *timer;
    UIToolbar *game_toolbar;
    int touchState;
    int touchXpoints, touchYpoints;
    int touchXpixels, touchYpixels;
    int touchButton;
    NSTimer *touchTimer;
    UIToolbar *toolbar;
    UIActionSheet *gameMenu;
}

@synthesize bitmap;
@synthesize statusbar;

struct StringReadContext {
    void *save;
    int pos;
};

static int saveGameRead(void *ctx, void *buf, int len)
{
    struct StringReadContext *srctx = (struct StringReadContext *)ctx;
    NSString *save = (__bridge NSString *)(srctx->save);
    NSUInteger used = 0;
    BOOL r = [save getBytes:buf maxLength:len usedLength:&used encoding:NSUTF8StringEncoding options:0 range:NSMakeRange(srctx->pos, save.length-srctx->pos) remainingRange:NULL];
    srctx->pos += used;
    return r;
}

- (id)initWithFrame:(CGRect)frame nc:(UINavigationController *)nc game:(const game *)g saved:(NSString *)saved inprogress:(BOOL)inprogress
{
    self = [super initWithFrame:frame];
    if (self) {
        navigationController = nc;
        ourgame = g;
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        fe.gv = (__bridge void *)(self);
        {
            char buf[80], value[10];
            int j, k;
    
            sprintf(buf, "%s_TILESIZE", ourgame->name);
            for (j = k = 0; buf[j]; j++)
                if (!isspace((unsigned char)buf[j]))
                    buf[k++] = toupper((unsigned char)buf[j]);
            buf[k] = '\0';
            snprintf(value, sizeof(value), "%d", ourgame->preferred_tilesize*4);
            setenv(buf, value, 1);
        }
        me = midend_new(&fe, ourgame, &ios_drawing, &fe);
        if (saved) {
            struct StringReadContext srctx;
            srctx.save = (__bridge void *)(saved);
            srctx.pos = 0;
            const char *msg = midend_deserialise(me, saveGameRead, &srctx);
            if (msg) {
                [[[UIAlertView alloc] initWithTitle:@"Puzzles" message:[NSString stringWithUTF8String:msg] delegate:nil cancelButtonTitle:@"OK" otherButtonTitles:nil] show];
                midend_new_game(me);
            }
            if (!inprogress) {
                midend_new_game(me);
            }
        } else {
            if (ourgame == &pattern && [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPhone) {
                midend_game_id(me, "5");
            }
            midend_new_game(me);
        }
        fe.colours = (rgb *)midend_colours(me, &fe.ncolours);
        self.backgroundColor = [UIColor colorWithRed:0.8 green:0.8 blue:0.8 alpha:1];
    }
    return self;
}

- (void)dealloc
{
    midend_free(me);
}

static void saveGameWrite(void *ctx, void *buf, int len)
{
    NSMutableString *save = (__bridge NSMutableString *)(ctx);
    [save appendString:[[NSString alloc] initWithBytes:buf length:len encoding:NSUTF8StringEncoding]];
}

- (NSString *)saveGameState_inprogress:(BOOL *)inprogress
{
    *inprogress = midend_can_undo(me) && midend_status(me) == 0;
    NSMutableString *save = [[NSMutableString alloc] init];
    midend_serialise(me, saveGameWrite, (__bridge void *)(save));
    return save;
}

- (void)layoutSubviews
{
    int usable_height = self.frame.size.height;
    usable_height -= 44;
    CGRect r = CGRectMake(0, usable_height, self.frame.size.width, 44);
    if (toolbar) {
        [toolbar setFrame:r];
    } else {
        toolbar = [[UIToolbar alloc] initWithFrame:r];
        NSArray *items = @[
            [[UIBarButtonItem alloc] initWithTitle:@"Game" style:UIBarButtonItemStyleBordered target:self action:@selector(doGameMenu)],
            [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil],
            [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemUndo target:self action:@selector(doUndo)],
            [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemRedo target:self action:@selector(doRedo)],
            [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil],
            [[UIBarButtonItem alloc] initWithTitle:@"Type" style:UIBarButtonItemStyleBordered target:self action:@selector(doType)],
        ];
        [toolbar setItems:items];
        toolbar.barStyle = UIBarStyleBlack;
        [self addSubview:toolbar];
    }
    if (midend_wants_statusbar(me)) {
        usable_height -= 20;
        CGRect r = CGRectMake(0, usable_height, self.frame.size.width, 20);
        if (statusbar) {
            [statusbar setFrame:r];
        } else {
            statusbar = [[UILabel alloc] initWithFrame:r];
            [self addSubview:statusbar];
        }
    } else {
        if (statusbar) {
            [statusbar removeFromSuperview];
            statusbar = nil;
        }
    }
    if (ourgame == &filling
     || ourgame == &keen
     || ourgame == &map
     || ourgame == &solo
     || ourgame == &towers
     || ourgame == &undead
     || ourgame == &unequal) {
        usable_height -= 44;
        int n = 9;
        const char **labels = NULL;
        if (ourgame == &keen) {
            n = atoi(midend_get_game_id(me));
        } else if (ourgame == &map) {
            static const char *MapLabels[] = {"Labels"};
            n = 1;
            labels = MapLabels;
        } else if (ourgame == &solo) {
            const char *game_id = midend_get_game_id(me);
            int x, y;
            if (sscanf(game_id, "%dx%d", &x, &y) == 2) {
                n = x * y;
            }
        } else if (ourgame == &towers) {
            n = atoi(midend_get_game_id(me));
        } else if (ourgame == &undead) {
            static const char *UndeadLabels[] = {"Ghost", "Vampire", "Zombie"};
            n = 3;
            labels = UndeadLabels;
        } else if (ourgame == &unequal) {
            n = atoi(midend_get_game_id(me));
        }
        CGRect r = CGRectMake(0, usable_height, self.frame.size.width, 44);
        if (game_toolbar == nil) {
            game_toolbar = [[UIToolbar alloc] initWithFrame:r];
        } else {
            game_toolbar.frame = r;
        }
        UIBarButtonItemStyle style = ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPhone && n > 9) ? UIBarButtonItemStylePlain : UIBarButtonItemStyleBordered;
        NSMutableArray *items = [[NSMutableArray alloc] init];
        [items addObject:[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil]];
        [items addObject:[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil]];
        for (int i = 0; i < n; i++) {
            NSString *title = NULL;
            if (labels) {
                title = [NSString stringWithUTF8String:labels[i]];
            } else {
                if (i < 9) {
                    title = [NSString stringWithFormat:@"%d", 1+i];
                } else {
                    title = [NSString stringWithFormat:@"%c", 'a'+(i-9)];
                }
            }
            [items addObject:[[UIBarButtonItem alloc] initWithTitle:title style:style target:self action:@selector(keyButton:)]];
            [items addObject:[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil]];
        }
        [items addObject:[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil]];
        game_toolbar.items = items;
        [self addSubview:game_toolbar];
    } else {
        [game_toolbar removeFromSuperview];
        game_toolbar = nil;
    }
    usable_frame = CGRectMake(0, 0, self.frame.size.width, usable_height);
    int fw = self.frame.size.width * self.contentScaleFactor;
    int fh = usable_height * self.contentScaleFactor;
    int w = fw;
    int h = fh;
    midend_size(me, &w, &h, FALSE);
    game_rect = CGRectMake((fw - w)/2/self.contentScaleFactor, (fh - h)/2/self.contentScaleFactor, w/self.contentScaleFactor, h/self.contentScaleFactor);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    bitmap = CGBitmapContextCreate(NULL, w, h, 8, w*4, cs, kCGImageAlphaNoneSkipLast);
    CGColorSpaceRelease(cs);
    midend_force_redraw(me);
    [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect
{
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGImageRef image = CGBitmapContextCreateImage(bitmap);
    CGContextDrawImage(context, game_rect, image);
    CGImageRelease(image);
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    CGPoint p = [touch locationInView:self];
    p.x -= game_rect.origin.x;
    p.y -= game_rect.origin.y;
    touchTimer = [NSTimer timerWithTimeInterval:0.5 target:self selector:@selector(handleTouchTimer:) userInfo:nil repeats:NO];
    [[NSRunLoop currentRunLoop] addTimer:touchTimer forMode:NSDefaultRunLoopMode];
    touchState = 1;
    touchXpoints = p.x;
    touchYpoints = p.y;
    touchXpixels = p.x * self.contentScaleFactor;
    touchYpixels = p.y * self.contentScaleFactor;
    touchButton = 0;
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    CGPoint p = [touch locationInView:self];
    p.x -= game_rect.origin.x;
    p.y -= game_rect.origin.y;
    int xpoints = min(game_rect.size.width-1, max(0, p.x));
    int ypoints = min(game_rect.size.height-1, max(0, p.y));
    int xpixels = xpoints * self.contentScaleFactor;
    int ypixels = ypoints * self.contentScaleFactor;
    if (touchState == 1) {
        if (abs(xpoints - touchXpoints) >= 10 || abs(ypoints - touchYpoints) >= 10) {
            [touchTimer invalidate];
            touchTimer = nil;
            midend_process_key(me, touchXpixels, touchYpixels, ButtonDown[touchButton]);
            touchState = 2;
        }
    }
    if (touchState == 2) {
        midend_process_key(me, xpixels, ypixels, ButtonDrag[touchButton]);
    }
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
    UITouch *touch = [touches anyObject];
    CGPoint p = [touch locationInView:self];
    p.x -= game_rect.origin.x;
    p.y -= game_rect.origin.y;
    int xpixels = min(game_rect.size.width-1, max(0, p.x)) * self.contentScaleFactor;
    int ypixels = min(game_rect.size.height-1, max(0, p.y)) * self.contentScaleFactor;
    if (touchState == 1) {
        midend_process_key(me, touchXpixels, touchYpixels, ButtonDown[touchButton]);
    }
    midend_process_key(me, xpixels, ypixels, ButtonUp[touchButton]);
    touchState = 0;
    [touchTimer invalidate];
    touchTimer = nil;
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event
{
    touchState = 0;
    [touchTimer invalidate];
    touchTimer = nil;
}

- (void)handleTouchTimer:(NSTimer *)timer
{
    if (touchState == 1) {
        if (ourgame == &net) {
            touchButton = 2; // middle button
        } else {
            touchButton = 1; // right button
        }
        midend_process_key(me, touchXpixels, touchYpixels, ButtonDown[touchButton]);
        touchState = 2;
        AudioServicesPlaySystemSound(0x450); // standard key click
    }
}

- (void)keyButton:(UIBarButtonItem *)sender
{
    midend_process_key(me, -1, -1, [sender.title characterAtIndex:0]);
}

- (void)activateTimer
{
    if (timer != nil) {
        [timer invalidate];
    }
    timer = [NSTimer timerWithTimeInterval:0.02 target:self selector:@selector(timerFire:) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:timer forMode:NSDefaultRunLoopMode];
}

- (void)deactivateTimer
{
    [timer invalidate];
    timer = nil;
}

- (void)timerFire:(NSTimer *)timer
{
    midend_timer(me, 0.02);
}

- (void)drawGameRect:(CGRect)rect
{
    [self setNeedsDisplayInRect:CGRectOffset(CGRectMake(rect.origin.x/self.contentScaleFactor, rect.origin.y/self.contentScaleFactor, rect.size.width/self.contentScaleFactor, rect.size.height/self.contentScaleFactor), game_rect.origin.x, game_rect.origin.y)];
}

- (void)doGameMenu
{
    if (gameMenu) {
        [gameMenu dismissWithClickedButtonIndex:gameMenu.cancelButtonIndex animated:YES];
        gameMenu = nil;
        return;
    }
    gameMenu = [[UIActionSheet alloc] initWithTitle:nil delegate:self cancelButtonTitle:@"Cancel" destructiveButtonTitle:@"New game" otherButtonTitles:@"Specific game", @"Specific Random Seed", @"Restart", @"Solve", nil];
    // Avoid doing this because on the iPad, the popover will automatically add the toolbar to the list of passthrough
    // views, causing unwanted effects if you click on other toolbar buttons before the popover dismisses.
    // See http://stackoverflow.com/questions/5448987/ipads-uiactionsheet-showing-multiple-times
    //[gameMenu showFromBarButtonItem:toolbar.items[0] animated:YES];
    [gameMenu showFromRect:CGRectIntersection(toolbar.frame, CGRectMake(0, 0, 60, self.frame.size.height)) inView:self animated:YES];
}

- (void)actionSheet:(UIActionSheet *)actionSheet clickedButtonAtIndex:(NSInteger)buttonIndex
{
    switch (buttonIndex) {
        case 0: [self doNewGame]; break;
        case 1: [self doSpecificGame]; break;
        case 2: [self doSpecificSeed]; break;
        case 3: [self doRestart]; break;
        case 4: [self doSolve]; break;
    }
}

- (void)actionSheet:(UIActionSheet *)actionSheet didDismissWithButtonIndex:(NSInteger)buttonIndex
{
    gameMenu = nil;
}

- (void)doNewGame
{
    midend_new_game(me);
    [self layoutSubviews];
}

- (void)doSpecificGame
{
    char *wintitle;
    config_item *config = midend_get_config(me, CFG_DESC, &wintitle);
    [navigationController pushViewController:[[GameSettingsController alloc] initWithConfig:config type:CFG_DESC title:[NSString stringWithUTF8String:wintitle] delegate:self] animated:YES];
    free(wintitle);
}

- (void)doSpecificSeed
{
    char *wintitle;
    config_item *config = midend_get_config(me, CFG_SEED, &wintitle);
    [navigationController pushViewController:[[GameSettingsController alloc] initWithConfig:config type:CFG_SEED title:[NSString stringWithUTF8String:wintitle] delegate:self] animated:YES];
    free(wintitle);
}

- (void)didApply:(config_item *)config
{
    const char *msg = midend_game_id(me, config[0].sval);
    if (msg) {
        [[[UIAlertView alloc] initWithTitle:@"Puzzles" message:[NSString stringWithUTF8String:msg] delegate:nil cancelButtonTitle:@"Close" otherButtonTitles:nil] show];
        return;
    }
    midend_new_game(me);
    [self layoutSubviews];
    [navigationController popViewControllerAnimated:YES];
}

- (void)doUndo
{
    midend_process_key(me, -1, -1, 'u');
}

- (void)doRedo
{
    midend_process_key(me, -1, -1, 'r'&0x1F);
}

- (void)doRestart
{
    midend_restart_game(me);
}

- (void)doSolve
{
    const char *msg = midend_solve(me);
    if (msg) {
        [[[UIAlertView alloc] initWithTitle:@"Puzzles" message:[NSString stringWithUTF8String:msg] delegate:nil cancelButtonTitle:@"Close" otherButtonTitles:nil] show];
    }
}

- (void)doType
{
    [navigationController pushViewController:[[GameTypeController alloc] initWithMidend:me gameview:self] animated:YES];
}

@end

static void ios_draw_text(void *handle, int x, int y, int fonttype,
                          int fontsize, int align, int colour, char *text)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    CFStringRef str = CFStringCreateWithBytes(NULL, (UInt8 *)text, strlen(text), kCFStringEncodingUTF8, false);
    CTFontRef font = CTFontCreateWithName(CFSTR("Helvetica"), fontsize, NULL);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGFloat components[] = {fe->colours[colour][0], fe->colours[colour][1], fe->colours[colour][2], 1};
    CGColorRef color = CGColorCreate(cs, components);
    CFStringRef attr_keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
    CFTypeRef attr_values[] = {font,                 color};
    CFDictionaryRef attributes = CFDictionaryCreate(NULL, (const void **)attr_keys, (const void **)attr_values, sizeof(attr_keys)/sizeof(attr_keys[0]), &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, str, attributes);
    CTLineRef line = CTLineCreateWithAttributedString(as);
    CGContextSetTextMatrix(gv.bitmap, CGAffineTransformMake(1, 0, 0, -1, 0, 0));
    CGFloat width = CTLineGetOffsetForStringIndex(line, CFAttributedStringGetLength(as), NULL);
    CGFloat tx = x;
    CGFloat ty = y;
    switch (align & (ALIGN_HLEFT|ALIGN_HCENTRE|ALIGN_HRIGHT)) {
        case ALIGN_HLEFT:
            break;
        case ALIGN_HCENTRE:
            tx -= width / 2;
            break;
        case ALIGN_HRIGHT:
            tx -= width;
            break;
    }
    switch (align & (ALIGN_VNORMAL|ALIGN_VCENTRE)) {
        case ALIGN_VNORMAL:
            break;
        case ALIGN_VCENTRE:
            ty += fontsize * 0.4;
            break;
    }
    CGContextSetTextPosition(gv.bitmap, tx, ty);
    CTLineDraw(line, gv.bitmap);
    CFRelease(line);
    CFRelease(as);
    CFRelease(attributes);
    CGColorRelease(color);
    CGColorSpaceRelease(cs);
    CFRelease(font);
    CFRelease(str);
}

static void ios_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    CGContextSetRGBFillColor(gv.bitmap, fe->colours[colour][0], fe->colours[colour][1], fe->colours[colour][2], 1);
    CGContextFillRect(gv.bitmap, CGRectMake(x, y, w, h));
}

static void ios_draw_line(void *handle, int x1, int y1, int x2, int y2, int colour)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    CGContextSetRGBStrokeColor(gv.bitmap, fe->colours[colour][0], fe->colours[colour][1], fe->colours[colour][2], 1);
    CGContextBeginPath(gv.bitmap);
    CGContextMoveToPoint(gv.bitmap, x1, y1);
    CGContextAddLineToPoint(gv.bitmap, x2, y2);
    CGContextStrokePath(gv.bitmap);
}

static void ios_draw_polygon(void *handle, int *coords, int npoints,
                             int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    CGContextSetRGBStrokeColor(gv.bitmap, fe->colours[outlinecolour][0], fe->colours[outlinecolour][1], fe->colours[outlinecolour][2], 1);
    CGContextBeginPath(gv.bitmap);
    CGContextMoveToPoint(gv.bitmap, coords[0], coords[1]);
    for (int i = 1; i < npoints; i++) {
        CGContextAddLineToPoint(gv.bitmap, coords[i*2], coords[i*2+1]);
    }
    CGContextAddLineToPoint(gv.bitmap, coords[0], coords[1]);
    CGPathDrawingMode mode = kCGPathStroke;
    if (fillcolour >= 0) {
        CGContextSetRGBFillColor(gv.bitmap, fe->colours[fillcolour][0], fe->colours[fillcolour][1], fe->colours[fillcolour][2], 1);
        mode = kCGPathFillStroke;
    }
    CGContextDrawPath(gv.bitmap, mode);
}

static void ios_draw_circle(void *handle, int cx, int cy, int radius,
                            int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    if (fillcolour >= 0) {
        CGContextSetRGBFillColor(gv.bitmap, fe->colours[fillcolour][0], fe->colours[fillcolour][1], fe->colours[fillcolour][2], 1);
        CGContextFillEllipseInRect(gv.bitmap, CGRectMake(cx-radius+1, cy-radius+1, radius*2-1, radius*2-1));
    }
    CGContextSetRGBStrokeColor(gv.bitmap, fe->colours[outlinecolour][0], fe->colours[outlinecolour][1], fe->colours[outlinecolour][2], 1);
    CGContextStrokeEllipseInRect(gv.bitmap, CGRectMake(cx-radius+1, cy-radius+1, radius*2-1, radius*2-1));
}

static void ios_draw_update(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    [gv drawGameRect:CGRectMake(x, y, w, h)];
}

static void ios_clip(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    if (!fe->clipping) {
        CGContextSaveGState(gv.bitmap);
    }
    CGContextClipToRect(gv.bitmap, CGRectMake(x, y, w, h));
    fe->clipping = YES;
}

static void ios_unclip(void *handle)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    if (fe->clipping) {
        CGContextRestoreGState(gv.bitmap);
    }
    fe->clipping = NO;
}

static void ios_start_draw(void *handle)
{
}

static void ios_end_draw(void *handle)
{
}

static void ios_status_bar(void *handle, char *text)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    gv.statusbar.text = [NSString stringWithUTF8String:text];
}

struct blitter {
    int w, h;
    int x, y;
    int ox, oy;
    CGImageRef img;
};

static blitter *ios_blitter_new(void *handle, int w, int h)
{
    blitter *bl = snew(blitter);
    bl->w = w;
    bl->h = h;
    bl->x = -1;
    bl->y = -1;
    bl->img = NULL;
    return bl;
}

static void ios_blitter_free(void *handle, blitter *bl)
{
    if (bl->img != NULL) {
        CGImageRelease(bl->img);
    }
    sfree(bl);
}

static void ios_blitter_save(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    if (bl->img != NULL) {
        CGImageRelease(bl->img);
    }
    CGRect visible = CGRectIntersection(CGRectMake(x, y, bl->w, bl->h), CGRectMake(0, 0, CGBitmapContextGetWidth(gv.bitmap), CGBitmapContextGetHeight(gv.bitmap)));
    bl->x = x;
    bl->y = y;
    bl->ox = visible.origin.x - x;
    bl->oy = visible.origin.y - y;
    CGImageRef bitmap = CGBitmapContextCreateImage(gv.bitmap);
    // Not certain why the y coordinate inversion is necessary here, but it is
    bl->img = CGImageCreateWithImageInRect(bitmap, CGRectMake(visible.origin.x, CGBitmapContextGetHeight(gv.bitmap)-visible.origin.y-visible.size.height, visible.size.width, visible.size.height));
    CGImageRelease(bitmap);
}

static void ios_blitter_load(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    GameView *gv = (__bridge GameView *)(fe->gv);
    if (x == BLITTER_FROMSAVED && y == BLITTER_FROMSAVED) {
        x = bl->x;
        y = bl->y;
    }
    x += bl->ox;
    y += bl->oy;
    CGContextDrawImage(gv.bitmap, CGRectMake(x, y, CGImageGetWidth(bl->img), CGImageGetHeight(bl->img)), bl->img);
}

static char *ios_text_fallback(void *handle, const char *const *strings,
                               int nstrings)
{
    // we should be able to handle any requested UTF-8 string
    return dupstr(strings[0]);
}

const struct drawing_api ios_drawing = {
    ios_draw_text,
    ios_draw_rect,
    ios_draw_line,
    ios_draw_polygon,
    ios_draw_circle,
    ios_draw_update,
    ios_clip,
    ios_unclip,
    ios_start_draw,
    ios_end_draw,
    ios_status_bar,
    ios_blitter_new,
    ios_blitter_free,
    ios_blitter_save,
    ios_blitter_load,
    NULL, NULL, NULL, NULL, NULL, NULL, /* {begin,end}_{doc,page,puzzle} */
    NULL, NULL,                        /* line_width, line_dotted */
    ios_text_fallback,
};      

void fatal(char *fmt, ...)
{
}

void frontend_default_colour(frontend *fe, float *output)
{
    output[0] = output[1] = output[2] = 0.8f;
}

void get_random_seed(void **randseed, int *randseedsize)
{
    time_t *tp = snew(time_t);
    time(tp);
    *randseed = (void *)tp;
    *randseedsize = sizeof(time_t);
}

void activate_timer(frontend *fe)
{
    GameView *gv = (__bridge GameView *)(fe->gv);
    [gv activateTimer];
}

void deactivate_timer(frontend *fe)
{
    GameView *gv = (__bridge GameView *)(fe->gv);
    [gv deactivateTimer];
}
