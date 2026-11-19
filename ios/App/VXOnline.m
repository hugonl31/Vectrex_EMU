/* VXOnline.m - online play: host or join a game for two, the desktop's way (the core's netvx.c - a phone plays a PC
 * too): in the game room (public), with a code (private), or on this Wi-Fi without the internet; the game room's
 * waiting games and the ones on this network, a tap to join. The game itself is started by the app (VXApp.m vxa_net_tick)
 * once both sides agree. Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#import "VXApp.h"
#import <Security/Security.h>

static UIFont *OF (CGFloat s, UIFontWeight w) { return [UIFont systemFontOfSize:s weight:w]; }
static UITableViewCell *OCell (NSString *t, NSString *d, BOOL tap)
{
	UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
	c.textLabel.text = t; c.textLabel.textColor = tap ? VX_HI : VX_FG; c.textLabel.numberOfLines = 0;
	c.detailTextLabel.text = d; c.detailTextLabel.textColor = VX_DIM; c.detailTextLabel.numberOfLines = 0;
	c.backgroundColor = VX_PANEL; UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb;
	if (!tap) c.selectionStyle = UITableViewCellSelectionStyleNone;
	return c;
}
NSString *vxa_net_name (void)
{
	NSString *n = [NSUserDefaults.standardUserDefaults stringForKey:@"net.name"];
	return n.length ? n : @"";
}
/* this phone's secret for its name at the meeting point: made once, kept in the Keychain - it stays through a reinstall,
 * so a fresh install gets its name back (vxa_name_first) */
static NSDictionary *OKeyQuery (void) { return @{ (id) kSecClass: (id) kSecClassGenericPassword, (id) kSecAttrService: @"com.hugob.vectrexemu.net", (id) kSecAttrAccount: @"key" }; }
static NSString *OKeychainGet (void)
{
	NSMutableDictionary *q = [OKeyQuery () mutableCopy]; q[(id) kSecReturnData] = @YES; q[(id) kSecMatchLimit] = (id) kSecMatchLimitOne;
	CFTypeRef r = NULL; if (SecItemCopyMatching ((__bridge CFDictionaryRef) q, &r) != errSecSuccess || !r) return nil;
	NSData *d = (__bridge_transfer NSData *) r; return [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding];
}
static void OKeychainSet (NSString *k)
{
	SecItemDelete ((__bridge CFDictionaryRef) OKeyQuery ()); NSMutableDictionary *a = [OKeyQuery () mutableCopy];
	a[(id) kSecValueData] = [k dataUsingEncoding:NSUTF8StringEncoding]; a[(id) kSecAttrAccessible] = (id) kSecAttrAccessibleAfterFirstUnlock;
	OSStatus st = SecItemAdd ((__bridge CFDictionaryRef) a, NULL); if (st != errSecSuccess) ulog ([NSString stringWithFormat:@"online: key not kept in the Keychain (%d)", (int) st]);
}
NSString *vxa_net_key (void)
{
	static NSString *k; if (k.length) return k;
	k = OKeychainGet ();
	if (!k.length) { k = [NSUserDefaults.standardUserDefaults stringForKey:@"net.key"];   /* (an earlier build's: into the Keychain) */
		if (!k.length) { NSMutableString *m = [NSMutableString string]; for (int i = 0; i < 16; i++) [m appendFormat:@"%02x", arc4random_uniform (256)]; k = m; }
		OKeychainSet (k); }
	[NSUserDefaults.standardUserDefaults setObject:k forKey:@"net.key"];
	return k;
}
/* the name reserved at the meeting point (on a background queue; done on the main one): 1 reserved, 0 another player's,
 * -1 not allowed, -2 no answer (kept here, reserved at the next online game) */
