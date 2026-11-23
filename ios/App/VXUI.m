/* VXUI.m - Vectrex_EMU for iPhone and iPad, step 2: the app's own screens in the PC app's colours (the mockup canvas) -
 * the game library (the overlays as tiles), the game menu (a sheet: RESET, eject, overlay, save states, port 2), the
 * settings (the core's own groups and help texts; numbers on one slider over the running game, choices as a list) and
 * the quick bar above the picture. Every tap is logged. Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see
 * LICENSE.md. */
#import "VXApp.h"
#include <ctype.h>
unsigned char *vx_genart_make (int look, const char *name, int *w, int *h);   /* (libretro/genart.c, in the core) */
void vx_genart_free (void *p);
unsigned char *vx_genart_label (const char *path, int *w, int *h);   /* (the player's own label on our cartridge) */
unsigned char *vx_genart_fit (const char *path, int maxw, int *w, int *h);   /* (a picture made at most maxw wide) */
#import <GameController/GameController.h>

static UIFont *VF (CGFloat s, UIFontWeight w) { return [UIFont systemFontOfSize:s weight:w]; }
static UIFont *VT (CGFloat s) { return [UIFont monospacedSystemFontOfSize:s weight:UIFontWeightSemibold]; }   /* (the vector-like lettering) */
static UIButton *VB (NSString *title, NSString *sym, BOOL on, void (^f) (void))
{
	UIButtonConfiguration *c = [UIButtonConfiguration plainButtonConfiguration];
	c.title = title; c.image = sym ? [UIImage systemImageNamed:sym withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:15 weight:UIImageSymbolWeightMedium]] : nil;
	c.imagePlacement = NSDirectionalRectEdgeTop; c.imagePadding = 4; c.baseForegroundColor = on ? VX_HI2 : VX_HI;
	c.titleTextAttributesTransformer = ^NSDictionary *(NSDictionary *a) { NSMutableDictionary *m = [a mutableCopy]; m[NSFontAttributeName] = VF (sym ? 10 : 15, UIFontWeightSemibold); return m; };
	if (sym) { c.contentInsets = NSDirectionalEdgeInsetsMake (6, 2, 6, 2); c.titleLineBreakMode = NSLineBreakByClipping; }   /* (OVERLAY on one line in a narrow cell) */
	c.background.backgroundColor = on ? VX_SEL : VX_PANEL; c.background.strokeColor = on ? VX_EDGE2 : VX_EDGE; c.background.strokeWidth = 1; c.background.cornerRadius = 12;
	UIButton *b = [UIButton buttonWithConfiguration:c primaryAction:[UIAction actionWithHandler:^(UIAction *a) { (void) a; ulog ([NSString stringWithFormat:@"tap %@", title]); f (); }]];
	b.accessibilityLabel = title; return b;
}
static UILabel *VL (NSString *t, UIFont *f, UIColor *c) { UILabel *l = [UILabel new]; l.text = t; l.font = f; l.textColor = c; l.numberOfLines = 0; return l; }
static void VToast (UIViewController *vc, NSString *s)
{
	UILabel *l = VL ([NSString stringWithFormat:@"   %@   ", s], VF (14, UIFontWeightMedium), VX_HI); l.backgroundColor = [VX_PANEL colorWithAlphaComponent:0.95];
	l.layer.cornerRadius = 10; l.layer.masksToBounds = YES; l.layer.borderWidth = 1; l.layer.borderColor = VX_EDGE2.CGColor; [l sizeToFit];
	CGSize v = vc.view.bounds.size; l.frame = CGRectMake ((v.width - l.frame.size.width) / 2, vc.view.safeAreaInsets.top + 60, l.frame.size.width, 34);
	[vc.view addSubview:l]; [UIView animateWithDuration:0.4 delay:1.6 options:0 animations:^{ l.alpha = 0; } completion:^(BOOL d) { (void) d; [l removeFromSuperview]; }];
}

/* ------------------------------------------------ a setting's values: numbers on a slider, the rest a list */
static int VNum (NSString *v, double *out)
{
	NSString *t = [v.lowercaseString stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
	if ([t hasPrefix:@"off"]) { *out = 0; return 1; }
	if ([t hasPrefix:@"x"]) t = [t substringFromIndex:1];
	NSScanner *s = [NSScanner scannerWithString:t]; double d; if ([s scanDouble:&d]) { *out = d; return 1; }
	return 0;
}
/* (numbers: at least 4 of them - sorted for the slider; the others, auto and the like, as buttons beside it) */
static int VSlider (int i, NSArray<NSString *> **nums, NSArray<NSString *> **extra)
{
	NSArray *vals = [@(vxa_ovals (i)) componentsSeparatedByString:@"|"]; NSMutableArray *n = [NSMutableArray array], *x = [NSMutableArray array];
	for (NSString *v in vals) if ([v containsString:@" Hz"]) return 0;   /* (screen rates: a choice of the screen's own, not a slide) */
	for (NSString *v in vals) { double d; if (VNum (v, &d)) [n addObject:v]; else [x addObject:v]; }
	if (n.count < 4) return 0;
	[n sortUsingComparator:^NSComparisonResult (NSString *a, NSString *b) { double p, q; VNum (a, &p); VNum (b, &q); return p < q ? NSOrderedAscending : p > q ? NSOrderedDescending : NSOrderedSame; }];
	if (nums) *nums = n; if (extra) *extra = x; return 1;
}

/* ------------------------------------------------ the one-slider panel over the running game */
@interface VXAdjust : UIView
@property (nonatomic) int opt; @property (nonatomic, weak) UIViewController *vc; @property (nonatomic) BOOL fromSettings;
@end
#define VX_ADJ_HDR (-2)                           /* (not a core setting: the app's HDR brightness, 0 .. 1 - the same panel over the running game) */
@implementation VXAdjust { UILabel *_cat, *_name, *_info, *_val; UISlider *_sl; UIStackView *_extra; NSArray<NSString *> *_nums; int _last; UIButton *_prev, *_next; }
- (instancetype) initWithOpt:(int)i vc:(UIViewController *)vc
{
	self = [super initWithFrame:CGRectZero]; _vc = vc;
	self.backgroundColor = [VX_BAR colorWithAlphaComponent:0.95]; self.layer.cornerRadius = 22; self.layer.borderWidth = 1; self.layer.borderColor = VX_EDGE2.CGColor;
	_cat = VL (@"", VF (11, UIFontWeightSemibold), VX_DIM); _name = VL (@"", VT (17), VX_HI); _info = VL (@"", VF (12, UIFontWeightRegular), VX_DIM); _val = VL (@"", VT (20), VX_HI);
	_cat.textAlignment = _name.textAlignment = _info.textAlignment = NSTextAlignmentCenter; _info.numberOfLines = 2;
	__weak VXAdjust *ws = self;
	UIButton *prev = VB (@"", @"chevron.left", NO, ^{ [ws step:-1]; }), *next = VB (@"", @"chevron.right", NO, ^{ [ws step:1]; });
	prev.accessibilityLabel = @"Previous setting"; next.accessibilityLabel = @"Next setting"; _prev = prev; _next = next;
	UIStackView *mid = [[UIStackView alloc] initWithArrangedSubviews:@[ _cat, _name ]]; mid.axis = UILayoutConstraintAxisVertical; mid.spacing = 2;
	UIStackView *top = [[UIStackView alloc] initWithArrangedSubviews:@[ prev, mid, next ]]; top.alignment = UIStackViewAlignmentCenter; top.spacing = 8;
	[prev.widthAnchor constraintEqualToConstant:44].active = YES; [next.widthAnchor constraintEqualToConstant:44].active = YES;
	[prev.heightAnchor constraintEqualToConstant:44].active = YES; [next.heightAnchor constraintEqualToConstant:44].active = YES;
	_sl = [UISlider new]; _sl.minimumTrackTintColor = VX_G; _sl.maximumTrackTintColor = VX_SEL; _sl.thumbTintColor = VX_HI;
	[_sl addTarget:self action:@selector(slid) forControlEvents:UIControlEventValueChanged];
	{ UITapGestureRecognizer *dt = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(toDefault)]; dt.numberOfTapsRequired = 2; [_sl addGestureRecognizer:dt]; }   /* (a double tap: the default) */
	_extra = [UIStackView new]; _extra.spacing = 8; _extra.distribution = UIStackViewDistributionFillEqually;
	UIButton *def = VB (@"Default", nil, NO, ^{ if (ws.opt == VX_ADJ_HDR) [ws hdrSet:1.0]; else [ws setValue:@(vxa_odef (ws.opt))]; }), *done = VB (@"Done", nil, YES, ^{ [ws close]; });
	UIButton *allb = VB (@"", @"list.bullet", NO, ^{ [ws closeTo:YES]; }); allb.accessibilityLabel = @"All settings";   /* (the full list, at this setting) */
	[allb.widthAnchor constraintEqualToConstant:44].active = YES; [allb.heightAnchor constraintEqualToConstant:40].active = YES;
	UIStackView *bot = [[UIStackView alloc] initWithArrangedSubviews:@[ _val, [UIView new], allb, def, done ]]; bot.alignment = UIStackViewAlignmentCenter; bot.spacing = 8;
	[def.heightAnchor constraintEqualToConstant:40].active = YES; [done.heightAnchor constraintEqualToConstant:40].active = YES; [done.widthAnchor constraintEqualToConstant:80].active = YES;
	UIStackView *all = [[UIStackView alloc] initWithArrangedSubviews:@[ top, _info, _sl, _extra, bot ]]; all.axis = UILayoutConstraintAxisVertical; all.spacing = 10;
	all.translatesAutoresizingMaskIntoConstraints = NO; [self addSubview:all];
	[NSLayoutConstraint activateConstraints:@[ [all.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:14], [all.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-14],
		[all.topAnchor constraintEqualToAnchor:self.topAnchor constant:12], [all.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-14] ]];
	[self show:i]; return self;
}
/* (HDR brightness - the owner, 2026-11-22: over the running game, the picture changes as the slider moves; it sat in the settings list, which covers it) */
- (void) hdrSet:(double)v
{
	extern float vxa_hdr_headroom (void); extern void vxa_hdr_update (void); const double hr = vxa_hdr_headroom ();
	if (v >= 0) { [NSUserDefaults.standardUserDefaults setObject:@(v) forKey:@"app.hdr"]; vxa_hdr_update (); }
	NSNumber *f = [NSUserDefaults.standardUserDefaults objectForKey:@"app.hdr"]; const double fr = f ? f.doubleValue : 1.0;
	if (v < 0 || v == 1.0) _sl.value = (float) fr;
	_val.text = fr > 0.01 ? [NSString stringWithFormat:@"up to %.1fx white", 1 + (hr - 1) * fr] : @"Off"; _val.textColor = fr < 0.995 ? VX_CHG : VX_HI;
}
- (void) showHdr
{
	_opt = VX_ADJ_HDR; _nums = @[]; _cat.text = @"APP"; _name.text = @"HDR brightness"; _info.text = @"The brightest lines and dots above the screen's white.";
	_sl.minimumValue = 0; _sl.maximumValue = 1; _sl.alpha = 1; _prev.hidden = _next.hidden = YES;
	for (UIView *v in _extra.arrangedSubviews) [v removeFromSuperview];
	_extra.hidden = YES; [self hdrSet:-1]; ulog (@"adjust hdr");
}
- (void) show:(int)i
{
	if (i == VX_ADJ_HDR) { [self showHdr]; return; }
	_prev.hidden = _next.hidden = NO;
	_opt = i; NSArray *ex = nil; NSArray *n = nil; VSlider (i, &n, &ex); _nums = n ?: @[];
	NSString *cat = @""; for (int c = 0; c < vxa_ncats (); c++) if (!strcmp (vxa_ckey (c), vxa_ocat (i))) cat = @(vxa_cdesc (c));
	_cat.text = cat.uppercaseString; _name.text = @(vxa_odesc (i)); _info.text = @(vxa_oinfo (i));
	_sl.minimumValue = 0; _sl.maximumValue = MAX (1, (float) _nums.count - 1);
	for (UIView *v in _extra.arrangedSubviews) [v removeFromSuperview];
	__weak VXAdjust *ws = self;
	for (NSString *e in ex) { UIButton *b = VB (e, nil, !strcmp (vxa_oval (i), e.UTF8String), ^{ [ws setValue:e]; }); [b.heightAnchor constraintEqualToConstant:36].active = YES; [_extra addArrangedSubview:b]; }
	_extra.hidden = ex.count == 0; [self refresh];
	ulog ([NSString stringWithFormat:@"adjust %s", vxa_okey (i)]);
}
- (void) refresh
{
	NSString *v = @(vxa_oval (_opt)); NSUInteger k = [_nums indexOfObject:v]; if (k != NSNotFound) { _sl.value = (float) k; _last = (int) k; }
	else {                                           /* (a word - auto: the thumb at the default's place, else 1x's, else the middle -
	                                                    not where the setting shown before left it, the owner 2026-11-19) */
		NSUInteger d = [_nums indexOfObject:@(vxa_odef (_opt))]; if (d == NSNotFound) d = [_nums indexOfObject:@"1x"];
		_sl.value = d != NSNotFound ? (float) d : (_sl.maximumValue / 2); _last = -1; }
	_sl.alpha = k == NSNotFound ? 0.4 : 1;
	_val.text = v; _val.textColor = strcmp (vxa_oval (_opt), vxa_odef (_opt)) ? VX_CHG : VX_HI;
}
- (void) slid
{
	if (_opt == VX_ADJ_HDR) { [self hdrSet:_sl.value]; return; }
	int k = (int) lroundf (_sl.value); if (k < 0 || k >= (int) _nums.count) return;
	if (k != _last) { _last = k; [[UISelectionFeedbackGenerator new] selectionChanged]; vxa_oset (_opt, _nums[k].UTF8String); [self refresh]; [self show:_opt]; }
}
- (void) setValue:(NSString *)v { vxa_oset (_opt, v.UTF8String); [self show:_opt]; }
- (void) toDefault { if (_opt == VX_ADJ_HDR) { [[UIImpactFeedbackGenerator new] impactOccurred]; [self hdrSet:1.0]; return; } ulog ([NSString stringWithFormat:@"adjust %s: double tap - the default", vxa_okey (_opt)]); [[UIImpactFeedbackGenerator new] impactOccurred]; [self setValue:@(vxa_odef (_opt))]; }
- (void) step:(int)d                                 /* (the next setting with a slider in the same group) */
{
	if (_opt < 0) return;
	int n = vxa_nopts ();
	for (int k = 1; k < n; k++) { int j = ((_opt + d * k) % n + n) % n; if (!strcmp (vxa_ocat (j), vxa_ocat (_opt)) && VSlider (j, NULL, NULL)) { [self show:j]; return; } }
}
/* (Done: back where it was opened from - the settings list, at this setting; or the game, from the quick bar) */
- (void) close { [self closeTo:NO]; }   /* (Done: straight back to the game; the list button: all settings) */
- (void) closeTo:(BOOL)list
{
	ulog (list ? @"adjust done: back to the settings" : @"adjust done"); UIViewController *g = self.vc; int i = self.opt;
	[UIView animateWithDuration:0.2 animations:^{ self.alpha = 0; } completion:^(BOOL f) { (void) f; [self removeFromSuperview]; if (list && g) [VXUI showSettingsFrom:g at:i < 0 ? -1 : i]; }];
}
@end

/* ------------------------------------------------ settings: the core's groups, a value list, help texts */
@interface VXChoice : UITableViewController
@property (nonatomic) int opt;
@end
@implementation VXChoice { NSArray<NSString *> *_vals; }
- (void) viewDidLoad
{
	[super viewDidLoad]; _vals = [@(vxa_ovals (_opt)) componentsSeparatedByString:@"|"]; self.title = @(vxa_odesc (_opt));
	self.tableView.backgroundColor = VX_BG; self.tableView.separatorColor = VX_EDGE; self.tableView.tintColor = VX_G;
}
- (NSInteger) tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return (NSInteger) _vals.count; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s { return [NSString stringWithFormat:@"%s\nDefault: %s", vxa_oinfo (_opt), vxa_odef (_opt)]; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
	UITableViewCell *c = [t dequeueReusableCellWithIdentifier:@"v"] ?: [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"v"];
	NSString *v = _vals[(NSUInteger) ip.row]; BOOL on = !strcmp (vxa_oval (_opt), v.UTF8String);
	c.textLabel.text = v; c.textLabel.textColor = on ? VX_HI2 : VX_FG; c.textLabel.font = VF (16, on ? UIFontWeightSemibold : UIFontWeightRegular);
	c.backgroundColor = on ? VX_SEL : VX_PANEL; c.accessoryType = on ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
	UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c;
}
- (void) tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)ip
{
	vxa_oset (_opt, _vals[(NSUInteger) ip.row].UTF8String); [[UISelectionFeedbackGenerator new] selectionChanged];
	[t reloadData];
	if (self.navigationController.viewControllers.firstObject == self) [self dismissViewControllerAnimated:YES completion:nil];   /* (opened on its own: from the game menu) */
	else [self.navigationController popViewControllerAnimated:YES];
}
@end

