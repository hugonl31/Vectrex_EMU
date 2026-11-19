/* VXUI.m - Vectrex_EMU for iPhone and iPad, step 2: the app's own screens in the PC app's colours (the mockup canvas) -
 * the game library (the overlays as tiles), the game menu (a sheet: RESET, eject, overlay, save states, port 2), the
 * settings (the core's own groups and help texts; numbers on one slider over the running game, choices as a list) and
 * the quick bar above the picture. Every tap is logged. Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see
 * LICENSE.md. */
#import "VXApp.h"
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
@implementation VXAdjust { UILabel *_cat, *_name, *_info, *_val; UISlider *_sl; UIStackView *_extra; NSArray<NSString *> *_nums; int _last; }
- (instancetype) initWithOpt:(int)i vc:(UIViewController *)vc
{
	self = [super initWithFrame:CGRectZero]; _vc = vc;
	self.backgroundColor = [VX_BAR colorWithAlphaComponent:0.95]; self.layer.cornerRadius = 22; self.layer.borderWidth = 1; self.layer.borderColor = VX_EDGE2.CGColor;
	_cat = VL (@"", VF (11, UIFontWeightSemibold), VX_DIM); _name = VL (@"", VT (17), VX_HI); _info = VL (@"", VF (12, UIFontWeightRegular), VX_DIM); _val = VL (@"", VT (20), VX_HI);
	_cat.textAlignment = _name.textAlignment = _info.textAlignment = NSTextAlignmentCenter; _info.numberOfLines = 2;
	__weak VXAdjust *ws = self;
	UIButton *prev = VB (@"", @"chevron.left", NO, ^{ [ws step:-1]; }), *next = VB (@"", @"chevron.right", NO, ^{ [ws step:1]; });
	prev.accessibilityLabel = @"Previous setting"; next.accessibilityLabel = @"Next setting";
	UIStackView *mid = [[UIStackView alloc] initWithArrangedSubviews:@[ _cat, _name ]]; mid.axis = UILayoutConstraintAxisVertical; mid.spacing = 2;
	UIStackView *top = [[UIStackView alloc] initWithArrangedSubviews:@[ prev, mid, next ]]; top.alignment = UIStackViewAlignmentCenter; top.spacing = 8;
	[prev.widthAnchor constraintEqualToConstant:44].active = YES; [next.widthAnchor constraintEqualToConstant:44].active = YES;
	[prev.heightAnchor constraintEqualToConstant:44].active = YES; [next.heightAnchor constraintEqualToConstant:44].active = YES;
	_sl = [UISlider new]; _sl.minimumTrackTintColor = VX_G; _sl.maximumTrackTintColor = VX_SEL; _sl.thumbTintColor = VX_HI;
	[_sl addTarget:self action:@selector(slid) forControlEvents:UIControlEventValueChanged];
	_extra = [UIStackView new]; _extra.spacing = 8; _extra.distribution = UIStackViewDistributionFillEqually;
	UIButton *def = VB (@"Default", nil, NO, ^{ [ws setValue:@(vxa_odef (ws.opt))]; }), *done = VB (@"Done", nil, YES, ^{ [ws close]; });
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
- (void) show:(int)i
{
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
	_sl.alpha = k == NSNotFound ? 0.4 : 1;
	_val.text = v; _val.textColor = strcmp (vxa_oval (_opt), vxa_odef (_opt)) ? VX_CHG : VX_HI;
}
- (void) slid
{
	int k = (int) lroundf (_sl.value); if (k < 0 || k >= (int) _nums.count) return;
	if (k != _last) { _last = k; [[UISelectionFeedbackGenerator new] selectionChanged]; vxa_oset (_opt, _nums[k].UTF8String); [self refresh]; [self show:_opt]; }
}
- (void) setValue:(NSString *)v { vxa_oset (_opt, v.UTF8String); [self show:_opt]; }
- (void) step:(int)d                                 /* (the next setting with a slider in the same group) */
{
	int n = vxa_nopts ();
	for (int k = 1; k < n; k++) { int j = ((_opt + d * k) % n + n) % n; if (!strcmp (vxa_ocat (j), vxa_ocat (_opt)) && VSlider (j, NULL, NULL)) { [self show:j]; return; } }
}
/* (Done: back where it was opened from - the settings list, at this setting; or the game, from the quick bar) */
- (void) close { [self closeTo:NO]; }   /* (Done: straight back to the game; the list button: all settings) */
- (void) closeTo:(BOOL)list
{
	ulog (list ? @"adjust done: back to the settings" : @"adjust done"); UIViewController *g = self.vc; int i = self.opt;
	[UIView animateWithDuration:0.2 animations:^{ self.alpha = 0; } completion:^(BOOL f) { (void) f; [self removeFromSuperview]; if (list && g) [VXUI showSettingsFrom:g at:i]; }];
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
	 *  usual ones on top; settings that mean nothing here left out - the screen's shape, RetroArch's ROM browser button) */
	NSArray *order = @[ @"video", @"overlay", @"case", @"tube", @"controls", @"audio", @"system", @"unit", @"circuit" ];
	NSArray *top = @[ @"vectrex_emu_profile", @"vectrex_emu_deck", @"vectrex_emu_rotate", @"vectrex_emu_rate", @"vectrex_emu_size",
	                  @"vectrex_emu_overlay", @"vectrex_emu_ov_glow", @"vectrex_emu_scratches", @"vectrex_emu_room", @"vectrex_emu_ceiling",
	                  @"vectrex_emu_glow", @"vectrex_emu_afterglow", @"vectrex_emu_brightness", @"vectrex_emu_keys", @"vectrex_emu_port2" ];
	NSSet *hide = [NSSet setWithArray:@[ @"vectrex_emu_shape", @"vectrex_emu_btn_browser" ]];
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
- (NSInteger) tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return s ? (NSInteger) _rows[(NSUInteger) s - 1].count : 5; }
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return s ? @(vxa_cdesc (_cats[(NSUInteger) s - 1].intValue)).uppercaseString : @"APP"; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s { return s ? @(vxa_cinfo (_cats[(NSUInteger) s - 1].intValue)) : @"Clock and battery: the time and battery at the top of the screen, beside the camera. Top bar: always in its own row, or over the picture and hiding itself - tap the top of the screen to bring it back (the picture then gets the room)."; }
- (void) tableView:(UITableView *)t willDisplayHeaderView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
	if (!ip.section && ip.row == 4) {               /* (the controls: controllers, buttons, touch, keyboard) */
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
		c.textLabel.text = @"Controls"; c.textLabel.textColor = VX_FG; c.detailTextLabel.text = @"controllers, touch, keyboard"; c.detailTextLabel.textColor = VX_DIM;
		c.backgroundColor = VX_PANEL; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator; UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c; }
	if (!ip.section && ip.row == 3) {               /* (HDR: the brightest dots above the screen's white) */
		extern float vxa_hdr_headroom (void); extern void vxa_hdr_update (void); float hr = vxa_hdr_headroom ();
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
		c.textLabel.text = @"HDR brightness"; c.textLabel.textColor = VX_FG; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone;
		c.detailTextLabel.textColor = VX_DIM; c.detailTextLabel.numberOfLines = 2;
		if (hr <= 1.05) { c.detailTextLabel.text = @"Not on this screen (it shows no HDR)"; return c; }
		NSNumber *f = [NSUserDefaults.standardUserDefaults objectForKey:@"app.hdr"]; double fr = f ? f.doubleValue : 1.0;
		c.detailTextLabel.text = fr > 0.01 ? [NSString stringWithFormat:@"The brightest dots up to %.1fx white", 1 + (hr - 1) * fr] : @"Off: as an ordinary screen";
		UISlider *sl = [[UISlider alloc] initWithFrame:CGRectMake (0, 0, 130, 30)]; sl.value = (float) fr; sl.minimumTrackTintColor = VX_G; sl.maximumTrackTintColor = VX_SEL; sl.thumbTintColor = VX_HI;
		__weak UITableViewCell *wc = c;
		[sl addAction:[UIAction actionWithHandler:^(UIAction *a) { double v = ((UISlider *) a.sender).value; [NSUserDefaults.standardUserDefaults setObject:@(v) forKey:@"app.hdr"]; vxa_hdr_update ();
			wc.detailTextLabel.text = v > 0.01 ? [NSString stringWithFormat:@"The brightest dots up to %.1fx white", 1 + (hr - 1) * v] : @"Off: as an ordinary screen"; }] forControlEvents:UIControlEventValueChanged];
		[sl addAction:[UIAction actionWithHandler:^(UIAction *a) { ulog ([NSString stringWithFormat:@"HDR %.0f %%", ((UISlider *) a.sender).value * 100]); }] forControlEvents:UIControlEventTouchUpInside];
		c.accessoryView = sl; return c; }
	if (!ip.section && ip.row == 2) {               /* (the downloads: which parts are here, add or remove) */
		UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
		c.textLabel.text = @"Downloads"; c.textLabel.textColor = VX_FG; c.detailTextLabel.text = @"games, boxes, UVMC2, PiTrex"; c.detailTextLabel.textColor = VX_DIM;
		c.backgroundColor = VX_PANEL; c.accessoryType = UITableViewCellAccessoryDisclosureIndicator; UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb; return c; }
	if (!ip.section && ip.row == 1) {               /* (the quick bar: its own row, or hiding itself over the picture) */
		UITableViewCell *c = [t dequeueReusableCellWithIdentifier:@"bar"] ?: [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"bar"];
		c.textLabel.text = @"Top bar"; c.textLabel.textColor = VX_FG; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone;
		UISegmentedControl *sg = [[UISegmentedControl alloc] initWithItems:@[ @"Always", @"Auto-hide" ]];
		NSString *m = [NSUserDefaults.standardUserDefaults stringForKey:@"app.topbar"]; BOOL au = m ? [m isEqualToString:@"auto"] : UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
		sg.selectedSegmentIndex = au ? 1 : 0; sg.selectedSegmentTintColor = VX_SEL; [sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_HI2 } forState:UIControlStateSelected];
		[sg setTitleTextAttributes:@{ NSForegroundColorAttributeName: VX_FG2 } forState:UIControlStateNormal]; __weak VXSettings *ws = self;
		[sg addAction:[UIAction actionWithHandler:^(UIAction *a) { BOOL on = ((UISegmentedControl *) a.sender).selectedSegmentIndex == 1;
			[NSUserDefaults.standardUserDefaults setObject:on ? @"auto" : @"always" forKey:@"app.topbar"]; ulog ([NSString stringWithFormat:@"top bar %@", on ? @"auto-hide" : @"always"]);
			if ([ws.game respondsToSelector:@selector(barModeChanged)]) [ws.game performSelector:@selector(barModeChanged)]; }] forControlEvents:UIControlEventValueChanged];
		[sg sizeToFit]; c.accessoryView = sg; return c; }
	if (!ip.section) {                              /* (the clock and battery: iOS's own status bar) */
		UITableViewCell *c = [t dequeueReusableCellWithIdentifier:@"app"] ?: [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"app"];
		c.textLabel.text = @"Clock and battery"; c.textLabel.textColor = VX_FG; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone;
		UISwitch *sw = [UISwitch new]; sw.onTintColor = VX_G; sw.on = [NSUserDefaults.standardUserDefaults boolForKey:@"app.statusbar"]; __weak VXSettings *ws = self;
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
	if (!ip.section && ip.row == 4) { UIViewController *g = self.game; [self dismissViewControllerAnimated:YES completion:^{ [VXUI showControlsFrom:g]; }]; return; }
	if (!ip.section && ip.row == 2) { UIViewController *g = self.game; ulog (@"settings: downloads");
		[self dismissViewControllerAnimated:YES completion:^{ if ([g respondsToSelector:@selector(openDownloads)]) [g performSelector:@selector(openDownloads)]; }]; return; }
	if (!ip.section) return;
	int i = _rows[(NSUInteger) ip.section - 1][(NSUInteger) ip.row].intValue; ulog ([NSString stringWithFormat:@"setting opened %s", vxa_okey (i)]);
	if (VSlider (i, NULL, NULL)) {                  /* (a number: the slider over the running game, the settings out of the way) */
		UIViewController *g = self.game; [self dismissViewControllerAnimated:YES completion:^{ [VXUI adjustOption:i over:g fromSettings:YES]; }]; return; }
	VXChoice *ch = [[VXChoice alloc] initWithStyle:UITableViewStyleInsetGrouped]; ch.opt = i; [self.navigationController pushViewController:ch animated:YES];
}
@end

