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
}

- (id)initWithGame:(const game *)g
{
    self = [super init];
    if (self) {
        thegame = g;
        self.title = [NSString stringWithUTF8String:thegame->name];
    }
    return self;
}

- (void)loadView
{
    self.view = [[GameView alloc] initWithFrame:CGRectMake(0, 0, 320, 480) game:thegame];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view.
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
