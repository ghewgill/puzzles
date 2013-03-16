//
//  GameMenuController.h
//  Puzzles
//
//  Created by Greg Hewgill on 16/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import <UIKit/UIKit.h>

#include "puzzles.h"

@interface GameMenuController : UITableViewController <UITextFieldDelegate>

- (id)initWithGameView:(id)gameview midend:(midend *)me;

@end