static void OClaim (NSString *name, void (^done) (int, NSString *))
{
	NSString *old = [NSUserDefaults.standardUserDefaults boolForKey:@"net.name_ok"] ? vxa_net_name () : nil, *key = vxa_net_key ();
	dispatch_async (dispatch_get_global_queue (QOS_CLASS_UTILITY, 0), ^{
		char got[64] = ""; const int r = vx_net_claim (name.UTF8String, key.UTF8String, old.length && ![old isEqualToString:name] ? old.UTF8String : NULL, got, sizeof got);
		NSString *g = r == 1 && got[0] ? @(got) : name;
		dispatch_async (dispatch_get_main_queue (), ^{
			if (r == 1 || r == -2) { [NSUserDefaults.standardUserDefaults setObject:g forKey:@"net.name"]; [NSUserDefaults.standardUserDefaults setBool:r == 1 forKey:@"net.name_ok"]; }
			ulog ([NSString stringWithFormat:@"online: name %@: %s", name, r == 1 ? "reserved" : r == 0 ? "another player's" : r == -1 ? "not allowed" : "no answer - kept here"]);
			if (done) done (r, g); }); });
}
static NSString *OClaimSays (int r, NSString *n)
{
	return r == 1 ? [NSString stringWithFormat:@"YOUR PLAYER NAME: %@", n] : r == 0 ? [NSString stringWithFormat:@"%@ BELONGS TO ANOTHER PLAYER - PICK ANOTHER", n]
		: r == -1 ? @"THAT NAME IS NOT ALLOWED (A-Z, 0-9)" : [NSString stringWithFormat:@"NO ANSWER FROM THE MEETING POINT: %@ KEPT HERE", n];
}
static NSString *OClean (NSString *s);
static void OAsk (UIViewController *vc, NSString *msg)       /* (the name asked for: typed, checked and kept) */
{
	while (vc.presentedViewController) vc = vc.presentedViewController;
	UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Your player name" message:msg ?: @"The name other players see when you play online. Up to 10 letters A-Z and digits 0-9 - reserved for this phone, the same in every online mode." preferredStyle:UIAlertControllerStyleAlert];
	[a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.placeholder = @"YOUR NAME"; f.text = vxa_net_name (); f.autocapitalizationType = UITextAutocapitalizationTypeAllCharacters; f.autocorrectionType = UITextAutocorrectionTypeNo; }];
	[a addAction:[UIAlertAction actionWithTitle:@"Later" style:UIAlertActionStyleCancel handler:^(UIAlertAction *x) { (void) x; ulog (@"online: name later"); }]];
	__weak UIViewController *wv = vc;
	[a addAction:[UIAlertAction actionWithTitle:@"Check and keep" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) { (void) x;
		NSString *n = OClean (a.textFields.firstObject.text ?: @"");
		if (!n.length) { UIViewController *v = wv; if (v) OAsk (v, @"A name first: 1 to 10 letters A-Z or digits 0-9."); return; }
		OClaim (n, ^(int r, NSString *g) { UIViewController *v = wv;
			if (r == 0 || r == -1) { if (v) OAsk (v, [OClaimSays (r, g).capitalizedString stringByAppendingString:@"."]); }
			else vxa_toast_ext (OClaimSays (r, g)); }); }]];
	[vc presentViewController:a animated:YES completion:nil];
}
void vxa_name_first (UIViewController *vc)
{
	NSString *nm = vxa_net_name (); if (nm.length && [NSUserDefaults.standardUserDefaults boolForKey:@"net.name_ok"]) return;
	NSString *key = vxa_net_key (); __weak UIViewController *wv = vc;
	dispatch_async (dispatch_get_global_queue (QOS_CLASS_UTILITY, 0), ^{
		char got[64] = ""; const int r = vx_net_whoami (key.UTF8String, got, sizeof got); NSString *g = r == 1 && got[0] ? @(got) : nil;
		dispatch_async (dispatch_get_main_queue (), ^{
			if (g.length) { [NSUserDefaults.standardUserDefaults setObject:g forKey:@"net.name"]; [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"net.name_ok"];
				ulog ([NSString stringWithFormat:@"online: welcome back %@", g]); vxa_toast_ext ([NSString stringWithFormat:@"WELCOME BACK, %@", g]); return; }
			if (nm.length) { OClaim (nm, nil); return; }          /* (a name kept here while offline: reserved now) */
			UIViewController *v = wv; if (v) OAsk (v, nil); }); });
}
static NSString *OClean (NSString *s)                 /* (a gamer name: A-Z and 0-9 only, up to 10 - the rest left out) */
{
	NSMutableString *o = [NSMutableString string];
	for (NSUInteger i = 0; i < s.length && o.length < 10; i++) { unichar c = [s.uppercaseString characterAtIndex:i];
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) [o appendFormat:@"%C", c]; }
	return o;
}

