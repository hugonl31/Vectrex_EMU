/* VXP2P.m - iPhone / iPad to iPhone / iPad with no network at all: Apple's peer-to-peer Wi-Fi (AWDL - as AirDrop), through
 * the Network framework's includePeerToPeer. The host advertises its game nearby (Bonjour "_vectrexemu._udp" over the
 * peer-to-peer link), the other device sees it and connects straight to it - no router, no hotspot, no internet; it works
 * where a hotel's Wi-Fi keeps its guests apart. The game itself stays the core's (netvx.c, UDP over IPv4): a small bridge
 * on each side hands its packets between the peer-to-peer connection (IPv6, link-local) and the core's socket on
 * 127.0.0.1 - the host's core sees a player on this device, the guest's core joins 127.0.0.1:<bridge>.
 * Apple devices only (Android and the PC: Wi-Fi Direct or a hotspot - Online > Nearby).
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#import "VXApp.h"
#import <Network/Network.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#define VXP_TYPE "_vectrexemu._udp"
extern int vx_net_port (void);

/* one bridge: the peer-to-peer connection <-> a UDP socket on 127.0.0.1 */
@interface VXPBridge : NSObject { @public int fd; struct sockaddr_in to; BOOL have_to, host; nw_connection_t conn; dispatch_source_t src; long in, out; }
@end
@implementation VXPBridge
@end

static dispatch_queue_t vxp_q;
static nw_listener_t vxp_listener;
static nw_browser_t vxp_browser;
static NSMutableArray<VXPBridge *> *vxp_bridges;
static NSMutableArray<NSDictionary *> *vxp_seen;           /* (the games nearby: name, endpoint) */

static dispatch_queue_t vxp_queue (void) { if (!vxp_q) vxp_q = dispatch_queue_create ("vx.p2p", DISPATCH_QUEUE_SERIAL); return vxp_q; }
static nw_parameters_t vxp_params (void)
{
	nw_parameters_t p = nw_parameters_create_secure_udp (NW_PARAMETERS_DISABLE_PROTOCOL, NW_PARAMETERS_DEFAULT_CONFIGURATION);
	nw_parameters_set_include_peer_to_peer (p, true); return p;
}
static void vxp_log (NSString *s) { dispatch_async (dispatch_get_main_queue (), ^{ ulog ([@"p2p: " stringByAppendingString:s]); }); }

