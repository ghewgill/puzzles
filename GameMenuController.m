//
//  GameMenuController.m
//  Puzzles
//
//  Created by Greg Hewgill on 16/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameMenuController.h"

@interface GameMenuController ()

@end

@implementation GameMenuController {
    id gameview;
    midend *me;
    bool specificGameOpen;
    bool specificSeedOpen;
}

- (id)initWithGameView:(id)gv midend:(midend *)m
{
    self = [super initWithStyle:UITableViewStyleGrouped];
    if (self) {
        // Custom initialization
        gameview = gv;
        me = m;
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    // Uncomment the following line to preserve selection between presentations.
    // self.clearsSelectionOnViewWillAppear = NO;
 
    // Uncomment the following line to display an Edit button in the navigation bar for this view controller.
    // self.navigationItem.rightBarButtonItem = self.editButtonItem;
}

- (void)viewWillAppear:(BOOL)animated
{
    specificGameOpen = NO;
    specificSeedOpen = NO;
    [self.tableView reloadData];
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
    return 3;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    // Return the number of rows in the section.
    switch (section) {
        case 1:
            if (specificGameOpen) {
                return 2;
            }
            break;
        case 2:
            if (specificSeedOpen) {
                return 2;
            }
            break;
    }
    return 1;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    static NSString *CellIdentifier = @"Cell";
    //UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:CellIdentifier forIndexPath:indexPath];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:CellIdentifier];
    UITextField *field;
    if (indexPath.row == 1) {
        field = [[UITextField alloc] initWithFrame:CGRectMake(10, (cell.contentView.frame.size.height-31)/2, 240, 31)];
        field.tag = indexPath.section;
        field.delegate = self;
        [cell.contentView addSubview:field];
    }
    
    // Configure the cell...
    cell.textLabel.textAlignment = NSTextAlignmentCenter;
    switch (indexPath.section) {
        case 0:
            cell.textLabel.text = @"New Game";
            break;
        case 1:
            switch (indexPath.row) {
                case 0:
                    cell.textLabel.text = @"Specific Game";
                    break;
                case 1: {
                    char *wintitle;
                    config_item *cfg = midend_get_config(me, CFG_DESC, &wintitle);
                    field.text = [NSString stringWithUTF8String:cfg[0].sval];
                    free_cfg(cfg);
                    break;
                }
            }
            break;
        case 2:
            switch (indexPath.row) {
                case 0:
                    cell.textLabel.text = @"Specific Random Seed";
                    break;
                case 1: {
                    char *wintitle;
                    config_item *cfg = midend_get_config(me, CFG_SEED, &wintitle);
                    field.text = [NSString stringWithUTF8String:cfg[0].sval];
                    free_cfg(cfg);
                    break;
                }
            }
            break;
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
    switch (indexPath.section) {
        case 0:
            [gameview performSelector:@selector(doNewGame)];
            break;
        case 1:
            specificGameOpen = YES;
            [self.tableView insertRowsAtIndexPaths:@[[NSIndexPath indexPathForItem:1 inSection:1]] withRowAnimation:UITableViewRowAnimationAutomatic];
            [[[self.tableView cellForRowAtIndexPath:[NSIndexPath indexPathForItem:1 inSection:1]] viewWithTag:1] becomeFirstResponder];
            break;
        case 2:
            specificSeedOpen = YES;
            [self.tableView insertRowsAtIndexPaths:@[[NSIndexPath indexPathForItem:1 inSection:2]] withRowAnimation:UITableViewRowAnimationAutomatic];
            [[[self.tableView cellForRowAtIndexPath:[NSIndexPath indexPathForItem:1 inSection:2]] viewWithTag:2] becomeFirstResponder];
            break;
    }
}

#pragma mark - Text field delegate

- (BOOL)textFieldShouldReturn:(UITextField *)textField
{
    NSLog(@"%g %g %g %g", textField.frame.origin.x, textField.frame.origin.y, textField.frame.size.width, textField.frame.size.height);
    switch (textField.tag) {
        case 1:
            [gameview performSelector:@selector(doSpecificGame:) withObject:textField.text];
            break;
        case 2:
            [gameview performSelector:@selector(doSpecificSeed:) withObject:textField.text];
            break;
    }
    return NO;
}

@end