@interface VXSettings : UITableViewController
@property (nonatomic, weak) UIViewController *game; @property (nonatomic) int focus;
@end
@implementation VXSettings { NSMutableArray<NSMutableArray<NSNumber *> *> *_rows; NSMutableArray<NSNumber *> *_cats; }
- (void) viewDidLoad
{
	[super viewDidLoad]; self.title = @"Settings";
	self.tableView.backgroundColor = VX_BG; self.tableView.separatorColor = VX_EDGE;
	_rows = [NSMutableArray array]; _cats = [NSMutableArray array];
	/* (the most used first: the groups as a phone player reaches for them, the service trim pots last; in each group the
	 *  rows in the core's order - the desktop's Config tab's (2026-11-23); settings that mean nothing here left out - the
	 *  screen's shape, RetroArch's ROM browser button) */
	NSArray *order = @[ @"video", @"overlay", @"case", @"tube", @"controls", @"audio", @"system", @"unit", @"circuit" ];
	NSArray *top = @[ @"vectrex_emu_profile" ];
	NSSet *hide = [NSSet setWithArray:@[ @"vectrex_emu_shape", @"vectrex_emu_btn_browser", @"vectrex_emu_deck", @"vectrex_emu_skip_intro", @"vectrex_emu_tube_look" ]];   /* (the deck, skip intro, the look: under App, on top) */
	NSMutableArray *cs = [NSMutableArray array];
	for (NSString *k in order) for (int c = 0; c < vxa_ncats (); c++) if ([k isEqualToString:@(vxa_ckey (c))]) [cs addObject:@(c)];
	for (int c = 0; c < vxa_ncats (); c++) if (![cs containsObject:@(c)]) [cs addObject:@(c)];
	for (NSNumber *cn in cs) { int c = cn.intValue;
		NSMutableArray *r = [NSMutableArray array];
		for (int i = 0; i < vxa_nopts (); i++) if (!strcmp (vxa_ocat (i), vxa_ckey (c)) && ![hide containsObject:@(vxa_okey (i))]) [r addObject:@(i)];
		[r sortWithOptions:NSSortStable usingComparator:^NSComparisonResult (NSNumber *x, NSNumber *y) {
			NSUInteger a = [top indexOfObject:@(vxa_okey (x.intValue))], b = [top indexOfObject:@(vxa_okey (y.intValue))];
			return a < b ? NSOrderedAscending : a > b ? NSOrderedDescending : NSOrderedSame; }];
		if (r.count) { [_rows addObject:r]; [_cats addObject:@(c)]; } }
	__weak VXSettings *ws = self;
	self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithPrimaryAction:[UIAction actionWithTitle:@"Done" image:nil identifier:nil handler:^(UIAction *a) { (void) a; [ws dismissViewControllerAnimated:YES completion:nil]; }]];
}
- (void) viewWillAppear:(BOOL)a { [super viewWillAppear:a]; [self.tableView reloadData]; }
- (UIContextMenuConfiguration *) tableView:(UITableView *)t contextMenuConfigurationForRowAtIndexPath:(NSIndexPath *)ip point:(CGPoint)p
{
	if (!ip.section) return nil;
	int i = _rows[(NSUInteger) ip.section - 1][(NSUInteger) ip.row].intValue; __weak VXSettings *ws = self; ulog ([NSString stringWithFormat:@"help %s", vxa_okey (i)]);
	return [UIContextMenuConfiguration configurationWithIdentifier:nil previewProvider:nil actionProvider:^UIMenu *(NSArray *sg) { (void) sg;
		UIAction *d = [UIAction actionWithTitle:[NSString stringWithFormat:@"Back to the default: %s", vxa_odef (i)] image:[UIImage systemImageNamed:@"arrow.uturn.backward"] identifier:nil
			handler:^(UIAction *a) { (void) a; vxa_oset (i, vxa_odef (i)); [ws.tableView reloadData]; }];
		if (!strcmp (vxa_oval (i), vxa_odef (i))) d.attributes = UIMenuElementAttributesDisabled;
		return [UIMenu menuWithTitle:[NSString stringWithFormat:@"%s\n\n%s", vxa_odesc (i), vxa_oinfo (i)] children:@[ d ]]; }];
}
- (void) viewDidAppear:(BOOL)a
{
	[super viewDidAppear:a]; if (self.focus < 0) return;
	for (NSUInteger sct = 0; sct < _rows.count; sct++) for (NSUInteger r = 0; r < _rows[sct].count; r++) if (_rows[sct][r].intValue == self.focus) {
		NSIndexPath *ip = [NSIndexPath indexPathForRow:(NSInteger) r inSection:(NSInteger) sct + 1];
		[self.tableView scrollToRowAtIndexPath:ip atScrollPosition:UITableViewScrollPositionMiddle animated:NO];
		[self.tableView selectRowAtIndexPath:ip animated:NO scrollPosition:UITableViewScrollPositionNone];
		dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (0.6 * NSEC_PER_SEC)), dispatch_get_main_queue (), ^{ [self.tableView deselectRowAtIndexPath:ip animated:YES]; }); }
	self.focus = -1;
}
/* (section 0: the app's own settings; then the core's groups) */
- (NSInteger) numberOfSectionsInTableView:(UITableView *)t { return (NSInteger) _rows.count + 1; }
- (NSInteger) tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return s ? (NSInteger) _rows[(NSUInteger) s - 1].count : 12; }
/* (App's rows in order: clock, top bar, control deck, pop-up, skip intro, downloads, HDR, look, afterglow, controls, about, reset - as numbered below) */
static NSInteger vxs_app (NSIndexPath *ip) { static const NSInteger m[12] = { 0, 1, 7, 8, 9, 2, 3, 10, 11, 4, 5, 6 }; return ip.section || ip.row < 0 || ip.row > 11 ? -1 : m[ip.row]; }   /* (10, the look: Vectrex or Bright, 2026-11-22) */
static UISegmentedControl *vxs_seg (NSArray *items, NSInteger sel)
{
	UISegmentedControl *sg = [[UISegmentedControl alloc] initWithItems:items]; sg.selectedSegmentIndex = sel; sg.selectedSegmentTintColor = VX_SEL;
	[sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_HI2 } forState:UIControlStateSelected];
	[sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_FG2 } forState:UIControlStateNormal]; [sg sizeToFit]; return sg;
}
static UITableViewCell *vxs_cell (NSString *title)
{
	UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
	c.textLabel.text = title; c.textLabel.textColor = VX_FG; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone; return c;
}
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return s ? @(vxa_cdesc (_cats[(NSUInteger) s - 1].intValue)).uppercaseString : @"APP"; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s { return s ? @(vxa_cinfo (_cats[(NSUInteger) s - 1].intValue)) : @"Auto: what fits this screen with the picture at its full width. Top bar auto-hide: tap the top of the screen to bring it back. Deck pop-up: hidden until you tap the picture's bottom. Skip intro: games start straight away."; }
- (void) tableView:(UITableView *)t willDisplayHeaderView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
	const NSInteger R = vxs_app (ip); __weak VXSettings *ws = self;
	if (R == 7) {                                   /* (the control deck: its look) */
		extern int vxa_deck_sel (void), vxa_deck_pop (void); extern void vxa_deck_set (int, int);
		UITableViewCell *c = vxs_cell (@"Control deck"); UISegmentedControl *sg = vxs_seg (@[ @"Auto", @"Off", @"Full", @"Slim" ], vxa_deck_sel ());
		[sg addAction:[UIAction actionWithHandler:^(UIAction *a) { vxa_deck_set ((int) ((UISegmentedControl *) a.sender).selectedSegmentIndex, vxa_deck_pop ());
			[ws.tableView reloadRowsAtIndexPaths:@[ [NSIndexPath indexPathForRow:3 inSection:0] ] withRowAnimation:UITableViewRowAnimationNone]; }] forControlEvents:UIControlEventValueChanged];
		c.accessoryView = sg; return c; }
	if (R == 8) {                                   /* (the deck pops up: hidden until the picture's bottom is tapped) */
		extern int vxa_deck_sel (void), vxa_deck_pop (void); extern void vxa_deck_set (int, int);
		UITableViewCell *c = vxs_cell (@"Deck pop-up"); UISwitch *sw = [UISwitch new]; sw.onTintColor = VX_G; sw.on = vxa_deck_pop () != 0; sw.enabled = vxa_deck_sel () >= 2;   /* (Auto: its own choice, shown) */
		c.textLabel.textColor = sw.enabled ? VX_FG : VX_DIM;
		[sw addAction:[UIAction actionWithHandler:^(UIAction *a) { vxa_deck_set (vxa_deck_sel (), ((UISwitch *) a.sender).on); }] forControlEvents:UIControlEventValueChanged];
		c.accessoryView = sw; return c; }
	if (R == 10) {                                  /* (the look: Vectrex - the measured picture - or Bright: the lines lifted and wider, for a small screen) */
		extern void vxa_hdr_update (void);
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil]; c.textLabel.text = @"Look"; c.textLabel.textColor = VX_FG;
		c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone; c.detailTextLabel.textColor = VX_DIM; c.detailTextLabel.numberOfLines = 2;
		NSNumber *lk = [NSUserDefaults.standardUserDefaults objectForKey:@"app.look"]; const NSInteger md = lk ? lk.integerValue : 1;   /* (Bright the default on phones and tablets) */
		c.detailTextLabel.text = md == 1 ? @"Brighter, wider lines: for a small screen" : @"As a real Vectrex";
		UISegmentedControl *sg = [[UISegmentedControl alloc] initWithItems:@[ @"Vectrex", @"Bright" ]];
		sg.selectedSegmentIndex = md == 1 ? 1 : 0; sg.selectedSegmentTintColor = VX_G;
		[sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_FG } forState:UIControlStateNormal];
		[sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: UIColor.blackColor } forState:UIControlStateSelected];
		[sg sizeToFit];
		[sg addAction:[UIAction actionWithHandler:^(UIAction *a) { const NSInteger v = ((UISegmentedControl *) a.sender).selectedSegmentIndex == 1 ? 1 : 0;
			[NSUserDefaults.standardUserDefaults setInteger:v forKey:@"app.look"];
			ulog ([NSString stringWithFormat:@"look %@", v ? @"Bright" : @"Vectrex"]); vxa_hdr_update (); [ws.tableView reloadData]; }] forControlEvents:UIControlEventValueChanged];
		c.accessoryView = sg; return c; }
	if (R == 11) {                                  /* (the afterglow 20 % brighter: a phone's small picture loses it - on there by default, off on an iPad) */
		extern void vxa_hdr_update (void); extern int vxa_trail_boost (void);
		UITableViewCell *c = vxs_cell (@"Brighter afterglow");
		UISwitch *sw = [UISwitch new]; sw.onTintColor = VX_G; sw.on = vxa_trail_boost ();
		[sw addAction:[UIAction actionWithHandler:^(UIAction *a) { const BOOL on = ((UISwitch *) a.sender).on; [NSUserDefaults.standardUserDefaults setBool:on forKey:@"app.trailboost"];
			ulog ([NSString stringWithFormat:@"brighter afterglow %d", (int) on]); vxa_hdr_update (); }] forControlEvents:UIControlEventValueChanged];
		c.accessoryView = sw; return c; }
	if (R == 9) {                                   /* (skip the Vectrex intro: the core's setting, here on top) */
		int i = vxa_ofind ("vectrex_emu_skip_intro"); UITableViewCell *c = vxs_cell (@"Skip intro");
		UISwitch *sw = [UISwitch new]; sw.onTintColor = VX_G; sw.on = i >= 0 && !strcmp (vxa_oval (i), "on");
		[sw addAction:[UIAction actionWithHandler:^(UIAction *a) { vxa_oset (i, ((UISwitch *) a.sender).on ? "on" : "off"); }] forControlEvents:UIControlEventValueChanged];
		c.accessoryView = sw; return c; }
	if (R == 6) {                                   /* (every setting back to its default - as RetroArch's Reset Core Options) */
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
		int n = 0; for (int i = 0; i < vxa_nopts (); i++) if (strcmp (vxa_oval (i), vxa_odef (i))) n++;
		c.textLabel.text = @"Reset all settings"; c.textLabel.textColor = n ? VX_CHG : VX_DIM; c.backgroundColor = VX_PANEL;
		c.detailTextLabel.text = n ? [NSString stringWithFormat:@"%d changed - all back to their defaults (a long press on one: that one)", n] : @"All at their defaults"; c.detailTextLabel.textColor = VX_DIM;
		UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c; }
	if (R == 5) {                                   /* (about and help: the version, how to play, online, credits, licence) */
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
		c.textLabel.text = @"About and help"; c.textLabel.textColor = VX_FG; c.detailTextLabel.text = @"how to play, online, credits"; c.detailTextLabel.textColor = VX_DIM;
		c.backgroundColor = VX_PANEL; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator; UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c; }
	if (R == 4) {                                   /* (the controls: controllers, buttons, touch, keyboard) */
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
		c.textLabel.text = @"Controls"; c.textLabel.textColor = VX_FG; c.detailTextLabel.text = @"controllers, touch, keyboard"; c.detailTextLabel.textColor = VX_DIM;
		c.backgroundColor = VX_PANEL; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator; UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c; }
	if (R == 3) {                                   /* (HDR: the brightest dots above the screen's white) */
		extern float vxa_hdr_headroom (void); extern void vxa_hdr_update (void); float hr = vxa_hdr_headroom ();
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
		c.textLabel.text = @"HDR brightness"; c.textLabel.textColor = VX_FG; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone;
		c.detailTextLabel.textColor = VX_DIM; c.detailTextLabel.numberOfLines = 2;
		if (hr <= 1.05) { c.detailTextLabel.text = @"Not on this screen (it shows no HDR)"; return c; }
		NSNumber *f = [NSUserDefaults.standardUserDefaults objectForKey:@"app.hdr"]; double fr = f ? f.doubleValue : 1.0;
		c.detailTextLabel.text = fr > 0.01 ? [NSString stringWithFormat:@"The brightest dots up to %.1fx white", 1 + (hr - 1) * fr] : @"Off: as an ordinary screen";
		c.textLabel.textColor = VX_HI; c.selectionStyle = UITableViewCellSelectionStyleDefault; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator; return c; }   /* (a tap: the slider over the running game) */
	if (R == 2) {                                   /* (the downloads: which parts are here, add or remove) */
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
		c.textLabel.text = @"Downloads"; c.textLabel.textColor = VX_FG; c.detailTextLabel.text = @"games, boxes, UVMC2, PiTrex"; c.detailTextLabel.textColor = VX_DIM;
		c.backgroundColor = VX_PANEL; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator; UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c; }
	if (R == 1) {                                   /* (the quick bar: its own row, or hiding itself over the picture) */
		UITableViewCell *c = [t dequeueReusableCellWithIdentifier:@"bar"] ?: [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"bar"];
		c.textLabel.text = @"Top bar"; c.textLabel.textColor = VX_FG; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone;
		UISegmentedControl *sg = [[UISegmentedControl alloc] initWithItems:@[ @"Auto", @"Shown", @"Auto-hide" ]];
		extern int vxa_bar_sel (void); sg.selectedSegmentIndex = vxa_bar_sel (); sg.selectedSegmentTintColor = VX_SEL; [sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_HI2 } forState:UIControlStateSelected];
		[sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_FG2 } forState:UIControlStateNormal];
		[sg addAction:[UIAction actionWithHandler:^(UIAction *a) { const NSInteger k = ((UISegmentedControl *) a.sender).selectedSegmentIndex;
			[NSUserDefaults.standardUserDefaults setObject:k == 1 ? @"always" : k == 2 ? @"hide" : @"auto" forKey:@"app.bar"]; ulog ([NSString stringWithFormat:@"top bar %@", k == 1 ? @"shown" : k == 2 ? @"auto-hide" : @"auto"]);
			if ([ws.game respondsToSelector:@selector(barModeChanged)]) [ws.game performSelector:@selector(barModeChanged)]; }] forControlEvents:UIControlEventValueChanged];
		[sg sizeToFit]; c.accessoryView = sg; return c; }
	if (R == 0) {                                   /* (the clock and battery: iOS's own status bar) */
		UITableViewCell *c = [t dequeueReusableCellWithIdentifier:@"app"] ?: [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"app"];
		c.textLabel.text = @"Clock and battery"; c.textLabel.textColor = VX_FG; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone;
		UISwitch *sw = [UISwitch new]; sw.onTintColor = VX_G; sw.on = [NSUserDefaults.standardUserDefaults boolForKey:@"app.statusbar"];
		[sw addAction:[UIAction actionWithHandler:^(UIAction *a) { BOOL on = ((UISwitch *) a.sender).on; [NSUserDefaults.standardUserDefaults setBool:on forKey:@"app.statusbar"];
			ulog ([NSString stringWithFormat:@"clock and battery %@", on ? @"on" : @"off"]); [ws.game setNeedsStatusBarAppearanceUpdate]; }] forControlEvents:UIControlEventValueChanged];
		c.accessoryView = sw; return c; }
	UITableViewCell *c = [t dequeueReusableCellWithIdentifier:@"o"] ?: [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:@"o"];
	int i = _rows[(NSUInteger) ip.section - 1][(NSUInteger) ip.row].intValue; BOOL chg = strcmp (vxa_oval (i), vxa_odef (i)) != 0;
	c.textLabel.text = @(vxa_odesc (i)); c.textLabel.textColor = VX_FG; c.textLabel.font = VF (15, UIFontWeightRegular); c.textLabel.numberOfLines = 2;
	c.detailTextLabel.text = @(vxa_oval (i)); c.detailTextLabel.textColor = chg ? VX_CHG : VX_DIM; c.detailTextLabel.font = VF (14, UIFontWeightRegular);
	c.backgroundColor = VX_PANEL; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator; c.tintColor = VX_DIM;
	UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c;
}
- (void) tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)ip
{
	const NSInteger R = vxs_app (ip);
	if (R == 3) { extern float vxa_hdr_headroom (void); [t deselectRowAtIndexPath:ip animated:YES]; if (vxa_hdr_headroom () <= 1.05) return;
		UIViewController *g = self.game; [self dismissViewControllerAnimated:YES completion:^{ [VXUI adjustOption:VX_ADJ_HDR over:g fromSettings:YES]; }]; return; }
	if (R == 4) { UIViewController *g = self.game; [self dismissViewControllerAnimated:YES completion:^{ [VXUI showControlsFrom:g]; }]; return; }
	if (R == 6) { [t deselectRowAtIndexPath:ip animated:YES]; __weak VXSettings *ws = self;
		UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Reset all settings?" message:@"Every setting of the picture, the overlay, the sound and the controls back to its default." preferredStyle:UIAlertControllerStyleAlert];
		[a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
		[a addAction:[UIAlertAction actionWithTitle:@"Reset" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *x) { (void) x; int n = 0;
			for (int i = 0; i < vxa_nopts (); i++) if (strcmp (vxa_oval (i), vxa_odef (i))) { vxa_oset (i, vxa_odef (i)); n++; }
			[NSUserDefaults.standardUserDefaults removeObjectForKey:@"app.deckpop"]; [NSUserDefaults.standardUserDefaults removeObjectForKey:@"app.bar"];   /* (the deck's pop-up and the top bar too: Auto) */
			if ([ws.game respondsToSelector:@selector(barModeChanged)]) [ws.game performSelector:@selector(barModeChanged)];
			ulog ([NSString stringWithFormat:@"settings: reset all (%d changed)", n]); [ws.tableView reloadData]; }]];
		[self presentViewController:a animated:YES completion:nil]; return; }
	if (R == 5) { extern UIViewController *vxa_about_vc (void); [self.navigationController pushViewController:vxa_about_vc () animated:YES]; ulog (@"settings: about and help"); return; }
	if (R == 2) { UIViewController *g = self.game; ulog (@"settings: downloads");
		[self dismissViewControllerAnimated:YES completion:^{ if ([g respondsToSelector:@selector(openDownloads)]) [g performSelector:@selector(openDownloads)]; }]; return; }
	if (!ip.section) return;
	int i = _rows[(NSUInteger) ip.section - 1][(NSUInteger) ip.row].intValue; ulog ([NSString stringWithFormat:@"setting opened %s", vxa_okey (i)]);
	if (VSlider (i, NULL, NULL)) {                  /* (a number: the slider over the running game, the settings out of the way) */
		UIViewController *g = self.game; [self dismissViewControllerAnimated:YES completion:^{ [VXUI adjustOption:i over:g fromSettings:YES]; }]; return; }
	VXChoice *ch = [[VXChoice alloc] initWithStyle:UITableViewStyleInsetGrouped]; ch.opt = i; [self.navigationController pushViewController:ch animated:YES];
}
@end

