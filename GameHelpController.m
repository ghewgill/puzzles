//
//  GameHelpController.m
//  Puzzles
//
//  Created by Greg Hewgill on 16/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameHelpController.h"

#include "puzzles.h"

@interface GameHelpController ()

@end

@implementation GameHelpController {
    NSString *file;
    WKWebView *webview;
    UIBarButtonItem *backButton;
    UIBarButtonItem *forwardButton;
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
    self.view = webview = [[WKWebView alloc] init];
    webview.navigationDelegate = self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view.
    if ([self.navigationItem respondsToSelector:@selector(setLeftItemsSupplementBackButton:)]) {
        self.navigationItem.leftItemsSupplementBackButton = YES;
        self.navigationItem.leftBarButtonItems = @[
            backButton = [[UIBarButtonItem alloc] initWithImage:[UIImage imageNamed:@"help-back.png"] style:UIBarButtonItemStylePlain target:webview action:@selector(goBack)],
            forwardButton = [[UIBarButtonItem alloc] initWithImage:[UIImage imageNamed:@"help-forward.png"] style:UIBarButtonItemStylePlain target:webview action:@selector(goForward)],
        ];
    }
    [webview loadRequest:[NSURLRequest requestWithURL:[NSURL fileURLWithPath:[[NSBundle mainBundle] pathForResource:file ofType:nil]]]];
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

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation
{
    backButton.enabled = webview.canGoBack;
    forwardButton.enabled = webview.canGoForward;
    if ([webView.URL.scheme isEqualToString:@"file"] && [webView.URL.lastPathComponent isEqualToString:@"help.html"]) {
        [webView evaluateJavaScript:[NSString stringWithFormat:@"document.getElementById('version').innerHTML = '%@';", [NSBundle mainBundle].infoDictionary[@"CFBundleVersion"]] completionHandler:NULL];
        [webView evaluateJavaScript:[NSString stringWithFormat:@"document.getElementById('orig_version').innerHTML = '%s';", ver] completionHandler:NULL];
    }
}

@end
