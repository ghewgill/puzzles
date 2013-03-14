//
//  GameViewController.h
//  Puzzles
//
//  Created by Greg Hewgill on 8/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import <UIKit/UIKit.h>

#include "puzzles.h"

@protocol GameViewControllerSaver <NSObject>

- (void)saveGame:(NSString *)name state:(NSString *)save inprogress:(BOOL)inprogress;

@end

@interface GameViewController : UIViewController

- (id)initWithGame:(const game *)g saved:(NSString *)saved inprogress:(BOOL)inprogress saver:(id<GameViewControllerSaver>)saver;

@end
