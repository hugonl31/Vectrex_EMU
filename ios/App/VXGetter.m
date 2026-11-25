/* VXGetter.m - Get games & extras, as the Android app's (Getter.java) and the PC's (src/frontend/getter.py): the same
 * items, sources and checks - the original GCE games, the free homebrew games that have an overlay, the PiTrex's SD
 * card files, the speech ROM, the full box art set. Each item has a chain of public sources tried in turn - the
 * project's own copies (Google Drive, archive.org), the other archive.org sets, GitHub, the authors' pages, the Wayback
 * Machine -; a download is checked before anything of it is unpacked (the project's copies by their exact bytes, a set
 * of games by the games' fingerprints, the PiTrex an SD card) and named as the emulator lists it. Files already there
 * are kept. The user downloads them and takes the responsibility (the notice, agreed to once). Add files...: the
 * user's own (a game, a zip of games, a UVMC2 game, the speech ROM, a PiTrex SD card zip), from Files.
 * The owner, 2026-11-25: the phone apps identical - the download page and its order (downloads first, the device test
 * after them) the same on Android and iOS.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <CommonCrypto/CommonDigest.h>
#include <zlib.h>
#import "VXApp.h"

static NSString *const VXG_DISCLAIMER = @"These files are not part of Vectrex_EMU and do not come with it. The maintainer of Vectrex_EMU on GitHub "
	"does not take responsibility for the sharing of these files. You, the user, download them yourself from "
	"the sites named, and you take the responsibility for doing so and for respecting their copyrights, which "
	"belong to their owners. (The original Vectrex games are free for non-commercial use by Smith Engineering's "
	"permission; the homebrew games, the overlays, the PiTrex files and the speech ROM belong to their authors.)";
static NSString *const VXG_WHAT[][2] = {
	{ @"GCE Vectrex games", @"the project's copy (Google Drive, archive.org), archive.org" },
	{ @"Free homebrew games", @"the project's copy (Google Drive, archive.org), archive.org, GitHub (Malban, MiSTer)" },
	{ @"PiTrex SD card files", @"Malban's site, the project's copy (archive.org, Google Drive), the Wayback Machine" },
	{ @"Speech ROM (al2.bin)", @"the project's copy (Google Drive, archive.org), Joe Zbiciak's site, the Wayback Machine" },
	{ @"Box art and labels (full set)", @"this project's GitHub release (its own remade artwork)" } };

/* ------------------------------------------------------------ the games' names (getter.py) */
static NSString *const VXG_GCE[] = { @"3D Crazy Coaster", @"3D Mine Storm", @"3D Narrow Escape", @"Armor Attack", @"Bedlam", @"Berzerk",
	@"Blitz! Action Football", @"Clean Sweep", @"Cosmic Chasm", @"Dark Tower (Proto)", @"Fortress of Narzod",
	@"Heads-Up Action Soccer", @"HyperChase Auto Race", @"Lunar Lander (Proto)", @"Mail Plane (Proto)", @"Mine Storm",
	@"Mine Storm II", @"Mine Storm III (Proto)", @"Mr. Boston Clean Sweep", @"Pitcher's Duel (Proto)", @"Polar Rescue",
	@"Pole Position", @"Rip Off", @"Rocket Sledge (Proto)", @"Scramble", @"Solar Quest", @"Space Wars", @"Spike",
	@"Spinball", @"Star Castle", @"Star Ship", @"Star Trek - The Motion Picture", @"StarHawk", @"Tour De France (Proto)",
	@"Web Wars", @"ZZAP (Proto)" };
static NSString *const VXG_HB[] = { @"Moon Lander", @"Nebula Commander", @"Omega Chase", @"Patriots", @"Spike Hoppin'", @"Thrust", @"Vectrexians",
	@"Web Warp", @"Telengard", @"Vectorblade", @"Neon Hawk (Demo)" };
#define VXG_NGCE ((int) (sizeof VXG_GCE / sizeof VXG_GCE[0]))
#define VXG_NHB ((int) (sizeof VXG_HB / sizeof VXG_HB[0]))
static NSString *const VXG_SUMLIST[] = { @"a81c9e4ca6c68e42", @"3D Crazy Coaster", @"e3b8594d71d33cc9", @"3D Mine Storm", @"8988075388910d62", @"3D Narrow Escape",
	@"eb76f860130f8cc8", @"Armor Attack", @"6efd76b38d603797", @"Bedlam", @"6e27b509020260e3", @"Berzerk", @"d4fd15ceed0d4eff", @"Blitz! Action Football",
	@"67f8513958c04e93", @"Clean Sweep", @"56a7f277bf09f944", @"Cosmic Chasm", @"fbfd58464d42986d", @"Dark Tower (Proto)", @"bc27411caba93e4d", @"Fortress of Narzod",
	@"0406c13952fb0bd6", @"Heads-Up Action Soccer", @"a246f1d94a5b062b", @"HyperChase Auto Race", @"4318d453257b3599", @"Lunar Lander (Proto)",
	@"05f56e708f752ca8", @"Mail Plane (Proto)", @"5f756a350f7c163f", @"Mine Storm", @"39dbec9632885095", @"Mine Storm II", @"b456780dfbb2a5ec", @"Mine Storm III (Proto)",
	@"bc244985be4108c1", @"Moon Lander", @"765af69f6a231590", @"Mr. Boston Clean Sweep", @"d30fade000f73db3", @"Nebula Commander", @"42427dc02a6a25c0", @"Neon Hawk (Demo)",
	@"c390f580f841b270", @"Omega Chase", @"228dc86174f12aca", @"Patriots", @"4de711490c7edfe1", @"Pitcher's Duel (Proto)", @"a501ef26d670d50b", @"Polar Rescue",
	@"b0c0b12cbea918d8", @"Pole Position", @"22f7a490f6f7aaa3", @"Rip Off", @"526b347bc02a47c8", @"Rocket Sledge (Proto)", @"38e38b5c60466146", @"Scramble",
	@"b76f14c824344c02", @"Solar Quest", @"70c26d3974219ef4", @"Space Wars", @"9a2f8d06286c4315", @"Spike", @"775d6e5fd438246a", @"Spike Hoppin'",
	@"2e8285061530ccad", @"Spinball", @"5444ef96ef9fd758", @"Star Castle", @"d182eac5a54ffc64", @"Star Ship", @"3a2ee61316fb316d", @"Star Trek - The Motion Picture",
	@"86c6e6039ecdf971", @"StarHawk", @"ffc1e68c7b7fc721", @"Telengard", @"a29a9974706b5cff", @"Telengard", @"451ba51be3f94b3a", @"Thrust",
	@"4f461b0b51c82433", @"Tour De France (Proto)", @"95d4704dd59fe584", @"Vectorblade", @"9b40748fb9e395f7", @"Vectrexians", @"8e984cbb6c85d9bc", @"Web Warp",
	@"922621cf8e0fa4d8", @"Web Wars", @"1db76a96327738be", @"ZZAP (Proto)" };