/* ------------------------------------------------ the game library */
/* the PiTrex's pictogram of the PC's simple list: a raspberry, in lines (10 x 6.4 units) */
static UIImage *vxs_raspberry (UIColor *col, CGFloat h)
{
	const CGFloat k = h / 6.4; UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake (10 * k, 6.4 * k)];
	return [r imageWithActions:^(UIGraphicsImageRendererContext *x) { CGContextRef c = x.CGContext; [col setStroke]; CGContextSetLineWidth (c, MAX (1.2, k * 0.42)); CGContextSetLineCap (c, kCGLineCapRound);
		CGContextTranslateCTM (c, 0, 0.5 * k); CGContextStrokeEllipseInRect (c, CGRectMake (2.4 * k, 1.2 * k, 5.2 * k, 4.8 * k));
		static const CGFloat L[6][4] = { { 5, 1.2, 2.8, -0.2 }, { 5, 1.2, 7.2, -0.2 }, { 2.8, -0.2, 4.2, 0.6 }, { 7.2, -0.2, 5.8, 0.6 }, { 3.6, 3.4, 6.4, 3.4 }, { 5, 1.6, 5, 5.6 } };
		for (int i = 0; i < 6; i++) { CGContextMoveToPoint (c, L[i][0] * k, L[i][1] * k); CGContextAddLineToPoint (c, L[i][2] * k, L[i][3] * k); } CGContextStrokePath (c); }];
}
@interface VXGameCell : UICollectionViewCell
@property (nonatomic, strong) UIImageView *img; @property (nonatomic, strong) UILabel *name, *ph, *kind;
@property (nonatomic) BOOL titles, focusOn;       /* (titles only: a row of the list; focusOn: the highlight - a controller, the keys) */
@end
@implementation VXGameCell
- (instancetype) initWithFrame:(CGRect)f
{
	self = [super initWithFrame:f];
	_img = [UIImageView new]; _img.alpha = 0.7;   /* (the pictures 30 % less bright, as the PC's library: the eyes stay used to the vectors' level - the owner, 2026-11-22) */
	_img.contentMode = UIViewContentModeScaleAspectFit; _img.clipsToBounds = YES; _img.layer.cornerRadius = 8;   /* (a cartridge: its own clear outline) */
	_ph = VL (@"", VT (15), VX_G); _ph.textAlignment = NSTextAlignmentCenter;
	_name = VL (@"", VF (12, UIFontWeightMedium), VX_FG); _name.numberOfLines = 2;
	_kind = VL (@"", VF (10, UIFontWeightSemibold), VX_DIM); _kind.numberOfLines = 1; _kind.textAlignment = NSTextAlignmentRight;
	[self.contentView addSubview:_img]; [_img addSubview:_ph]; [self.contentView addSubview:_name]; [self.contentView addSubview:_kind];
	self.contentView.layer.cornerRadius = 8; return self;
}
- (void) layoutSubviews
{
	[super layoutSubviews]; CGFloat w = self.bounds.size.width, h = self.bounds.size.height - 38;
	if (_titles) { _img.hidden = YES; _kind.hidden = NO; _name.numberOfLines = 1; _name.frame = CGRectMake (14, 0, w - 28 - 70, self.bounds.size.height); _kind.frame = CGRectMake (w - 14 - 70, 0, 70, self.bounds.size.height); }
	else { _img.hidden = NO; _kind.hidden = YES; _name.numberOfLines = 2; _img.frame = CGRectMake (0, 0, w, h); _ph.frame = CGRectInset (_img.bounds, 6, 6); _name.frame = CGRectMake (0, h + 4, w, 32); }
	self.contentView.backgroundColor = _titles && _focusOn ? VX_SEL : UIColor.clearColor;
}
@end

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
@interface VXLibrary : UIViewController <UICollectionViewDataSource, UICollectionViewDelegateFlowLayout, UITextFieldDelegate, UIDocumentPickerDelegate>
@end
@implementation VXLibrary { UICollectionView *_cv; UITextField *_search; UIStackView *_chips; NSArray<NSDictionary *> *_all, *_shown; NSString *_filter; UISegmentedControl *_look;
	UIButton *_size; NSInteger _sel; BOOL _atCur, _ready; CADisplayLink *_pad; uint32_t _padPrev; double _padNext; }
/* (the tiles a row: 1-5, 0 titles only - one setting for every look, the same on an iPhone and an iPad (the owner); 3 at first) */
+ (int) perRow { NSNumber *n = [NSUserDefaults.standardUserDefaults objectForKey:@"lib.perrow"]; return n ? MAX (0, MIN (5, n.intValue)) : 3; }
- (int) cols { const int k = [VXLibrary perRow]; return k ? k : 1; }
/* (the tiles' look, the player's choice: Cartridges - the real cartridge seen from above with its label -, Boxes, Overlays;
 *  a game without one in that look takes the next: cartridge, box, overlay; Last Human (UVMC2) takes Last Human's) */
+ (NSString *) look { return [NSUserDefaults.standardUserDefaults stringForKey:@"lib.look"] ?: @"boxes"; }   /* (the default: the boxes - the owner, 2026-11-19) */
+ (CGFloat) aspect { NSString *l = [self look]; return [l isEqualToString:@"carts"] ? 0.96 : [l isEqualToString:@"boxes"] ? 1.39 : 207.0 / 162; }
+ (UIImage *) tileFor:(NSString *)n
{
	NSString *th = [vxa_docs () stringByAppendingPathComponent:@"system/vectrex_emu/thumbs"], *base = [n componentsSeparatedByString:@" ("].firstObject;
	/* (each look its own pictures only - no mixing (the owner: carts, boxes and overlays mixed); none: the name's tile) */
	NSArray *dirs = [[self look] isEqualToString:@"boxes"] ? @[ @"boxes" ] : [[self look] isEqualToString:@"overlays"] ? @[ @"overlays", @"" ] : @[ @"carts" ];
	for (NSString *d in dirs) for (NSString *nm in @[ n, base ]) {
		NSString *p = [[th stringByAppendingPathComponent:d] stringByAppendingPathComponent:[nm stringByAppendingPathExtension:@"png"]];
		if ([NSFileManager.defaultManager fileExistsAtPath:p]) { UIImage *im = [self fileTileFor:p]; if (im) return im; } }
	if ([[self look] isEqualToString:@"carts"]) for (NSString *nm in @[ n, base ]) {   /* (no cartridge picture: the player's own label on our cartridge) */
		NSString *p = [[th stringByAppendingPathComponent:@"labels"] stringByAppendingPathComponent:[nm stringByAppendingPathExtension:@"png"]];
		if ([NSFileManager.defaultManager fileExistsAtPath:p]) { UIImage *im = [self labelTileFor:p]; if (im) return im; } }
	return [self drawnTileFor:n];
}
/* (the pictures genart.c makes, kept while memory allows) */
+ (NSCache *) made { static NSCache *made; static dispatch_once_t once; dispatch_once (&once, ^{ made = [NSCache new]; made.totalCostLimit = 64 << 20; }); return made; }
/* (a picture in the thumbs folder: made at most 840 px wide once (genart.c) - a big one never kept whole; again when it changes) */
+ (UIImage *) fileTileFor:(NSString *)p
{
	NSDate *mt = [[NSFileManager.defaultManager attributesOfItemAtPath:p error:nil] fileModificationDate];
	NSString *key = [NSString stringWithFormat:@"F/%@/%.0f", p, mt.timeIntervalSince1970]; UIImage *im = [[self made] objectForKey:key]; if (im) return im;
	int w = 0, h = 0; unsigned char *px = vx_genart_fit (p.fileSystemRepresentation, 840, &w, &h);
	im = px ? [self imageOfGenart:px w:w h:h] : [UIImage imageWithContentsOfFile:p];   /* (not a PNG stb reads: as before) */
	if (im && px) [[self made] setObject:im forKey:key cost:(NSUInteger) w * (NSUInteger) h * 4];
	return im;
}
/* (a cartridge with the player's own label, thumbs/labels/<game>.png: put on our cartridge by the core's libretro/genart.c -
 *  as the PC, RetroArch and Android; made again when the file changes) */
+ (UIImage *) labelTileFor:(NSString *)p
{
	NSDate *mt = [[NSFileManager.defaultManager attributesOfItemAtPath:p error:nil] fileModificationDate];
	NSString *key = [NSString stringWithFormat:@"L/%@/%.0f", p, mt.timeIntervalSince1970]; UIImage *im = [[self made] objectForKey:key]; if (im) return im;
	int w = 0, h = 0; unsigned char *px = vx_genart_label (p.fileSystemRepresentation, &w, &h); if (!px) return nil;
	im = [self imageOfGenart:px w:w h:h]; if (im) [[self made] setObject:im forKey:key cost:(NSUInteger) w * (NSUInteger) h * 4];
	return im;
}
/* (a cartridge or box for a game with none of its own: drawn by the core's libretro/genart.c - the same pictures as the PC,
 *  RetroArch and Android; kept while memory allows. Overlays: none - the name's tile) */
