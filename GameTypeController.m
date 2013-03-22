//
//  GameTypeController.m
//  Puzzles
//
//  Created by Greg Hewgill on 11/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameTypeController.h"

@interface GameTypeController ()

@end

@implementation GameTypeController {
    midend *me;
    GameView *gameview;
}

- (id)initWithMidend:(midend *)m gameview:(GameView *)gv
{
    self = [super initWithStyle:UITableViewStyleGrouped];
    if (self) {
        // Custom initialization
        me = m;
        gameview = gv;
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = @"Game Type";

    // Uncomment the following line to preserve selection between presentations.
    // self.clearsSelectionOnViewWillAppear = NO;
 
    // Uncomment the following line to display an Edit button in the navigation bar for this view controller.
    // self.navigationItem.rightBarButtonItem = self.editButtonItem;
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    // Return the number of sections.
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    // Return the number of rows in the section.
    return midend_num_presets(me) + 1;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    static NSString *CellIdentifier = @"Cell";
    //UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:CellIdentifier forIndexPath:indexPath];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:CellIdentifier];
    
    // Configure the cell...
    if (indexPath.row < midend_num_presets(me)) {
        char *name;
        game_params *params;
        midend_fetch_preset(me, indexPath.row, &name, &params);
        cell.textLabel.text = [NSString stringWithUTF8String:name];
        if (indexPath.row == midend_which_preset(me)) {
            cell.accessoryType = UITableViewCellAccessoryCheckmark;
        }
    } else {
        cell.textLabel.text = @"Custom";
        if (midend_which_preset(me) < 0) {
            cell.accessoryType = UITableViewCellAccessoryCheckmark;
        }
    }
    
    return cell;
}

/*
// Override to support conditional editing of the table view.
- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
    // Return NO if you do not want the specified item to be editable.
    return YES;
}
*/

/*
// Override to support editing the table view.
- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (editingStyle == UITableViewCellEditingStyleDelete) {
        // Delete the row from the data source
        [tableView deleteRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationFade];
    }   
    else if (editingStyle == UITableViewCellEditingStyleInsert) {
        // Create a new instance of the appropriate class, insert it into the array, and add a new row to the table view
    }   
}
*/

/*
// Override to support rearranging the table view.
- (void)tableView:(UITableView *)tableView moveRowAtIndexPath:(NSIndexPath *)fromIndexPath toIndexPath:(NSIndexPath *)toIndexPath
{
}
*/

/*
// Override to support conditional rearranging of the table view.
- (BOOL)tableView:(UITableView *)tableView canMoveRowAtIndexPath:(NSIndexPath *)indexPath
{
    // Return NO if you do not want the item to be re-orderable.
    return YES;
}
*/

#pragma mark - Table view delegate

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    // Navigation logic may go here. Create and push another view controller.
    /*
     <#DetailViewController#> *detailViewController = [[<#DetailViewController#> alloc] initWithNibName:@"<#Nib name#>" bundle:nil];
     // ...
     // Pass the selected object to the new view controller.
     [self.navigationController pushViewController:detailViewController animated:YES];
     */
    if (indexPath.row < midend_num_presets(me)) {
        char *name;
        game_params *params;
        midend_fetch_preset(me, indexPath.row, &name, &params);
        midend_set_params(me, params);
        midend_new_game(me);
        [gameview layoutSubviews];
        // bit of a hack here, gameview.nextResponder is actually the view controller we want
        [self.navigationController popToViewController:(UIViewController *)gameview.nextResponder animated:YES];
    } else {
        char *wintitle;
        config_item *config = midend_get_config(me, CFG_SETTINGS, &wintitle);
        [self.navigationController pushViewController:[[GameSettingsController alloc] initWithConfig:config title:[NSString stringWithUTF8String:wintitle] delegate:self] animated:YES];
        free(wintitle);
    }
}

- (void)didApply:(config_item *)config
{
    char *msg = midend_set_config(me, CFG_SETTINGS, config);
    if (msg) {
        [[[UIAlertView alloc] initWithTitle:@"Puzzles" message:[NSString stringWithUTF8String:msg] delegate:nil cancelButtonTitle:@"Close" otherButtonTitles:nil] show];
    } else {
        midend_new_game(me);
        [gameview layoutSubviews];
        // bit of a hack here, gameview.nextResponder is actually the view controller we want
        [self.navigationController popToViewController:(UIViewController *)gameview.nextResponder animated:YES];
    }
}

@end
