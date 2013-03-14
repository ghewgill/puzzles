//
//  GameSettingsController.m
//  Puzzles
//
//  Created by Greg Hewgill on 11/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameSettingsController.h"

#import "GameSettingsChoiceController.h"

@interface GameSettingsController ()

@end

@implementation GameSettingsController {
    midend *me;
    GameView *gameview;
    config_item *config_items;
    int num;
    NSArray *choiceText;
}

- (id)initWithMidend:(midend *)m gameview:(GameView *)gv;
{
    self = [super initWithStyle:UITableViewStyleGrouped];
    if (self) {
        // Custom initialization
        me = m;
        gameview = gv;
        char *wintitle;
        config_items = midend_get_config(me, CFG_SETTINGS, &wintitle);
        self.title = [NSString stringWithUTF8String:wintitle];
        free(wintitle);
        NSMutableArray *choices = [[NSMutableArray alloc] init];
        num = 0;
        while (config_items[num].type != C_END) {
            [choices addObject:@[]];
            if (config_items[num].type == C_CHOICES) {
                NSString *sval = [NSString stringWithUTF8String:config_items[num].sval];
                NSCharacterSet *delim = [NSCharacterSet characterSetWithCharactersInString:[sval substringToIndex:1]];
                choices[num] = [[sval substringFromIndex:1] componentsSeparatedByCharactersInSet:delim];
            }
            num++;
        }
        choiceText = choices;
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

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    // Return the number of sections.
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    // Return the number of rows in the section.
    switch (section) {
        case 0:
            return num;
        case 1:
            return 1;
        default:
            return 0;
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    static NSString *CellIdentifier = @"Cell";
    //UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:CellIdentifier forIndexPath:indexPath];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:CellIdentifier];
    
    // Configure the cell...
    int roffset = [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad ? 40 : 0;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    switch (indexPath.section) {
        case 0:
            cell.textLabel.text = [NSString stringWithUTF8String:config_items[indexPath.row].name];
            switch (config_items[indexPath.row].type) {
                case C_STRING: {
                    UITextField *text = [[UITextField alloc] initWithFrame:CGRectMake(self.view.frame.size.width-100-roffset, 12, 80, 31)];
                    text.tag = indexPath.row;
                    [text addTarget:self action:@selector(valueChanged:) forControlEvents:UIControlEventEditingChanged];
                    text.textAlignment = NSTextAlignmentRight;
                    text.text = [NSString stringWithUTF8String:config_items[indexPath.row].sval];
                    [cell addSubview:text];
                    break;
                }
                case C_BOOLEAN: {
                    UISwitch *sw = [[UISwitch alloc] initWithFrame:CGRectMake(self.view.frame.size.width-95-roffset, 9, 80, 31)];
                    sw.tag = indexPath.row;
                    [sw addTarget:self action:@selector(valueChanged:) forControlEvents:UIControlEventValueChanged];
                    sw.on = config_items[indexPath.row].ival;
                    [cell addSubview:sw];
                    break;
                }
                case C_CHOICES: {
                    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
                    UITextField *label = [[UITextField alloc] initWithFrame:CGRectMake(self.view.frame.size.width-100-roffset, 11, 65, 31)];
                    label.enabled = NO;
                    label.textAlignment = NSTextAlignmentRight;
                    label.text = choiceText[indexPath.row][config_items[indexPath.row].ival];
                    [cell addSubview:label];
                    break;
                }
            }
            break;
        case 1:
            cell.textLabel.text = @"Apply";
            cell.textLabel.textAlignment = NSTextAlignmentCenter;
            break;
    }
    
    return cell;
}

- (void)willRotateToInterfaceOrientation:(UIInterfaceOrientation)toInterfaceOrientation duration:(NSTimeInterval)duration
{
    [self.tableView reloadData];
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

- (void)valueChanged:(UIControl *)sender
{
    config_item *item = &config_items[sender.tag];
    switch (item->type) {
        case C_STRING: {
            UITextField *text = (UITextField *)sender;
            free(item->sval);
            item->sval = dupstr([text.text cStringUsingEncoding:NSUTF8StringEncoding]);
            break;
        }
        case C_BOOLEAN: {
            UISwitch *sw = (UISwitch *)sender;
            item->ival = sw.on;
            break;
        }
        case C_CHOICES: {
            break;
        }
    }
}

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
    if (indexPath.section == 0) {
        if (config_items[indexPath.row].type == C_CHOICES) {
            [self.navigationController pushViewController:[[GameSettingsChoiceController alloc] initWithIndex:indexPath.row choices:choiceText[indexPath.row] value:config_items[indexPath.row].ival title:[NSString stringWithUTF8String:config_items[indexPath.row].name]] animated:YES];
        }
    }
    if (indexPath.section == 1 && indexPath.row == 0) {
        char *msg = midend_set_config(me, CFG_SETTINGS, config_items);
        if (msg) {
            [[[UIAlertView alloc] initWithTitle:@"Puzzles" message:[NSString stringWithUTF8String:msg] delegate:nil cancelButtonTitle:@"Close" otherButtonTitles:nil] show];
        } else {
            midend_new_game(me);
            [gameview layoutSubviews];
            // bit of a hack here, gameview.nextResponder is actually the view controller we want
            [self.navigationController popToViewController:(UIViewController *)gameview.nextResponder animated:YES];
        }
    }
}

- (void)setChoice:(int)index value:(int)value
{
    config_items[index].ival = value;
    [self.tableView reloadData];
}

@end