+ (UIImage *) drawnTileFor:(NSString *)n
{
	NSString *lk = [self look]; const int look = [lk isEqualToString:@"carts"] ? 0 : [lk isEqualToString:@"boxes"] ? 1 : -1;
	if (look < 0 || !n.length) return nil;
	NSString *key = [NSString stringWithFormat:@"%d/%@", look, n]; UIImage *im = [[self made] objectForKey:key]; if (im) return im;
	int w = 0, h = 0; unsigned char *px = vx_genart_make (look, n.UTF8String, &w, &h); if (!px) return nil;
	im = [self imageOfGenart:px w:w h:h]; if (im) [[self made] setObject:im forKey:key cost:(NSUInteger) w * (NSUInteger) h * 4];
	return im;
}
/* (genart.c's RGBA as a UIImage; the pixels freed) */
+ (UIImage *) imageOfGenart:(unsigned char *)px w:(int)w h:(int)h
{
	UIImage *im = nil;
	for (size_t i = 0, k = (size_t) w * (size_t) h; i < k; i++) { unsigned char *q = px + i * 4; const unsigned a = q[3];   /* (premultiplied, as Core Graphics wants) */
		q[0] = (unsigned char) ((q[0] * a + 127) / 255); q[1] = (unsigned char) ((q[1] * a + 127) / 255); q[2] = (unsigned char) ((q[2] * a + 127) / 255); }
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB ();
	CGContextRef ctx = CGBitmapContextCreate (px, (size_t) w, (size_t) h, 8, (size_t) w * 4, cs, (CGBitmapInfo) kCGImageAlphaPremultipliedLast | kCGBitmapByteOrderDefault);
	CGImageRef ci = ctx ? CGBitmapContextCreateImage (ctx) : NULL;
	if (ci) { im = [UIImage imageWithCGImage:ci]; CGImageRelease (ci); }
	if (ctx) CGContextRelease (ctx);
	CGColorSpaceRelease (cs); vx_genart_free (px);
	return im;
}
- (void) scan
{
	NSMutableArray *a = [NSMutableArray array]; NSString *roms = [vxa_docs () stringByAppendingPathComponent:@"roms"];
	NSSet *ext = [NSSet setWithArray:@[ @"bin", @"vec", @"gam", @"um2", @"v4e" ]];
	for (NSString *rel in [NSFileManager.defaultManager enumeratorAtPath:roms]) {
		if (![ext containsObject:rel.pathExtension.lowercaseString]) continue;
		NSString *kind = [rel.pathExtension.lowercaseString isEqualToString:@"um2"] ? @"UVMC2" : [rel hasPrefix:@"Tests/"] ? @"Tests" : @"Games";
		[a addObject:@{ @"name": rel.lastPathComponent.stringByDeletingPathExtension, @"path": [roms stringByAppendingPathComponent:rel], @"kind": kind }]; }
	NSString *px = [vxa_docs () stringByAppendingPathComponent:@"system/vectrex_emu/pitrex/pitrex.img"];
	if ([NSFileManager.defaultManager fileExistsAtPath:px]) [a addObject:@{ @"name": @"PiTrex", @"path": px, @"kind": @"PiTrex" }];
	/* (the PiTrex itself first, as in the PC's list - the owner, 2026-11-22; then by name) */
	[a sortUsingComparator:^NSComparisonResult (NSDictionary *x, NSDictionary *y) { const BOOL px = [x[@"kind"] isEqualToString:@"PiTrex"], py = [y[@"kind"] isEqualToString:@"PiTrex"];
		if (px != py) return px ? NSOrderedAscending : NSOrderedDescending; return [x[@"name"] localizedCaseInsensitiveCompare:y[@"name"]]; }];
	if ([NSFileManager.defaultManager fileExistsAtPath:px]) { extern void vxa_pitrex_tiles (void); vxa_pitrex_tiles (); }   /* (its cartridge and box: in the thumbs folder) */
	_all = a; ulog ([NSString stringWithFormat:@"library: %lu games", (unsigned long) a.count]);
}
/* (your own games: picked in Files - from iCloud, the phone, a USB stick - and copied into roms/Imported) */
- (void) importGames
{
	UIDocumentPickerViewController *p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ] asCopy:YES];
	p.allowsMultipleSelection = YES; p.delegate = self; [self presentViewController:p animated:YES completion:nil];
}
- (void) documentPicker:(UIDocumentPickerViewController *)c didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
	NSSet *ext = [NSSet setWithArray:@[ @"bin", @"vec", @"gam", @"um2", @"v4e", @"rom" ]];
	NSString *dir = [[vxa_docs () stringByAppendingPathComponent:@"roms"] stringByAppendingPathComponent:@"Imported"];
	[NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
	int added = 0, skipped = 0;
	for (NSURL *u in urls) { NSString *e = u.pathExtension.lowercaseString;
		if (![ext containsObject:e]) { skipped++; continue; }
		NSString *to = [dir stringByAppendingPathComponent:u.lastPathComponent]; [NSFileManager.defaultManager removeItemAtPath:to error:nil];
		if ([NSFileManager.defaultManager copyItemAtPath:u.path toPath:to error:nil]) added++; else skipped++; }
	ulog ([NSString stringWithFormat:@"library: %d games imported, %d skipped", added, skipped]);
	[self scan]; [self applyFilter];
	VToast (self, skipped ? [NSString stringWithFormat:@"Added %d - %d not a Vectrex game (.bin .vec .gam .um2)", added, skipped] : [NSString stringWithFormat:@"Added %d game%@", added, added == 1 ? @"" : @"s"]);
}
- (void) applyFilter
{
	NSString *keep = _sel >= 0 && _sel < (NSInteger) _shown.count ? _shown[(NSUInteger) _sel][@"path"] : nil;   /* (the game highlighted) */
	NSString *q = _search.text.lowercaseString; NSArray *rec = [NSUserDefaults.standardUserDefaults arrayForKey:@"recent"] ?: @[];
	NSMutableArray *r = [NSMutableArray array];
	if ([_filter isEqualToString:@"Recent"]) { for (NSString *p in rec) for (NSDictionary *g in _all) if ([g[@"path"] isEqualToString:p]) [r addObject:g]; }
	else for (NSDictionary *g in _all) if (([_filter isEqualToString:@"All"] && ![g[@"kind"] isEqualToString:@"Tests"]) || [g[@"kind"] isEqualToString:_filter]) [r addObject:g];
	if (q.length) [r filterUsingPredicate:[NSPredicate predicateWithBlock:^BOOL (NSDictionary *g, NSDictionary *b) { (void) b; return [[g[@"name"] lowercaseString] containsString:q]; }]];
	/* (the highlight in the new list: the same game while it is in it, else the first) */
	NSInteger ns = -1; if (keep) for (NSUInteger i = 0; i < r.count; i++) if ([r[i][@"path"] isEqualToString:keep]) { ns = (NSInteger) i; break; }
	if (ns < 0 && _ready && r.count) ns = 0;
	_shown = r; _sel = ns; [_cv reloadData];
	if (_ready && _sel >= 0 && _cv.window) { [_cv layoutIfNeeded]; [_cv scrollToItemAtIndexPath:[NSIndexPath indexPathForItem:_sel inSection:0] atScrollPosition:UICollectionViewScrollPositionCenteredVertically animated:NO]; }
	for (UIButton *b in _chips.arrangedSubviews) { BOOL on = [b.accessibilityLabel isEqualToString:_filter];
		UIButtonConfiguration *c = b.configuration; c.baseForegroundColor = on ? VX_HI2 : VX_FG2; c.background.backgroundColor = on ? VX_SEL : UIColor.clearColor;
		c.background.strokeColor = on ? VX_EDGE2 : VX_EDGE2; b.configuration = c; }
}
- (void) viewDidLoad
{
	[super viewDidLoad]; self.view.backgroundColor = VX_BG; _filter = @"All"; [self scan];
	UIView *head = [UIView new]; head.backgroundColor = VX_BAR; head.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:head];
	UILabel *title = VL (@"Games", VT (26), VX_HI);
	__weak VXLibrary *ws = self;
	UIButton *close = VB (@"", @"xmark", NO, ^{ [ws dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); }]; }); close.accessibilityLabel = @"Close";
	_search = [UITextField new]; _search.placeholder = [NSString stringWithFormat:@"Search %lu games", (unsigned long) _all.count]; _search.textColor = VX_FG; _search.backgroundColor = VX_BODY;
	_search.attributedPlaceholder = [[NSAttributedString alloc] initWithString:_search.placeholder attributes:@{ NSForegroundColorAttributeName: VX_DIM }];
	_search.layer.cornerRadius = 10; _search.layer.borderWidth = 1; _search.layer.borderColor = VX_EDGE.CGColor; _search.clearButtonMode = UITextFieldViewModeWhileEditing;
	_search.leftView = [[UIView alloc] initWithFrame:CGRectMake (0, 0, 12, 10)]; _search.leftViewMode = UITextFieldViewModeAlways; _search.returnKeyType = UIReturnKeyDone; _search.delegate = self;
	[_search addTarget:self action:@selector(applyFilter) forControlEvents:UIControlEventEditingChanged];
	_size = [UIButton buttonWithType:UIButtonTypeSystem]; _size.showsMenuAsPrimaryAction = YES; [self sizeMenu];
	_size.backgroundColor = VX_BODY; _size.layer.cornerRadius = 10; _size.layer.borderWidth = 1; _size.layer.borderColor = VX_EDGE2.CGColor; _size.tintColor = VX_HI;
	_size.titleLabel.font = VF (12, UIFontWeightBold); _size.contentEdgeInsets = UIEdgeInsetsMake (0, 10, 0, 10); _size.accessibilityLabel = @"Games per row";
	[_size setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal]; [_size setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
	UIStackView *srow = [[UIStackView alloc] initWithArrangedSubviews:@[ _search, _size ]]; srow.spacing = 8;
	_chips = [UIStackView new]; _chips.spacing = 6;
	for (NSString *f in @[ @"All", @"Recent", @"Games", @"UVMC2", @"PiTrex", @"Tests" ]) {
		UIButton *b = VB (f, nil, NO, ^{ [ws pick:f]; }); UIButtonConfiguration *c = b.configuration; c.background.cornerRadius = 16;
		c.contentInsets = NSDirectionalEdgeInsetsMake (6, 12, 6, 12); b.configuration = c; [_chips addArrangedSubview:b]; }
	UIScrollView *cs = [UIScrollView new]; cs.showsHorizontalScrollIndicator = NO; [cs addSubview:_chips]; _chips.translatesAutoresizingMaskIntoConstraints = NO;
	[NSLayoutConstraint activateConstraints:@[ [_chips.leadingAnchor constraintEqualToAnchor:cs.contentLayoutGuide.leadingAnchor], [_chips.trailingAnchor constraintEqualToAnchor:cs.contentLayoutGuide.trailingAnchor],
		[_chips.topAnchor constraintEqualToAnchor:cs.contentLayoutGuide.topAnchor], [_chips.bottomAnchor constraintEqualToAnchor:cs.contentLayoutGuide.bottomAnchor], [_chips.heightAnchor constraintEqualToAnchor:cs.frameLayoutGuide.heightAnchor] ]];
	_look = [[UISegmentedControl alloc] initWithItems:@[ @"Cartridges", @"Boxes", @"Overlays" ]];
	_look.selectedSegmentIndex = [[VXLibrary look] isEqualToString:@"boxes"] ? 1 : [[VXLibrary look] isEqualToString:@"overlays"] ? 2 : 0;
	_look.selectedSegmentTintColor = VX_SEL; [_look setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_HI2 } forState:UIControlStateSelected];
	[_look setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_FG2 } forState:UIControlStateNormal];
	[_look addAction:[UIAction actionWithHandler:^(UIAction *a) { NSInteger k = ((UISegmentedControl *) a.sender).selectedSegmentIndex;
		[NSUserDefaults.standardUserDefaults setObject:k == 1 ? @"boxes" : k == 2 ? @"overlays" : @"carts" forKey:@"lib.look"];
		ulog ([NSString stringWithFormat:@"library look %ld", (long) k]); [ws reload]; }] forControlEvents:UIControlEventValueChanged];
	UIButton *add = VB (@"", @"plus", NO, ^{ [ws importGames]; }); add.accessibilityLabel = @"Add games from Files";   /* (your own ROMs: from Files) */
	[add.widthAnchor constraintEqualToConstant:44].active = YES; [add.heightAnchor constraintEqualToConstant:44].active = YES;
	UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[ title, [UIView new], add, close ]]; row.alignment = UIStackViewAlignmentCenter; row.spacing = 8;
	UIStackView *hs = [[UIStackView alloc] initWithArrangedSubviews:@[ row, _look, srow, cs ]]; hs.axis = UILayoutConstraintAxisVertical; hs.spacing = 10; hs.translatesAutoresizingMaskIntoConstraints = NO;
	[head addSubview:hs];
	UICollectionViewFlowLayout *fl = [UICollectionViewFlowLayout new]; fl.minimumInteritemSpacing = 12; fl.minimumLineSpacing = 16; fl.sectionInset = UIEdgeInsetsMake (16, 16, 24, 16);
	_cv = [[UICollectionView alloc] initWithFrame:CGRectZero collectionViewLayout:fl]; _cv.backgroundColor = VX_BG; _cv.dataSource = self; _cv.delegate = self;
	_cv.keyboardDismissMode = UIScrollViewKeyboardDismissModeOnDrag; [_cv registerClass:VXGameCell.class forCellWithReuseIdentifier:@"g"]; _cv.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:_cv];
	UILayoutGuide *sa = self.view.safeAreaLayoutGuide;
	[NSLayoutConstraint activateConstraints:@[
		[head.topAnchor constraintEqualToAnchor:self.view.topAnchor], [head.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor], [head.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
		[hs.topAnchor constraintEqualToAnchor:sa.topAnchor constant:8], [hs.leadingAnchor constraintEqualToAnchor:sa.leadingAnchor constant:16], [hs.trailingAnchor constraintEqualToAnchor:sa.trailingAnchor constant:-16],
		[hs.bottomAnchor constraintEqualToAnchor:head.bottomAnchor constant:-12], [close.widthAnchor constraintEqualToConstant:44], [close.heightAnchor constraintEqualToConstant:44],
		[_search.heightAnchor constraintEqualToConstant:40], [cs.heightAnchor constraintEqualToConstant:34],
		[_cv.topAnchor constraintEqualToAnchor:head.bottomAnchor], [_cv.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor], [_cv.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
		[_cv.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor] ]];
	_sel = -1; [self applyFilter];
	{ NSString *cur = vxa_game (); for (NSUInteger i = 0; i < _shown.count; i++) if ([_shown[i][@"path"] isEqualToString:cur]) { _sel = (NSInteger) i; break; } }   /* (opened at the game playing) */
	_ready = YES;
}
/* the size menu: 1-5 per row, titles only */
- (void) sizeMenu
{
	const int k = [VXLibrary perRow]; __weak VXLibrary *ws = self; NSMutableArray *acts = [NSMutableArray array];
	for (int i = 1; i <= 6; i++) { const int v = i == 6 ? 0 : i; NSString *t = v ? [NSString stringWithFormat:@"%d per row", v] : @"Titles only";
		UIAction *a = [UIAction actionWithTitle:t image:nil identifier:nil handler:^(UIAction *x) { (void) x;
			[NSUserDefaults.standardUserDefaults setInteger:v forKey:@"lib.perrow"]; ulog ([NSString stringWithFormat:@"library: %@", t]); [ws sizeMenu]; [ws reload]; }];
		if (v == k) a.state = UIMenuElementStateOn; [acts addObject:a]; }
	_size.menu = [UIMenu menuWithTitle:@"" children:acts];
	[_size setTitle:(k ? [NSString stringWithFormat:@"%d PER ROW \u25BE", k] : @"TITLES ONLY \u25BE") forState:UIControlStateNormal];
}
/* a controller or the keys: up / down / left / right move the highlight (titles only: up / down one, left / right a page),
 * L / R (Page Up / Down) a page, the Vectrex's button 4 (Return) plays, button 1 (Esc) goes back - the player's own buttons
 * (Settings > Controls); 2 and 3 are free: held while button 4 starts a UVMC2 game they reach it (its calibration) */
- (void) viewDidLayoutSubviews
{
	[super viewDidLayoutSubviews];
	if (!_atCur && _sel >= 0 && _sel < (NSInteger) _shown.count && _cv.bounds.size.height > 0) { _atCur = YES; [_cv layoutIfNeeded];
		[_cv scrollToItemAtIndexPath:[NSIndexPath indexPathForItem:_sel inSection:0] atScrollPosition:UICollectionViewScrollPositionCenteredVertically animated:NO]; }
}
- (void) viewDidAppear:(BOOL)a { [super viewDidAppear:a]; [self becomeFirstResponder]; _pad = [CADisplayLink displayLinkWithTarget:self selector:@selector(padTick)]; [_pad addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes]; }
- (void) viewWillDisappear:(BOOL)a { [super viewWillDisappear:a]; [_pad invalidate]; _pad = nil; }
- (BOOL) canBecomeFirstResponder { return YES; }
- (NSArray<UIKeyCommand *> *) keyCommands
{
	NSMutableArray *k = [NSMutableArray array];
	for (NSString *ky in @[ UIKeyInputUpArrow, UIKeyInputDownArrow, UIKeyInputLeftArrow, UIKeyInputRightArrow, UIKeyInputPageUp, UIKeyInputPageDown, @"\r", UIKeyInputEscape ]) {
		UIKeyCommand *c = [UIKeyCommand keyCommandWithInput:ky modifierFlags:0 action:@selector(key:)]; c.wantsPriorityOverSystemBehavior = YES; [k addObject:c]; }
	return k;
}
- (void) key:(UIKeyCommand *)c
{
	if (_search.isFirstResponder) return;
	NSString *i = c.input;
	if ([i isEqualToString:UIKeyInputUpArrow]) [self moveSel:0]; else if ([i isEqualToString:UIKeyInputDownArrow]) [self moveSel:1];
	else if ([i isEqualToString:UIKeyInputLeftArrow]) [self moveSel:2]; else if ([i isEqualToString:UIKeyInputRightArrow]) [self moveSel:3];
	else if ([i isEqualToString:UIKeyInputPageUp]) [self page:-1]; else if ([i isEqualToString:UIKeyInputPageDown]) [self page:1];
	else if ([i isEqualToString:@"\r"]) [self playSel]; else if ([i isEqualToString:UIKeyInputEscape]) [self dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); }];
}
- (void) padTick
{
	extern uint32_t vxa_pad_buttons (void); const uint32_t b = vxa_pad_buttons (), down = b & ~_padPrev; const double now = CACurrentMediaTime ();
	enum { UP = 4, DN = 5, LT = 6, RT = 7, A = 8, L = 10, R = 11 };   /* (the RetroPad's: the Vectrex's 1-4 on Y X B A) */
	const uint32_t dirs = b & ((1u << UP) | (1u << DN) | (1u << LT) | (1u << RT) | (1u << L) | (1u << R));
	BOOL rep = NO; if (dirs && (dirs & ~_padPrev)) _padNext = now + 0.38; else if (dirs && now >= _padNext) { rep = YES; _padNext = now + 0.09; }   /* (held: repeated) */
	const uint32_t act = (dirs & ~_padPrev) | (rep ? dirs : 0);
	if ((act || (down >> A & 1)) && _search.isFirstResponder) {   /* (a controller: always the highlight - the search field and its keyboard let go) */
		[_search resignFirstResponder]; [self becomeFirstResponder]; ulog (@"library: the controller takes over from the search field"); }
	if (act >> UP & 1) [self moveSel:0]; if (act >> DN & 1) [self moveSel:1]; if (act >> LT & 1) [self moveSel:2]; if (act >> RT & 1) [self moveSel:3];
	if (act >> L & 1) [self page:-1]; if (act >> R & 1) [self page:1];
	if (down >> A & 1) [self playSel];             /* (button 1, back: the game's own tick closes the library - one button for back everywhere) */
	_padPrev = b;
}
- (NSInteger) pageItems                         /* (a screenful: the rows that fit, times the tiles a row) */
{
	UICollectionViewFlowLayout *fl = (UICollectionViewFlowLayout *) _cv.collectionViewLayout; const CGSize it = [self collectionView:_cv layout:fl sizeForItemAtIndexPath:[NSIndexPath indexPathForItem:0 inSection:0]];
	const NSInteger rows = MAX (1, (NSInteger) floor (_cv.bounds.size.height / (it.height + fl.minimumLineSpacing))); return rows * [self cols];
}
- (void) moveSel:(int)d                            /* (0 up, 1 down, 2 left, 3 right) */
{
	if (!_shown.count) return; const NSInteger n = (NSInteger) _shown.count, cols = [self cols];
	if (_sel < 0) { _sel = 0; [self showSel]; return; }   /* (the first press: the highlight appears) */
	if ([VXLibrary perRow] == 0 && (d == 2 || d == 3)) { [self page:d == 2 ? -1 : 1]; return; }   /* (titles only: left / right a page) */
	NSInteger s = _sel + (d == 0 ? -cols : d == 1 ? cols : d == 2 ? -1 : 1);
	if (s < 0 || s >= n) { if (d == 1 && _sel / cols < (n - 1) / cols) s = n - 1; else return; }   /* (down from the row above a short last row: its last) */
	_sel = s; [self showSel];
}
- (void) page:(int)dir
{
	if (!_shown.count) return; if (_sel < 0) _sel = 0;
	_sel = MAX (0, MIN ((NSInteger) _shown.count - 1, _sel + dir * [self pageItems])); [self showSel];
}
- (void) showSel
{
	if (_sel < 0 || _sel >= (NSInteger) _shown.count) return;
	for (VXGameCell *c in _cv.visibleCells) { NSIndexPath *ip = [_cv indexPathForCell:c]; [self paint:c on:ip.item == _sel]; }
	[_cv scrollToItemAtIndexPath:[NSIndexPath indexPathForItem:_sel inSection:0] atScrollPosition:UICollectionViewScrollPositionCenteredVertically animated:YES];
	[[UISelectionFeedbackGenerator new] selectionChanged];
}
- (void) paint:(VXGameCell *)c on:(BOOL)on
{
	c.focusOn = on; const BOOL titles = [VXLibrary perRow] == 0; c.titles = titles;
	if (!titles) { c.img.layer.borderColor = (on ? VX_HI2 : VX_EDGE).CGColor; c.img.layer.borderWidth = on ? 3 : (c.img.image ? 0 : 1); }
	if (on) { c.name.textColor = VX_HI2; c.name.font = VF (titles ? 15 : 12, UIFontWeightBold); }
	[c setNeedsLayout];
}
- (void) playSel
{
	if (_sel < 0 || _sel >= (NSInteger) _shown.count) return;
	[self collectionView:_cv didSelectItemAtIndexPath:[NSIndexPath indexPathForItem:_sel inSection:0]];
}
- (void) pick:(NSString *)f { _filter = f; [self applyFilter]; }
- (void) reload { [_cv.collectionViewLayout invalidateLayout]; [_cv reloadData]; }
- (void) viewDidDisappear:(BOOL)a { [super viewDidDisappear:a]; vxa_hold (0); }
- (BOOL) textFieldShouldReturn:(UITextField *)t { [t resignFirstResponder]; return YES; }
- (CGSize) collectionView:(UICollectionView *)cv layout:(UICollectionViewLayout *)l sizeForItemAtIndexPath:(NSIndexPath *)ip
{
	CGFloat w = cv.bounds.size.width - 32;
	if ([VXLibrary perRow] == 0) return CGSizeMake (w, 44);   /* (titles only: a list) */
	const int cols = [self cols]; CGFloat iw = floor ((w - 12 * (cols - 1)) / cols);
	return CGSizeMake (iw, iw * [VXLibrary aspect] + 38);
}
- (NSInteger) collectionView:(UICollectionView *)cv numberOfItemsInSection:(NSInteger)s { return (NSInteger) _shown.count; }
- (UICollectionViewCell *) collectionView:(UICollectionView *)cv cellForItemAtIndexPath:(NSIndexPath *)ip
{
	VXGameCell *c = [cv dequeueReusableCellWithReuseIdentifier:@"g" forIndexPath:ip]; NSDictionary *g = _shown[(NSUInteger) ip.item]; NSString *n = g[@"name"];
	UIImage *im = [VXLibrary tileFor:n];
	c.img.image = im; c.ph.text = im ? @"" : n.uppercaseString; c.name.text = n;
	c.img.backgroundColor = im ? UIColor.clearColor : VX_PANEL;
	const BOOL titles = [VXLibrary perRow] == 0; c.titles = titles; c.kind.text = [g[@"kind"] isEqualToString:@"Games"] ? @"" : [g[@"kind"] uppercaseString];
	BOOL now = [g[@"path"] isEqualToString:vxa_game ()]; c.name.textColor = now ? VX_HI : VX_FG; c.name.font = VF (titles ? 15 : 12, now ? UIFontWeightBold : UIFontWeightMedium);
	if (titles) {                                   /* (titles only, as the PC's list: UVMC2 games in their own colour, the PiTrex with its raspberry) */
		if ([g[@"kind"] isEqualToString:@"UVMC2"]) c.name.textColor = now ? VXC (0x8ac8dc) : VXC (0x66a4b8);
		if ([g[@"kind"] isEqualToString:@"PiTrex"]) { UIColor *col = now ? VX_HI2 : VX_HI; NSTextAttachment *at = [NSTextAttachment new]; at.image = vxs_raspberry (col, 15); at.bounds = CGRectMake (0, -2, at.image.size.width, at.image.size.height);
			NSMutableAttributedString *as = [[NSMutableAttributedString alloc] initWithAttributedString:[NSAttributedString attributedStringWithAttachment:at]];
			[as appendAttributedString:[[NSAttributedString alloc] initWithString:[@"  " stringByAppendingString:n] attributes:@{ NSForegroundColorAttributeName: col, NSFontAttributeName: c.name.font }]]; c.name.attributedText = as; } }
	c.img.contentMode = [[VXLibrary look] isEqualToString:@"overlays"] ? UIViewContentModeScaleToFill : UIViewContentModeScaleAspectFit;   /* (an overlay: always the sheet's own 162:207, whatever its picture's size) */
	c.img.layer.borderColor = (now ? VX_HI : VX_EDGE).CGColor; c.img.layer.borderWidth = im ? (now ? 2 : 0) : 1;
	c.focusOn = NO; if (ip.item == _sel) [self paint:c on:YES]; else [c setNeedsLayout];
	return c;
}
- (void) collectionView:(UICollectionView *)cv didSelectItemAtIndexPath:(NSIndexPath *)ip
{
	NSDictionary *g = _shown[(NSUInteger) ip.item]; ulog ([NSString stringWithFormat:@"library: play %@", g[@"name"]]);
	[[UIImpactFeedbackGenerator new] impactOccurred]; vxa_load (g[@"path"]);
	[self dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); }];
}
- (UIContextMenuConfiguration *) collectionView:(UICollectionView *)cv contextMenuConfigurationForItemAtIndexPath:(NSIndexPath *)ip point:(CGPoint)p
{
	NSDictionary *g = _shown[(NSUInteger) ip.item]; __weak VXLibrary *ws = self;
	return [UIContextMenuConfiguration configurationWithIdentifier:nil previewProvider:nil actionProvider:^UIMenu *(NSArray *s) { (void) s;
		NSMutableArray *acts = [NSMutableArray arrayWithObject:[UIAction actionWithTitle:@"Play" image:[UIImage systemImageNamed:@"play.fill"] identifier:nil handler:^(UIAction *a) { (void) a;
			vxa_load (g[@"path"]); [ws dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); }]; }]];
		if ([[g[@"path"] pathExtension].lowercaseString isEqualToString:@"um2"])   /* (a UVMC2 game: its calibration screen - buttons 2 + 3 held at its start) */
			[acts addObject:[UIAction actionWithTitle:@"Calibration (2 + 3 held)" image:[UIImage systemImageNamed:@"slider.horizontal.3"] identifier:nil handler:^(UIAction *a) { (void) a;
				extern void vxa_load_cal (NSString *); vxa_load_cal (g[@"path"]); [ws dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); }]; }]];
		return [UIMenu menuWithTitle:g[@"name"] children:acts]; }];
}
@end

/* the game held while any screen is over it (the menu, settings, controls); it runs again only once nothing is over it -
 * the menu's way into settings kept it held throughout; a slider over the game lets it run (adjustOption) */
