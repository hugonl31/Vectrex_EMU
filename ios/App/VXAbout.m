/* VXAbout.m - Settings > About and help: the version, how to play, online play, your own games, the credits and the
 * licence. Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#import "VXApp.h"

@interface VXAbout : UIViewController
@end
@implementation VXAbout
- (void) viewDidLoad
{
	[super viewDidLoad]; self.title = @"About and help"; self.view.backgroundColor = VX_BG;
	NSDictionary *ib = NSBundle.mainBundle.infoDictionary;
	NSString *ver = [NSString stringWithFormat:@"%@ (build %@)", ib[@"CFBundleShortVersionString"] ?: @"?", ib[@"CFBundleVersion"] ?: @"?"];
	NSArray *parts = @[
		@[ @"Vectrex_EMU", [NSString stringWithFormat:@"Version %@. The Vectrex as it looks on a real tube: the beam, the phosphor's afterglow, the glow, the overlays, the glass and the case.", ver] ],
		@[ @"Playing", @"MENU (top bar) opens the game menu: save and load, games, RESET, overlay, room light, all settings. With a controller: Select + X; with a keyboard: M.\nThe knob below the picture: the volume; hold it to switch the Vectrex off and on.\nTouch a setting and hold: what it does, and back to its default." ] ,
		@[ @"Controls", @"The touch controls show while no controller or keyboard is connected (Settings > Controls: always, never, their size and side). Controller buttons 1-4 are chosen there too. In a menu or the games: button 4 picks, button 1 goes back.\nA UVMC2 game's calibration: hold buttons 2 + 3 while button 4 starts it, or touch and hold the game: Calibration (2 + 3 held)." ],
		@[ @"Online", @"Game menu > Online: host your game in the game room (anyone can join), with a code (only who you tell), or on this Wi-Fi (no internet needed); join with a code or from the lists. Phones and PCs play together - both need the same game and the same version of Vectrex_EMU. A menu open pauses both players." ],
		@[ @"Your own games", @"Games > + adds ROMs from Files (.bin .vec .gam .um2). Or use the Files app: On My iPhone > Vectrex_EMU > roms." ],
		@[ @"Credits", @"Vectrex_EMU by Hugo Bokhorst, started from vecx, the open-source Vectrex emulator. Overlays: the libretro overlay-borders collection and their creators (the full list: overlays/CREDITS.md in the app's files). Last Human and the UVMC2 games with their makers' permission. Box art and title artwork: Simon Quincey's Vectrex 2D Front Cover Box Art pack, used with permission. Label art and reconstructions by Hugo Bokhorst.\nVectrex is a trademark of its owners; this app is not affiliated with them." ],
		@[ @"Licence", @"Free software: GNU General Public License version 3. The source code: github.com/hugonl31/Vectrex_EMU" ] ];
	UIStackView *st = [UIStackView new]; st.axis = UILayoutConstraintAxisVertical; st.spacing = 18; st.translatesAutoresizingMaskIntoConstraints = NO;
	for (NSArray *p in parts) {
		UILabel *h = [UILabel new]; h.text = [p[0] uppercaseString]; h.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightSemibold]; h.textColor = VX_HI;
		UILabel *b = [UILabel new]; b.text = p[1]; b.font = [UIFont systemFontOfSize:15]; b.textColor = VX_FG; b.numberOfLines = 0;
		UIStackView *g = [[UIStackView alloc] initWithArrangedSubviews:@[ h, b ]]; g.axis = UILayoutConstraintAxisVertical; g.spacing = 6; [st addArrangedSubview:g]; }
	UIScrollView *sc = [UIScrollView new]; sc.translatesAutoresizingMaskIntoConstraints = NO; [sc addSubview:st]; [self.view addSubview:sc];
	[NSLayoutConstraint activateConstraints:@[ [sc.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor], [sc.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
		[sc.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor], [sc.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
		[st.topAnchor constraintEqualToAnchor:sc.contentLayoutGuide.topAnchor constant:16], [st.bottomAnchor constraintEqualToAnchor:sc.contentLayoutGuide.bottomAnchor constant:-32],
		[st.leadingAnchor constraintEqualToAnchor:sc.frameLayoutGuide.leadingAnchor constant:20], [st.trailingAnchor constraintEqualToAnchor:sc.frameLayoutGuide.trailingAnchor constant:-20] ]];
}
@end
UIViewController *vxa_about_vc (void) { return [VXAbout new]; }
