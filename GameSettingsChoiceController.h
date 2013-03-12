//
//  GameSettingsChoiceController.h
//  Puzzles
//
//  Created by Greg Hewgill on 12/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface GameSettingsChoiceController : UITableViewController

- (id)initWithIndex:(int)index choices:(NSArray *)choices value:(int)value title:(NSString *)title;

@end