static void vxu_release (UIViewController *game)
{
	__weak UIViewController *g = game;
	dispatch_async (dispatch_get_main_queue (), ^{ UIViewController *gg = g; if (!gg || !gg.presentedViewController) vxa_hold (0); });
}
@interface VXNav : UINavigationController
@property (nonatomic, weak) UIViewController *game;
@end
@implementation VXNav
- (void) viewDidDisappear:(BOOL)a { [super viewDidDisappear:a]; if (self.isBeingDismissed || !self.presentingViewController) vxu_release (self.game); }
@end

/* ------------------------------------------------ the game menu: a sheet from below, the game paused above it */
@interface VXGameMenu : UIViewController <UIAdaptivePresentationControllerDelegate>
@property (nonatomic, weak) UIViewController *game;
@property (nonatomic, strong) UIStackView *all;
- (CGFloat) fitHeight:(CGFloat)w;
@end
@implementation VXGameMenu
- (void) presentationControllerDidDismiss:(UIPresentationController *)p { ulog (@"game menu swiped away"); vxu_release (self.game); }
- (void) viewDidDisappear:(BOOL)a { [super viewDidDisappear:a]; if (!self.presentedViewController) vxu_release (self.game); }   /* (closed in any way: the game goes on - unless another screen took its place) */
- (void) close:(void (^) (void))then { UIViewController *g = self.game; [self dismissViewControllerAnimated:YES completion:^{ vxu_release (g); if (then) then (); }]; }
- (void) viewDidLoad
{
	[super viewDidLoad]; self.view.backgroundColor = VX_BAR; __weak VXGameMenu *ws = self;
	NSString *g = vxa_game ().lastPathComponent.stringByDeletingPathExtension;
	UILabel *t = VL (g.length ? g : @"Vectrex", VT (18), VX_HI), *sub = VL (@"Paused while this menu is open", VF (12, UIFontWeightRegular), VX_DIM);
	UIButton *resume = VB (@"Resume", nil, YES, ^{ [ws close:nil]; });
	UIStackView *tt = [[UIStackView alloc] initWithArrangedSubviews:@[ t, sub ]]; tt.axis = UILayoutConstraintAxisVertical; tt.spacing = 2;
	UIStackView *head = [[UIStackView alloc] initWithArrangedSubviews:@[ tt, resume ]]; head.alignment = UIStackViewAlignmentCenter; head.spacing = 12;
	[resume.widthAnchor constraintEqualToConstant:100].active = YES; [resume.heightAnchor constraintEqualToConstant:44].active = YES;
	/* (the most used first, as a phone player uses it: save / load, then games and RESET, the picture's two quick ones, all
	 *  settings; the Vectrex's own hardware - eject, port 2 - small at the foot) */
	UILabel *st = VL (@"Save states", VF (15, UIFontWeightSemibold), VX_FG);
	UIStackView *slots = [UIStackView new]; slots.distribution = UIStackViewDistributionFillEqually; slots.spacing = 8;
	BOOL can = vxa_state_supported () != 0 && !vxa_net_playing ();   /* (online: a loaded state would part the two games) */
	for (int s = 1; s <= 3; s++) {
		UIButton *sv = VB ([NSString stringWithFormat:@"Save %d", s], @"square.and.arrow.down", NO, ^{ int ok = vxa_state (1, s); UIViewController *gm = ws.game; [ws close:^{ VToast (gm, ok ? [NSString stringWithFormat:@"Saved in slot %d", s] : @"Not saved"); }]; });
		UIButton *ld = VB ([NSString stringWithFormat:@"Load %d", s], @"square.and.arrow.up", NO, ^{ int ok = vxa_state (0, s); UIViewController *gm = ws.game; [ws close:^{ VToast (gm, ok ? [NSString stringWithFormat:@"Slot %d loaded", s] : @"Nothing loaded"); }]; });
		ld.enabled = can && vxa_state_exists (s); sv.enabled = can;
		UIStackView *col = [[UIStackView alloc] initWithArrangedSubviews:@[ sv, ld ]]; col.axis = UILayoutConstraintAxisVertical; col.spacing = 6; col.distribution = UIStackViewDistributionFillEqually;
		[col.heightAnchor constraintEqualToConstant:116].active = YES; [slots addArrangedSubview:col]; }
	UILabel *stn = VL (can ? @"" : vxa_net_playing () ? @"No save states during an online game." : @"This game has no save states (UVMC2 and PiTrex games keep their own).", VF (12, UIFontWeightRegular), VX_DIM); stn.hidden = can;
	int ov = vxa_ofind ("vectrex_emu_overlay"); BOOL ovOn = ov >= 0 && !strcmp (vxa_oval (ov), "on");
	UIStackView *acts = [[UIStackView alloc] initWithArrangedSubviews:@[
		VB (@"Games", @"square.grid.2x2", NO, ^{ UIViewController *gm = ws.game; [ws dismissViewControllerAnimated:YES completion:^{ [VXUI showLibraryFrom:gm]; }]; }),
		VB (@"RESET", @"arrow.counterclockwise", NO, ^{ vxa_do (@"reset"); [ws close:nil]; }),
		VB (ovOn ? @"Overlay on" : @"Overlay off", @"rectangle.on.rectangle", ovOn, ^{ vxa_do (@"overlay"); [ws close:nil]; }),
		VB (@"Room light", @"lightbulb", NO, ^{ UIViewController *gm = ws.game; [ws close:^{ [VXUI adjustOption:vxa_ofind ("vectrex_emu_room") over:gm]; }]; }) ]];
	acts.distribution = UIStackViewDistributionFillEqually; acts.spacing = 8; [acts.heightAnchor constraintEqualToConstant:64].active = YES;
	UIButton *allb = VB (@"All settings", @"slider.horizontal.3", NO, ^{ UIViewController *gm = ws.game; [ws dismissViewControllerAnimated:YES completion:^{ [VXUI showSettingsFrom:gm]; }]; });
	[allb.heightAnchor constraintEqualToConstant:56].active = YES;
	UIButton *onl = VB (@"Online: play with a friend", @"globe", NO, ^{ UIViewController *gm = ws.game; [ws dismissViewControllerAnimated:YES completion:^{ [VXUI showOnlineFrom:gm]; }]; });
	[onl.heightAnchor constraintEqualToConstant:56].active = YES;
	UILabel *hw = VL (@"VECTREX HARDWARE", VF (11, UIFontWeightSemibold), VX_DIM);
	int p2 = vxa_ofind ("vectrex_emu_port2");
	UIButton *ej = VB (@"Eject / insert cartridge", nil, NO, ^{ vxa_do (@"eject"); [ws close:nil]; });
	UIButton *pb = VB (p2 >= 0 ? [NSString stringWithFormat:@"Port 2: %s", vxa_oval (p2)] : @"Port 2", nil, NO, ^{
		if (p2 < 0) return; VXChoice *ch = [[VXChoice alloc] initWithStyle:UITableViewStyleInsetGrouped]; ch.opt = p2;
		UINavigationController *n = [[UINavigationController alloc] initWithRootViewController:ch]; [VXUI styleNav:n]; [ws presentViewController:n animated:YES completion:nil]; });
	for (UIButton *b in @[ ej, pb ]) { UIButtonConfiguration *c = b.configuration; c.titleTextAttributesTransformer = ^NSDictionary *(NSDictionary *a) { NSMutableDictionary *m = [a mutableCopy]; m[NSFontAttributeName] = VF (13, UIFontWeightRegular); return m; };
		c.baseForegroundColor = VX_FG2; b.configuration = c; [b.heightAnchor constraintEqualToConstant:40].active = YES; }
	UIStackView *hwr = [[UIStackView alloc] initWithArrangedSubviews:@[ ej, pb ]]; hwr.distribution = UIStackViewDistributionFillEqually; hwr.spacing = 8;
	UIStackView *all = [[UIStackView alloc] initWithArrangedSubviews:@[ head, st, slots, stn, acts, allb, onl, hw, hwr ]]; all.axis = UILayoutConstraintAxisVertical; all.spacing = 14;
	[all setCustomSpacing:24 afterView:onl]; self.all = all;
	UIScrollView *sc = [UIScrollView new]; sc.translatesAutoresizingMaskIntoConstraints = NO; all.translatesAutoresizingMaskIntoConstraints = NO; [sc addSubview:all]; [self.view addSubview:sc];
	[NSLayoutConstraint activateConstraints:@[ [sc.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:24], [sc.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
		[sc.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor], [sc.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
		[all.topAnchor constraintEqualToAnchor:sc.contentLayoutGuide.topAnchor], [all.bottomAnchor constraintEqualToAnchor:sc.contentLayoutGuide.bottomAnchor constant:-24],
		[all.leadingAnchor constraintEqualToAnchor:sc.frameLayoutGuide.leadingAnchor constant:16], [all.trailingAnchor constraintEqualToAnchor:sc.frameLayoutGuide.trailingAnchor constant:-16] ]];
}
- (CGFloat) fitHeight:(CGFloat)w   /* (the whole menu's height: the sheet opens that tall, all of it seen) */
{
	[self loadViewIfNeeded];
	return ceil ([self.all systemLayoutSizeFittingSize:CGSizeMake (w - 32, UILayoutFittingCompressedSize.height) withHorizontalFittingPriority:UILayoutPriorityRequired
		verticalFittingPriority:UILayoutPriorityFittingSizeLevel].height) + 24 + 24;
}
@end

@implementation VXUI
+ (void) styleNav:(UINavigationController *)n
{
	UINavigationBarAppearance *a = [UINavigationBarAppearance new]; [a configureWithOpaqueBackground]; a.backgroundColor = VX_BAR; a.shadowColor = VX_EDGE;
	a.titleTextAttributes = @{ NSForegroundColorAttributeName: VX_HI, NSFontAttributeName: VT (17) };
	a.largeTitleTextAttributes = @{ NSForegroundColorAttributeName: VX_HI, NSFontAttributeName: VT (28) };
	n.navigationBar.standardAppearance = a; n.navigationBar.scrollEdgeAppearance = a; n.navigationBar.tintColor = VX_G;
	n.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
}
+ (BOOL) busy:(UIViewController *)vc { return vc.presentedViewController != nil; }
+ (void) showLibraryFrom:(UIViewController *)vc
{
	if ([self busy:vc]) return; ulog (@"library opened"); vxa_hold (1);
	VXLibrary *l = [VXLibrary new]; l.modalPresentationStyle = UIModalPresentationFullScreen; l.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
	[vc presentViewController:l animated:YES completion:nil];
}
+ (void) showGameMenuFrom:(UIViewController *)vc
{
	if ([self busy:vc]) return; ulog (@"game menu opened"); vxa_hold (1);
	VXGameMenu *m = [VXGameMenu new]; m.game = vc; m.modalPresentationStyle = UIModalPresentationPageSheet; m.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
	UISheetPresentationController *s = m.sheetPresentationController;
	if (@available (iOS 16.0, *)) {   /* (as tall as the menu: everything down to the hardware row seen; scrolls when the screen is smaller) */
		__weak VXGameMenu *wm = m; __weak UIViewController *wv = vc;
		s.detents = @[ [UISheetPresentationControllerDetent customDetentWithIdentifier:@"vxfit" resolver:^CGFloat (id<UISheetPresentationControllerDetentResolutionContext> ctx) {
			CGFloat w = wm.view.bounds.size.width > 100 ? wm.view.bounds.size.width : wv.view.bounds.size.width;
			return MIN (ctx.maximumDetentValue, [wm fitHeight:w]); }] ];
	} else s.detents = @[ UISheetPresentationControllerDetent.largeDetent ];
	s.prefersGrabberVisible = YES; s.preferredCornerRadius = 22;
	m.presentationController.delegate = m; [vc presentViewController:m animated:YES completion:nil];
}
+ (void) showSettingsFrom:(UIViewController *)vc { [self showSettingsFrom:vc at:-1]; }
+ (void) showSettingsFrom:(UIViewController *)vc at:(int)focus
{
	if ([self busy:vc]) return; ulog (@"settings opened");
	VXSettings *s = [[VXSettings alloc] initWithStyle:UITableViewStyleInsetGrouped]; s.game = vc; s.focus = focus;
	VXNav *n = [[VXNav alloc] initWithRootViewController:s]; n.game = vc; [self styleNav:n]; vxa_hold (1);   /* (held while settings are open) */
	n.modalPresentationStyle = UIModalPresentationPageSheet; n.sheetPresentationController.prefersGrabberVisible = YES;
	[vc presentViewController:n animated:YES completion:nil];
}
+ (void) adjustOption:(int)i over:(UIViewController *)vc { [self adjustOption:i over:vc fromSettings:NO]; }
+ (void) adjustOption:(int)i over:(UIViewController *)vc fromSettings:(BOOL)fs
{
	if (i < 0 && i != VX_ADJ_HDR) return; vxa_hold (0);
	for (UIView *v in vc.view.subviews) if ([v isKindOfClass:VXAdjust.class]) [v removeFromSuperview];
	VXAdjust *a = [[VXAdjust alloc] initWithOpt:i vc:vc]; a.fromSettings = fs; a.translatesAutoresizingMaskIntoConstraints = NO; [vc.view addSubview:a];
	UILayoutGuide *sa = vc.view.safeAreaLayoutGuide;
	[NSLayoutConstraint activateConstraints:@[ [a.leadingAnchor constraintEqualToAnchor:sa.leadingAnchor constant:10], [a.trailingAnchor constraintEqualToAnchor:sa.trailingAnchor constant:-10],
		[a.bottomAnchor constraintEqualToAnchor:sa.bottomAnchor constant:-8], [a.widthAnchor constraintLessThanOrEqualToConstant:560] ]];
	a.alpha = 0; [UIView animateWithDuration:0.2 animations:^{ a.alpha = 1; }];
}
+ (UIView *) quickBarFor:(UIViewController *)vc
{
	__weak UIViewController *w = vc;
	UIStackView *s = [[UIStackView alloc] initWithArrangedSubviews:@[
		VB (@"GAMES", @"square.grid.2x2", NO, ^{ [VXUI showLibraryFrom:w]; }),
		VB (@"SAVE", @"square.and.arrow.down", NO, ^{ VToast (w, vxa_state (1, 1) ? @"Saved in slot 1" : @"This game has no save states"); }),
		VB (@"LOAD", @"square.and.arrow.up", NO, ^{ VToast (w, vxa_state_exists (1) && vxa_state (0, 1) ? @"Slot 1 loaded" : @"Nothing saved in slot 1"); }),
		VB (@"OVERLAY", @"rectangle.on.rectangle", NO, ^{ vxa_do (@"overlay"); }),
		VB (@"ROOM", @"lightbulb", NO, ^{ [VXUI adjustOption:vxa_ofind ("vectrex_emu_room") over:w]; }),
		VB (@"MENU", @"line.3.horizontal", NO, ^{ [VXUI showGameMenuFrom:w]; }) ]];
	s.distribution = UIStackViewDistributionFillEqually; s.spacing = 6; return s;
}
@end

/* ------------------------------------------------ the welcome: the start screen while the games arrive, "ready to play"
 * once after the first ones (the mockup's 1 and 2) */
@implementation VXUI (Welcome)
+ (UIView *) welcomeView
{
	UIView *w = [UIView new]; w.backgroundColor = VX_BG;
	CAShapeLayer *logo = [CAShapeLayer layer]; UIBezierPath *p = [UIBezierPath bezierPath];   /* (a vector shape, as the console draws) */
	CGPoint o[6] = { {0, -78}, {68, -38}, {68, 42}, {0, 82}, {-68, 42}, {-68, -38} }, i[6] = { {0, -48}, {40, -24}, {40, 26}, {0, 50}, {-40, 26}, {-40, -24} };
	[p moveToPoint:o[0]]; for (int k = 1; k < 6; k++) [p addLineToPoint:o[k]]; [p closePath];
	[p moveToPoint:i[0]]; for (int k = 1; k < 6; k++) [p addLineToPoint:i[k]]; [p closePath];
	[p moveToPoint:i[0]]; [p addLineToPoint:i[3]]; [p moveToPoint:i[5]]; [p addLineToPoint:i[2]]; [p moveToPoint:i[1]]; [p addLineToPoint:i[4]];
	logo.path = p.CGPath; logo.strokeColor = VX_G.CGColor; logo.fillColor = UIColor.clearColor.CGColor; logo.lineWidth = 2.2; logo.lineCap = kCALineCapRound;
	logo.shadowColor = VX_G.CGColor; logo.shadowRadius = 10; logo.shadowOpacity = 0.7; logo.shadowOffset = CGSizeZero;
	UIView *lv = [UIView new]; [lv.layer addSublayer:logo]; lv.translatesAutoresizingMaskIntoConstraints = NO; [lv.widthAnchor constraintEqualToConstant:170].active = YES; [lv.heightAnchor constraintEqualToConstant:180].active = YES;
	logo.position = CGPointMake (85, 90);
	UILabel *t = VL (@"Vectrex_EMU", VT (32), VX_HI), *sub = VL (@"The Vectrex, as faithful as it gets", VF (14, UIFontWeightRegular), VX_DIM);
	t.textAlignment = sub.textAlignment = NSTextAlignmentCenter;
	UILabel *st = VL (@"", VF (14, UIFontWeightRegular), VX_FG2); st.tag = 11; st.textAlignment = NSTextAlignmentCenter;
	UIProgressView *pr = [UIProgressView new]; pr.tag = 12; pr.progressTintColor = VX_G; pr.trackTintColor = VXC (0x16302a); pr.hidden = YES;
	UILabel *ver = VL ([NSString stringWithFormat:@"%@ (%@) - test build - diagnostics on", [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"],
		[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"]], VF (11, UIFontWeightRegular), VX_DIM); ver.textAlignment = NSTextAlignmentCenter;
	UIStackView *mid = [[UIStackView alloc] initWithArrangedSubviews:@[ lv, t, sub ]]; mid.axis = UILayoutConstraintAxisVertical; mid.alignment = UIStackViewAlignmentCenter; mid.spacing = 10;
	UIStackView *bot = [[UIStackView alloc] initWithArrangedSubviews:@[ st, pr, ver ]]; bot.axis = UILayoutConstraintAxisVertical; bot.spacing = 12;
	for (UIView *v in @[ mid, bot ]) { v.translatesAutoresizingMaskIntoConstraints = NO; [w addSubview:v]; }
	UILayoutGuide *g = w.safeAreaLayoutGuide;
	[NSLayoutConstraint activateConstraints:@[ [mid.centerXAnchor constraintEqualToAnchor:w.centerXAnchor], [mid.centerYAnchor constraintEqualToAnchor:w.centerYAnchor constant:-60],
		[bot.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:32], [bot.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-32],
		[bot.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-28], [bot.widthAnchor constraintLessThanOrEqualToConstant:520] ]];
	return w;
}
+ (void) showReadyFrom:(UIViewController *)vc
{
	if ([NSUserDefaults.standardUserDefaults boolForKey:@"app.welcomed"] || vc.presentedViewController) return;
	[NSUserDefaults.standardUserDefaults setBool:YES forKey:@"app.welcomed"]; ulog (@"ready-to-play shown");
	UIViewController *r = [UIViewController new]; r.view.backgroundColor = VX_BG; r.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
	NSString *d = vxa_docs (); NSFileManager *fm = NSFileManager.defaultManager; int games = 0;
	for (NSString *f in [fm enumeratorAtPath:[d stringByAppendingPathComponent:@"roms"]]) if ([@[ @"bin", @"vec", @"um2" ] containsObject:f.pathExtension.lowercaseString] && ![f hasPrefix:@"Tests/"]) games++;
	NSArray *rows = @[
		@[ @"Vectrex BIOS", [fm fileExistsAtPath:[d stringByAppendingPathComponent:@"system/vectrex_emu/bios.bin"]] ? @"Ready" : @"Built in" ],
		@[ @"Games", [NSString stringWithFormat:@"%d, with overlays", games] ],
		@[ @"UVMC2 and Last Human", [fm fileExistsAtPath:[d stringByAppendingPathComponent:@"system/vectrex_emu/uvmc2"]] ? @"Ready" : @"-" ],
		@[ @"PiTrex SD card", [fm fileExistsAtPath:[d stringByAppendingPathComponent:@"system/vectrex_emu/pitrex/pitrex.img"]] ? @"Ready, calibrated" : @"-" ],
		@[ @"Speech ROM (VecVoice)", [fm fileExistsAtPath:[d stringByAppendingPathComponent:@"system/vectrex_emu/sp0256-al2.bin"]] ? @"Ready" : @"-" ],
		@[ @"Controls", GCController.controllers.count ? [NSString stringWithFormat:@"%@: player 1", GCController.controllers.firstObject.vendorName ?: @"Controller"] : @"Touch - a controller or keyboard works too" ] ];
	NSMutableArray *v = [NSMutableArray arrayWithArray:@[ VL (@"SET UP - ONCE", VF (12, UIFontWeightSemibold), VX_DIM), VL (@"Ready to play", VT (26), VX_HI),
		VL (@"Everything is in place. The picture's quality is measured for this device at the first game; change anything later in Settings.", VF (14, UIFontWeightRegular), VX_FG2) ]];
	UIStackView *list = [UIStackView new]; list.axis = UILayoutConstraintAxisVertical; list.spacing = 1; list.backgroundColor = VX_EDGE; list.layer.cornerRadius = 14; list.clipsToBounds = YES;
	for (NSArray *rw in rows) { UILabel *a = VL (rw[0], VF (15, UIFontWeightRegular), VX_FG), *b = VL (rw[1], VF (13, UIFontWeightMedium), VX_HI); b.textAlignment = NSTextAlignmentRight;
		UIStackView *h = [[UIStackView alloc] initWithArrangedSubviews:@[ a, b ]]; h.backgroundColor = VX_PANEL; h.layoutMargins = UIEdgeInsetsMake (13, 16, 13, 16); h.layoutMarginsRelativeArrangement = YES;
		[list addArrangedSubview:h]; }
	[v addObject:list];
	__weak UIViewController *wr = r;
	UIButton *play = VB (@"Play", nil, YES, ^{ [wr dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); }]; }), *pick = VB (@"Choose a game", nil, NO, ^{ [wr dismissViewControllerAnimated:YES completion:^{ [VXUI showLibraryFrom:vc]; }]; });
	[play.heightAnchor constraintEqualToConstant:54].active = YES; [pick.heightAnchor constraintEqualToConstant:46].active = YES;
	[v addObjectsFromArray:@[ [UIView new], play, pick ]];
	UIStackView *s = [[UIStackView alloc] initWithArrangedSubviews:v]; s.axis = UILayoutConstraintAxisVertical; s.spacing = 16; s.translatesAutoresizingMaskIntoConstraints = NO; [r.view addSubview:s];
	UILayoutGuide *g = r.view.safeAreaLayoutGuide;
	[NSLayoutConstraint activateConstraints:@[ [s.topAnchor constraintEqualToAnchor:g.topAnchor constant:24], [s.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-20],
		[s.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:20], [s.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-20] ]];
	r.modalPresentationStyle = UIModalPresentationFormSheet; vxa_hold (1);
	[vc presentViewController:r animated:YES completion:nil];
}
@end

/* ------------------------------------------------ what to fetch: the content's parts, each with its size, a switch (the
 * core's own and the games always); an installed part switched off is removed (its space back) */
@interface VXParts : UITableViewController
@property (nonatomic, strong) NSArray<NSDictionary *> *parts; @property (nonatomic, strong) NSDictionary *inst;
@property (nonatomic, strong) NSMutableSet<NSString *> *on; @property (nonatomic) BOOL first;
@property (nonatomic, copy) void (^go) (NSArray<NSString *> *, BOOL); @property (nonatomic, copy) void (^later) (void);
@end
@implementation VXParts { UILabel *_sum; }
- (NSString *) dirOf:(NSString *)pid
{
	NSDictionary *d = @{ @"boxes": @"system/vectrex_emu/thumbs/boxes", @"carts": @"system/vectrex_emu/thumbs/carts",
		@"uvmc2": @"system/vectrex_emu/uvmc2", @"pitrex": @"system/vectrex_emu/pitrex" };
	return d[pid];
}
- (void) viewDidLoad
{
	[super viewDidLoad]; self.title = @"Downloads"; self.tableView.backgroundColor = VX_BG; self.tableView.separatorColor = VX_EDGE;
	_sum = VL (@"", VF (14, UIFontWeightRegular), VX_FG2); _sum.textAlignment = NSTextAlignmentCenter;
	__weak VXParts *ws = self;
	UIButton *dl = VB (@"Download", nil, YES, ^{ [ws start:NO]; }), *wf = VB (@"Wait for Wi-Fi", nil, NO, ^{ [ws start:YES]; });
	UIButton *lt = VB (self.first ? @"Not now" : @"Later", nil, NO, ^{ ulog (@"downloads: later"); [ws dismissViewControllerAnimated:YES completion:^{ if (ws.later) ws.later (); }]; });
	[dl.heightAnchor constraintEqualToConstant:52].active = YES; [wf.heightAnchor constraintEqualToConstant:44].active = YES; [lt.heightAnchor constraintEqualToConstant:44].active = YES;
	extern int vxa_net_expensive (void); wf.hidden = !vxa_net_expensive (); dl.tag = 21;
	lt.hidden = self.first;                               /* (the first start: the needed parts come - no "not now") */
	UIStackView *f = [[UIStackView alloc] initWithArrangedSubviews:@[ _sum, dl, wf, lt ]]; f.axis = UILayoutConstraintAxisVertical; f.spacing = 10;
	f.frame = CGRectMake (0, 0, 320, 230); f.layoutMargins = UIEdgeInsetsMake (16, 16, 16, 16); f.layoutMarginsRelativeArrangement = YES;
	self.tableView.tableFooterView = f; [self refresh];
	if (!self.first) self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithPrimaryAction:[UIAction actionWithTitle:@"Close" image:nil identifier:nil handler:^(UIAction *a) { (void) a; [ws dismissViewControllerAnimated:YES completion:nil]; }]];
}
- (NSArray<NSString *> *) toFetch
{
	NSMutableArray *r = [NSMutableArray array];
	for (NSDictionary *p in self.parts) { NSString *pid = p[@"id"], *v = self.inst[pid];
		if ([self.on containsObject:pid] && (!v || ![v isEqualToString:p[@"version"]])) [r addObject:pid]; }
	return r;
}
- (void) refresh
{
	NSArray *tf = [self toFetch]; long long sz = 0; for (NSDictionary *p in self.parts) if ([tf containsObject:p[@"id"]]) sz += [p[@"size"] longLongValue];
	extern int vxa_net_expensive (void); const int cell = vxa_net_expensive ();
	_sum.text = sz ? [NSString stringWithFormat:@"To download: %.0f MB%@", sz / 1e6, cell ? @" - you are on mobile data" : @""] : @"Nothing to download.";
	UIButton *dl = [self.tableView.tableFooterView viewWithTag:21]; UIButtonConfiguration *c = dl.configuration;
	c.title = sz ? (cell ? [NSString stringWithFormat:@"Download now (%.0f MB of mobile data)", sz / 1e6] : [NSString stringWithFormat:@"Download (%.0f MB)", sz / 1e6]) : @"Done"; dl.configuration = c;
}
- (NSInteger) tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return (NSInteger) self.parts.count; }
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return self.first ? @"NEEDED PARTS COME ALWAYS - CHOOSE THE REST" : @"WHAT IS ON THIS DEVICE"; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s { return @"The Vectrex and the games are needed: without them nothing plays. Each part comes once; later only a part that changed comes again (a needed one by itself). Switch an optional part off to remove it."; }
- (void) tableView:(UITableView *)t willDisplayHeaderView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
	NSDictionary *p = self.parts[(NSUInteger) ip.row]; NSString *pid = p[@"id"], *v = self.inst[pid]; BOOL req = [p[@"required"] boolValue];
	UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
	c.textLabel.text = p[@"name"]; c.textLabel.textColor = VX_FG; c.textLabel.numberOfLines = 2; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone;
	NSString *st = !v ? @"not here" : [v isEqualToString:p[@"version"]] ? @"on this device" : @"a newer version";
	c.detailTextLabel.text = [NSString stringWithFormat:@"%.0f MB  -  %@  -  %@", [p[@"size"] doubleValue] / 1e6, st, req ? @"needed" : @"optional"];
	c.detailTextLabel.textColor = v && ![v isEqualToString:p[@"version"]] ? VX_CHG : VX_DIM;
	UISwitch *sw = [UISwitch new]; sw.onTintColor = VX_G; sw.on = [self.on containsObject:pid]; sw.enabled = !req; __weak VXParts *ws = self;
	[sw addAction:[UIAction actionWithHandler:^(UIAction *a) { BOOL on = ((UISwitch *) a.sender).on; if (on) [ws.on addObject:pid]; else [ws.on removeObject:pid];
		ulog ([NSString stringWithFormat:@"downloads: %@ %@", pid, on ? @"on" : @"off"]); [ws refresh]; }] forControlEvents:UIControlEventValueChanged];
	c.accessoryView = sw; return c;
}
- (void) start:(BOOL)wifi
{
	/* (the parts switched off that are here: removed) */
	for (NSDictionary *p in self.parts) { NSString *pid = p[@"id"]; if ([self.on containsObject:pid] || !self.inst[pid] || [p[@"required"] boolValue]) continue;
		NSString *d = [self dirOf:pid]; if (d) [NSFileManager.defaultManager removeItemAtPath:[vxa_docs () stringByAppendingPathComponent:d] error:nil];
		ulog ([NSString stringWithFormat:@"downloads: %@ removed", pid]);
		NSMutableDictionary *m = [self.inst mutableCopy]; [m removeObjectForKey:pid]; self.inst = m;
		[[NSJSONSerialization dataWithJSONObject:m options:0 error:nil] writeToFile:[vxa_docs () stringByAppendingPathComponent:@"parts.json"] atomically:YES]; }
	NSArray *ids = [self toFetch]; __weak VXParts *ws = self; ulog ([NSString stringWithFormat:@"downloads: start %@%@", [ids componentsJoinedByString:@","], wifi ? @" (Wi-Fi only)" : @""]);
	[self dismissViewControllerAnimated:YES completion:^{ if (ws.go) ws.go (ids, wifi); }];
}
@end

@implementation VXUI (Parts)
+ (void) choosePartsIn:(UIViewController *)vc parts:(NSArray *)parts installed:(NSDictionary *)inst first:(BOOL)first go:(void (^) (NSArray<NSString *> *, BOOL))go later:(void (^) (void))later
{
	if (vc.presentedViewController) return;
	VXParts *p = [[VXParts alloc] initWithStyle:UITableViewStyleInsetGrouped]; p.parts = parts; p.inst = inst ?: @{}; p.first = first; p.go = go; p.later = later;
	p.on = [NSMutableSet set];
	for (NSDictionary *x in parts) if ([x[@"required"] boolValue] || inst[x[@"id"]] || first) [p.on addObject:x[@"id"]];   /* (the first time: everything on) */
	UINavigationController *n = [[UINavigationController alloc] initWithRootViewController:p]; [self styleNav:n];
	n.modalPresentationStyle = UIModalPresentationFormSheet; n.modalInPresentation = first;
	ulog (@"downloads opened"); [vc presentViewController:n animated:YES completion:nil];
}
@end

/* ------------------------------------------------ the native control deck (the PC app's strip below the picture, in the
 * quick bar's style): the figures (the core's own, twice a second), port 2, the cartridge (the library), RESET, the
 * knob - turned round with a finger (a tick every 5 %), a long press switches the Vectrex off and on */
extern void vx_app_osd (char *fps, char *hz, char *emu, int *cpu, int *gpu);
extern double vx_app_volume (void); extern void vx_app_turn (double d); extern void vx_app_power (void);
@interface VXKnob : UIControl
@property (nonatomic) double value;                 /* (0..1; 0: off) */
@end
@implementation VXKnob { CAShapeLayer *_track, *_arc, *_cap; double _lastAng; int _lastStep; }
- (instancetype) initWithFrame:(CGRect)f
{
	self = [super initWithFrame:f];
	_track = [CAShapeLayer layer]; _arc = [CAShapeLayer layer]; _cap = [CAShapeLayer layer];
	for (CAShapeLayer *l in @[ _track, _arc ]) { l.fillColor = UIColor.clearColor.CGColor; l.lineWidth = 3.5; l.lineCap = kCALineCapRound; [self.layer addSublayer:l]; }
	_track.strokeColor = VXC (0x16302a).CGColor; _arc.strokeColor = VX_G.CGColor;
	_cap.fillColor = VX_BODY.CGColor; _cap.strokeColor = VX_HI.CGColor; _cap.lineWidth = 1.4; [self.layer addSublayer:_cap];
	UILongPressGestureRecognizer *lp = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(press:)]; [self addGestureRecognizer:lp];
	self.accessibilityLabel = @"Volume and power"; return self;
}
- (void) layoutSubviews
{
	[super layoutSubviews]; CGFloat r = MIN (self.bounds.size.width, self.bounds.size.height) / 2 - 4; CGPoint c = CGPointMake (CGRectGetMidX (self.bounds), CGRectGetMidY (self.bounds));
	_track.path = [UIBezierPath bezierPathWithArcCenter:c radius:r startAngle:M_PI * 0.75 endAngle:M_PI * 2.25 clockwise:YES].CGPath;
	_cap.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake (c.x - r * 0.62, c.y - r * 0.62, r * 1.24, r * 1.24)].CGPath; [self draw];
}
- (void) draw
{
	CGFloat r = MIN (self.bounds.size.width, self.bounds.size.height) / 2 - 4; CGPoint c = CGPointMake (CGRectGetMidX (self.bounds), CGRectGetMidY (self.bounds));
	_arc.path = [UIBezierPath bezierPathWithArcCenter:c radius:r startAngle:M_PI * 0.75 endAngle:M_PI * (0.75 + 1.5 * MAX (0.001, self.value)) clockwise:YES].CGPath;
	_arc.hidden = self.value <= 0;
}
- (void) setValue:(double)v { _value = v; [self draw]; }
- (double) angleAt:(CGPoint)p { return atan2 (p.y - CGRectGetMidY (self.bounds), p.x - CGRectGetMidX (self.bounds)); }
- (BOOL) beginTrackingWithTouch:(UITouch *)t withEvent:(UIEvent *)e { _lastAng = [self angleAt:[t locationInView:self]]; _lastStep = (int) (self.value * 20); return YES; }
- (BOOL) continueTrackingWithTouch:(UITouch *)t withEvent:(UIEvent *)e
{
	double a = [self angleAt:[t locationInView:self]], d = a - _lastAng; if (d > M_PI) d -= 2 * M_PI; if (d < -M_PI) d += 2 * M_PI; _lastAng = a;
	vx_app_turn (d / (M_PI * 1.5)); self.value = vx_app_volume ();   /* (a full turn of the arc: 0 to 100 %) */
	int st = (int) (self.value * 20); if (st != _lastStep) { _lastStep = st; [[UISelectionFeedbackGenerator new] selectionChanged]; }
	[self sendActionsForControlEvents:UIControlEventValueChanged]; return YES;
}
- (void) endTrackingWithTouch:(UITouch *)t withEvent:(UIEvent *)e { ulog ([NSString stringWithFormat:@"deck: volume %.0f %%", self.value * 100]); }
- (void) press:(UILongPressGestureRecognizer *)g
{
	if (g.state != UIGestureRecognizerStateBegan) return;
	vx_app_power (); self.value = vx_app_volume (); [[UIImpactFeedbackGenerator new] impactOccurred];
	ulog (self.value > 0 ? @"deck: switched on" : @"deck: switched off"); [self sendActionsForControlEvents:UIControlEventValueChanged];
}
@end