/* ------------------------------------------------ the game library */
@interface VXGameCell : UICollectionViewCell
@property (nonatomic, strong) UIImageView *img; @property (nonatomic, strong) UILabel *name, *ph;
@end
@implementation VXGameCell
- (instancetype) initWithFrame:(CGRect)f
{
	self = [super initWithFrame:f];
	_img = [UIImageView new]; _img.contentMode = UIViewContentModeScaleAspectFit; _img.clipsToBounds = YES; _img.layer.cornerRadius = 8;   /* (a cartridge: its own clear outline) */
	_ph = VL (@"", VT (15), VX_G); _ph.textAlignment = NSTextAlignmentCenter;
	_name = VL (@"", VF (12, UIFontWeightMedium), VX_FG); _name.numberOfLines = 2;
	[self.contentView addSubview:_img]; [_img addSubview:_ph]; [self.contentView addSubview:_name]; return self;
}
- (void) layoutSubviews
{
	[super layoutSubviews]; CGFloat w = self.bounds.size.width, h = self.bounds.size.height - 38;
	_img.frame = CGRectMake (0, 0, w, h); _ph.frame = CGRectInset (_img.bounds, 6, 6); _name.frame = CGRectMake (0, h + 4, w, 32);
}
@end

@interface VXLibrary : UIViewController <UICollectionViewDataSource, UICollectionViewDelegateFlowLayout, UITextFieldDelegate>
@end
@implementation VXLibrary { UICollectionView *_cv; UITextField *_search; UIStackView *_chips; NSArray<NSDictionary *> *_all, *_shown; NSString *_filter; UISegmentedControl *_look; }
/* (the tiles' look, the player's choice: Cartridges - the real cartridge seen from above with its label -, Boxes, Overlays;
 *  a game without one in that look takes the next: cartridge, box, overlay; Last Human (UVMC2) takes Last Human's) */
