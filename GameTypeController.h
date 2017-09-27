//
//  GameTypeController.h
//  Puzzles
//
//  Created by Greg Hewgill on 11/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "GameSettingsController.h"
#import "GameView.h"

@interface GameTypeController : UITableViewController <GameSettingsDelegate>

- (id)initWithGame:(const game *)game midend:(midend *)me menu:(struct preset_menu *)pm gameview:(GameView *)gv;

@end
