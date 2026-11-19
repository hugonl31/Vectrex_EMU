/* VXUI.m - Vectrex_EMU for iPhone and iPad, step 2: the app's own screens in the PC app's colours (the mockup canvas) -
 * the game library (the overlays as tiles), the game menu (a sheet: RESET, eject, overlay, save states, port 2), the
 * settings (the core's own groups and help texts; numbers on one slider over the running game, choices as a list) and
 * the quick bar above the picture. Every tap is logged. Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see
 * LICENSE.md. */
#import "VXApp.h"

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
	UIButton *all = VB (@"", @"list.bullet", NO, ^{ [ws closeTo:YES]; }); all.accessibilityLabel = @"All settings";   /* (the full list, at this setting) */
	[all.widthAnchor constraintEqualToConstant:44].active = YES; [all.heightAnchor constraintEqualToConstant:40].active = YES;
	UIStackView *bot = [[UIStackView alloc] initWithArrangedSubviews:@[ _val, [UIView new], all, def, done ]]; bot.alignment = UIStackViewAlignmentCenter; bot.spacing = 8;
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
- (NSInteger) tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return s ? (NSInteger) _rows[(NSUInteger) s - 1].count : 2; }
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return s ? @(vxa_cdesc (_cats[(NSUInteger) s - 1].intValue)).uppercaseString : @"APP"; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s { return s ? @(vxa_cinfo (_cats[(NSUInteger) s - 1].intValue)) : @"Clock and battery: the time and battery at the top of the screen, beside the camera. Top bar: always in its own row, or over the picture and hiding itself - tap the top of the screen to bring it back (the picture then gets the room)."; }
- (void) tableView:(UITableView *)t willDisplayHeaderView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
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
	NSArray *dirs = [[self look] isEqualToString:@"boxes"] ? @[ @"boxes", @"carts", @"" ] : [[self look] isEqualToString:@"overlays"] ? @[ @"", @"carts", @"boxes" ] : @[ @"carts", @"boxes", @"" ];
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