+ (NSString *) look { return [NSUserDefaults.standardUserDefaults stringForKey:@"lib.look"] ?: @"boxes"; }   /* (the default: the boxes - the owner, 2026-11-19) */
+ (CGFloat) aspect { NSString *l = [self look]; return [l isEqualToString:@"carts"] ? 0.96 : [l isEqualToString:@"boxes"] ? 1.39 : 207.0 / 162; }
+ (UIImage *) tileFor:(NSString *)n
{
	NSString *th = [vxa_docs () stringByAppendingPathComponent:@"system/vectrex_emu/thumbs"], *base = [n componentsSeparatedByString:@" ("].firstObject;
	NSArray *dirs = [[self look] isEqualToString:@"boxes"] ? @[ @"boxes", @"carts", @"overlays", @"" ] : [[self look] isEqualToString:@"overlays"] ? @[ @"overlays", @"", @"carts", @"boxes" ] : @[ @"carts", @"boxes", @"overlays", @"" ];
	for (NSString *d in dirs) for (NSString *nm in @[ n, base ]) {
		UIImage *im = [UIImage imageWithContentsOfFile:[[th stringByAppendingPathComponent:d] stringByAppendingPathComponent:[nm stringByAppendingPathExtension:@"png"]]]; if (im) return im; }
	return nil;
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
	[a sortUsingComparator:^NSComparisonResult (NSDictionary *x, NSDictionary *y) { return [x[@"name"] localizedCaseInsensitiveCompare:y[@"name"]]; }];
	_all = a; ulog ([NSString stringWithFormat:@"library: %lu games", (unsigned long) a.count]);
}
- (void) applyFilter
{
	NSString *q = _search.text.lowercaseString; NSArray *rec = [NSUserDefaults.standardUserDefaults arrayForKey:@"recent"] ?: @[];
	NSMutableArray *r = [NSMutableArray array];
	if ([_filter isEqualToString:@"Recent"]) { for (NSString *p in rec) for (NSDictionary *g in _all) if ([g[@"path"] isEqualToString:p]) [r addObject:g]; }
	else for (NSDictionary *g in _all) if (([_filter isEqualToString:@"All"] && ![g[@"kind"] isEqualToString:@"Tests"]) || [g[@"kind"] isEqualToString:_filter]) [r addObject:g];
	if (q.length) [r filterUsingPredicate:[NSPredicate predicateWithBlock:^BOOL (NSDictionary *g, NSDictionary *b) { (void) b; return [[g[@"name"] lowercaseString] containsString:q]; }]];
	_shown = r; [_cv reloadData];
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
	UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[ title, [UIView new], close ]]; row.alignment = UIStackViewAlignmentCenter;
	UIStackView *hs = [[UIStackView alloc] initWithArrangedSubviews:@[ row, _look, _search, cs ]]; hs.axis = UILayoutConstraintAxisVertical; hs.spacing = 10; hs.translatesAutoresizingMaskIntoConstraints = NO;
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
	[self applyFilter];
}
- (void) pick:(NSString *)f { _filter = f; [self applyFilter]; }
- (void) reload { [_cv.collectionViewLayout invalidateLayout]; [_cv reloadData]; }
- (void) viewDidDisappear:(BOOL)a { [super viewDidDisappear:a]; vxa_hold (0); }
- (BOOL) textFieldShouldReturn:(UITextField *)t { [t resignFirstResponder]; return YES; }
- (CGSize) collectionView:(UICollectionView *)cv layout:(UICollectionViewLayout *)l sizeForItemAtIndexPath:(NSIndexPath *)ip
{
	CGFloat w = cv.bounds.size.width - 32; BOOL carts = [[VXLibrary look] isEqualToString:@"carts"];
	int cols = carts ? (w > 1000 ? 5 : w > 700 ? 4 : 2) : (w > 1000 ? 7 : w > 700 ? 5 : 3); CGFloat iw = floor ((w - 12 * (cols - 1)) / cols);
	return CGSizeMake (iw, iw * [VXLibrary aspect] + 38);
}
- (NSInteger) collectionView:(UICollectionView *)cv numberOfItemsInSection:(NSInteger)s { return (NSInteger) _shown.count; }
- (UICollectionViewCell *) collectionView:(UICollectionView *)cv cellForItemAtIndexPath:(NSIndexPath *)ip
{
	VXGameCell *c = [cv dequeueReusableCellWithReuseIdentifier:@"g" forIndexPath:ip]; NSDictionary *g = _shown[(NSUInteger) ip.item]; NSString *n = g[@"name"];
	UIImage *im = [VXLibrary tileFor:n];
	c.img.image = im; c.ph.text = im ? @"" : n.uppercaseString; c.name.text = n;
	c.img.backgroundColor = im ? UIColor.clearColor : VX_PANEL;
	BOOL now = [g[@"path"] isEqualToString:vxa_game ()]; c.name.textColor = now ? VX_HI : VX_FG; c.name.font = VF (12, now ? UIFontWeightBold : UIFontWeightMedium);
	c.img.layer.borderColor = (now ? VX_HI : VX_EDGE).CGColor; c.img.layer.borderWidth = im ? (now ? 2 : 0) : 1;
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
		return [UIMenu menuWithTitle:g[@"name"] children:@[ [UIAction actionWithTitle:@"Play" image:[UIImage systemImageNamed:@"play.fill"] identifier:nil handler:^(UIAction *a) { (void) a;
			vxa_load (g[@"path"]); [ws dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); }]; }] ]]; }];
}
@end