/* the deck's panels: a rounded box, the PC app's strip look; on (online): the highlighted one */
@interface VXPan : UIControl
@property (nonatomic) BOOL on;
@end
@implementation VXPan
- (instancetype) initWithFrame:(CGRect)f { self = [super initWithFrame:f]; self.layer.cornerRadius = 10; self.layer.borderWidth = 1; self.on = NO; return self; }
- (void) setOn:(BOOL)o { _on = o; self.backgroundColor = o ? VX_SEL : VX_PANEL; self.layer.borderColor = (o ? VX_EDGE2 : VX_EDGE).CGColor; }
- (void) setHighlighted:(BOOL)h { [super setHighlighted:h]; self.alpha = h ? 0.6 : 1; }
@end
/* the volume as a bar (the slim deck): drag along it; a long press switches the Vectrex off and on, as the knob */
@interface VXVolBar : UIControl
@end
@implementation VXVolBar { UIImageView *_ic; CALayer *_tr, *_fill; UILabel *_v; }
- (instancetype) initWithFrame:(CGRect)f
{
	self = [super initWithFrame:f];
	_ic = [UIImageView new]; _ic.contentMode = UIViewContentModeScaleAspectFit; [self addSubview:_ic];
	_tr = [CALayer layer]; _tr.backgroundColor = VXC (0x16302a).CGColor; _tr.cornerRadius = 2.5; [self.layer addSublayer:_tr];
	_fill = [CALayer layer]; _fill.backgroundColor = VX_G.CGColor; _fill.cornerRadius = 2.5; [self.layer addSublayer:_fill];
	_v = VL (@"", [UIFont monospacedDigitSystemFontOfSize:11 weight:UIFontWeightSemibold], VX_HI); _v.numberOfLines = 1; _v.textAlignment = NSTextAlignmentRight; [self addSubview:_v];
	[self addGestureRecognizer:[[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(press:)]];
	self.accessibilityLabel = @"Volume and power"; return self;
}
- (void) layoutSubviews
{
	[super layoutSubviews]; CGSize s = self.bounds.size;
	_ic.frame = CGRectMake (9, (s.height - 15) / 2, 16, 15); _v.frame = CGRectMake (s.width - 9 - 28, 0, 28, s.height);
	_tr.frame = CGRectMake (32, s.height / 2 - 2.5, MAX (10, s.width - 32 - 42), 5); [self show];
}
- (void) show
{
	const double v = vx_app_volume (); const BOOL off = !vx_app_power_on ();
	_ic.image = [UIImage systemImageNamed:off ? @"power" : @"speaker.wave.2" withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:12 weight:UIImageSymbolWeightSemibold]];
	_ic.tintColor = off ? VX_CHG : VX_HI; _v.text = off ? @"OFF" : [NSString stringWithFormat:@"%.0f", v * 100]; _v.textColor = off ? VX_CHG : VX_HI;
	[CATransaction begin]; [CATransaction setDisableActions:YES];
	CGRect t = _tr.frame; _fill.frame = CGRectMake (t.origin.x, t.origin.y, off ? 0 : t.size.width * MAX (0.0, MIN (1.0, v)), t.size.height); [CATransaction commit];
}
- (void) setTo:(CGPoint)p
{
	CGRect t = _tr.frame; if (t.size.width <= 0 || !vx_app_power_on ()) return;
	const double want = MAX (0.0, MIN (1.0, (p.x - t.origin.x) / t.size.width)); vx_app_turn (want - vx_app_volume ()); [self show];
}
- (BOOL) beginTrackingWithTouch:(UITouch *)t withEvent:(UIEvent *)e { [self setTo:[t locationInView:self]]; [self sendActionsForControlEvents:UIControlEventValueChanged]; return YES; }
- (BOOL) continueTrackingWithTouch:(UITouch *)t withEvent:(UIEvent *)e { [self setTo:[t locationInView:self]]; [self sendActionsForControlEvents:UIControlEventValueChanged]; return YES; }
- (void) endTrackingWithTouch:(UITouch *)t withEvent:(UIEvent *)e { ulog ([NSString stringWithFormat:@"deck: volume %.0f %%", vx_app_volume () * 100]); }
- (void) press:(UILongPressGestureRecognizer *)g
{
	if (g.state != UIGestureRecognizerStateBegan) return;
	vx_app_power (); [self show]; [[UIImpactFeedbackGenerator new] impactOccurred];
	ulog (vx_app_power_on () ? @"deck: switched on" : @"deck: switched off"); [self sendActionsForControlEvents:UIControlEventValueChanged];
}
@end

