//
//  GameHelpController.m
//  Puzzles
//
//  Created by Greg Hewgill on 16/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameHelpController.h"

@interface GameHelpController ()

@end

@implementation GameHelpController {
    NSString *file;
    UIWebView *webview;
}

- (id)initWithFile:(NSString *)f;
{
    self = [super init];
    if (self) {
        // Custom initialization
        file = f;
    }
    return self;
}

- (void)loadView
{
    self.view = webview = [[UIWebView alloc] init];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view.
    [webview loadRequest:[NSURLRequest requestWithURL:[NSURL fileURLWithPath:[[NSBundle mainBundle] pathForResource:file ofType:nil]]]];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
