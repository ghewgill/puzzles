//
//  GameHelpController.h
//  Puzzles
//
//  Created by Greg Hewgill on 16/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>

@interface GameHelpController : UIViewController <WKNavigationDelegate>

- (id)initWithFile:(NSString *)file;

@end
