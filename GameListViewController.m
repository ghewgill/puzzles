//
//  GameListViewController.m
//  Puzzles
//
//  Created by Greg Hewgill on 8/03/13.
//  Copyright (c) 2013 Greg Hewgill. All rights reserved.
//

#import "GameListViewController.h"

#import "GameViewController.h"
#import "GameHelpController.h"

#include "puzzles.h"
#include "descriptions.h"

//#define LAUNCH_IMAGE

NSMutableSet *g_InProgress;

static NSString *CellIdentifier = @"Cell";

@interface GameListViewCell: PSUICollectionViewCell
@end

@implementation GameListViewCell

- (id)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        if ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad) {
            self.contentView.backgroundColor = [UIColor whiteColor];
            
            UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, self.contentView.frame.size.width, 31)];
            label.tag = 1;
            label.font = [UIFont boldSystemFontOfSize:16];
            label.textAlignment = NSTextAlignmentCenter;
            [self.contentView addSubview:label];
        
            UIImageView *image = [[UIImageView alloc] initWithFrame:CGRectMake((self.contentView.frame.size.width-96)/2, 31, 96, 96)];
            image.tag = 2;
            [self.contentView addSubview:image];
            
            UILabel *detail = [[UILabel alloc] initWithFrame:CGRectMake(5, 31+96, self.contentView.frame.size.width-10, 50)];
            detail.tag = 3;
            detail.font = [UIFont systemFontOfSize:14];
            detail.numberOfLines = 0;
            [self.contentView addSubview:detail];
            
            UIImageView *inprogress = [[UIImageView alloc] initWithFrame:CGRectMake(self.contentView.frame.size.width-50, 50, 40, 40)];
            inprogress.tag = 4;
            inprogress.image = [UIImage imageNamed:@"inprogress.png"];
            [self.contentView addSubview:inprogress];
        } else {
            self.contentView.backgroundColor = [UIColor whiteColor];
            
            UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(100, 0, self.contentView.frame.size.width-100, 26)];
            label.tag = 1;
            label.font = [UIFont boldSystemFontOfSize:20];
            [self.contentView addSubview:label];
        
            UIImageView *image = [[UIImageView alloc] initWithFrame:CGRectMake(2, 2, 96, 96)];
            image.tag = 2;
            [self.contentView addSubview:image];
            
            UILabel *detail = [[UILabel alloc] initWithFrame:CGRectMake(100, 30, self.contentView.frame.size.width-100, self.contentView.frame.size.height-30)];
            detail.tag = 3;
            detail.font = [UIFont systemFontOfSize:14];
            detail.numberOfLines = 0;
            [self.contentView addSubview:detail];
            
            UIImageView *inprogress = [[UIImageView alloc] initWithFrame:CGRectMake(self.contentView.frame.size.width-40, 5, 40, 40)];
            inprogress.tag = 4;
            inprogress.image = [UIImage imageNamed:@"inprogress.png"];
            [self.contentView addSubview:inprogress];
        }
    }
    return self;
}

@end

@interface GameListViewController ()

@end

@implementation GameListViewController {
    NSString *path;
    NSDictionary *descriptions;
}

- (id)init
{
    PSUICollectionViewFlowLayout *layout = [[PSUICollectionViewFlowLayout alloc] init];
    if ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad) {
        layout.itemSize = CGSizeMake(246, 31+96+50);
    } else {
        layout.itemSize = CGSizeMake(320, 100);
    }
    self = [super initWithCollectionViewLayout:layout];
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
        NSMutableDictionary *descs = [[NSMutableDictionary alloc] init];
        for (int i = 0; i < sizeof(GameDescriptions)/sizeof(GameDescriptions[0]); i++) {
            descs[GameDescriptions[i][0]] = GameDescriptions[i][1];
        }
        descriptions = descs;
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self.collectionView registerClass:[GameListViewCell class] forCellWithReuseIdentifier:CellIdentifier];

    // Uncomment the following line to preserve selection between presentations.
    // self.clearsSelectionOnViewWillAppear = NO;
 
    // Uncomment the following line to display an Edit button in the navigation bar for this view controller.
    // self.navigationItem.rightBarButtonItem = self.editButtonItem;
#ifndef LAUNCH_IMAGE
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Help" style:UIBarButtonItemStylePlain target:self action:@selector(showHelp)];
#endif
    
    NSString *lastgame = [[NSUserDefaults standardUserDefaults] stringForKey:@"lastgame"];
    if (lastgame) {
        int i = gamecount-1;
        while (i >= 0) {
            if ([[NSString stringWithUTF8String:gamelist[i]->name] isEqualToString:lastgame]) {
                break;
            }
            i--;
        }
        if (i >= 0) {
            [self collectionView:self.collectionView didSelectItemAtIndexPath:[NSIndexPath indexPathForRow:i inSection:0]];
        }
    }
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"lastgame"];
    [self.collectionView reloadData];
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

- (void)showHelp
{
    [self.navigationController pushViewController:[[GameHelpController alloc] initWithFile:@"help.html"] animated:YES];
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInCollectionView:(UICollectionView *)collectionView
{
    // Return the number of sections.
#ifdef LAUNCH_IMAGE
    return 0;
#endif
    return 1;
}

- (NSInteger)collectionView:(UICollectionView *)collectionView numberOfItemsInSection:(NSInteger)section
{
    // Return the number of rows in the section.
    return gamecount;
}

- (UICollectionViewCell *)collectionView:(UICollectionView *)collectionView cellForItemAtIndexPath:(NSIndexPath *)indexPath
{
    //UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:CellIdentifier forIndexPath:indexPath];
    UICollectionViewCell *cell = [self.collectionView dequeueReusableCellWithReuseIdentifier:CellIdentifier forIndexPath:indexPath];
    UILabel *label = (UILabel *)[cell viewWithTag:1];
    UIImageView *image = (UIImageView *)[cell viewWithTag:2];
    UILabel *detail = (UILabel *)[cell viewWithTag:3];
    UIImageView *inprogress = (UIImageView *)[cell viewWithTag:4];
    
    // Configure the cell...
    NSString *name = [NSString stringWithUTF8String:gamelist[indexPath.row]->name];
    label.text = name;
    detail.text = descriptions[name];
    NSString *iconname = [[name stringByReplacingOccurrencesOfString:@" " withString:@""] lowercaseString];
    if ([iconname isEqualToString:@"rectangles"]) {
        iconname = @"rect";
    }
    image.image = [UIImage imageNamed:[iconname stringByAppendingString:@"-96d24.png"]];
    inprogress.hidden = ![g_InProgress containsObject:name];
    
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

- (void)collectionView:(UICollectionView *)collectionView didSelectItemAtIndexPath:(NSIndexPath *)indexPath
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
    [[NSUserDefaults standardUserDefaults] setObject:name forKey:@"lastgame"];
}

@end
