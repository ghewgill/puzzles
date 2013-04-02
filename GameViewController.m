//
//  GameViewController.m
//  Puzzles
//
//  Created by Greg Hewgill on 8/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameViewController.h"

#import "GameView.h"
#import "GameHelpController.h"

@interface GameViewController ()

@end

@implementation GameViewController {
    const game *thegame;
    NSString *name;
    NSString *saved;
    BOOL init_inprogress;
    id<GameViewControllerSaver> saver;
    GameView *gameview;
}

- (id)initWithGame:(const game *)g saved:(NSString *)sav inprogress:(BOOL)inprog saver:(id<GameViewControllerSaver>)savr;
{
    self = [super init];
    if (self) {
        thegame = g;
        name = [NSString stringWithUTF8String:thegame->name];
        self.title = name;
        saved = sav;
        init_inprogress = inprog;
        saver = savr;
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(saveGame) name:@"applicationDidEnterBackground" object:nil];
    }
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)loadView
{
    self.view = gameview = [[GameView alloc] initWithFrame:[UIScreen mainScreen].bounds nc:self.navigationController game:thegame saved:saved inprogress:init_inprogress];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view.
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Help" style:UIBarButtonItemStylePlain target:self action:@selector(showHelp)];
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    [self saveGame];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)toInterfaceOrientation
{
    return YES;
}

- (void)saveGame
{
    BOOL inprogress;
    NSString *save = [gameview saveGameState_inprogress:&inprogress];
    if (save != nil) {
        [saver saveGame:name state:save inprogress:inprogress];
    }
}

- (void)showHelp
{
    [self.navigationController pushViewController:[[GameHelpController alloc] initWithFile:[NSString stringWithFormat:@"%s.html", thegame->htmlhelp_topic]] animated:YES];
}

@end
