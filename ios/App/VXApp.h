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

/* online play (the core's netvx.c: the desktop's way - phones and PCs together) */
int vx_net_host (int mode, const char *gamer, const char *key, const char *rom, int skip_boot, const char *cart_hw, double dacofs);
int vx_net_join (const char *where, const char *gamer, const char *key);
void vx_net_accept (int ok), vx_net_prepare (void), vx_net_go (void), vx_net_leave (void), vx_net_chat (const char *text);
void vx_net_pause (int on), vx_net_room (void), vx_net_lan (int on);
const char *vx_net_status (void);
int vx_net_regame (const char *rom, int skip_boot, const char *cart_hw, double dacofs); void vx_net_regame_answer (const char *path);
int vx_net_go_hot (void); void vx_net_prepare_hot (void), vx_net_take_port (int k);   /* (joining a running game; a port taken) */
int vx_app_port2 (void), vx_app_cart_out (void), vx_app_power_on (void), vx_app_paused (void);   /* (the deck's states) */
double vx_app_volume (void); void vx_app_turn (double d), vx_app_power (void);
int vxa_is_held (void); void vxa_toast_ext (NSString *s);
int vx_net_md5_file (const char *path, char *out33, long long *size);
NSString *vxa_net_ip (void);           /* this phone's address on its network */
NSString *vxa_net_gateway (void);      /* the nearby host: .1 of this phone's Wi-Fi (an ad-hoc network's host), nil: none */
NSString *vxa_net_name (void), *vxa_net_key (void);
int vxa_net_playing (void);              /* (1 while a game for two runs: no save states) */

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
@interface VXUI (Parts)
+ (void) choosePartsIn:(UIViewController *)vc parts:(NSArray *)parts installed:(NSDictionary *)inst first:(BOOL)first go:(void (^) (NSArray<NSString *> *, BOOL))go later:(void (^) (void))later;
@end
@interface VXUI (Deck)
+ (UIView *) deckFor:(UIViewController *)vc;                  /* (the native control deck, below the picture) */
+ (void) showChoice:(int)i from:(UIViewController *)vc;
@end
@interface VXUI (Controls)
+ (void) showControlsFrom:(UIViewController *)vc;
@end
@interface VXUI (Online)
+ (void) showOnlineFrom:(UIViewController *)vc;
@end
@interface VXDeck : UIView
@property (nonatomic, weak) UIViewController *game;
@property (nonatomic) int mode;                  /* (1 full, 2 slim) */
- (void) tick;
@end
