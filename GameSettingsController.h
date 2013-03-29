//
//  GameSettingsController.h
//  Puzzles
//
//  Created by Greg Hewgill on 11/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "GameSettingsChoiceController.h"

#include "puzzles.h"

@protocol GameSettingsDelegate <NSObject>

- (void)didApply:(config_item *)config;

@end

@interface GameSettingsController : UITableViewController <GameSettingsChoiceDelegate>

- (id)initWithGame:(const game *)game config:(config_item *)config type:(int)type title:(NSString *)title delegate:(id<GameSettingsDelegate>)delegate;

@end