/* VecVoice's robot (the simple, robotic one): drawn, there is no SF symbol for it */
static UIImage *VXRobot (CGFloat pt)
{
	UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake (pt, pt)];
	UIImage *im = [r imageWithActions:^(UIGraphicsImageRendererContext *c) { (void) c; const CGFloat k = pt / 24.0;
		[UIColor.blackColor setStroke]; [UIColor.blackColor setFill];
		UIBezierPath *h = [UIBezierPath bezierPathWithRoundedRect:CGRectMake (5 * k, 7 * k, 14 * k, 10 * k) cornerRadius:2.5 * k]; h.lineWidth = 1.9 * k; [h stroke];
		UIBezierPath *a = [UIBezierPath bezierPath]; [a moveToPoint:CGPointMake (12 * k, 7 * k)]; [a addLineToPoint:CGPointMake (12 * k, 4 * k)]; a.lineWidth = 1.9 * k; a.lineCapStyle = kCGLineCapRound; [a stroke];
		[[UIBezierPath bezierPathWithOvalInRect:CGRectMake (10.6 * k, 1.8 * k, 2.8 * k, 2.8 * k)] fill];
		[[UIBezierPath bezierPathWithOvalInRect:CGRectMake (8.2 * k, 10 * k, 2.6 * k, 2.6 * k)] fill]; [[UIBezierPath bezierPathWithOvalInRect:CGRectMake (13.2 * k, 10 * k, 2.6 * k, 2.6 * k)] fill];
		UIBezierPath *m = [UIBezierPath bezierPath]; [m moveToPoint:CGPointMake (9.5 * k, 14.6 * k)]; [m addLineToPoint:CGPointMake (14.5 * k, 14.6 * k)]; m.lineWidth = 1.5 * k; m.lineCapStyle = kCGLineCapRound; [m stroke];
		UIBezierPath *b = [UIBezierPath bezierPath]; [b moveToPoint:CGPointMake (8 * k, 17 * k)]; [b addLineToPoint:CGPointMake (8 * k, 21 * k)]; [b moveToPoint:CGPointMake (16 * k, 17 * k)]; [b addLineToPoint:CGPointMake (16 * k, 21 * k)];
		b.lineWidth = 1.9 * k; b.lineCapStyle = kCGLineCapRound; [b stroke]; }];
	return [im imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
}
static UIImage *VXSym (NSString *n, NSString *fallback, CGFloat pt)
{
	UIImageSymbolConfiguration *c = [UIImageSymbolConfiguration configurationWithPointSize:pt weight:UIImageSymbolWeightSemibold];
	return [UIImage systemImageNamed:n withConfiguration:c] ?: [UIImage systemImageNamed:fallback withConfiguration:c];
}

@implementation VXDeck {
	VXPan *_stats, *_dev, *_cart, *_rst, *_volP; VXKnob *_knob; VXVolBar *_bar;
	UILabel *_sMain, *_n1, *_n2, *_r1, *_r2, *_lc, *_lg, *_devW, *_cartW, *_rstW, *_kv; UIImageView *_devI, *_cartI, *_rstI;
	CALayer *_cT, *_cF, *_gT, *_gF; NSTimer *_tm; NSDictionary *_st; double _stT; int _online, _role;
}
- (UILabel *) word { UILabel *l = VL (@"", VF (9, UIFontWeightBold), VX_HI); l.numberOfLines = 1; l.textAlignment = NSTextAlignmentCenter; l.adjustsFontSizeToFitWidth = YES; l.minimumScaleFactor = 0.7; return l; }
- (VXPan *) pan:(SEL)act { VXPan *p = [VXPan new]; [p addTarget:self action:act forControlEvents:UIControlEventTouchUpInside]; [self addSubview:p]; return p; }
- (instancetype) initWithGame:(UIViewController *)g
{
	self = [super initWithFrame:CGRectZero]; _game = g; _mode = 1;
	_stats = [VXPan new]; [self addSubview:_stats]; [_stats addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(statsTap:)]]; _dev = [self pan:@selector(devTap)]; _cart = [self pan:@selector(cartTap)]; _rst = [self pan:@selector(rstTap)];
	UIFont *mono = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightSemibold];
	_sMain = VL (@"--", mono, VX_HI); _sMain.numberOfLines = 1; _sMain.textAlignment = NSTextAlignmentCenter; _sMain.adjustsFontSizeToFitWidth = YES; _sMain.minimumScaleFactor = 0.6; [_stats addSubview:_sMain];
	_n1 = VL (@"", VF (10, UIFontWeightBold), VX_HI); _n2 = VL (@"", VF (10, UIFontWeightBold), VX_HI); _r1 = VL (@"", VF (9, UIFontWeightBold), VX_DIM); _r2 = VL (@"", VF (9, UIFontWeightBold), VX_DIM);
	for (UILabel *l in @[ _n1, _n2, _r1, _r2 ]) { l.numberOfLines = 1; [_stats addSubview:l]; }
	_r1.textAlignment = _r2.textAlignment = NSTextAlignmentRight;
	_lc = VL (@"CPU", VF (8, UIFontWeightBold), VX_DIM); _lg = VL (@"GPU", VF (8, UIFontWeightBold), VX_DIM); [_stats addSubview:_lc]; [_stats addSubview:_lg];
	_cT = [CALayer layer]; _cF = [CALayer layer]; _gT = [CALayer layer]; _gF = [CALayer layer];
	for (CALayer *l in @[ _cT, _cF, _gT, _gF ]) { l.cornerRadius = 2; [_stats.layer addSublayer:l]; }
	_cT.backgroundColor = _gT.backgroundColor = VXC (0x16302a).CGColor; _cF.backgroundColor = _gF.backgroundColor = VX_G.CGColor;
	_devI = [UIImageView new]; _cartI = [UIImageView new]; _rstI = [UIImageView new];
	for (UIImageView *v in @[ _devI, _cartI, _rstI ]) { v.contentMode = UIViewContentModeScaleAspectFit; v.tintColor = VX_HI; }
	_devW = [self word]; _cartW = [self word]; _rstW = [self word];
	[_dev addSubview:_devI]; [_dev addSubview:_devW]; [_cart addSubview:_cartI]; [_cart addSubview:_cartW]; [_rst addSubview:_rstI]; [_rst addSubview:_rstW];
	_cartW.text = @"CART"; _rstW.text = @"RESET"; _cartI.image = VXSym (@"sdcard", @"square.grid.2x2", 15); _rstI.image = VXSym (@"arrow.counterclockwise", @"arrow.counterclockwise", 15);
	for (UIView *v in @[ _stats, _dev, _cart, _rst ]) for (UIView *s in v.subviews) s.userInteractionEnabled = NO;
	_volP = [VXPan new]; [self addSubview:_volP];
	_knob = [VXKnob new]; [_knob addTarget:self action:@selector(volChanged) forControlEvents:UIControlEventValueChanged]; [_volP addSubview:_knob];
	_kv = VL (@"", [UIFont monospacedDigitSystemFontOfSize:9 weight:UIFontWeightBold], VX_HI); _kv.textAlignment = NSTextAlignmentCenter; _kv.adjustsFontSizeToFitWidth = YES; _kv.minimumScaleFactor = 0.6; _kv.userInteractionEnabled = NO; [_knob addSubview:_kv];
	_bar = [VXVolBar new]; [_bar addTarget:self action:@selector(volChanged) forControlEvents:UIControlEventValueChanged]; [_volP addSubview:_bar];
	_stats.accessibilityLabel = @"Figures"; _cart.accessibilityLabel = @"Cartridge: the games"; _rst.accessibilityLabel = @"RESET";
	__weak VXDeck *ws = self; _tm = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) { (void) t; [ws tick]; }];
	[self tick]; return self;
}
- (void) dealloc { [_tm invalidate]; }
- (void) setMode:(int)m { if (m == _mode) return; _mode = m; [self setNeedsLayout]; [self tick]; }
- (void) poke { UIViewController *g = self.game; if ([g respondsToSelector:@selector(deckPoked)]) [g performSelector:@selector(deckPoked)]; }   /* (the pop-up: open a while longer) */
/* (a panel's icon and word: full - the icon above the word; slim - beside it) */
- (void) place:(UIImageView *)i word:(UILabel *)w in:(UIView *)p
{
	CGSize s = p.bounds.size;
	if (_mode == 1) { i.frame = CGRectMake (0, 7, s.width, 17); w.frame = CGRectMake (3, 27, s.width - 6, 12); }
	else { [w sizeToFit]; const CGFloat iw = 16, gap = 4, tw = MIN (w.bounds.size.width, s.width - iw - gap - 8), x = (s.width - iw - gap - tw) / 2;
		i.frame = CGRectMake (x, (s.height - 15) / 2, iw, 15); w.frame = CGRectMake (x + iw + gap, 0, tw, s.height); }
}
- (void) layoutSubviews
{
	[super layoutSubviews]; const CGFloat W = self.bounds.size.width, H = self.bounds.size.height, m = 8, gap = _mode == 1 ? 6 : 5;
	const CGFloat ph = _mode == 1 ? 46 : 30, y = (H - ph) / 2;
	if (_mode == 1) {                                      /* (full: the figures a third, the knob 50, the three buttons alike) */
		const CGFloat sw = floor ((W - 2 * m) * 0.33), kw = 50, bw = floor ((W - 2 * m - sw - kw - 4 * gap) / 3);
		_stats.frame = CGRectMake (m, y, sw, ph); CGFloat x = m + sw + gap;
		for (UIView *v in @[ _dev, _cart, _rst ]) { v.frame = CGRectMake (x, y, bw, ph); x += bw + gap; }
		_volP.frame = CGRectMake (x, y, W - m - x, ph); _knob.hidden = NO; _bar.hidden = YES; _knob.frame = CGRectMake ((_volP.bounds.size.width - 42) / 2, 2, 42, 42); { const CGFloat kw = (21 - 4) * 0.62 * 2 * 0.8; _kv.frame = CGRectMake ((42 - kw) / 2, (42 - 12) / 2, kw, 12); }   /* (inside the cap: "100" at 9 pt ran over the ring on the iPhone, 2026-11-19 - as the core and Android, 80 % of the cap) */
		_volP.backgroundColor = VX_PANEL; _volP.layer.cornerRadius = 10; _volP.layer.borderWidth = 1; _volP.layer.borderColor = VX_EDGE.CGColor;
	} else {                                               /* (slim: the figures, three 58 pt buttons, the volume the rest) */
		const CGFloat sw = MIN (170, floor ((W - 2 * m) * 0.29)), bw = 58;
		_stats.frame = CGRectMake (m, y, sw, ph); CGFloat x = m + sw + gap;
		for (UIView *v in @[ _dev, _cart, _rst ]) { v.frame = CGRectMake (x, y, bw, ph); x += bw + gap; }
		_volP.frame = CGRectMake (x, y, W - m - x, ph); _knob.hidden = YES; _bar.hidden = NO; _bar.frame = _volP.bounds;
		_volP.backgroundColor = VX_PANEL; _volP.layer.cornerRadius = 10; _volP.layer.borderWidth = 1; _volP.layer.borderColor = VX_EDGE.CGColor;
	}
	[self place:_devI word:_devW in:_dev]; [self place:_cartI word:_cartW in:_cart];
	if (_mode == 1) { _rstI.hidden = NO; [self place:_rstI word:_rstW in:_rst]; } else { _rstI.hidden = YES; _rstW.frame = _rst.bounds; }   /* (slim: RESET in words) */
	[self layoutStats];
}
- (void) layoutStats
{
	CGSize s = _stats.bounds.size; const BOOL names = _online != 0;
	_n1.hidden = _n2.hidden = _r1.hidden = _r2.hidden = !names; _sMain.hidden = names;
	const BOOL bars = _mode == 1 && !names; _lc.hidden = _lg.hidden = !bars;
	for (CALayer *l in @[ _cT, _cF, _gT, _gF ]) l.hidden = !bars;
	if (names) { const CGFloat rw = 52, top = (s.height - 26) / 2;
		_n1.frame = CGRectMake (9, top, s.width - 18 - rw, 13); _n2.frame = CGRectMake (9, top + 13, s.width - 18 - rw, 13);
		_r1.frame = CGRectMake (s.width - 9 - rw, top, rw, 13); _r2.frame = CGRectMake (s.width - 9 - rw, top + 13, rw, 13); return; }
	if (!bars) { _sMain.frame = CGRectInset (_stats.bounds, 7, 0); return; }
	_sMain.frame = CGRectMake (6, 6, s.width - 12, 17);
	[_lc sizeToFit]; [_lg sizeToFit]; const CGFloat by = 31, bw = (s.width - 16 - _lc.bounds.size.width - _lg.bounds.size.width - 16) / 2;
	_lc.frame = CGRectMake (8, by - 4, _lc.bounds.size.width, 9); _cT.frame = CGRectMake (CGRectGetMaxX (_lc.frame) + 4, by - 2, bw, 4);
	_lg.frame = CGRectMake (CGRectGetMaxX (_cT.frame) + 6, by - 4, _lg.bounds.size.width, 9); _gT.frame = CGRectMake (CGRectGetMaxX (_lg.frame) + 4, by - 2, bw, 4);
}
/* the figures: the EMU RATE coloured as the PC's (green from 97 %, orange from 80 %, red below) */
- (NSAttributedString *) fig:(const char *)f hz:(const char *)h emu:(const char *)e
{
	const int pct = atoi (e); UIColor *ec = !isdigit ((unsigned char) e[0]) || pct >= 97 ? VX_G : pct >= 80 ? VX_CHG : VXC (0xed5454);
	UIFont *mono = [UIFont monospacedDigitSystemFontOfSize:_mode == 1 ? 12 : 11 weight:UIFontWeightSemibold];
	NSMutableAttributedString *a = [[NSMutableAttributedString alloc] initWithString:[NSString stringWithFormat:@"%s  %s  ", f, h] attributes:@{ NSFontAttributeName: mono, NSForegroundColorAttributeName: VX_HI }];
	[a appendAttributedString:[[NSAttributedString alloc] initWithString:@(e) attributes:@{ NSFontAttributeName: mono, NSForegroundColorAttributeName: ec }]];
	return a;
}
- (void) tick
{
	if (self.hidden || !self.window) return;
	extern int vxa_net_playing (void), vxa_is_held (void);
	/* online: the status (the players, the ports, the round trip) - a few times a second is plenty */
	const double now = CACurrentMediaTime (); if (now - _stT > 0.45) { _stT = now;
		NSDictionary *st = [NSJSONSerialization JSONObjectWithData:[@(vx_net_status ()) dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil]; _st = [st isKindOfClass:NSDictionary.class] ? st : nil; }
	const int ph = [_st[@"phase"] intValue], role = [_st[@"role"] intValue], hosting = role == 1 && ph >= 1 && ph <= 3, online = vxa_net_playing () || hosting;
	if (online != (_online != 0) || role != _role) { _online = online; _role = role; [self layoutStats]; }
	const int power = vx_app_power_on (), paused = vx_app_paused () || vxa_is_held ();
	/* the figures, or online the players: "1 HUGO / 2 BART", the round trip; hosting: 2 FREE, WAITING */
	if (online) {
		NSString *me = [_st[@"me"] isKindOfClass:NSString.class] && [_st[@"me"] length] ? [_st[@"me"] uppercaseString] : @"YOU";
		NSString *peer = [_st[@"peer"] isKindOfClass:NSString.class] && [_st[@"peer"] length] ? [_st[@"peer"] uppercaseString] : (role == 1 ? @"PLAYER 2" : @"PLAYER 1");
		NSArray *pt = [_st[@"ports"] isKindOfClass:NSArray.class] ? _st[@"ports"] : @[ @1, @2 ];
		const int myPort = [pt[role == 1 ? 0 : 1] intValue] ?: (role == 1 ? 1 : 2);
		NSString *a1 = nil, *a2 = nil;   /* (who holds each port) */
		if (hosting) { a1 = me; a2 = nil; }
		else { NSString *host = role == 1 ? me : peer, *join = role == 1 ? peer : me; const int hp = [pt[0] intValue], jp = [pt[1] intValue];
			a1 = hp == 1 ? host : jp == 1 ? join : nil; a2 = hp == 2 ? host : jp == 2 ? join : nil; }
		_n1.text = [NSString stringWithFormat:@"1 %@", a1 ?: @"FREE"]; _n2.text = [NSString stringWithFormat:@"2 %@", a2 ?: @"FREE"];
		_n1.textColor = myPort == 1 ? VX_HI2 : a1 ? VX_HI : VX_DIM; _n2.textColor = myPort == 2 ? VX_HI2 : a2 ? VX_HI : VX_DIM;
		const int rtt = [_st[@"rtt"] intValue], relay = [_st[@"relay"] intValue], lost = [_st[@"lost"] intValue];
		if (hosting) { _r1.text = @"WAITING"; _r1.textColor = VX_CHG; _r2.text = @""; }
		else if (lost) { _r1.text = @"NO"; _r2.text = @"CONNECTION"; _r1.textColor = _r2.textColor = VX_CHG; }
		else { _r1.text = relay ? @"RELAY" : @""; _r2.text = rtt > 0 ? [NSString stringWithFormat:@"%d MS", rtt] : @""; _r1.textColor = _r2.textColor = VX_DIM; }
	} else if (paused && power) { _sMain.text = @"PAUSED"; _sMain.textColor = VX_CHG; _sMain.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightBold]; }
	else { char f[24], h[24], e[24]; int c = -1, gq = -1; vx_app_osd (f, h, e, &c, &gq);
		/* (FPS: the pictures the screen was given a second - what the phone manages after the emulator and the GPU; the core's
		 *  own figure counts the emulator's runs, 120 even when the GPU delivers fewer. INT: the game's own frames) */
		{ extern long vxa_presents; static long p0 = -1; static double t0; static int shown = -1; const double tn = CACurrentMediaTime ();
		  if (p0 < 0 || vxa_presents < p0) { p0 = vxa_presents; t0 = tn; }
		  else if (tn - t0 >= 0.95) { shown = (int) ((vxa_presents - p0) / (tn - t0) + 0.5); p0 = vxa_presents; t0 = tn; }
		  if (shown >= 0 && power) snprintf (h, sizeof h, "%d FPS", shown); }
		if (!power) { _sMain.text = @"-- INT  -- FPS  --"; _sMain.textColor = VXC (0x4a6658); _sMain.font = [UIFont monospacedDigitSystemFontOfSize:_mode == 1 ? 12 : 11 weight:UIFontWeightSemibold]; }
		else _sMain.attributedText = [self fig:f hz:h emu:e];
		[CATransaction begin]; [CATransaction setDisableActions:YES];
		_cF.frame = CGRectMake (_cT.frame.origin.x, _cT.frame.origin.y, _cT.frame.size.width * MAX (0.0, MIN (1.0, (power ? c : 0) / 100.0)), 4);
		_gF.frame = CGRectMake (_gT.frame.origin.x, _gT.frame.origin.y, _gT.frame.size.width * MAX (0.0, MIN (1.0, (power ? gq : 0) / 100.0)), 4);
		[CATransaction commit]; }
	/* port 2: its device - online, your badge (P1 / P2); VecLink later */
	const int full = _mode == 1;
	if (online) { const int me2 = [([_st[@"ports"] isKindOfClass:NSArray.class] ? _st[@"ports"] : @[ @1, @2 ])[role == 1 ? 0 : 1] intValue] ?: (role == 1 ? 1 : 2);
		_dev.on = YES; _devI.image = VXSym (@"globe", @"network", 15); _devI.tintColor = VX_HI2; _devW.textColor = VX_HI2;
		_devW.text = [NSString stringWithFormat:full ? @"ONLINE P%d" : @"P%d", me2]; _dev.accessibilityLabel = [NSString stringWithFormat:@"Online: you are player %d", me2]; }
	else { _dev.on = NO; _devI.tintColor = VX_HI; _devW.textColor = VX_HI; const int d = vx_app_port2 ();
		static NSString *const W[2][5] = { { @"P2", @"VOX", @"VOICE", @"3D", @"PEN" }, { @"P2", @"VECVOX", @"VECVOICE", @"3D IMAGER", @"LIGHT PEN" } };
		const int k = d >= 0 && d <= 4 ? d : 0; _devW.text = W[full][k];
		_devI.image = k == 2 ? VXRobot (17) : k == 1 ? VXSym (@"person.wave.2", @"waveform", 15) : k == 3 ? VXSym (@"eyeglasses", @"eyeglasses", 15) : k == 4 ? VXSym (@"pencil.tip", @"pencil", 15) : VXSym (@"gamecontroller", @"gamecontroller", 15);
		_dev.accessibilityLabel = [NSString stringWithFormat:@"Port 2: %@", W[1][k]]; }
	/* the cartridge: out - dim, OUT */
	const int out = vx_app_cart_out (); UIColor *cc = out ? VXC (0x4a6658) : VX_HI; _cartI.tintColor = cc; _cartW.textColor = cc; _cartW.text = out ? @"OUT" : @"CART";
	/* the volume: the knob (full) or the bar (slim), off: OFF */
	if (!_knob.tracking) { _knob.value = vx_app_volume (); [self volShow]; }   /* (shown only: not a touch - it kept the pop-up deck open) */
	if (!_bar.tracking) [_bar show];
	[self setNeedsLayout];
}
- (void) volChanged { [self volShow]; [self poke]; }   /* (the knob or the bar turned: the pop-up deck stays a while longer) */
- (void) volShow
{
	const int on = vx_app_power_on ();
	_kv.text = on ? [NSString stringWithFormat:@"%.0f", _knob.value * 100] : @"OFF"; _kv.textColor = on ? VX_HI : VX_CHG;
}
- (void) statsTap:(UITapGestureRecognizer *)g   /* (online, playing: a player's row tapped - that port taken, the PC's TAKE PORT) */
{
	[self poke]; extern int vxa_net_playing (void); if (!_online || !vxa_net_playing ()) return;
	const int k = [g locationInView:_stats].y < _stats.bounds.size.height / 2 ? 1 : 2; extern void vxa_toast_ext (NSString *s);
	vx_net_take_port (k); vxa_toast_ext ([NSString stringWithFormat:@"ONLINE: YOU TAKE PORT %d", k]); ulog ([NSString stringWithFormat:@"deck: take port %d", k]);
}
- (void) devTap
{
	[self poke]; UIViewController *g = self.game; if (!g) return;
	if (_online) {                                       /* (online: hosting - the code again; else the Online screen) */
		NSString *code = [_st[@"code"] isKindOfClass:NSString.class] ? _st[@"code"] : @"";
		if (_role == 1 && [_st[@"phase"] intValue] < 3 && code.length) { extern void vxa_toast_ext (NSString *s); vxa_toast_ext ([NSString stringWithFormat:@"ONLINE: HOSTING - CODE %@", code]); return; }
		[VXUI showOnlineFrom:g]; return; }
	int i = vxa_ofind ("vectrex_emu_port2"); if (i >= 0) [VXUI showChoice:i from:g];
}
- (void) cartTap { [self poke]; if (self.game) [VXUI showLibraryFrom:self.game]; }
- (void) rstTap { [self poke]; vxa_do (@"reset"); [[UIImpactFeedbackGenerator new] impactOccurred]; }
@end