@interface VXOnline : UITableViewController <UITextFieldDelegate>
@end
@implementation VXOnline { NSTimer *_tm; NSDictionary *_st; NSString *_last; NSArray *_secs; int _shownPlay, _sawSetup; }
- (void) viewDidLoad
{
	[super viewDidLoad]; self.title = @"Online";
	self.tableView.backgroundColor = VX_BG; self.tableView.separatorColor = VX_EDGE;
	self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector (done)];
	vx_net_lan (1); vx_net_room ();
	__weak VXOnline *ws = self;
	_tm = [NSTimer scheduledTimerWithTimeInterval:0.3 repeats:YES block:^(NSTimer *t) { [ws poll]; }];
	[self poll];
}
- (void) viewDidDisappear:(BOOL)a { [super viewDidDisappear:a]; [_tm invalidate]; _tm = nil; vx_net_lan (0); vxa_hold (0); }
- (void) done { [self dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); }]; }
- (int) phase { return [_st[@"phase"] intValue]; }
- (void) poll
{
	NSString *js = @(vx_net_status ()); NSDictionary *st = [NSJSONSerialization JSONObjectWithData:[js dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
	if (![st isKindOfClass:NSDictionary.class]) return;
	NSArray *chat = st[@"chat"]; if ([chat isKindOfClass:NSArray.class] && chat.count) { extern void vxa_net_chat_in (NSArray *lines); vxa_net_chat_in (chat); }
	NSMutableDictionary *k = [st mutableCopy]; [k removeObjectForKey:@"chat"]; [k removeObjectForKey:@"waiting"]; [k removeObjectForKey:@"frame"]; [k removeObjectForKey:@"sums"]; [k removeObjectForKey:@"checked"];
	NSString *key = [k description]; _st = st;
	if ([self phase] >= 1 && [self phase] <= 3) _sawSetup = 1;
	if ([self phase] == 4 && _sawSetup && !_shownPlay) { _shownPlay = 1; [self done]; return; }   /* (the game started here: back to it - not when opened during one) */
	if ([key isEqualToString:_last ?: @""]) return; _last = key;
	[self build]; [self.tableView reloadData];
}
/* the sections, as the state is: [title, footer, rows (title, detail, action)] */
- (void) build
{
	NSMutableArray *s = [NSMutableArray array]; int ph = [self phase], role = [_st[@"role"] intValue]; __weak VXOnline *ws = self;
	NSString *game = vxa_game ().lastPathComponent.stringByDeletingPathExtension;
	if (ph == 4) {                                    /* playing */
		NSString *peer = [_st[@"peer"] length] ? _st[@"peer"] : @"the other player";
		[s addObject:@[ @"Playing online", [NSString stringWithFormat:@"You are player %d. %@", role == 1 ? 1 : 2, [_st[@"relay"] intValue] ? @"The game goes through the meeting point (relayed): a little more delay." : @"A direct connection."], @[
			@[ [NSString stringWithFormat:@"With %@", peer], [NSString stringWithFormat:@"%d frames of input delay%@", [_st[@"delay"] intValue], [_st[@"desync"] intValue] >= 0 ? @" - OUT OF STEP: the two games differ" : @""], [NSNull null] ],
			@[ @"Chat", @"A line to the other player", ^{ [ws chat]; } ],
			@[ @"Leave the online game", @"", ^{ vx_net_leave (); [ws done]; } ] ] ]];
		_secs = s; return; }
	if (ph >= 1 && ph <= 3) {                         /* hosting or joining: what happens */
		NSString *t, *d; NSString *code = _st[@"code"];
		if (role == 1) {
			int mode = [_st[@"mode"] intValue];
			if (ph == 1) { t = @"Asking the meeting point for a code..."; d = @""; }
			else if (mode == 2) { t = @"Waiting on this Wi-Fi"; d = [NSString stringWithFormat:@"Player 2 picks your game under \"On this Wi-Fi\" or enters this phone's address: %@", vxa_net_ip ()]; }
			else { t = [NSString stringWithFormat:@"Your code:  %@", code.length ? code : @"..."]; d = mode == 0 ? @"Your game is in the game room. Player 2 picks it there, or enters the code." : @"Tell player 2 this code: they enter it under Join."; }
			if ([_st[@"peer"] length]) d = [NSString stringWithFormat:@"%@ is connecting...", _st[@"peer"]]; }
		else { t = ph == 3 ? @"The host's game came: starting..." : @"Connecting to the host..."; d = [_st[@"peer"] length] ? [NSString stringWithFormat:@"Host: %@", _st[@"peer"]] : @""; }
		[s addObject:@[ @"Online", @"", @[ @[ t, d, [NSNull null] ], @[ @"Cancel", @"", ^{ vx_net_leave (); [ws poll]; } ] ] ]]; }
	else {
		NSString *err = _st[@"error"];
		if ([err isKindOfClass:NSString.class] && err.length && ph == 5) [s addObject:@[ @"", @"", @[ @[ @"Not connected", err, [NSNull null] ] ] ]];
		[s addObject:@[ @"Your name", @"Up to 10 letters A-Z and digits 0-9. Players see it in the game room and the chat.", @[ @[ @"#name", @"", [NSNull null] ] ] ]];
		[s addObject:@[ game.length ? [NSString stringWithFormat:@"Host %@", game] : @"Host", game.length ? @"Player 2 needs the same game. Both start it together; you are player 1." : @"Start a game first, then host it here.", @[
			@[ @"In the game room", @"Public: anyone can join it there", ^{ [ws host:0]; } ],
			@[ @"With a code", @"Private: only who you tell the code", ^{ [ws host:1]; } ],
			@[ @"On this Wi-Fi", @"No internet needed: players on the same network", ^{ [ws host:2]; } ] ] ]];
		/* nearby, no router: the host makes the network (an Android phone or a PC does it itself; an iPhone: its Personal
		 *  Hotspot), the others join it in Settings > Wi-Fi - the network's host is its router (.1) */
		NSString *gw = vxa_net_gateway ();
		[s addObject:@[ @"Nearby - no router, no internet", @"Also where a hotel's or an airport's Wi-Fi keeps its guests' devices apart (On this Wi-Fi then finds nothing). Host: Personal Hotspot on (Settings > Personal Hotspot), then host here; the others join your iPhone's network in their Wi-Fi settings. Join: first join the host's network (its hotspot, DIRECT-VX-VECTREX-..., VECTREX-EMU-...) in Settings > Wi-Fi.", @[
			@[ @"Host on your Personal Hotspot", vxa_hotspot_on () ? @"Your hotspot is on: your game on it" : @"Turn Personal Hotspot on first (Settings)", ^{
				if (!vxa_hotspot_on ()) { [ws note:@"Personal Hotspot is not on - or nobody has joined it yet.\n\nSettings > Personal Hotspot > Allow Others to Join. The other player joins your iPhone's network in their Wi-Fi settings, then taps Join the nearby host. (An iPad without mobile data has no hotspot: let the other device host.)"]; ulog (@"online: hotspot not on"); return; }
				[ws host:2]; } ],
			@[ @"Join the nearby host", gw ? [NSString stringWithFormat:@"The host of this network: %@", gw] : @"Not on a host's own network yet: join it in Settings > Wi-Fi", ^{
				NSString *g = vxa_net_gateway (); if (!g) { [ws note:@"This device is on an ordinary Wi-Fi, not a host's own network.\n\nJoin the host's network first in Settings > Wi-Fi: the host iPhone's name (its Personal Hotspot), DIRECT-VX-VECTREX-... or VECTREX-EMU-..."]; return; }
				ulog ([NSString stringWithFormat:@"online: joining the nearby host %@", g]); vx_net_join (g.UTF8String, OClean (vxa_net_name ()).UTF8String, vxa_net_key ().UTF8String); } ] ] ]];
		[s addObject:@[ @"Join", @"", @[ @[ @"Enter a code", @"Five letters from the host", ^{ [ws ask:@"The host's code" then:^(NSString *v) { vx_net_join (v.UTF8String, OClean (vxa_net_name ()).UTF8String, vxa_net_key ().UTF8String); }]; } ],
			@[ @"Enter an address", @"A host on this Wi-Fi (its address)", ^{ [ws ask:@"The host's address" then:^(NSString *v) { vx_net_join (v.UTF8String, OClean (vxa_net_name ()).UTF8String, vxa_net_key ().UTF8String); }]; } ] ] ]];
		NSMutableArray *room = [NSMutableArray array]; NSDictionary *r = _st[@"room"];
		if ([r isKindOfClass:NSDictionary.class]) for (NSArray *g in r[@"g"]) if ([g isKindOfClass:NSArray.class] && g.count >= 6) {
			NSString *code = g[0]; [room addObject:@[ [NSString stringWithFormat:@"%@", [g[2] stringByDeletingPathExtension]], [NSString stringWithFormat:@"%@ - waiting %d s", g[1] == [NSNull null] ? @"?" : g[1], [g[5] intValue]],
				^{ vx_net_join (code.UTF8String, OClean (vxa_net_name ()).UTF8String, vxa_net_key ().UTF8String); } ]]; }
		int rs = [_st[@"room_state"] intValue];
		if (!room.count) [room addObject:@[ rs == 1 ? @"Looking..." : rs == 3 ? @"The meeting point does not answer" : @"No games waiting", @"", [NSNull null] ]];
		[room addObject:@[ @"Look again", @"", ^{ vx_net_room (); } ]];
		[s addObject:@[ @"Game room", @"Public games waiting for a player 2, from phones and PCs.", room ]];
		NSMutableArray *lan = [NSMutableArray array];
		for (NSDictionary *g in _st[@"lan"]) if ([g isKindOfClass:NSDictionary.class]) { NSString *where = [NSString stringWithFormat:@"%@:%@", g[@"ip"], g[@"port"]];
			[lan addObject:@[ [g[@"name"] stringByDeletingPathExtension], [NSString stringWithFormat:@"%@ - %@", [g[@"gamer"] length] ? g[@"gamer"] : @"?", where],
				^{ vx_net_join (where.UTF8String, OClean (vxa_net_name ()).UTF8String, vxa_net_key ().UTF8String); } ]]; }
		if (!lan.count) [lan addObject:@[ @"None found", [NSString stringWithFormat:@"This phone: %@", vxa_net_ip ()], [NSNull null] ]];
		[s addObject:@[ @"On this Wi-Fi", @"Games hosted on this network. Not listed? Enter the host's address under Join.", lan ]]; }
	_secs = s;
}
/* the guest left: the host's game goes on, hosted again the same way (the game room, a code, this Wi-Fi) - 1: hosted */
int vxa_net_hostmode = -1;
int vxa_net_rehost (void)
{
	NSString *rom = vxa_game (); if (vxa_net_hostmode < 0 || !rom.length) return 0;
	NSString *nm = OClean (vxa_net_name ()); if (vxa_net_hostmode == 0 && !nm.length) return 0;
	int ski = vxa_ofind ("vectrex_emu_skip_intro"); int skip = ski >= 0 && !strcmp (vxa_oval (ski), "on");
	extern void vxa_net_hosting (NSString *rom); vxa_net_hosting (rom);
	const int ok = vx_net_host (vxa_net_hostmode, nm.UTF8String, vxa_net_key ().UTF8String, rom.fileSystemRepresentation, skip, "auto", 0.0);
	ulog ([NSString stringWithFormat:@"online: hosting %@ again", rom.lastPathComponent]); return ok;
}
- (void) host:(int)mode
{
	NSString *rom = vxa_game ();
	if (!rom.length) { [self note:@"Start a game first: the one you host"]; return; }
	NSString *nm = OClean (vxa_net_name ());
	if (!nm.length) { [self note:@"Your player name is missing: enter it above (A-Z, 0-9)"]; return; }   /* (a name to host or join - the owner, 2026-11-19) */
	int ski = vxa_ofind ("vectrex_emu_skip_intro"); int skip = ski >= 0 && !strcmp (vxa_oval (ski), "on");
	extern void vxa_net_hosting (NSString *rom); vxa_net_hosting (rom); vxa_net_hostmode = mode;
	vx_net_host (mode, nm.UTF8String, vxa_net_key ().UTF8String, rom.fileSystemRepresentation, skip, "auto", 0.0);
	ulog ([NSString stringWithFormat:@"online: hosting %@ (%@)", rom.lastPathComponent, mode == 0 ? @"game room" : mode == 1 ? @"code" : @"Wi-Fi"]);
	[self poll];
}
- (void) note:(NSString *)t
{
	UIAlertController *a = [UIAlertController alertControllerWithTitle:nil message:t preferredStyle:UIAlertControllerStyleAlert];
	[a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]]; [self presentViewController:a animated:YES completion:nil];
}
- (void) ask:(NSString *)t then:(void (^) (NSString *))go
{
	UIAlertController *a = [UIAlertController alertControllerWithTitle:t message:nil preferredStyle:UIAlertControllerStyleAlert];
	[a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.autocapitalizationType = UITextAutocapitalizationTypeAllCharacters; f.autocorrectionType = UITextAutocorrectionTypeNo; }];
	[a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
	[a addAction:[UIAlertAction actionWithTitle:@"Join" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) { NSString *v = [a.textFields.firstObject.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
		if (v.length) { ulog ([NSString stringWithFormat:@"online: joining %@", v]); go (v); } }]];
	[self presentViewController:a animated:YES completion:nil];
}
- (void) chat
{
	UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Chat" message:nil preferredStyle:UIAlertControllerStyleAlert];
	[a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.placeholder = @"A line to the other player"; }];
	[a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
	[a addAction:[UIAlertAction actionWithTitle:@"Send" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) { NSString *v = a.textFields.firstObject.text; if (v.length) vx_net_chat (v.UTF8String); }]];
	[self presentViewController:a animated:YES completion:nil];
}
- (NSInteger) numberOfSectionsInTableView:(UITableView *)t { return (NSInteger) _secs.count; }
- (NSInteger) tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return (NSInteger) [_secs[(NSUInteger) s][2] count]; }
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { NSString *h = _secs[(NSUInteger) s][0]; return h.length ? h : nil; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s { NSString *f = _secs[(NSUInteger) s][1]; return f.length ? f : nil; }
- (void) tableView:(UITableView *)t willDisplayHeaderView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
	NSArray *r = _secs[(NSUInteger) ip.section][2][(NSUInteger) ip.row];
	if ([r[0] isEqualToString:@"#name"]) {             /* (the name: typed in place) */
		UITableViewCell *c = OCell (@"", @"", NO); UITextField *f = [UITextField new]; f.text = vxa_net_name (); f.placeholder = @"YOUR NAME"; f.textColor = VX_HI; f.font = OF (17, UIFontWeightSemibold);
		f.autocapitalizationType = UITextAutocapitalizationTypeAllCharacters; f.autocorrectionType = UITextAutocorrectionTypeNo; f.returnKeyType = UIReturnKeyDone; f.delegate = self;
		f.attributedPlaceholder = [[NSAttributedString alloc] initWithString:@"YOUR NAME" attributes:@{ NSForegroundColorAttributeName: VX_DIM }];
		f.translatesAutoresizingMaskIntoConstraints = NO; [c.contentView addSubview:f];
		[NSLayoutConstraint activateConstraints:@[ [f.leadingAnchor constraintEqualToAnchor:c.contentView.layoutMarginsGuide.leadingAnchor], [f.trailingAnchor constraintEqualToAnchor:c.contentView.layoutMarginsGuide.trailingAnchor],
			[f.topAnchor constraintEqualToAnchor:c.contentView.topAnchor constant:8], [f.bottomAnchor constraintEqualToAnchor:c.contentView.bottomAnchor constant:-8], [f.heightAnchor constraintGreaterThanOrEqualToConstant:32] ]];
		return c; }
	return OCell (r[0], r[1], r[2] != [NSNull null]);
}
- (BOOL) textFieldShouldReturn:(UITextField *)f { [f resignFirstResponder]; return YES; }
- (void) textFieldDidEndEditing:(UITextField *)f
{
	NSString *n = OClean (f.text ?: @""); f.text = n;
	if (!n.length) { [NSUserDefaults.standardUserDefaults setObject:@"" forKey:@"net.name"]; [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"net.name_ok"]; return; }
	if ([n isEqualToString:vxa_net_name ()] && [NSUserDefaults.standardUserDefaults boolForKey:@"net.name_ok"]) return;
	[NSUserDefaults.standardUserDefaults setObject:n forKey:@"net.name"];
	OClaim (n, ^(int r, NSString *g) { vxa_toast_ext (OClaimSays (r, g)); });   /* (a new name: reserved at the meeting point) */
}
- (void) tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)ip
{
	[t deselectRowAtIndexPath:ip animated:YES]; [self.view endEditing:YES];
	id a = _secs[(NSUInteger) ip.section][2][(NSUInteger) ip.row][2];
	{ NSString *sec = _secs[(NSUInteger) ip.section][0], *row = _secs[(NSUInteger) ip.section][2][(NSUInteger) ip.row][0];   /* (hosting or joining: a name first) */
	  BOOL needs = a != [NSNull null] && ([sec hasPrefix:@"Host"] || [sec hasPrefix:@"Join"] || [sec hasPrefix:@"Nearby"] || [sec hasPrefix:@"Game room"] || [sec hasPrefix:@"On this"]) && ![row isEqualToString:@"Look again"];
	  if (needs && !OClean (vxa_net_name ()).length) { ulog (@"online: no player name"); [self note:@"Your player name is missing: enter it above (A-Z, 0-9)"]; return; }
	  if (needs && ![NSUserDefaults.standardUserDefaults boolForKey:@"net.name_ok"]) OClaim (OClean (vxa_net_name ()), ^(int r, NSString *g) { if (r == 0 || r == -1) vxa_toast_ext (OClaimSays (r, g)); }); }
	if (a != [NSNull null]) { ulog ([NSString stringWithFormat:@"online: tap %@", _secs[(NSUInteger) ip.section][2][(NSUInteger) ip.row][0]]); ((void (^) (void)) a) (); [self poll]; }
}
@end

@implementation VXUI (Online)
+ (void) showOnlineFrom:(UIViewController *)vc
{
	vxa_hold (1); VXOnline *o = [[VXOnline alloc] initWithStyle:UITableViewStyleInsetGrouped];
	UINavigationController *n = [[UINavigationController alloc] initWithRootViewController:o]; [VXUI styleNav:n];
	[vc presentViewController:n animated:YES completion:nil];
}
@end