static void vxp_receive (VXPBridge *b)                    /* (the peer-to-peer side's packets to the core's socket) */
{
	nw_connection_receive_message (b->conn, ^(dispatch_data_t content, nw_content_context_t ctx, bool done, nw_error_t err) {
		(void) ctx; (void) done;
		if (content && b->have_to) { const void *p = NULL; size_t n = 0; dispatch_data_t m = dispatch_data_create_map (content, &p, &n);
			if (p && n) { sendto (b->fd, p, n, 0, (struct sockaddr *) &b->to, sizeof b->to); b->in++; } (void) m; }
		if (!err) vxp_receive (b);
		else vxp_log ([NSString stringWithFormat:@"connection ended (%d)", (int) nw_error_get_error_code (err)]); });
}
static VXPBridge *vxp_bridge (nw_connection_t c, BOOL host)
{
	VXPBridge *b = [VXPBridge new]; b->conn = c; b->host = host;
	b->fd = socket (AF_INET, SOCK_DGRAM, 0); if (b->fd < 0) return nil;
	struct sockaddr_in me; memset (&me, 0, sizeof me); me.sin_family = AF_INET; me.sin_addr.s_addr = htonl (INADDR_LOOPBACK); me.sin_port = 0;
	if (bind (b->fd, (struct sockaddr *) &me, sizeof me) < 0) { close (b->fd); return nil; }
	fcntl (b->fd, F_SETFL, fcntl (b->fd, F_GETFL) | O_NONBLOCK);
	if (host) {                                         /* (the host: the packets to its core's game port) */
		memset (&b->to, 0, sizeof b->to); b->to.sin_family = AF_INET; b->to.sin_addr.s_addr = htonl (INADDR_LOOPBACK); b->to.sin_port = htons ((uint16_t) vx_net_port ()); b->have_to = YES; }
	b->src = dispatch_source_create (DISPATCH_SOURCE_TYPE_READ, (uintptr_t) b->fd, 0, vxp_queue ());
	__weak VXPBridge *wb = b;
	dispatch_source_set_event_handler (b->src, ^{ VXPBridge *s = wb; if (!s) return;          /* (the core's packets to the other device) */
		for (;;) { char buf[2048]; struct sockaddr_in from; socklen_t fl = sizeof from;
			const ssize_t n = recvfrom (s->fd, buf, sizeof buf, 0, (struct sockaddr *) &from, &fl); if (n <= 0) break;
			if (!s->host) { s->to = from; s->have_to = YES; }       /* (the guest: its core's socket, learnt from its first packet) */
			dispatch_data_t d = dispatch_data_create (buf, (size_t) n, vxp_queue (), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
			nw_connection_send (s->conn, d, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true, ^(nw_error_t e) { (void) e; }); s->out++; } });
	dispatch_resume (b->src);
	nw_connection_set_queue (c, vxp_queue ());
	nw_connection_set_state_changed_handler (c, ^(nw_connection_state_t st, nw_error_t e) {
		if (st == nw_connection_state_ready) vxp_log (host ? @"a player nearby connected" : @"connected to the host nearby");
		else if (st == nw_connection_state_failed) vxp_log ([NSString stringWithFormat:@"the connection failed (%d)", e ? (int) nw_error_get_error_code (e) : 0]); });
	nw_connection_start (c); vxp_receive (b);
	if (!vxp_bridges) vxp_bridges = [NSMutableArray array];
	@synchronized (vxp_bridges) { [vxp_bridges addObject:b]; }
	return b;
}
static void vxp_bridges_end (void)
{
	if (!vxp_bridges) return;
	@synchronized (vxp_bridges) { for (VXPBridge *b in vxp_bridges) { dispatch_source_cancel (b->src); nw_connection_cancel (b->conn); close (b->fd); } [vxp_bridges removeAllObjects]; }
}

/* ---- the host: its game advertised nearby, each player's connection bridged to its core */
void vxp_host_start (NSString *name)
{
	if (vxp_listener) return;
	vxp_listener = nw_listener_create (vxp_params ()); if (!vxp_listener) { vxp_log (@"no listener"); return; }
	NSString *n = name.length > 60 ? [name substringToIndex:60] : name;
	nw_listener_set_advertise_descriptor (vxp_listener, nw_advertise_descriptor_create_bonjour_service (n.UTF8String, VXP_TYPE, NULL));
	nw_listener_set_queue (vxp_listener, vxp_queue ());
	nw_listener_set_state_changed_handler (vxp_listener, ^(nw_listener_state_t st, nw_error_t e) {
		if (st == nw_listener_state_ready) vxp_log ([NSString stringWithFormat:@"advertised nearby as \"%@\"", n]);
		else if (st == nw_listener_state_failed) vxp_log ([NSString stringWithFormat:@"advertising failed (%d)", e ? (int) nw_error_get_error_code (e) : 0]); });
	nw_listener_set_new_connection_handler (vxp_listener, ^(nw_connection_t c) {
		vxp_bridges_end (); vxp_bridge (c, YES); });           /* (one other player: a new one replaces the one before - a second round) */
	nw_listener_start (vxp_listener);
}
void vxp_stop (void)
{
	if (vxp_listener) { nw_listener_cancel (vxp_listener); vxp_listener = nil; vxp_log (@"advertising ended"); }
	vxp_bridges_end ();
}
int vxp_hosting (void) { return vxp_listener != nil; }

/* ---- the guest: the games nearby, one joined through a bridge */
void vxp_browse (int on)
{
	if (!on) { if (vxp_browser) { nw_browser_cancel (vxp_browser); vxp_browser = nil; } return; }
	if (vxp_browser) return;
	if (!vxp_seen) vxp_seen = [NSMutableArray array];
	@synchronized (vxp_seen) { [vxp_seen removeAllObjects]; }
	vxp_browser = nw_browser_create (nw_browse_descriptor_create_bonjour_service (VXP_TYPE, NULL), vxp_params ());
	nw_browser_set_queue (vxp_browser, vxp_queue ());
	nw_browser_set_browse_results_changed_handler (vxp_browser, ^(nw_browse_result_t old, nw_browse_result_t now, bool batch) {
		(void) batch; const nw_browse_result_change_t ch = nw_browse_result_get_changes (old, now);
		if (ch & nw_browse_result_change_result_added) { nw_endpoint_t ep = nw_browse_result_copy_endpoint (now); const char *nm = nw_endpoint_get_bonjour_service_name (ep);
			NSString *s = nm ? @(nm) : @"?"; @synchronized (vxp_seen) { [vxp_seen addObject:@{ @"name": s, @"ep": ep }]; } vxp_log ([NSString stringWithFormat:@"a game nearby: %@", s]); }
		if (ch & nw_browse_result_change_result_removed) { nw_endpoint_t ep = nw_browse_result_copy_endpoint (old); const char *nm = nw_endpoint_get_bonjour_service_name (ep);
			NSString *s = nm ? @(nm) : @"?"; @synchronized (vxp_seen) { NSUInteger i = [vxp_seen indexOfObjectPassingTest:^BOOL (NSDictionary *d, NSUInteger k, BOOL *stop) { (void) k; (void) stop; return [d[@"name"] isEqualToString:s]; }];
				if (i != NSNotFound) [vxp_seen removeObjectAtIndex:i]; } } });
	nw_browser_start (vxp_browser);
}
NSArray<NSString *> *vxp_games (void)
{
	if (!vxp_seen) return @[];
	NSMutableArray *a = [NSMutableArray array]; @synchronized (vxp_seen) { for (NSDictionary *d in vxp_seen) [a addObject:d[@"name"]]; } return a;
}
int vxp_join (NSString *name)                             /* (the bridge's port on 127.0.0.1 - the core joins that; 0 not found) */
{
	nw_endpoint_t ep = nil; if (vxp_seen) @synchronized (vxp_seen) { for (NSDictionary *d in vxp_seen) if ([d[@"name"] isEqualToString:name]) ep = d[@"ep"]; }
	if (!ep) return 0;
	vxp_bridges_end ();
	VXPBridge *b = vxp_bridge (nw_connection_create (ep, vxp_params ()), NO); if (!b) return 0;
	struct sockaddr_in me; socklen_t l = sizeof me; getsockname (b->fd, (struct sockaddr *) &me, &l);
	vxp_log ([NSString stringWithFormat:@"joining \"%@\" through 127.0.0.1:%d", name, ntohs (me.sin_port)]);
	return ntohs (me.sin_port);
}
