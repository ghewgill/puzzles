//
//  GameSettingsController.h
//  Puzzles
//
//  Created by Greg Hewgill on 11/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "GameView.h"

@interface GameSettingsController : UITableViewController

- (id)initWithMidend:(midend *)me gameview:(GameView *)gv;
- (void)setChoice:(int)index value:(int)value;

@end