@implementation VXUI (Deck)
+ (UIView *) deckFor:(UIViewController *)vc { VXDeck *d = [[VXDeck alloc] initWithGame:vc]; return d; }
+ (void) showChoice:(int)i from:(UIViewController *)vc
{
	if (vc.presentedViewController) return;
	VXChoice *ch = [[VXChoice alloc] initWithStyle:UITableViewStyleInsetGrouped]; ch.opt = i;
	UINavigationController *n = [[UINavigationController alloc] initWithRootViewController:ch]; [self styleNav:n];
	n.modalPresentationStyle = UIModalPresentationPageSheet; n.sheetPresentationController.detents = @[ UISheetPresentationControllerDetent.mediumDetent ];
	[vc presentViewController:n animated:YES completion:nil];
}
@end

/* ------------------------------------------------ Settings > Controls: what is connected, the Vectrex buttons on the
 * controller's, the touch controls (shown, size, opacity, left-handed), the PC keys */
@interface VXControls : UITableViewController
@property (nonatomic, weak) UIViewController *game;
@end
@implementation VXControls
static NSString *const VXPhys[8] = { @"A (bottom)", @"B (right)", @"X (left)", @"Y (top)", @"L1", @"R1", @"L2", @"R2" };
static const int VXPhysDef[4] = { 2, 3, 0, 1 };
- (void) viewDidLoad
{
	[super viewDidLoad]; self.title = @"Controls"; self.tableView.backgroundColor = VX_BG; self.tableView.separatorColor = VX_EDGE;
	for (NSString *n in @[ GCControllerDidConnectNotification, GCControllerDidDisconnectNotification, GCKeyboardDidConnectNotification, GCKeyboardDidDisconnectNotification ])
		[NSNotificationCenter.defaultCenter addObserver:self.tableView selector:@selector(reloadData) name:n object:nil];
}
- (void) changed { UIViewController *g = self.game; if ([g respondsToSelector:@selector(controlsChanged)]) [g performSelector:@selector(controlsChanged)]; }
- (NSInteger) numberOfSectionsInTableView:(UITableView *)t { return 4; }
- (NSInteger) tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s
{
	if (s == 0) return (NSInteger) MAX (1, GCController.controllers.count + (GCKeyboard.coalescedKeyboard ? 1 : 0));
	return s == 1 ? 4 : s == 2 ? 5 : 1;
}
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return @[ @"CONNECTED", @"VECTREX BUTTONS ON THE CONTROLLER", @"TOUCH CONTROLS", @"KEYBOARD" ][(NSUInteger) s]; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s
{
	return @[ @"Player 1 is the first controller, player 2 the second.",
		@"Tap a button to choose. The Vectrex controller's four buttons in a row; on a modern pad's diamond by default 1 left, 2 top, 3 bottom, 4 right.",
		@"Automatic: shown only while no controller or keyboard is connected. The stick appears where the thumb lands; the D-pad gives clear up, down, left, right (the small switch beside it changes it too).",
		@"As on the PC: the arrows the stick (Shift: half), A W S D buttons 1-4, P pause, L the games, O overlay, Ctrl+R RESET, Ctrl+E eject, Page Up / Down volume, M the menu." ][(NSUInteger) s];
}
- (void) tableView:(UITableView *)t willDisplayHeaderView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) cell:(NSString *)t detail:(NSString *)d
{
	UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
	c.textLabel.text = t; c.textLabel.textColor = VX_FG; c.detailTextLabel.text = d; c.detailTextLabel.textColor = VX_HI; c.backgroundColor = VX_PANEL;
	UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c;
}
- (UISlider *) slider:(NSString *)key min:(float)a max:(float)b def:(float)d
{
	UISlider *sl = [[UISlider alloc] initWithFrame:CGRectMake (0, 0, 150, 30)]; sl.minimumValue = a; sl.maximumValue = b;
	NSUserDefaults *u = NSUserDefaults.standardUserDefaults; sl.value = [u objectForKey:key] ? [u floatForKey:key] : d;
	sl.minimumTrackTintColor = VX_G; sl.maximumTrackTintColor = VX_SEL; sl.thumbTintColor = VX_HI; __weak VXControls *ws = self;
	[sl addAction:[UIAction actionWithHandler:^(UIAction *x) { [u setFloat:((UISlider *) x.sender).value forKey:key]; [ws changed]; }] forControlEvents:UIControlEventValueChanged];
	[sl addAction:[UIAction actionWithHandler:^(UIAction *x) { ulog ([NSString stringWithFormat:@"controls: %@ %.2f", key, ((UISlider *) x.sender).value]); }] forControlEvents:UIControlEventTouchUpInside];
	return sl;
}
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
	NSUserDefaults *u = NSUserDefaults.standardUserDefaults; __weak VXControls *ws = self;
	if (ip.section == 0) {
		NSArray<GCController *> *cs = GCController.controllers;
		if ((NSUInteger) ip.row < cs.count) { UITableViewCell *c = [self cell:cs[(NSUInteger) ip.row].vendorName ?: @"Controller" detail:[NSString stringWithFormat:@"player %ld", (long) ip.row + 1]]; c.selectionStyle = UITableViewCellSelectionStyleNone; return c; }
		UITableViewCell *c = [self cell:GCKeyboard.coalescedKeyboard ? @"Keyboard" : @"Nothing connected" detail:GCKeyboard.coalescedKeyboard ? @"the PC keys" : @"touch"]; c.selectionStyle = UITableViewCellSelectionStyleNone; return c; }
	if (ip.section == 1) {
		NSNumber *m = [u objectForKey:[NSString stringWithFormat:@"map.%ld", (long) ip.row + 1]]; int src = m ? m.intValue : VXPhysDef[ip.row];
		UITableViewCell *c = [self cell:[NSString stringWithFormat:@"Button %ld", (long) ip.row + 1] detail:VXPhys[src >= 0 && src < 8 ? src : 0]];
		c.detailTextLabel.textColor = m && m.intValue != VXPhysDef[ip.row] ? VX_CHG : VX_HI; return c; }
	if (ip.section == 2) {
		if (ip.row == 0) { UITableViewCell *c = [self cell:@"Show" detail:nil]; c.selectionStyle = UITableViewCellSelectionStyleNone;
			UISegmentedControl *sg = [[UISegmentedControl alloc] initWithItems:@[ @"Automatic", @"Always", @"Never" ]]; NSString *mo = [u stringForKey:@"touch.show"] ?: @"auto";
			sg.selectedSegmentIndex = [mo isEqualToString:@"always"] ? 1 : [mo isEqualToString:@"never"] ? 2 : 0; sg.selectedSegmentTintColor = VX_SEL;
			[sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_HI2 } forState:UIControlStateSelected]; [sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_FG2 } forState:UIControlStateNormal];
			[sg addAction:[UIAction actionWithHandler:^(UIAction *x) { NSInteger k = ((UISegmentedControl *) x.sender).selectedSegmentIndex;
				[u setObject:k == 1 ? @"always" : k == 2 ? @"never" : @"auto" forKey:@"touch.show"]; ulog ([NSString stringWithFormat:@"controls: touch shown %ld", (long) k]); [ws changed]; }] forControlEvents:UIControlEventValueChanged];
			[sg sizeToFit]; c.accessoryView = sg; return c; }
		if (ip.row == 1) { UITableViewCell *c = [self cell:@"Size" detail:nil]; c.selectionStyle = UITableViewCellSelectionStyleNone; c.accessoryView = [self slider:@"touch.size" min:0.7f max:1.4f def:1.0f]; return c; }
		if (ip.row == 3) { UITableViewCell *c = [self cell:@"Directions" detail:nil]; c.selectionStyle = UITableViewCellSelectionStyleNone;   /* (the stick, or a D-pad: clear turns) */
			UISegmentedControl *sg = [[UISegmentedControl alloc] initWithItems:@[ @"Stick", @"D-pad" ]]; sg.selectedSegmentIndex = [u boolForKey:@"touch.dpad"] ? 1 : 0; sg.selectedSegmentTintColor = VX_SEL;
			[sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_HI2 } forState:UIControlStateSelected]; [sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_FG2 } forState:UIControlStateNormal];
			[sg addAction:[UIAction actionWithHandler:^(UIAction *x) { const BOOL d = ((UISegmentedControl *) x.sender).selectedSegmentIndex == 1; [u setBool:d forKey:@"touch.dpad"];
				ulog ([NSString stringWithFormat:@"controls: %@", d ? @"D-pad" : @"stick"]); [ws changed]; }] forControlEvents:UIControlEventValueChanged];
			[sg sizeToFit]; c.accessoryView = sg; return c; }
		if (ip.row == 2) { UITableViewCell *c = [self cell:@"Opacity" detail:nil]; c.selectionStyle = UITableViewCellSelectionStyleNone; c.accessoryView = [self slider:@"touch.alpha" min:0.2f max:1.0f def:1.0f]; return c; }
		UITableViewCell *c = [self cell:@"Left-handed (the stick on the right)" detail:nil]; c.selectionStyle = UITableViewCellSelectionStyleNone;
		UISwitch *sw = [UISwitch new]; sw.onTintColor = VX_G; sw.on = [u boolForKey:@"touch.lefty"];
		[sw addAction:[UIAction actionWithHandler:^(UIAction *x) { [u setBool:((UISwitch *) x.sender).on forKey:@"touch.lefty"]; ulog (@"controls: left-handed toggled"); [ws changed]; }] forControlEvents:UIControlEventValueChanged];
		c.accessoryView = sw; return c; }
	int k = vxa_ofind ("vectrex_emu_keys"); UITableViewCell *c = [self cell:@"Keyboard as on the PC" detail:nil]; c.selectionStyle = UITableViewCellSelectionStyleNone;
	UISwitch *sw = [UISwitch new]; sw.onTintColor = VX_G; sw.on = k >= 0 && !strcmp (vxa_oval (k), "on"); sw.enabled = k >= 0;
	[sw addAction:[UIAction actionWithHandler:^(UIAction *x) { if (k >= 0) vxa_oset (k, ((UISwitch *) x.sender).on ? "on" : "off"); }] forControlEvents:UIControlEventValueChanged];
	c.accessoryView = sw; return c;
}
- (void) tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)ip
{
	if (ip.section != 1) return;
	NSInteger btn = ip.row; NSUserDefaults *u = NSUserDefaults.standardUserDefaults; __weak VXControls *ws = self;
	UIAlertController *a = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Vectrex button %ld", (long) btn + 1] message:@"On the controller's" preferredStyle:UIAlertControllerStyleActionSheet];
	for (int i = 0; i < 8; i++) [a addAction:[UIAlertAction actionWithTitle:VXPhys[i] style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) { (void) x;
		[u setInteger:i forKey:[NSString stringWithFormat:@"map.%ld", (long) btn + 1]]; ulog ([NSString stringWithFormat:@"controls: button %ld on %@", (long) btn + 1, VXPhys[i]]); [ws.tableView reloadData]; }]];
	[a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
	a.popoverPresentationController.sourceView = [t cellForRowAtIndexPath:ip]; a.view.tintColor = VX_HI; a.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
	[self presentViewController:a animated:YES completion:nil]; [t deselectRowAtIndexPath:ip animated:YES];
}
@end

@implementation VXUI (Controls)
+ (void) showControlsFrom:(UIViewController *)vc
{
	if (vc.presentedViewController) return; ulog (@"controls opened");
	VXControls *c = [[VXControls alloc] initWithStyle:UITableViewStyleInsetGrouped]; c.game = vc;
	VXNav *n = [[VXNav alloc] initWithRootViewController:c]; n.game = vc; [self styleNav:n]; vxa_hold (1);
	__weak UINavigationController *wn = n;
	c.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithPrimaryAction:[UIAction actionWithTitle:@"Done" image:nil identifier:nil handler:^(UIAction *a) { (void) a; [wn dismissViewControllerAnimated:YES completion:nil]; }]];
	n.modalPresentationStyle = UIModalPresentationPageSheet; [vc presentViewController:n animated:YES completion:nil];
}
@end
