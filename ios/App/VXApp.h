/* VXApp.h - what the app's screens (VXUI.m) use of its core host (VXApp.m). Copyright (c) 2026 Hugo Bokhorst. Part of
 * Vectrex_EMU; see LICENSE.md. */
#import <UIKit/UIKit.h>

/* the core's settings, as its option definitions give them */
int vxa_nopts (void);
const char *vxa_okey (int i), *vxa_odesc (int i), *vxa_ocat (int i), *vxa_oinfo (int i), *vxa_ovals (int i), *vxa_oval (int i), *vxa_odef (int i);
int vxa_ofind (const char *key);
void vxa_oset (int i, const char *value);
int vxa_ncats (void);
const char *vxa_ckey (int i), *vxa_cdesc (int i), *vxa_cinfo (int i);

/* actions */
void vxa_do (NSString *what);          /* reset, eject, pause, overlay */
void vxa_load (NSString *path);        /* a game, its full path */
NSString *vxa_docs (void);
NSString *vxa_game (void);             /* the game in the slot now, its full path */
int vxa_state (int save, int slot);    /* 1: done; 0: this game has no save states */
int vxa_state_exists (int slot);
int vxa_state_supported (void);     /* 0: UVMC2 / PiTrex - no save states */
void vxa_hold (int on);                /* the game stopped while a screen covers it */
void ulog (NSString *s);               /* into the app's live log */

/* the theme: the PC app's colours */
#define VXC(rgb) [UIColor colorWithRed:((rgb) >> 16 & 255) / 255.0 green:((rgb) >> 8 & 255) / 255.0 blue:((rgb) & 255) / 255.0 alpha:1]
#define VX_BG VXC (0x05080a)
#define VX_BAR VXC (0x0a1210)
#define VX_PANEL VXC (0x0b1511)
#define VX_BODY VXC (0x0f1c17)
#define VX_EDGE VXC (0x1d3329)
#define VX_EDGE2 VXC (0x2f5544)
#define VX_SEL VXC (0x24443a)
#define VX_G VXC (0x66aa88)
#define VX_HI VXC (0x88ccbb)
#define VX_HI2 VXC (0xbbffee)
#define VX_FG VXC (0xc8d6cf)
#define VX_FG2 VXC (0xa9bdb3)
#define VX_DIM VXC (0x7f978b)
#define VX_CHG VXC (0xd9a35e)

/* the screens (VXUI.m) */
@interface VXUI : NSObject
+ (void) showLibraryFrom:(UIViewController *)vc;
+ (void) showGameMenuFrom:(UIViewController *)vc;
+ (void) showSettingsFrom:(UIViewController *)vc;
+ (void) adjustOption:(int)i over:(UIViewController *)vc;   /* the one-slider panel over the running game */
+ (UIView *) quickBarFor:(UIViewController *)vc;              /* the row above the picture */
+ (void) styleNav:(UINavigationController *)n;
+ (void) showSettingsFrom:(UIViewController *)vc at:(int)focus;   /* (scrolled to that setting) */
+ (void) adjustOption:(int)i over:(UIViewController *)vc fromSettings:(BOOL)fs;
@end
@interface VXUI (Welcome)
+ (UIView *) welcomeView;                                      /* (tag 11: the line of text, 12: the progress bar) */
+ (void) showReadyFrom:(UIViewController *)vc;                 /* (once, after the first games arrived) */
@end
