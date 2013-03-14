//
//  GameListViewController.m
//  Puzzles
//
//  Created by Greg Hewgill on 8/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameListViewController.h"

#import "GameViewController.h"

#include "puzzles.h"

NSMutableSet *g_InProgress;

@interface GameListViewController ()

@end

@implementation GameListViewController {
    NSString *path;
}

- (id)initWithStyle:(UITableViewStyle)style
{
    self = [super initWithStyle:style];
    if (self) {
        // Custom initialization
        self.title = @"Puzzles";
        g_InProgress = [[NSMutableSet alloc] init];
        path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
        NSArray *files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:path error:NULL];
        for (NSString *fn in files) {
            if ([fn hasSuffix:@".save"]) {
                [g_InProgress addObject:[fn substringToIndex:fn.length-5]];
            }
        }
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

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [self.tableView reloadData];
}

- (void)saveGame:(NSString *)name state:(NSString *)save inprogress:(BOOL)inprogress
{
    if (inprogress) {
        [g_InProgress addObject:name];
    } else {
        [g_InProgress removeObject:name];
    }
    [save writeToFile:[NSString stringWithFormat:@"%@/%@.%@", path, name, (inprogress ? @"save" : @"new")] atomically:YES encoding:NSUTF8StringEncoding error:NULL];
    if (!inprogress) {
        [[NSFileManager defaultManager] removeItemAtPath:[NSString stringWithFormat:@"%@/%@.save", path, name] error:NULL];
    }
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
    return gamecount;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    static NSString *CellIdentifier = @"Cell";
    //UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:CellIdentifier forIndexPath:indexPath];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:CellIdentifier];
    
    // Configure the cell...
    NSString *name = [NSString stringWithUTF8String:gamelist[indexPath.row]->name];
    cell.textLabel.text = name;
    NSString *iconname = [[name stringByReplacingOccurrencesOfString:@" " withString:@""] lowercaseString];
    if ([iconname isEqualToString:@"rectangles"]) {
        iconname = @"rect";
    }
    cell.imageView.image = [UIImage imageNamed:[iconname stringByAppendingString:@"-96d24.png"]];
    if ([g_InProgress containsObject:name]) {
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
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
    const game *game = gamelist[indexPath.row];
    NSString *name = [NSString stringWithUTF8String:game->name];
    BOOL inprogress = YES;
    NSString *saved = [NSString stringWithContentsOfFile:[NSString stringWithFormat:@"%@/%@.save", path, name] encoding:NSUTF8StringEncoding error:NULL];
    if (saved == nil) {
        saved = [NSString stringWithContentsOfFile:[NSString stringWithFormat:@"%@/%@.new", path, name] encoding:NSUTF8StringEncoding error:NULL];
        inprogress = NO;
    }
    GameViewController *gv = [[GameViewController alloc] initWithGame:game saved:saved inprogress:inprogress saver:self];
    [self.navigationController pushViewController:gv animated:YES];
}

@end
