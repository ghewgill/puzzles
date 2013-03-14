//
//  GameViewController.m
//  Puzzles
//
//  Created by Greg Hewgill on 8/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameViewController.h"

#import "GameView.h"

@interface GameViewController ()

@end

@implementation GameViewController {
    const game *thegame;
    NSString *name;
    NSString *saved;
    id<GameViewControllerSaver> saver;
    GameView *gameview;
}

- (id)initWithGame:(const game *)g saved:(NSString *)sav saver:(id<GameViewControllerSaver>)savr;
{
    self = [super init];
    if (self) {
        thegame = g;
        name = [NSString stringWithUTF8String:thegame->name];
        self.title = name;
        saved = sav;
        saver = savr;
    }
    return self;
}

- (void)loadView
{
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
    BOOL inprogress;
    NSString *save = [gameview saveGameState_inprogress:&inprogress];
    [saver saveGame:name state:save inprogress:inprogress];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