/* ------------------------------------------------ the game menu: a sheet from below, the game paused above it */
@interface VXGameMenu : UIViewController <UIAdaptivePresentationControllerDelegate>
@property (nonatomic, weak) UIViewController *game;
@end
@implementation VXGameMenu
- (void) presentationControllerDidDismiss:(UIPresentationController *)p { ulog (@"game menu swiped away"); vxa_hold (0); }
- (void) viewDidDisappear:(BOOL)a { [super viewDidDisappear:a]; if (!self.presentedViewController) vxa_hold (0); }   /* (closed in any way: the game goes on) */
- (void) close:(void (^) (void))then { [self dismissViewControllerAnimated:YES completion:^{ vxa_hold (0); if (then) then (); }]; }
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
	BOOL can = vxa_state_supported () != 0;
	for (int s = 1; s <= 3; s++) {
		UIButton *sv = VB ([NSString stringWithFormat:@"Save %d", s], @"square.and.arrow.down", NO, ^{ int ok = vxa_state (1, s); UIViewController *gm = ws.game; [ws close:^{ VToast (gm, ok ? [NSString stringWithFormat:@"Saved in slot %d", s] : @"Not saved"); }]; });
		UIButton *ld = VB ([NSString stringWithFormat:@"Load %d", s], @"square.and.arrow.up", NO, ^{ int ok = vxa_state (0, s); UIViewController *gm = ws.game; [ws close:^{ VToast (gm, ok ? [NSString stringWithFormat:@"Slot %d loaded", s] : @"Nothing loaded"); }]; });
		ld.enabled = can && vxa_state_exists (s); sv.enabled = can;
		UIStackView *col = [[UIStackView alloc] initWithArrangedSubviews:@[ sv, ld ]]; col.axis = UILayoutConstraintAxisVertical; col.spacing = 6; col.distribution = UIStackViewDistributionFillEqually;
		[col.heightAnchor constraintEqualToConstant:116].active = YES; [slots addArrangedSubview:col]; }
	UILabel *stn = VL (can ? @"" : @"This game has no save states (UVMC2 and PiTrex games keep their own).", VF (12, UIFontWeightRegular), VX_DIM); stn.hidden = can;
	int ov = vxa_ofind ("vectrex_emu_overlay"); BOOL ovOn = ov >= 0 && !strcmp (vxa_oval (ov), "on");
	UIStackView *acts = [[UIStackView alloc] initWithArrangedSubviews:@[
		VB (@"Games", @"square.grid.2x2", NO, ^{ UIViewController *gm = ws.game; [ws dismissViewControllerAnimated:YES completion:^{ [VXUI showLibraryFrom:gm]; }]; }),
		VB (@"RESET", @"arrow.counterclockwise", NO, ^{ vxa_do (@"reset"); [ws close:nil]; }),
		VB (ovOn ? @"Overlay on" : @"Overlay off", @"rectangle.on.rectangle", ovOn, ^{ vxa_do (@"overlay"); [ws close:nil]; }),
		VB (@"Room light", @"lightbulb", NO, ^{ UIViewController *gm = ws.game; [ws close:^{ [VXUI adjustOption:vxa_ofind ("vectrex_emu_room") over:gm]; }]; }) ]];
	acts.distribution = UIStackViewDistributionFillEqually; acts.spacing = 8; [acts.heightAnchor constraintEqualToConstant:64].active = YES;
	UIButton *allb = VB (@"All settings", @"slider.horizontal.3", NO, ^{ UIViewController *gm = ws.game; [ws dismissViewControllerAnimated:YES completion:^{ [VXUI showSettingsFrom:gm]; }]; });
	[allb.heightAnchor constraintEqualToConstant:56].active = YES;
	UILabel *hw = VL (@"VECTREX HARDWARE", VF (11, UIFontWeightSemibold), VX_DIM);
	int p2 = vxa_ofind ("vectrex_emu_port2");
	UIButton *ej = VB (@"Eject / insert cartridge", nil, NO, ^{ vxa_do (@"eject"); [ws close:nil]; });
	UIButton *pb = VB (p2 >= 0 ? [NSString stringWithFormat:@"Port 2: %s", vxa_oval (p2)] : @"Port 2", nil, NO, ^{
		if (p2 < 0) return; VXChoice *ch = [[VXChoice alloc] initWithStyle:UITableViewStyleInsetGrouped]; ch.opt = p2;
		UINavigationController *n = [[UINavigationController alloc] initWithRootViewController:ch]; [VXUI styleNav:n]; [ws presentViewController:n animated:YES completion:nil]; });
	for (UIButton *b in @[ ej, pb ]) { UIButtonConfiguration *c = b.configuration; c.titleTextAttributesTransformer = ^NSDictionary *(NSDictionary *a) { NSMutableDictionary *m = [a mutableCopy]; m[NSFontAttributeName] = VF (13, UIFontWeightRegular); return m; };
		c.baseForegroundColor = VX_FG2; b.configuration = c; [b.heightAnchor constraintEqualToConstant:40].active = YES; }
	UIStackView *hwr = [[UIStackView alloc] initWithArrangedSubviews:@[ ej, pb ]]; hwr.distribution = UIStackViewDistributionFillEqually; hwr.spacing = 8;
	UIStackView *all = [[UIStackView alloc] initWithArrangedSubviews:@[ head, st, slots, stn, acts, allb, hw, hwr ]]; all.axis = UILayoutConstraintAxisVertical; all.spacing = 14;
	[all setCustomSpacing:24 afterView:allb];
	UIScrollView *sc = [UIScrollView new]; sc.translatesAutoresizingMaskIntoConstraints = NO; all.translatesAutoresizingMaskIntoConstraints = NO; [sc addSubview:all]; [self.view addSubview:sc];
	[NSLayoutConstraint activateConstraints:@[ [sc.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:24], [sc.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
		[sc.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor], [sc.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
		[all.topAnchor constraintEqualToAnchor:sc.contentLayoutGuide.topAnchor], [all.bottomAnchor constraintEqualToAnchor:sc.contentLayoutGuide.bottomAnchor constant:-24],
		[all.leadingAnchor constraintEqualToAnchor:sc.frameLayoutGuide.leadingAnchor constant:16], [all.trailingAnchor constraintEqualToAnchor:sc.frameLayoutGuide.trailingAnchor constant:-16] ]];
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
	s.detents = @[ UISheetPresentationControllerDetent.mediumDetent, UISheetPresentationControllerDetent.largeDetent ]; s.prefersGrabberVisible = YES; s.preferredCornerRadius = 22;
	m.presentationController.delegate = m; [vc presentViewController:m animated:YES completion:nil];
}
+ (void) showSettingsFrom:(UIViewController *)vc { [self showSettingsFrom:vc at:-1]; }
+ (void) showSettingsFrom:(UIViewController *)vc at:(int)focus
{
	if ([self busy:vc]) return; ulog (@"settings opened");
	VXSettings *s = [[VXSettings alloc] initWithStyle:UITableViewStyleInsetGrouped]; s.game = vc; s.focus = focus;
	UINavigationController *n = [[UINavigationController alloc] initWithRootViewController:s]; [self styleNav:n];
	n.modalPresentationStyle = UIModalPresentationPageSheet; n.sheetPresentationController.prefersGrabberVisible = YES;
	[vc presentViewController:n animated:YES completion:nil];
}
+ (void) adjustOption:(int)i over:(UIViewController *)vc { [self adjustOption:i over:vc fromSettings:NO]; }
+ (void) adjustOption:(int)i over:(UIViewController *)vc fromSettings:(BOOL)fs
{
	if (i < 0) return; vxa_hold (0);
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
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return self.first ? @"CHOOSE WHAT TO DOWNLOAD - ONCE" : @"WHAT IS ON THIS DEVICE"; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s { return @"Each part comes once; later only a part that changed comes again. Switch a part off to remove it and get its space back."; }
- (void) tableView:(UITableView *)t willDisplayHeaderView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
	NSDictionary *p = self.parts[(NSUInteger) ip.row]; NSString *pid = p[@"id"], *v = self.inst[pid]; BOOL req = [p[@"required"] boolValue];
	UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
	c.textLabel.text = p[@"name"]; c.textLabel.textColor = VX_FG; c.textLabel.numberOfLines = 2; c.backgroundColor = VX_PANEL; c.selectionStyle = UITableViewCellSelectionStyleNone;
	NSString *st = !v ? @"not here" : [v isEqualToString:p[@"version"]] ? @"on this device" : @"a newer version";
	c.detailTextLabel.text = [NSString stringWithFormat:@"%.0f MB  -  %@%@", [p[@"size"] doubleValue] / 1e6, st, req ? @"  -  always" : @""];
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
	_cap.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake (c.x - r * 0.52, c.y - r * 0.52, r * 1.04, r * 1.04)].CGPath; [self draw];
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

@interface VXDeck : UIView
@property (nonatomic, weak) UIViewController *game;
@end
@implementation VXDeck { UILabel *_fps, *_hz, *_emu, *_vol; UIView *_cpu, *_gpu, *_cpuF, *_gpuF; NSLayoutConstraint *_cpuW, *_gpuW; VXKnob *_knob; NSTimer *_tm; }
- (UIView *) bar:(NSLayoutConstraint *__strong *)w fill:(UIView *__strong *)fo
{
	UIView *tr = [UIView new]; tr.backgroundColor = VXC (0x16302a); tr.layer.cornerRadius = 2;
	UIView *f = [UIView new]; f.backgroundColor = VX_G; f.layer.cornerRadius = 2; f.translatesAutoresizingMaskIntoConstraints = NO; [tr addSubview:f];
	*w = [f.widthAnchor constraintEqualToAnchor:tr.widthAnchor multiplier:0.01]; *fo = f;
	[NSLayoutConstraint activateConstraints:@[ [f.leadingAnchor constraintEqualToAnchor:tr.leadingAnchor], [f.topAnchor constraintEqualToAnchor:tr.topAnchor], [f.bottomAnchor constraintEqualToAnchor:tr.bottomAnchor], *w ]];
	[tr.heightAnchor constraintEqualToConstant:4].active = YES; return tr;
}
- (instancetype) initWithGame:(UIViewController *)g
{
	self = [super initWithFrame:CGRectZero]; _game = g;
	UIFont *mono = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightMedium], *small = VF (9, UIFontWeightSemibold);
	_fps = VL (@"--", mono, VX_HI); _hz = VL (@"--", mono, VX_HI); _emu = VL (@"--", mono, VX_HI);
	UILabel *l1 = VL (@"FPS", small, VX_DIM), *l2 = VL (@"HZ", small, VX_DIM), *l3 = VL (@"EMU", small, VX_DIM), *lc = VL (@"CPU", small, VX_DIM), *lg = VL (@"GPU", small, VX_DIM);
	UIStackView *r1 = [[UIStackView alloc] initWithArrangedSubviews:@[ l1, _fps, [UIView new], l2, _hz ]]; r1.spacing = 4; r1.alignment = UIStackViewAlignmentFirstBaseline;
	UIStackView *r2 = [[UIStackView alloc] initWithArrangedSubviews:@[ l3, _emu, [UIView new] ]]; r2.spacing = 4; r2.alignment = UIStackViewAlignmentFirstBaseline;
	NSLayoutConstraint *cw, *gw; UIView *cf, *gf; UIView *cb = [self bar:&cw fill:&cf], *gb = [self bar:&gw fill:&gf]; _cpuW = cw; _gpuW = gw; _cpu = cb; _gpu = gb; _cpuF = cf; _gpuF = gf;
	UIStackView *r3 = [[UIStackView alloc] initWithArrangedSubviews:@[ lc, cb, lg, gb ]]; r3.spacing = 4; r3.alignment = UIStackViewAlignmentCenter;
	[cb.widthAnchor constraintEqualToAnchor:gb.widthAnchor].active = YES;
	UIStackView *osd = [[UIStackView alloc] initWithArrangedSubviews:@[ r1, r2, r3 ]]; osd.axis = UILayoutConstraintAxisVertical; osd.spacing = 3;
	osd.layoutMargins = UIEdgeInsetsMake (6, 8, 6, 8); osd.layoutMarginsRelativeArrangement = YES; osd.backgroundColor = VX_PANEL; osd.layer.cornerRadius = 12; osd.layer.borderWidth = 1; osd.layer.borderColor = VX_EDGE.CGColor;
	__weak VXDeck *ws = self;
	UIButton *p2 = VB (@"P2 PORT", @"gamecontroller", NO, ^{ int i = vxa_ofind ("vectrex_emu_port2"); if (i >= 0 && ws.game) [VXUI showChoice:i from:ws.game]; });
	UIButton *cart = VB (@"CART", @"square.grid.2x2", NO, ^{ if (ws.game) [VXUI showLibraryFrom:ws.game]; });
	UIButton *rst = VB (@"RESET", @"arrow.counterclockwise", NO, ^{ vxa_do (@"reset"); [[UIImpactFeedbackGenerator new] impactOccurred]; });
	_knob = [VXKnob new]; [_knob addTarget:self action:@selector(knobbed) forControlEvents:UIControlEventValueChanged];
	_vol = VL (@"VOL", VF (10, UIFontWeightSemibold), VX_HI); _vol.textAlignment = NSTextAlignmentCenter;
	UIStackView *kn = [[UIStackView alloc] initWithArrangedSubviews:@[ _knob, _vol ]]; kn.axis = UILayoutConstraintAxisVertical; kn.spacing = 0; kn.alignment = UIStackViewAlignmentCenter;
	kn.backgroundColor = VX_PANEL; kn.layer.cornerRadius = 12; kn.layer.borderWidth = 1; kn.layer.borderColor = VX_EDGE.CGColor; kn.layoutMargins = UIEdgeInsetsMake (4, 4, 4, 4); kn.layoutMarginsRelativeArrangement = YES;
	[_knob.widthAnchor constraintEqualToConstant:40].active = YES; [_knob.heightAnchor constraintEqualToConstant:40].active = YES;
	UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[ osd, p2, cart, rst, kn ]]; row.spacing = 6; row.distribution = UIStackViewDistributionFill;
	[osd.widthAnchor constraintEqualToAnchor:row.widthAnchor multiplier:0.34].active = YES;
	for (UIView *v in @[ p2, cart, rst ]) [v.widthAnchor constraintEqualToAnchor:kn.widthAnchor multiplier:0.95].active = YES;
	row.translatesAutoresizingMaskIntoConstraints = NO; [self addSubview:row];
	[NSLayoutConstraint activateConstraints:@[ [row.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8], [row.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
		[row.topAnchor constraintEqualToAnchor:self.topAnchor constant:6], [row.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-6] ]];
	_tm = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *t) { (void) t; [ws tick]; }];
	return self;
}
- (void) dealloc { [_tm invalidate]; }
- (void) knobbed { _vol.text = _knob.value > 0 ? [NSString stringWithFormat:@"%.0f%%", _knob.value * 100] : @"OFF"; }
- (void) tick
{
	if (self.hidden || !self.window) return;
	char f[24], h[24], e[24]; int c = -1, g = -1; vx_app_osd (f, h, e, &c, &g);
	_fps.text = [@(f) stringByReplacingOccurrencesOfString:@" FPS" withString:@""]; _hz.text = [@(h) stringByReplacingOccurrencesOfString:@" HZ" withString:@""]; _emu.text = @(e);
	_cpuW.active = NO; _cpuW = [_cpuF.widthAnchor constraintEqualToAnchor:_cpu.widthAnchor multiplier:MAX (0.01, MIN (1.0, c / 100.0))]; _cpuW.active = YES;
	_gpuW.active = NO; _gpuW = [_gpuF.widthAnchor constraintEqualToAnchor:_gpu.widthAnchor multiplier:MAX (0.01, MIN (1.0, g / 100.0))]; _gpuW.active = YES;
	if (!_knob.tracking) { _knob.value = vx_app_volume (); [self knobbed]; }
}
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
	return s == 1 ? 4 : s == 2 ? 4 : 1;
}
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return @[ @"CONNECTED", @"VECTREX BUTTONS ON THE CONTROLLER", @"TOUCH CONTROLS", @"KEYBOARD" ][(NSUInteger) s]; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s
{
	return @[ @"Player 1 is the first controller, player 2 the second.",
		@"Tap a button to choose. The Vectrex controller's four buttons in a row; on a modern pad's diamond by default 1 left, 2 top, 3 bottom, 4 right.",
		@"Automatic: shown only while no controller or keyboard is connected. The stick appears where the thumb lands.",
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
	UINavigationController *n = [[UINavigationController alloc] initWithRootViewController:c]; [self styleNav:n];
	__weak UINavigationController *wn = n;
	c.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithPrimaryAction:[UIAction actionWithTitle:@"Done" image:nil identifier:nil handler:^(UIAction *a) { (void) a; [wn dismissViewControllerAnimated:YES completion:nil]; }]];
	n.modalPresentationStyle = UIModalPresentationPageSheet; [vc presentViewController:n animated:YES completion:nil];
}
@end
