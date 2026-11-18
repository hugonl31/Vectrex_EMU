/* main.m - Vectrex_EMU for iPhone and iPad: the app's start. Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see
 * LICENSE.md. */
#import <UIKit/UIKit.h>
@interface VXAppDelegate : UIResponder <UIApplicationDelegate>
@end
int main (int argc, char *argv[])
{
	@autoreleasepool { return UIApplicationMain (argc, argv, nil, NSStringFromClass ([VXAppDelegate class])); }
}