static NSDictionary<NSString *, NSString *> *vxg_known, *vxg_hbknown, *vxg_sums;
static NSArray<NSString *> *vxg_gce, *vxg_hb;
static NSObject *vxg_lock;
static NSString *vxg_rep (NSString *s, NSString *pat, NSString *with, BOOL ci)
{
	NSRegularExpression *r = [NSRegularExpression regularExpressionWithPattern:pat options:ci ? NSRegularExpressionCaseInsensitive : 0 error:nil];
	return r ? [r stringByReplacingMatchesInString:s options:0 range:NSMakeRange (0, s.length) withTemplate:with] : s;
}
static BOOL vxg_has (NSString *s, NSString *pat, BOOL ci)
{
	NSRegularExpression *r = [NSRegularExpression regularExpressionWithPattern:pat options:ci ? NSRegularExpressionCaseInsensitive : 0 error:nil];
	return r && [r firstMatchInString:s options:0 range:NSMakeRange (0, s.length)] != nil;
}
static NSString *vxg_key (NSString *s)
{
	NSString *l = s.lowercaseString; NSMutableString *o = [NSMutableString string];
	for (NSUInteger i = 0; i < l.length; i++) { unichar c = [l characterAtIndex:i]; if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) [o appendFormat:@"%C", c]; }
	return o;
}
static void vxg_init (void)
{
	static dispatch_once_t once; dispatch_once (&once, ^{
		vxg_lock = [NSObject new];
		vxg_gce = [NSArray arrayWithObjects:VXG_GCE count:VXG_NGCE]; vxg_hb = [NSArray arrayWithObjects:VXG_HB count:VXG_NHB];
		NSMutableDictionary *k = [NSMutableDictionary dictionary], *h = [NSMutableDictionary dictionary], *s = [NSMutableDictionary dictionary];
		for (NSString *n in vxg_gce) k[vxg_key (vxg_rep (n, @"\\s*\\(Proto\\)", @"", NO))] = n;
		k[@"zzapscrollingspacegame"] = @"ZZAP (Proto)"; k[@"minestorm3"] = @"Mine Storm III (Proto)"; k[@"hyperchase"] = @"HyperChase Auto Race";
		k[@"blitz"] = @"Blitz! Action Football"; k[@"blitzfootball"] = @"Blitz! Action Football"; k[@"headsup"] = @"Heads-Up Action Soccer";
		k[@"headsupsoccer"] = @"Heads-Up Action Soccer"; k[@"startrek"] = @"Star Trek - The Motion Picture";
		for (NSString *n in vxg_hb) h[vxg_key (vxg_rep (n, @"\\s*\\(Demo\\)", @"", NO))] = n;
		h[@"neonhawkdemo"] = @"Neon Hawk (Demo)"; h[@"telengard256kflash"] = @"Telengard"; h[@"vectrexthrust"] = @"Thrust";
		const int n = (int) (sizeof VXG_SUMLIST / sizeof VXG_SUMLIST[0]); for (int i = 0; i + 1 < n; i += 2) s[VXG_SUMLIST[i]] = VXG_SUMLIST[i + 1];
		vxg_known = k; vxg_hbknown = h; vxg_sums = s; });
}
static BOOL vxg_bad (NSString *low) { return vxg_has (low, @"\\[(b|h|a|f|o|t|p)\\d*\\]", NO); }
static NSString *vxg_hbname (NSString *stem)
{
	if (vxg_bad (stem.lowercaseString)) return nil;
	NSString *t = vxg_rep (stem, @"^zzz-unk-", @"", YES); t = vxg_rep (t, @"\\s*[\\(\\[][^\\)\\]]*[\\)\\]]", @"", NO); t = vxg_rep (t, @"\\s+by\\s+.*$", @"", YES);
	t = [t stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]; t = vxg_rep (t, @"\\s+v?\\d+(\\.\\d+)+$", @"", NO);
	return vxg_hbknown[vxg_key (t)];
}
static NSString *vxg_gamename (NSString *stem)
{
	NSString *low = stem.lowercaseString;
	if (vxg_bad (low) || [low containsString:@"(pd)"]) return nil;
	if ([low containsString:@"boston"]) return @"Mr. Boston Clean Sweep";
	NSString *title = [vxg_rep (stem, @"\\s*[\\(\\[][^\\)\\]]*[\\)\\]]", @"", NO) stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
	NSString *k = vxg_known[vxg_key (title)]; if (k) return k;
	return title.length ? ([low containsString:@"proto"] ? [title stringByAppendingString:@" (Proto)"] : title) : nil;
}
static NSString *vxg_hex (const unsigned char *d, int n) { NSMutableString *s = [NSMutableString stringWithCapacity:(NSUInteger) n * 2]; for (int i = 0; i < n; i++) [s appendFormat:@"%02x", d[i]]; return s; }
static NSString *vxg_sha1 (const void *b, size_t n) { unsigned char h[CC_SHA1_DIGEST_LENGTH]; static const char z = 0; CC_SHA1 (b ? b : &z, (CC_LONG) n, h); return vxg_hex (h, CC_SHA1_DIGEST_LENGTH); }
static size_t vxg_trimmed (NSData *d) { const unsigned char *p = d.bytes; size_t n = d.length; while (n > 0 && p[n - 1] == 0xff) n--; while (n > 0 && p[n - 1] == 0) n--; return n; }
static NSString *vxg_fp (NSData *d) { return [vxg_sha1 (d.bytes, vxg_trimmed (d)) substringToIndex:16]; }   /* (a game's bytes without the padding at the end) */
static BOOL vxg_romext (NSString *n) { NSString *e = n.pathExtension.lowercaseString; return [@[ @"bin", @"vec", @"gam", @"rom" ] containsObject:e]; }
static NSString *vxg_stem (NSString *n) { return n.lastPathComponent.stringByDeletingPathExtension; }
static NSString *vxg_file_sha256 (NSString *path)
{
	FILE *f = fopen (path.fileSystemRepresentation, "rb"); if (!f) return @"";
	CC_SHA256_CTX c; CC_SHA256_Init (&c); unsigned char b[1 << 16]; size_t n; while ((n = fread (b, 1, sizeof b, f)) > 0) CC_SHA256_Update (&c, b, (CC_LONG) n); fclose (f);
	unsigned char h[CC_SHA256_DIGEST_LENGTH]; CC_SHA256_Final (h, &c); return vxg_hex (h, CC_SHA256_DIGEST_LENGTH);
}
static long long vxg_size (NSString *p) { return [[NSFileManager.defaultManager attributesOfItemAtPath:p error:nil][NSFileSize] longLongValue]; }
static BOOL vxg_iszip (NSData *d) { const unsigned char *p = d.bytes; return d.length > 4 && p[0] == 'P' && p[1] == 'K'; }

/* ------------------------------------------------------------ a zip read (its directory; stored or deflated entries) */
static unsigned vxz16 (const unsigned char *p) { return p[0] | (p[1] << 8); }
static size_t vxz32 (const unsigned char *p) { return (size_t) p[0] | ((size_t) p[1] << 8) | ((size_t) p[2] << 16) | ((size_t) p[3] << 24); }
typedef void (^vxz_cb) (NSString *name, NSData *(^read) (void), BOOL *stop);
static BOOL vxz_each (NSData *z, vxz_cb cb)
{
	const unsigned char *p = z.bytes; const size_t n = z.length; if (n < 22 || p[0] != 'P' || p[1] != 'K') return NO;
	long e = -1; for (long i = (long) n - 22; i >= 0 && i >= (long) n - 65557; i--) if (p[i] == 'P' && p[i + 1] == 'K' && p[i + 2] == 5 && p[i + 3] == 6) { e = i; break; }
	if (e < 0) return NO;
	const unsigned cnt = vxz16 (p + e + 10); size_t cd = vxz32 (p + e + 16); BOOL stop = NO;
	for (unsigned k = 0; k < cnt && !stop; k++) {
		if (cd + 46 > n || vxz32 (p + cd) != 0x02014b50) return NO;
		const unsigned meth = vxz16 (p + cd + 10), nl = vxz16 (p + cd + 28), xl = vxz16 (p + cd + 30), cl = vxz16 (p + cd + 32);
		const size_t csz = vxz32 (p + cd + 20), usz = vxz32 (p + cd + 24), lo = vxz32 (p + cd + 42);
		if (cd + 46 + nl > n) return NO;
		NSString *name = [[NSString alloc] initWithBytes:p + cd + 46 length:nl encoding:NSUTF8StringEncoding] ?: [[NSString alloc] initWithBytes:p + cd + 46 length:nl encoding:NSISOLatin1StringEncoding];
		cd += 46 + nl + xl + cl;
		name = [name stringByReplacingOccurrencesOfString:@"\\" withString:@"/"];
		if (!name.length || [name hasSuffix:@"/"]) continue;
		NSData *(^read) (void) = ^NSData * {
			if (lo + 30 > n || vxz32 (p + lo) != 0x04034b50) return nil;
			const size_t ds = lo + 30 + vxz16 (p + lo + 26) + vxz16 (p + lo + 28); if (ds + csz > n) return nil;
			if (meth == 0) return [NSData dataWithBytes:p + ds length:csz];
			if (meth != 8) return nil;
			NSMutableData *o = [NSMutableData dataWithLength:usz ? usz : 1]; z_stream s; memset (&s, 0, sizeof s);
			if (inflateInit2 (&s, -MAX_WBITS) != Z_OK) return nil;
			s.next_in = (Bytef *) (p + ds); s.avail_in = (uInt) csz; s.next_out = o.mutableBytes; s.avail_out = (uInt) (usz ? usz : 1);
			const int r = inflate (&s, Z_FINISH); inflateEnd (&s); o.length = usz; return r == Z_STREAM_END ? o : nil; };
		cb (name, read, &stop); }
	return YES;
}
static NSData *vxg_map (NSString *path) { return [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil]; }

/* ------------------------------------------------------------ what is there */
static NSString *vxg_roms (void) { return [vxa_docs () stringByAppendingPathComponent:@"roms"]; }
static NSString *vxg_sys (void) { return [vxa_docs () stringByAppendingPathComponent:@"system"]; }
static NSString *vxg_px (void) { return [vxg_sys () stringByAppendingPathComponent:@"vectrex_emu/pitrex"]; }
static NSString *vxg_speech (void) { return [vxg_sys () stringByAppendingPathComponent:@"vectrex_emu/al2.bin"]; }
static NSArray<NSString *> *vxg_romfiles (void)
{
	NSMutableArray *o = [NSMutableArray array]; NSString *r = vxg_roms ();
	NSDirectoryEnumerator *en = [NSFileManager.defaultManager enumeratorAtPath:r];
	for (NSString *rel in en) { if (en.level > 5) { [en skipDescendants]; continue; }
		if (!vxg_romext (rel)) continue; NSString *p = [r stringByAppendingPathComponent:rel];
		long long sz = vxg_size (p); if (sz > 0 && sz <= (1 << 20)) [o addObject:p]; }
	return o;
}
static NSSet<NSString *> *vxg_present (void)
{
	vxg_init (); NSMutableSet *have = [NSMutableSet set];
	for (NSString *f in vxg_romfiles ()) { NSString *s = vxg_stem (f);
		for (NSString *n in @[ s, vxg_gamename (s) ?: @"", vxg_hbname (s) ?: @"" ]) if (n.length && ([vxg_gce containsObject:n] || [vxg_hb containsObject:n])) [have addObject:n];
		NSData *d = [NSData dataWithContentsOfFile:f]; NSString *g = d ? vxg_sums[vxg_fp (d)] : nil; if (g) [have addObject:g]; }
	return have;
}
static int vxg_count (NSSet *have, NSArray *of) { int n = 0; for (NSString *s in of) if ([have containsObject:s]) n++; return n; }
static NSString *vxg_state (int k, NSSet *have)
{
	NSFileManager *fm = NSFileManager.defaultManager;
	switch (k) {
	case 0: return [NSString stringWithFormat:@"%d of the %d original games", vxg_count (have, vxg_gce), VXG_NGCE];
	case 1: return [NSString stringWithFormat:@"%d of %d free homebrew games", vxg_count (have, vxg_hb), VXG_NHB];
	case 2: return [fm fileExistsAtPath:[vxg_px () stringByAppendingPathComponent:@"pitrex.img"]] ? @"found" : @"not there";
	case 4: { NSUInteger n = [fm contentsOfDirectoryAtPath:[vxg_sys () stringByAppendingPathComponent:@"vectrex_emu/thumbs/labels"] error:nil].count;
		return n >= 250 ? [NSString stringWithFormat:@"found (%lu labels)", (unsigned long) n] : n > 0 ? [NSString stringWithFormat:@"%lu labels", (unsigned long) n] : @"the 112 that come with the app"; }
	default: return [fm fileExistsAtPath:vxg_speech ()] || [fm fileExistsAtPath:[vxg_sys () stringByAppendingPathComponent:@"vectrex_emu/sp0256-al2.bin"]] ? @"found" : @"not there"; }
}
static BOOL vxg_complete (int k, NSSet *have)
{
	return k == 0 ? vxg_count (have, vxg_gce) >= VXG_NGCE : k == 1 ? vxg_count (have, vxg_hb) >= VXG_NHB : [vxg_state (k, have) hasPrefix:@"found"];
}

/* ------------------------------------------------------------ the sources: each a chain of addresses */
#define VXG_DRIVE @"https://drive.usercontent.google.com/download?export=download&confirm=t&id="
static NSString *const VXG_DRIVE_GAMES = VXG_DRIVE @"1dcRdUxgMg2yX0ux-izPzcttY0SislQgF", *const VXG_DRIVE_SPEECH = VXG_DRIVE @"1L_fWaxPa4KLWnE7-RNCDqmI1clzSXxPP",
	*const VXG_DRIVE_PITREX = VXG_DRIVE @"17XNGNgabHEQ0eNc7zzuPhTPxmZ7G4Rri", *const VXG_DRIVE_HOMEBREW = VXG_DRIVE @"1N2lCNS8VSlBQeCqKp40nYoQVegHyhub7";
static NSString *const VXG_DRIVE_PITREX_NAME = @"PiTrex_sd_Jul29_25";   /* (the version the project's copy of the PiTrex's SD card is) */
#define VXG_MIRROR @"https://archive.org/download/vectrex-emu-extras/"
static NSString *const VXG_MIRROR_GAMES = VXG_MIRROR @"GCE_Vectrex_games.zip", *const VXG_MIRROR_HOMEBREW = VXG_MIRROR @"Vectrex_homebrew_free.zip",
	*const VXG_MIRROR_PITREX = VXG_MIRROR @"PiTrex_sd_Jul29_25.zip", *const VXG_MIRROR_SPEECH = VXG_MIRROR @"al2.bin";
/* (the full artwork set - the project's own remade box art and labels, 280 games, Simon Quincey's title artwork and
 *  pictures with his permission: from the project's GitHub release, the fastest source for a big file) */
static NSString *const VXG_ART = @"https://github.com/hugonl31/Vectrex_EMU/releases/download/artwork-1/Vectrex_EMU-artwork-full.zip";
static NSString *const VXG_SPEECH_SHA = @"6a4c0cd4aa9ae472ce28109b650680c76f76a53e6d6fd63af6b9bfde801a9e48";
static NSArray *vxg_pinned (NSString *u)
{
	static NSDictionary *pin; static dispatch_once_t once; dispatch_once (&once, ^{
		NSArray *g = @[ @169168LL, @"deaa7cf0ca07c5a57b9dd65f57089dc87a17de946f8d1c5246e992db8fa9d92d" ], *h = @[ @148368LL, @"c2b7987d991b8599f1563606897ccd6ba2124b0cab64372ace026546aa37aed8" ],
			*s = @[ @2048LL, VXG_SPEECH_SHA ], *p = @[ @188168647LL, @"237b4f61b267c15a1d1d3c16e761f4e63d31da11b24a1d25bec1e35b4a8d81ab" ];
		pin = @{ VXG_DRIVE_GAMES: g, VXG_MIRROR_GAMES: g, VXG_DRIVE_HOMEBREW: h, VXG_MIRROR_HOMEBREW: h, VXG_DRIVE_SPEECH: s, VXG_MIRROR_SPEECH: s,
			VXG_DRIVE_PITREX: p, VXG_MIRROR_PITREX: p, VXG_ART: @[ @96438203LL, @"375fb111813e7a11e69a96a5b9baa11d9f42baf521b2dc5067d833fc2448defe" ] }; });
	return pin[u];
}
static NSString *vxg_enc (NSString *s)   /* (as Java's URLEncoder with "%20": only letters, digits and -._~ left as they are) */
{
	static NSCharacterSet *ok; static dispatch_once_t once;
	dispatch_once (&once, ^{ NSMutableCharacterSet *c = [NSMutableCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~"]; ok = c; });
	return [s stringByAddingPercentEncodingWithAllowedCharacters:ok] ?: s;
}
static NSString *vxg_dec (NSString *s) { return [s stringByRemovingPercentEncoding] ?: s; }   /* (a file's name in its address: as the file is called) */
/* a page's text (on the getter's own thread: waits) */
static NSString *vxg_text (NSString *url, double timeout)
{
	NSURL *u = [NSURL URLWithString:url]; if (!u) return nil;
	NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:u]; [rq setValue:@"Vectrex_EMU" forHTTPHeaderField:@"User-Agent"]; rq.timeoutInterval = timeout;
	__block NSData *got = nil; dispatch_semaphore_t sem = dispatch_semaphore_create (0);
	[[NSURLSession.sharedSession dataTaskWithRequest:rq completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
		if (!e && [r isKindOfClass:NSHTTPURLResponse.class] && ((NSHTTPURLResponse *) r).statusCode == 200) got = d; dispatch_semaphore_signal (sem); }] resume];
	dispatch_semaphore_wait (sem, DISPATCH_TIME_FOREVER);
	return got ? ([[NSString alloc] initWithData:got encoding:NSUTF8StringEncoding] ?: [[NSString alloc] initWithData:got encoding:NSISOLatin1StringEncoding]) : nil;
}
/* a file fetched to a path, its progress told every quarter second */
static BOOL vxg_fetch (NSString *url, NSString *to, void (^prog) (long long done, long long total), NSString **err)
{
	NSURL *u = [NSURL URLWithString:url]; if (!u) { if (err) *err = @"bad address"; return NO; }
	NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:u]; [rq setValue:@"Vectrex_EMU" forHTTPHeaderField:@"User-Agent"]; rq.timeoutInterval = 90;
	__block NSString *bad = nil; dispatch_semaphore_t sem = dispatch_semaphore_create (0);
	NSURLSessionDownloadTask *t = [NSURLSession.sharedSession downloadTaskWithRequest:rq completionHandler:^(NSURL *loc, NSURLResponse *r, NSError *e) {
		const NSInteger code = [r isKindOfClass:NSHTTPURLResponse.class] ? ((NSHTTPURLResponse *) r).statusCode : 0;
		if (e) bad = e.localizedDescription; else if (code != 200) bad = [NSString stringWithFormat:@"HTTP %ld", (long) code];
		else { NSError *me = nil; [NSFileManager.defaultManager removeItemAtPath:to error:nil];
			if (![NSFileManager.defaultManager moveItemAtURL:loc toURL:[NSURL fileURLWithPath:to] error:&me]) bad = me.localizedDescription ?: @"not saved"; }
		dispatch_semaphore_signal (sem); }];
	[t resume];
	while (dispatch_semaphore_wait (sem, dispatch_time (DISPATCH_TIME_NOW, (int64_t) (250 * NSEC_PER_MSEC)))) if (prog) prog (t.countOfBytesReceived, t.countOfBytesExpectedToReceive);
	if (prog) prog (t.countOfBytesReceived, t.countOfBytesExpectedToReceive);
	if (bad && err) *err = bad;
	return bad == nil;
}
static NSArray<NSString *> *vxg_iasearch (NSString *q)
{
	NSMutableArray *o = [NSMutableArray array];
	NSString *t = vxg_text ([NSString stringWithFormat:@"https://archive.org/advancedsearch.php?q=%@&fl%%5B%%5D=identifier&sort%%5B%%5D=addeddate%%20desc&rows=5&output=json", vxg_enc (q)], 30);
	NSDictionary *j = t ? [NSJSONSerialization JSONObjectWithData:[t dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil] : nil;
	if ([j isKindOfClass:NSDictionary.class]) for (NSDictionary *d in j[@"response"][@"docs"]) if ([d isKindOfClass:NSDictionary.class] && d[@"identifier"]) [o addObject:d[@"identifier"]];
	return o;
}
static NSString *vxg_iazip (NSString *ident, NSString *pick)
{
	NSString *t = vxg_text ([@"https://archive.org/metadata/" stringByAppendingString:ident], 30);
	NSDictionary *j = t ? [NSJSONSerialization JSONObjectWithData:[t dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil] : nil;
	if (![j isKindOfClass:NSDictionary.class]) return nil;
	NSString *best = nil; long long bs = -1;
	for (NSDictionary *x in j[@"files"]) { if (![x isKindOfClass:NSDictionary.class]) continue; NSString *n = x[@"name"], *l = n.lowercaseString;
		if (![l hasSuffix:@".zip"] || ![x[@"source"] isEqual:@"original"] || vxg_has (l, @"(extra|patch)", NO)) continue;
		if (pick && ![l containsString:pick]) continue;
		long long sz = [x[@"size"] longLongValue]; if (sz > bs) { bs = sz; best = n; } }
	return best ? [NSString stringWithFormat:@"https://archive.org/download/%@/%@", ident, vxg_enc (best)] : nil;
}
static NSArray<NSString *> *vxg_wayback (NSString *prefix, NSString *pattern)
{
	NSMutableArray *hits = [NSMutableArray array];
	NSString *t = vxg_text ([NSString stringWithFormat:@"https://web.archive.org/cdx/search/cdx?url=%@&matchType=prefix&fl=original,timestamp,length&filter=statuscode:200", vxg_enc (prefix)], 60);
	for (NSString *ln in [t componentsSeparatedByString:@"\n"]) { NSArray *p = [[ln stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] componentsSeparatedByString:@" "];
		if (p.count == 3 && vxg_has (p[0], pattern, YES) && [p[2] longLongValue] > 1000) [hits addObject:@[ p[1], p[0] ]]; }
	[hits sortUsingComparator:^NSComparisonResult (NSArray *a, NSArray *b) { return [(NSString *) b[0] compare:(NSString *) a[0]]; }];
	NSMutableArray *o = [NSMutableArray array]; for (NSArray *h in hits) [o addObject:[NSString stringWithFormat:@"https://web.archive.org/web/%@id_/%@", h[0], h[1]]];
	return o;
}
typedef NSArray<NSString *> *(^vxg_step) (void);
static vxg_step vxg_one (NSString *u) { return ^NSArray * { return @[ u ]; }; }
static NSArray *vxg_nn (NSArray *a) { NSMutableArray *o = [NSMutableArray array]; for (id x in a) if ([x isKindOfClass:NSString.class]) [o addObject:x]; return o; }
static NSArray<vxg_step> *vxg_chain (int k)
{
	NSString *mi = @"https://raw.githubusercontent.com/Videodr0me/Vectrex_MiSTer/master/games/";
	switch (k) {
	case 0: return @[ vxg_one (VXG_DRIVE_GAMES), vxg_one (VXG_MIRROR_GAMES), ^NSArray * { return vxg_nn (@[ ((id) vxg_iazip (@"vectrex-champion-collection", @"collection") ?: (id) NSNull.null) ]); },
		^NSArray * { NSMutableArray *o = [NSMutableArray array]; for (NSString *i in vxg_iasearch (@"title:(vectrex champion collection)")) { NSString *z = vxg_iazip (i, @"collection"); if (z) [o addObject:z]; } return o; },
		vxg_one (@"https://archive.org/download/vectrex-champion-collection/Vectrex%20Champion%20Collection.zip"),
		^NSArray * { return vxg_nn (@[ ((id) vxg_iazip (@"gce-vectrex-games-complete", nil) ?: (id) NSNull.null), ((id) vxg_iazip (@"GCE_Vectrex_TOSEC_2012_04_23", nil) ?: (id) NSNull.null), ((id) vxg_iazip (@"vectrex_202604", nil) ?: (id) NSNull.null) ]); } ];
	case 1: return @[ vxg_one (VXG_DRIVE_HOMEBREW), vxg_one ([mi stringByAppendingString:@"Vectorblade/Vectorblade%20(v1.13).bin"]), vxg_one ([mi stringByAppendingString:@"NeonHawk/Neon%20Hawk%20Demo%20(v1.0).bin"]),
		vxg_one (@"https://raw.githubusercontent.com/malbanGit/Telengard/master/Telengard_256kFlash.bin"), vxg_one (VXG_MIRROR_HOMEBREW),
		^NSArray * { return vxg_nn (@[ ((id) vxg_iazip (@"gce-vectrex-games-complete", nil) ?: (id) NSNull.null), ((id) vxg_iazip (@"GCE_Vectrex_TOSEC_2012_04_23", nil) ?: (id) NSNull.null) ]); },
		vxg_one ([mi stringByAppendingString:@"Telengard/Telengard%20(v1.03).bin"]) ];
	case 2: return @[ ^NSArray * { NSMutableArray *o = [NSMutableArray array]; NSString *page = vxg_text (@"http://vide.malban.de/pitrex/pitrex-baremetal-download", 30); if (!page) return o;
			/* (Malban's site is slow for 190 MB: when the newest file it offers is the version the project's own copy has,
			 *  that copy first - Google Drive, checked byte for byte; a newer version from his site) */
			NSRegularExpression *zn = [NSRegularExpression regularExpressionWithPattern:@"PiTrex_sd_\\w+\\.zip" options:NSRegularExpressionCaseInsensitive error:nil];
			NSMutableSet *names = [NSMutableSet set]; for (NSTextCheckingResult *m in [zn matchesInString:page options:0 range:NSMakeRange (0, page.length)]) [names addObject:[page substringWithRange:m.range].lowercaseString];
			if (names.count && [names isEqualToSet:[NSSet setWithObject:[VXG_DRIVE_PITREX_NAME.lowercaseString stringByAppendingString:@".zip"]]]) [o addObject:VXG_DRIVE_PITREX];
			NSRegularExpression *di = [NSRegularExpression regularExpressionWithPattern:@"download_id=(\\d+)" options:0 error:nil]; NSMutableSet *ids = [NSMutableSet set];
			for (NSTextCheckingResult *m in [di matchesInString:page options:0 range:NSMakeRange (0, page.length)]) {
				NSRange after = NSMakeRange (m.range.location, MIN (page.length - m.range.location, (NSUInteger) 1500));
				if ([zn firstMatchInString:page options:0 range:after]) [ids addObject:@([page substringWithRange:[m rangeAtIndex:1]].intValue)]; }
			for (NSNumber *i in [ids.allObjects sortedArrayUsingComparator:^NSComparisonResult (NSNumber *a, NSNumber *b) { return [b compare:a]; }])
				[o addObject:[NSString stringWithFormat:@"http://vide.malban.de/?sdm_process_download=1&download_id=%@", i]];
			return o; },
		vxg_one (@"http://vide.malban.de/?sdm_process_download=1&download_id=7074"), vxg_one (@"http://vectrex.malban.de/pitrex/PiTrex_sd_Jul29_25.zip"), vxg_one (VXG_MIRROR_PITREX),
		^NSArray * { return vxg_wayback (@"vectrex.malban.de/pitrex/", @"(pitrex_sd_|sd_drive_)[^/]*\\.zip$"); } ];
	case 4: return @[ vxg_one (VXG_ART) ];
	default: return @[ vxg_one (VXG_DRIVE_SPEECH), vxg_one (@"http://spatula-city.org/~im14u2c/sp0256-al2/al2.bin"), vxg_one (VXG_MIRROR_SPEECH),
		^NSArray * { return vxg_wayback (@"spatula-city.org/~im14u2c/sp0256-al2/al2.bin", @"al2\\.bin$"); } ]; }
}

/* ------------------------------------------------------------ checked */
/* the item's games (by their fingerprints) a zip has - zips in zips too */
static void vxg_found (NSData *zip, int k, NSMutableSet *out, int depth)
{
	NSArray *want = k == 0 ? vxg_gce : vxg_hb;
	vxz_each (zip, ^(NSString *n, NSData *(^read) (void), BOOL *stop) { (void) stop;
		if ([n.lowercaseString hasSuffix:@".zip"] && depth < 2) { NSData *d = read (); if (d) vxg_found (d, k, out, depth + 1); }
		else if (vxg_romext (n)) { NSData *d = read (); NSString *g = d ? vxg_sums[vxg_fp (d)] : nil; if (g && [want containsObject:g]) [out addObject:g]; } });
}
static BOOL vxg_is_pitrex (NSData *z)
{
	NSMutableSet *tops = [NSMutableSet set];
	if (!vxz_each (z, ^(NSString *n, NSData *(^read) (void), BOOL *stop) { (void) read; (void) stop;
			NSArray *parts = [n componentsSeparatedByString:@"/"]; if (parts.count <= 2) [tops addObject:[parts.lastObject lowercaseString]]; })) return NO;
	for (NSString *key in @[ @"pitrex.img", @"bootcode.bin", @"start.elf", @"config.txt" ]) if (![tops containsObject:key]) return NO;
	return YES;
}
/* a download is what it should be: the project's copy that very file, al2.bin its known bytes, a set enough of the
 * games, a homebrew file a known game, the PiTrex an SD card */
static BOOL vxg_check (int k, NSString *url, NSString *f)
{
	NSArray *pin = vxg_pinned (url); const long long sz = vxg_size (f);
	if (pin) return sz == [pin[0] longLongValue] && [vxg_file_sha256 (f) isEqualToString:pin[1]];
	if (k == 3) return sz == 2048 && [vxg_file_sha256 (f) isEqualToString:VXG_SPEECH_SHA];
	if (k == 2) { NSData *z = vxg_map (f); return z && vxg_iszip (z) && vxg_is_pitrex (z); }
	if (sz > (64LL << 20)) return NO;
	NSData *d = [NSData dataWithContentsOfFile:f]; if (!d) return NO;
	if (!vxg_iszip (d)) { if (k == 0) return NO; NSString *g = vxg_sums[vxg_fp (d)]; return g && [vxg_hb containsObject:g]; }
	NSMutableSet *got = [NSMutableSet set]; vxg_found (d, k, got, 0); return (int) got.count >= (k == 0 ? 20 : 6);
}

/* ------------------------------------------------------------ unpacked and named */
static NSMutableSet *vxg_ownsums, *vxg_ownnames;
static void vxg_owned (void)
{
	vxg_ownsums = [NSMutableSet set]; vxg_ownnames = [NSMutableSet set];
	for (NSString *f in vxg_romfiles ()) { NSString *s = vxg_stem (f); for (NSString *n in @[ s, vxg_gamename (s) ?: @"", vxg_hbname (s) ?: @"" ]) if (n.length) [vxg_ownnames addObject:n];
		NSData *d = [NSData dataWithContentsOfFile:f]; if (d) { [vxg_ownsums addObject:vxg_sha1 (d.bytes, d.length)]; [vxg_ownsums addObject:vxg_sha1 (d.bytes, vxg_trimmed (d))]; } }
}
static BOOL vxg_write (NSString *path, NSData *d)
{
	[NSFileManager.defaultManager createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
	return [d writeToFile:path atomically:YES];
}
/* a game written only if the roms folder has it not yet (the same bytes under another name count): 1 if written */
static int vxg_writegame (NSString *name, NSData *d)
{
	NSString *out = [vxg_roms () stringByAppendingPathComponent:[name stringByAppendingString:@".bin"]]; if ([NSFileManager.defaultManager fileExistsAtPath:out]) return 0;
	NSString *a = vxg_sha1 (d.bytes, d.length), *b = vxg_sha1 (d.bytes, vxg_trimmed (d));
	if ([vxg_ownnames containsObject:name] || [vxg_ownsums containsObject:a] || [vxg_ownsums containsObject:b]) return 0;
	[vxg_ownnames addObject:name]; [vxg_ownsums addObject:a]; [vxg_ownsums addObject:b];
	return vxg_write (out, d) ? 1 : 0;
}
/* one file of a download as ours: games and homebrew into roms (named), the speech ROM into the system folder */
static int vxg_place (int k, NSString *path, NSData *d)
{
	if (k == 3) { if (d.length != 2048 || [NSFileManager.defaultManager fileExistsAtPath:vxg_speech ()]) return 0; return vxg_write (vxg_speech (), d) ? 1 : 0; }
	if (!vxg_romext (path)) return 0; NSString *n = k == 0 ? vxg_gamename (vxg_stem (path)) : vxg_hbname (vxg_stem (path));
	return n ? vxg_writegame (n, d) : 0;
}
static int vxg_unpackbytes (int k, NSString *name, NSData *d, int depth)
{
	if (!vxg_iszip (d)) return vxg_place (k, name, d);
	__block int added = 0;
	vxz_each (d, ^(NSString *n, NSData *(^read) (void), BOOL *stop) { (void) stop; NSData *b = read (); if (!b) return;
		added += [n.lowercaseString hasSuffix:@".zip"] && depth < 2 ? vxg_unpackbytes (k, n, b, depth + 1) : vxg_place (k, n, b); });
	return added;
}
/* the PiTrex's SD card: every file into its folder (one folder round it all taken off), files there kept */
static int vxg_unpackpitrex (NSString *f)
{
	NSData *z = vxg_map (f); if (!z) return 0;
	NSMutableArray *names = [NSMutableArray array]; vxz_each (z, ^(NSString *n, NSData *(^read) (void), BOOL *stop) { (void) read; (void) stop; [names addObject:n]; });
	NSMutableSet *tops = [NSMutableSet set]; BOOL allIn = YES; for (NSString *n in names) { [tops addObject:[n componentsSeparatedByString:@"/"].firstObject]; if (![n containsString:@"/"]) allIn = NO; }
	NSString *strip = tops.count == 1 && allIn ? [tops.anyObject stringByAppendingString:@"/"] : @"";
	__block int added = 0; NSString *dir = vxg_px ();
	vxz_each (z, ^(NSString *n, NSData *(^read) (void), BOOL *stop) { (void) stop;
		if (strip.length && [n hasPrefix:strip]) n = [n substringFromIndex:strip.length];
		if (!n.length || [n containsString:@".."]) return; NSString *out = [dir stringByAppendingPathComponent:n];
		if ([NSFileManager.defaultManager fileExistsAtPath:out]) return;
		@autoreleasepool { NSData *b = read (); if (b && vxg_write (out, b)) added++; } });
	return added;
}
/* the artwork set: its system/vectrex_emu/thumbs/... into this app's system folder (where the library reads them); a
 * picture already there is kept - the player's own wins */
static int vxg_unpackart (NSString *f)
{
	NSData *z = vxg_map (f); if (!z) return 0; __block int added = 0;
	vxz_each (z, ^(NSString *n, NSData *(^read) (void), BOOL *stop) { (void) stop;
		if (![n hasPrefix:@"system/"] || [n containsString:@".."]) return;
		NSString *out = [vxg_sys () stringByAppendingPathComponent:[n substringFromIndex:7]]; if ([NSFileManager.defaultManager fileExistsAtPath:out]) return;
		@autoreleasepool { NSData *b = read (); if (b && vxg_write (out, b)) added++; } });
	return added;
}
/* an item through its chain: each address until one works (the games: until all of them are there); the files added */
static int vxg_getitem (int k, void (^prog) (long long, long long), void (^say) (NSString *), NSString **err)
{
	vxg_init (); NSMutableSet *tried = [NSMutableSet set]; int added = 0; BOOL ok = NO;
	NSString *tmp = [NSTemporaryDirectory () stringByAppendingPathComponent:[NSString stringWithFormat:@"download%d.tmp", k]];   /* (each item its own: Download all runs them side by side) */
	@synchronized (vxg_lock) { vxg_owned (); }
	for (id sx in vxg_chain (k)) { vxg_step s = sx;
		NSArray *urls = s (); BOOL done = NO;
		for (NSString *u in urls) {
			if (![u isKindOfClass:NSString.class] || [tried containsObject:u]) continue; [tried addObject:u];
			say ([NSString stringWithFormat:@"trying %@...", [NSURL URLWithString:u].host ?: @"?"]);
			NSString *e = nil;
			if (!vxg_fetch (u, tmp, prog, &e)) { ulog ([NSString stringWithFormat:@"get: %@ - %@", u, e]); continue; }
			if (!vxg_check (k, u, tmp)) { ulog ([NSString stringWithFormat:@"get: %@ - not the file it should be", u]); [NSFileManager.defaultManager removeItemAtPath:tmp error:nil]; continue; }
			@synchronized (vxg_lock) {
				if (k == 4) added += vxg_unpackart (tmp); else if (k == 2) added += vxg_unpackpitrex (tmp);
				else { NSData *d = [NSData dataWithContentsOfFile:tmp]; if (d) added += vxg_unpackbytes (k, vxg_dec (u.lastPathComponent), d, 0); } }
			[NSFileManager.defaultManager removeItemAtPath:tmp error:nil];
			ok = YES; ulog ([NSString stringWithFormat:@"get: %@ - %d files", u, added]);
			if (vxg_complete (k, vxg_present ())) { done = YES; break; } }
		if (done) break; }
	if (!ok && err) *err = @"none of its sources worked";
	return ok ? added : -1;
}

/* ------------------------------------------------------------ Add files...: the user's own, from Files */
/* a game file (named ours when known, else as it was), or a zip of them, into roms/Imported; a UVMC2 game (.um2) or a
 * Vec4ever file (.v4e) as it is */
static int vxg_importone (NSString *path, NSData *d)
{
	if (!d.length || d.length > (1 << 20)) return 0;
	NSString *known = vxg_sums[vxg_fp (d)], *s = vxg_stem (path), *gn = vxg_gamename (s), *hn = vxg_hbname (s);
	NSString *n = known ?: (gn && [vxg_known.allValues containsObject:gn] ? gn : hn ?: s);
	NSString *a = vxg_sha1 (d.bytes, d.length); if ([vxg_ownsums containsObject:a]) return 0;
	NSString *safe = vxg_rep (n, @"[\\\\/:*?\"<>|]", @"_", NO);
	NSString *out = [[vxg_roms () stringByAppendingPathComponent:@"Imported"] stringByAppendingPathComponent:[safe stringByAppendingString:@".bin"]];
	if ([NSFileManager.defaultManager fileExistsAtPath:out]) return 0;
	if (!vxg_write (out, d)) return 0; [vxg_ownsums addObject:a]; return 1;
}
static int vxg_importgames (NSString *name, NSData *d, int depth)
{
	NSString *e = name.pathExtension.lowercaseString;
	if ([e isEqualToString:@"um2"] || [e isEqualToString:@"v4e"]) {   /* (as it is: a UVMC2 game, a Vec4ever file) */
		NSString *out = [[vxg_roms () stringByAppendingPathComponent:@"Imported"] stringByAppendingPathComponent:name.lastPathComponent];
		return ![NSFileManager.defaultManager fileExistsAtPath:out] && vxg_write (out, d) ? 1 : 0; }
	if (vxg_iszip (d)) { __block int n = 0;
		vxz_each (d, ^(NSString *en, NSData *(^read) (void), BOOL *stop) { (void) stop; NSString *x = en.pathExtension.lowercaseString;
			if ([x isEqualToString:@"zip"] && depth < 2) { NSData *b = read (); if (b) n += vxg_importgames (en, b, depth + 1); }
			else if (vxg_romext (en) || [x isEqualToString:@"um2"] || [x isEqualToString:@"v4e"]) { NSData *b = read (); if (b) n += vxg_importgames (en, b, depth + 1); } });
		return n; }
	return vxg_romext (name) ? vxg_importone (name, d) : 0;
}

/* ------------------------------------------------------------ the screen */
static NSString *const VXG_TITLE[] = { @"GCE Vectrex games", @"Homebrew games (free)", @"PiTrex SD card", @"Speech ROM (al2.bin)", @"Box art and labels (full set)" };
static NSString *const VXG_SIZE[] = { @"0.2 MB", @"1 MB", @"190 MB", @"2 KB", @"96 MB" };
static NSString *const VXG_SITE[] = { @"archive.org", @"archive.org, GitHub", @"vide.malban.de", @"spatula-city.org", @"this project's GitHub" };
#define VXG_N 5
static BOOL vxg_busy[VXG_N];
static NSString *vxg_det[VXG_N];   /* (a row's line while it downloads, and its result) */
int vxg_any_busy (void) { for (int i = 0; i < VXG_N; i++) if (vxg_busy[i]) return 1; return 0; }

@interface VXGetterVC : UITableViewController <UIDocumentPickerDelegate>
@property (nonatomic, copy) void (^onClose) (void);
@end
@implementation VXGetterVC { NSSet *_have; BOOL _closed; }
- (void) viewDidLoad
{
	[super viewDidLoad]; self.title = @"Get games & extras"; self.tableView.backgroundColor = VX_BG; self.tableView.separatorColor = VX_EDGE;
	_have = vxg_present (); for (int i = 0; i < VXG_N; i++) if (!vxg_busy[i]) vxg_det[i] = nil;
	__weak VXGetterVC *ws = self;
	self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithPrimaryAction:[UIAction actionWithTitle:@"Close" image:nil identifier:nil handler:^(UIAction *a) { (void) a; [ws.navigationController dismissViewControllerAnimated:YES completion:nil]; }]];
	ulog (@"ui get games & extras");
}
- (void) viewDidDisappear:(BOOL)a
{
	[super viewDidDisappear:a];
	if (!_closed && (self.navigationController.isBeingDismissed || self.isBeingDismissed || !self.navigationController.presentingViewController)) { _closed = YES; if (self.onClose) self.onClose (); }
}
- (NSInteger) numberOfSectionsInTableView:(UITableView *)t { return 3; }
- (NSInteger) tableView:(UITableView *)t numberOfRowsInSection:(NSInteger)s { return s == 0 ? VXG_N + 1 : s == 1 ? 1 : 0; }
- (NSString *) tableView:(UITableView *)t titleForHeaderInSection:(NSInteger)s { return s == 0 ? @"DOWNLOAD" : s == 1 ? @"YOUR OWN FILES" : @"NOTICE"; }
- (NSString *) tableView:(UITableView *)t titleForFooterInSection:(NSInteger)s
{
	return s == 0 ? @"Files already there are kept. The box art for 112 games comes with the app; the full set has 280. The PiTrex's SD card (190 MB) and the full box art (96 MB) are large: best on Wi-Fi."
		: s == 1 ? @"Games (.bin, .vec, a zip of them), UVMC2 games (.um2), the speech ROM, a PiTrex SD card zip." : VXG_DISCLAIMER;
}
- (void) tableView:(UITableView *)t willDisplayHeaderView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (void) tableView:(UITableView *)t willDisplayFooterView:(UITableViewHeaderFooterView *)v forSection:(NSInteger)s { v.textLabel.textColor = VX_DIM; }
- (UITableViewCell *) tableView:(UITableView *)t cellForRowAtIndexPath:(NSIndexPath *)ip
{
	UITableViewCell *c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
	c.backgroundColor = VX_PANEL; c.textLabel.textColor = VX_FG; c.detailTextLabel.textColor = VX_DIM; c.detailTextLabel.numberOfLines = 0;
	UIView *sb = [UIView new]; sb.backgroundColor = VX_SEL; c.selectedBackgroundView = sb;
	const NSInteger r = ip.row;
	if (ip.section == 1) { c.textLabel.text = @"Add files..."; c.detailTextLabel.text = @"from Files: iCloud, this device, a USB stick"; return c; }
	if (r == VXG_N) { c.textLabel.text = @"Download all"; c.detailTextLabel.text = @"What is not here yet"; c.textLabel.textColor = VX_HI; return c; }
	c.textLabel.text = VXG_TITLE[r];
	c.detailTextLabel.text = vxg_det[r] ?: [NSString stringWithFormat:@"%@  -  %@, %@", vxg_state ((int) r, _have), VXG_SIZE[r], VXG_SITE[r]];
	if (vxg_busy[r]) c.detailTextLabel.textColor = VX_CHG;
	return c;
}
- (void) tableView:(UITableView *)t didSelectRowAtIndexPath:(NSIndexPath *)ip
{
	[t deselectRowAtIndexPath:ip animated:YES];
	if (ip.section == 1) { [self pick]; return; }
	if (ip.section) return;
	if (ip.row == VXG_N) { NSSet *h = vxg_present (); NSMutableArray *ks = [NSMutableArray array]; for (int i = 0; i < VXG_N; i++) if (!vxg_complete (i, h)) [ks addObject:@(i)];
		if (!ks.count) { [self toast:@"Everything is here"]; return; } [self start:ks]; return; }
	[self start:@[ @(ip.row) ]];
}
- (void) toast:(NSString *)s
{
	UILabel *l = [UILabel new]; l.text = [NSString stringWithFormat:@"   %@   ", s]; l.font = [UIFont systemFontOfSize:14 weight:UIFontWeightMedium]; l.textColor = VX_HI; l.numberOfLines = 0;
	l.backgroundColor = [VX_PANEL colorWithAlphaComponent:0.95]; l.layer.cornerRadius = 10; l.layer.masksToBounds = YES; l.layer.borderWidth = 1; l.layer.borderColor = VX_EDGE2.CGColor;
	UIView *v = self.navigationController.view ?: self.view; CGSize sz = [l sizeThatFits:CGSizeMake (v.bounds.size.width - 40, 400)];
	l.frame = CGRectMake ((v.bounds.size.width - MIN (sz.width, v.bounds.size.width - 40)) / 2, v.safeAreaInsets.top + 60, MIN (sz.width, v.bounds.size.width - 40), MAX (34, sz.height + 12));
	[v addSubview:l]; [UIView animateWithDuration:0.4 delay:2.6 options:0 animations:^{ l.alpha = 0; } completion:^(BOOL d) { (void) d; [l removeFromSuperview]; }];
}
- (void) start:(NSArray<NSNumber *> *)ks
{
	if ([NSUserDefaults.standardUserDefaults boolForKey:@"dl.agreed"]) { [self run:ks]; return; }
	NSMutableString *s = [NSMutableString stringWithString:@"The files and where they come from:\n"];
	for (int i = 0; i < 5; i++) [s appendFormat:@"\n%@: %@", VXG_WHAT[i][0], VXG_WHAT[i][1]];
	__weak VXGetterVC *ws = self;   /* (the app's own prompt, as the Android app's) */
	vxa_prompt (self, @"Before you download", @"Notice", VXG_DISCLAIMER, nil, nil, NO, @[
			@[ @"I agree - download", @"", VXGO (^(NSString *x) { (void) x; [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"dl.agreed"]; ulog (@"downloads: the notice agreed to"); [ws run:ks]; }) ],
			@[ @"Cancel", @"", VXGO (^(NSString *x) { (void) x; }) ] ], s, nil);
}
- (void) say:(int)k text:(NSString *)s
{
	dispatch_async (dispatch_get_main_queue (), ^{ vxg_det[k] = s; if (self.tableView.window) [self.tableView reloadRowsAtIndexPaths:@[ [NSIndexPath indexPathForRow:k inSection:0] ] withRowAnimation:UITableViewRowAnimationNone]; });
}
- (void) run:(NSArray<NSNumber *> *)ks
{
	for (NSNumber *kn in ks) { const int k = kn.intValue; if (vxg_busy[k]) continue; vxg_busy[k] = YES; [self say:k text:@"starting..."];
		__weak VXGetterVC *ws = self;
		dispatch_async (dispatch_get_global_queue (QOS_CLASS_USER_INITIATED, 0), ^{
			__block double last = 0; NSString *e = nil;
			const int n = vxg_getitem (k, ^(long long done, long long total) { const double now = CACurrentMediaTime (); if (now - last < 0.25) return; last = now;
					[ws say:k text:total > 0 ? [NSString stringWithFormat:@"%.1f of %.1f MB  -  %.0f %%", done / 1e6, total / 1e6, 100.0 * done / total] : [NSString stringWithFormat:@"%.1f MB", done / 1e6]]; },
				^(NSString *s) { [ws say:k text:s]; }, &e);
			NSString *res = n >= 0 ? [NSString stringWithFormat:@"%@  -  %@", vxg_state (k, vxg_present ()), n > 0 ? [NSString stringWithFormat:@"%d files added", n] : @"nothing new"]
				: [NSString stringWithFormat:@"Could not be downloaded: %@. Try again later, or get it by hand and use Add files.", e ?: @"?"];
			if (n < 0) ulog ([NSString stringWithFormat:@"get %@: %@", VXG_TITLE[k], e]);
			vxg_busy[k] = NO; [ws say:k text:res];
			dispatch_async (dispatch_get_main_queue (), ^{ VXGetterVC *g = ws; if (g) [g toast:[NSString stringWithFormat:@"%@: %@", VXG_TITLE[k], res]]; else vxa_toast_ext ([NSString stringWithFormat:@"%@: %@", VXG_TITLE[k], res]); }); }); }
}
- (void) pick
{
	UIDocumentPickerViewController *p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ] asCopy:YES];
	p.allowsMultipleSelection = YES; p.delegate = self; [self presentViewController:p animated:YES completion:nil];
}
- (void) documentPicker:(UIDocumentPickerViewController *)c didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
	[self toast:[NSString stringWithFormat:@"Adding %lu %@", (unsigned long) urls.count, urls.count == 1 ? @"file..." : @"files..."]];
	__weak VXGetterVC *ws = self;
	dispatch_async (dispatch_get_global_queue (QOS_CLASS_USER_INITIATED, 0), ^{
		vxg_init (); int games = 0, other = 0;
		@synchronized (vxg_lock) { vxg_owned ();
			for (NSURL *u in urls) { @autoreleasepool {
				NSString *name = u.lastPathComponent; NSData *d = vxg_map (u.path); if (!d) { ulog ([NSString stringWithFormat:@"add: %@ - not read", name]); continue; }
				if (vxg_iszip (d) && vxg_is_pitrex (d)) { other += vxg_unpackpitrex (u.path); continue; }   /* (a PiTrex SD card) */
				if (d.length == 2048 && [vxg_file_sha256 (u.path) isEqualToString:VXG_SPEECH_SHA]) { other += vxg_place (3, @"al2.bin", d); continue; }
				if (d.length > (64u << 20)) continue;
				games += vxg_importgames (name, d, 0); } } }
		ulog ([NSString stringWithFormat:@"add: %d games, %d other files", games, other]);
		NSString *r = [NSString stringWithFormat:@"%d %@%@ added%@", games, games == 1 ? @"game" : @"games", other > 0 ? [NSString stringWithFormat:@", %d other files", other] : @"",
			games + other == 0 ? @" (nothing new, or not a Vectrex file)" : @""];
		dispatch_async (dispatch_get_main_queue (), ^{ VXGetterVC *g = ws; if (!g) return; g->_have = vxg_present (); [g.tableView reloadData]; [g toast:r]; }); });
}
@end

@implementation VXUI (Getter)
+ (void) showGetterFrom:(UIViewController *)vc then:(void (^) (void))then
{
	UIViewController *top = vc; while (top.presentedViewController) top = top.presentedViewController;
	VXGetterVC *g = [[VXGetterVC alloc] initWithStyle:UITableViewStyleInsetGrouped]; g.onClose = then;
	UINavigationController *n = [[UINavigationController alloc] initWithRootViewController:g]; [self styleNav:n];
	n.modalPresentationStyle = UIModalPresentationFormSheet;
	[top presentViewController:n animated:YES completion:nil];
}
+ (int) getterGames { vxg_init (); return vxg_count (vxg_present (), vxg_gce); }
@end
