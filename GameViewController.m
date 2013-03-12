//
//  GameViewController.m
//  Puzzles
//
//  Created by Greg Hewgill on 8/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameViewController.h"

#import "GameView.h"

NSMutableDictionary *g_SavedGames;

@interface GameViewController ()

@end

@implementation GameViewController {
    const game *thegame;
    GameView *gameview;
    NSString *name;
}

- (id)initWithGame:(const game *)g
{
    self = [super init];
    if (self) {
        thegame = g;
        name = [NSString stringWithUTF8String:thegame->name];
        self.title = name;
        if (g_SavedGames == nil) {
            g_SavedGames = [[NSMutableDictionary alloc] init];
        }
    }
    return self;
}

- (void)loadView
{
    NSString *saved;
    if (g_SavedGames && g_SavedGames[name]) {
        saved = g_SavedGames[name];
    }
    self.view = gameview = [[GameView alloc] initWithFrame:[UIScreen mainScreen].bounds nc:self.navigationController game:thegame saved:saved];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view.
    //self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Menu" style:UIBarButtonItemStylePlain target:self action:@selector(showMenu)];
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    NSString *save = [gameview saveGameState];
    if (save != nil) {
        g_SavedGames[name] = save;
    } else {
        [g_SavedGames removeObjectForKey:name];
    }
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
