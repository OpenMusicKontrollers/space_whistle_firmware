/*
 * Copyright (c) 2014 Hanspeter Portner (dev@open-music-kontrollers.ch)
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 
 *     1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 * 
 *     2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 * 
 *     3. This notice may not be removed or altered from any source
 *     distribution.
 */

#include <string.h>

#include <libmaple/systick.h>

#include "ptp_private.h"

#include <chimaera.h>
#include <chimutil.h>
#include <config.h>
#include <debug.h>

static fix_32_32_t t0 = 0.0ULLK;

static fix_32_32_t t1, t2, t3, t4;
static fix_32_32_t c1, c2, c3;

static fix_32_32_t ta, tb, tc, td;
static fix_32_32_t ca, cb, cc;

static fix_32_32_t D0, D1, DD0 = 0.0002ULLK, DD1 = 0.0002ULLK; // initial delay of 200us
static fix_s31_32_t O0, O1, OO0, OO1;
static fix_s31_32_t Os;
static fix_32_32_t Ds;

static uint_fast8_t ptp_timescale = 0;
static uint16_t utc_offset;
static uint64_t master_clock_id;
static uint16_t sync_seq_id;
static uint16_t resp_seq_id;
static uint16_t req_seq_id = 0;
static uint16_t pdelay_req_seq_id = 0;
static uint_fast8_t sync_counter = 0;

void
ptp_reset()
{
	t0 = 0.0ULLK;
	D0 = D1 = DD0 = DD1 = 0.0002ULLK;
	O0 = O1 = OO0 = OO1 = 0.0ULLK;

	// initialize stiffness
	Os = 1.0LLK / (fix_s31_32_t)config.ptp.offset_stiffness;
	Ds = 1.0ULLK / (fix_32_32_t)config.ptp.delay_stiffness;
}

static inline __always_inline void
ptp_request_ntoh(PTP_Request *req)
{
	req->message_length = ntoh(req->message_length);
	req->flags = ntoh(req->flags);
	req->correction = ntohll(req->correction); //TODO use it
	req->clock_identity = ntohll(req->clock_identity);
	req->source_port_id = hton(req->source_port_id);
	req->sequence_id = hton(req->sequence_id);
	req->timestamp.epoch = hton(req->timestamp.epoch);
	req->timestamp.sec = htonl(req->timestamp.sec);
	req->timestamp.nsec = htonl(req->timestamp.nsec);
}

static inline __always_inline void
ptp_response_ntoh(PTP_Response *resp)
{
	resp->requesting_clock_identity = ntohll(resp->requesting_clock_identity);
	resp->requesting_source_port_id = ntoh(resp->requesting_source_port_id);
}

static inline __always_inline void
ptp_announce_ntoh(PTP_Announce *ann)
{
	ann->origin_current_utc_offset = ntoh(ann->origin_current_utc_offset);
	ann->grandmaster_clock_variance = ntoh(ann->grandmaster_clock_variance);
	ann->grandmaster_clock_identity = ntohll(ann->grandmaster_clock_identity);
	ann->local_steps_removed = ntoh(ann->local_steps_removed);
}

//#define JAN_1970 2208988800ULLK
#define JAN_1970 2208988800UL
#define NSEC 0.000000001ULLK
#define PTP_TO_FIX(ts) ((ts).sec + (ts).nsec * NSEC)
#define TICK_TO_FIX(tick) (t0 + (tick) * PTP_SYSTICK_DURATION)
#define FIX_TO_OSC(fix) ((fix) + JAN_1970 - (ptp_timescale ? utc_offset : 0))
#define SLAVE_CLOCK_ID (*(uint64_t *)UID_BASE)

static void
_ptp_update_offset()
{
	// offset = t2 - t1 - delay - correction1 - correction2

	if(t0 == 0ULLK)
		t0 = t1 + DD1 - t2;
	else
	{
		if(t2 > t1)
			O1 = (fix_s31_32_t)(t2 - t1) - (fix_s31_32_t)DD1 - c1 - c2;
		else
			O1 = -(fix_s31_32_t)(t1 - t2) - (fix_s31_32_t)DD1 - c1 - c2;
		OO1 = Os * (O0 + O1) / 2 + OO0 * (1LLK - Os);
		t0 -= OO1;

#if 0
DEBUG("tttt", O0, O1, OO0, OO1);
#endif

		O0 = O1;
		OO0 = OO1;
	}

#if 1
	float oo = OO1;
	float dd1 = DD1;
	DEBUG("ff", oo, dd1);
#endif

	// send delay request
	if(sync_counter++ >= config.ptp.multiplier)
	{
		uint16_t len = ptp_request_serialize(BUF_O_OFFSET(buf_o_ptr));
		udp_send(config.ptp.event.sock, BUF_O_BASE(buf_o_ptr), len); // port 319
		ptp_request_timestamp(systick_uptime());

		sync_counter = 0;
	}
}

static void
_ptp_update_delay_e2e()
{
	uint_fast8_t i;

	D1 = (t4 - t1) - (t3 - t2);
	D1 -= c1 + c2 + c3;
	D1 /= 2;

	DD1 = Ds * (D0 + D1) / 2 + DD0 * (1ULLK - Ds);

	D0 = D1;
	DD0 = DD1;

#if 0
	DEBUG("ttttt", t0, t1, t2, t3, t4);
#endif
}

#if 0 // we need two more sockets for P2P mode
static void
_ptp_update_delay_p2p()
{
	uint_fast8_t i;

	D1 = (tb - tc) * rr + (td - ta);
	D1 -= ca + cb + cc;
	D1 /= 2;

	DD1 = Ds * (D0 + D1) / 2 + DD0 * (1ULLK - Ds);

	D0 = D1;
	DD0 = DD1;
}
#endif

static void
_ptp_dispatch_announce(PTP_Announce *ann)
{
	master_clock_id = ann->req.clock_identity;
	utc_offset = ann->origin_current_utc_offset;
	//TODO
}

static void
_ptp_dispatch_sync(PTP_Sync *sync, uint32_t tick)
{
	if(master_clock_id != sync->clock_identity)
		return; // not our master

	t2 = TICK_TO_FIX(tick); // set receive timestamp
	c2 = (fix_32_32_t)sync->correction * (fix_32_32_t)0x0.0001p0;
	
	ptp_timescale = sync->flags & PTP_FLAGS_TIMESCALE; // are we using PTP timescale?

	if(sync->flags & PTP_FLAGS_TWO_STEP)
		sync_seq_id = sync->sequence_id; // wait for follow_up message
	else
	{
		t1 = PTP_TO_FIX(sync->timestamp);
		c1 = 0ULLK;
		_ptp_update_offset();
	}
}

static void
_ptp_dispatch_follow_up(PTP_Follow_Up *folup)
{
	if(master_clock_id != folup->clock_identity)
		return; // not our master

	if(sync_seq_id != folup->sequence_id)
		return; // does not match previously received sync packet

	t1 = PTP_TO_FIX(folup->timestamp);
	c1 = (fix_32_32_t)folup->correction * (fix_32_32_t)0x0.0001p0;
	_ptp_update_offset();
}

static void
_ptp_dispatch_delay_resp(PTP_Response *resp)
{
	if(master_clock_id != resp->req.clock_identity)
		return; // not our master

	if(req_seq_id != resp->req.sequence_id)
		return; // not response for us

	if(SLAVE_CLOCK_ID != resp->requesting_clock_identity)
		return;

	t4 = PTP_TO_FIX(resp->req.timestamp);
	c3 = (fix_32_32_t)resp->req.correction * (fix_32_32_t)0x0.0001p0;
	_ptp_update_delay_e2e();
}

#if 0 // we need two more sockets for P2P mode
static void
_ptp_dispatch_pdelay_req(PTP_Request *req, uint32_t tick)
{
	static PTP_Response resp;
	uint8_t *offset = BUF_O_OFFSET(buf_o_ptr);
	uint8_t *base = BUF_O_BASE(buf_o_ptr);

	if(master_clock_id != req->clock_identity)
		return;

	// PEER DELAY RESPONSE
	fix_32_32_t tx = TICK_TO_FIX(tick);

	resp.req.message_id = PTP_MESSAGE_ID_PDELAY_RESP;
	resp.req.version = PTP_VERSION_2;
	resp.req.message_length = sizeof(PTP_Response);
	resp.req.flags = PTP_FLAGS_TWO_STEP;
	resp.req.correction = 0ULL;
	resp.req.clock_identity = SLAVE_CLOCK_ID;
	resp.req.source_port_id = 1U;
	resp.req.sequence_id = req->sequence_id;
	resp.req.control = PTP_CONTROL_OTHER;
	resp.req.log_message_interval = 0x7f;

	resp.req.timestamp.epoch = 0U;
	resp.req.timestamp.sec = (uint32_t)tx;
	resp.req.timestamp.nsec = (tx - resp.req.timestamp.sec) * 1000000000;

	resp.requesting_clock_identity = req->clock_identity;
	resp.requesting_source_port_id = req->source_port_id;

	memcpy(offset, &resp, sizeof(PTP_Response));
	ptp_request_ntoh((PTP_Request *)offset);
	ptp_response_ntoh((PTP_Response *)offset);
	udp_send(config.ptp.event.sock, base, sizeof(PTP_Response)); // port 319

	// PEER DELAY RESPONSE FOLLOW UP
	fix_32_32_t ty = TICK_TO_FIX(systick_uptime());

	resp.req.message_id = PTP_MESSAGE_ID_PDELAY_RESP_FOLLOW_UP;
	resp.req.flags = 0U;
	resp.req.timestamp.sec = (uint32_t)ty;
	resp.req.timestamp.nsec = (ty - resp.req.timestamp.sec) * 1000000000;

	memcpy(offset, &resp, sizeof(PTP_Response));
	ptp_request_ntoh((PTP_Request *)offset);
	ptp_response_ntoh((PTP_Response *)offset);
	udp_send(config.ptp.general.sock, base, sizeof(PTP_Response)); // port 320

	// PEER DELAY REQUEST
	resp.req.message_id = PTP_MESSAGE_ID_PDELAY_REQ;
	resp.req.message_length = sizeof(PTP_Request);
	resp.req.sequence_id = ++pdelay_req_seq_id;
	resp.req.timestamp.sec = 0UL;
	resp.req.timestamp.nsec = 0UL;

	ptp_request_ntoh(&resp.req);

	memcpy(offset, &resp, sizeof(PTP_Request));
	ptp_request_ntoh((PTP_Request *)offset);
	udp_send(config.ptp.event.sock, base, sizeof(PTP_Response)); // port 319

	ta = TICK_TO_FIX(systick_uptime());
	cb = (fix_32_32_t)req->correction * (fix_32_32_t)0x0.0001p0;
}

static void
_ptp_dispatch_pdelay_resp(PTP_Response *resp, uint32_t tick)
{
	if(master_clock_id != resp->req.clock_identity)
		return;

	if(pdelay_req_seq_id != resp->req.sequence_id)
		return;

	if(SLAVE_CLOCK_ID != resp->requesting_clock_identity)
		return;

	tb = PTP_TO_FIX(resp->req.timestamp);
	cb = (fix_32_32_t)resp->req.correction * (fix_32_32_t)0x0.0001p0;
	tc = TICK_TO_FIX(tick);
	// now wait for follow up
}

static void
_ptp_dispatch_pdelay_resp_follow_up(PTP_Response *resp)
{
	if(master_clock_id != resp->req.clock_identity)
		return;

	if(pdelay_req_seq_id != resp->req.sequence_id)
		return;

	if(SLAVE_CLOCK_ID != resp->requesting_clock_identity)
		return;

	td = PTP_TO_FIX(resp->req.timestamp);
	cc = (fix_32_32_t)resp->req.correction * (fix_32_32_t)0x0.0001p0;
	//TODO update roundtrip delay
}
#endif

void
ptp_timestamp_refresh(uint32_t tick, nOSC_Timestamp *now, nOSC_Timestamp *offset)
{
	fix_32_32_t _now;
	
	_now = TICK_TO_FIX(tick);
	*now = FIX_TO_OSC(_now);

	if(offset)
	{
		if(config.output.offset > 0ULLK)
			*offset = *now + config.output.offset;
		else
			*offset = nOSC_IMMEDIATE;
	}
}

uint16_t
ptp_request_serialize(uint8_t *buf)
{
	static PTP_Request req;

	req.message_id = PTP_MESSAGE_ID_DELAY_REQ;
	req.version = PTP_VERSION_2;
	req.message_length = sizeof(PTP_Request);
	req.flags = 0U;
	req.correction = 0ULL;
	req.clock_identity = SLAVE_CLOCK_ID;
	req.source_port_id = 1U;
	req.sequence_id = ++req_seq_id;
	req.control = PTP_CONTROL_DELAY_REQ;
	req.log_message_interval = 0x7f;
	req.timestamp.epoch = 0U;
	req.timestamp.sec = 0UL;
	req.timestamp.nsec = 0UL;

	ptp_request_ntoh(&req);
	memcpy(buf, &req, sizeof(PTP_Request));

	return sizeof(PTP_Request);
}

void
ptp_request_timestamp(uint32_t tick)
{
	t3 = TICK_TO_FIX(tick);
}

void
ptp_dispatch(uint8_t *buf, uint32_t tick)
{
	PTP_Request *msg = (PTP_Request *)buf;

	// byte swap short message part
	ptp_request_ntoh(msg);

	// check for PTP version 2
	if(msg->version & 0xf != PTP_VERSION_2)
		return;

	// byte swap extended message part
	if(msg->message_length == sizeof(PTP_Response))
		ptp_response_ntoh((PTP_Response *)msg);
	else if(msg->message_length == sizeof(PTP_Announce))
		ptp_announce_ntoh((PTP_Announce *)msg);

	PTP_Message_ID msg_id = msg->message_id & 0xf;
	switch(msg_id)
	{
		// event port
		case PTP_MESSAGE_ID_SYNC:
			_ptp_dispatch_sync(msg, tick);
			break;
		case PTP_MESSAGE_ID_DELAY_REQ:
			// we only act as PTP slave, not as master
			break;
		case PTP_MESSAGE_ID_PDELAY_REQ:
			// we need two more sockets for P2P mode
			//_ptp_dispatch_pdelay_req(msg, tick);
			break;

		// general port
		case PTP_MESSAGE_ID_PDELAY_RESP:
			// we need two more sockets for P2P mode
			//_ptp_dispatch_pdelay_resp((PTP_Response *)msg, tick);
			break;
		case PTP_MESSAGE_ID_FOLLOW_UP:
			_ptp_dispatch_follow_up(msg);
			break;
		case PTP_MESSAGE_ID_DELAY_RESP:
			_ptp_dispatch_delay_resp((PTP_Response *)msg);
			break;
		case PTP_MESSAGE_ID_PDELAY_RESP_FOLLOW_UP:
			// we need two more sockets for P2P mode
			//_ptp_dispatch_pdelay_resp_follow_up((PTP_Response *)msg);
			break;
		case PTP_MESSAGE_ID_ANNOUNCE:
			_ptp_dispatch_announce((PTP_Announce *)msg);
			break;
		case PTP_MESSAGE_ID_SIGNALING:
			// ignored for now
			break;
		case PTP_MESSAGE_ID_MANAGEMENT:
			// ignored for now
			break;
	}
}

/*
 * Config
 */

static uint_fast8_t
_ptp_enabled(const char *path, const char *fmt, uint_fast8_t argc, nOSC_Arg *args)
{
	return config_socket_enabled(&config.ptp.event, path, fmt, argc, args);
}

static uint_fast8_t
_ptp_multiplier(const char *path, const char *fmt, uint_fast8_t argc, nOSC_Arg *args)
{
	return config_check_uint8(path, fmt, argc, args, &config.ptp.multiplier);
}

static uint_fast8_t
_ptp_offset_stiffness(const char *path, const char *fmt, uint_fast8_t argc, nOSC_Arg *args)
{
	uint_fast8_t res = config_check_uint8(path, fmt, argc, args, &config.ptp.offset_stiffness);
	Os = 1.0LLK / (fix_32_32_t)config.ptp.offset_stiffness;
	return res;
}

static uint_fast8_t
_ptp_delay_stiffness(const char *path, const char *fmt, uint_fast8_t argc, nOSC_Arg *args)
{
	uint_fast8_t res = config_check_uint8(path, fmt, argc, args, &config.ptp.delay_stiffness);
	Ds = 1.0ULLK / (fix_32_32_t)config.ptp.delay_stiffness;
	//FIXME ptp_init()
	return res;
}

static uint_fast8_t
_ptp_offset(const char *path, const char *fmt, uint_fast8_t argc, nOSC_Arg *args)
{
	uint16_t size;
	int32_t uuid = args[0].i;

	float offset = OO1;
	size = CONFIG_SUCCESS("isf", uuid, path, offset);

	CONFIG_SEND(size);

	return 1;
}

static uint_fast8_t
_ptp_delay(const char *path, const char *fmt, uint_fast8_t argc, nOSC_Arg *args)
{
	uint16_t size;
	int32_t uuid = args[0].i;

	float delay = DD1;
	size = CONFIG_SUCCESS("isf", uuid, path, delay);

	CONFIG_SEND(size);

	return 1;
}

/*
 * Query
 */

static const nOSC_Query_Argument ptp_multiplier_args [] = {
	nOSC_QUERY_ARGUMENT_INT32("Rate", nOSC_QUERY_MODE_RW, 1, 16)
};

static const nOSC_Query_Argument ptp_stiffness_args [] = {
	nOSC_QUERY_ARGUMENT_INT32("Stiffness", nOSC_QUERY_MODE_RW, 1, 64)
};

static const nOSC_Query_Argument ptp_offset_args [] = {
	nOSC_QUERY_ARGUMENT_FLOAT("Seconds", nOSC_QUERY_MODE_R, -INFINITY, INFINITY)
};

static const nOSC_Query_Argument ptp_delay_args [] = {
	nOSC_QUERY_ARGUMENT_FLOAT("Seconds", nOSC_QUERY_MODE_R, 0.f, INFINITY)
};

const nOSC_Query_Item ptp_tree [] = {
	// read-write
	nOSC_QUERY_ITEM_METHOD("enabled", "Enable/disable", _ptp_enabled, config_boolean_args),
	nOSC_QUERY_ITEM_METHOD("multiplier", "Delay request rate multiplier", _ptp_multiplier, ptp_multiplier_args),
	nOSC_QUERY_ITEM_METHOD("offset_stiffness", "Stiffness of offset filter", _ptp_offset_stiffness, ptp_stiffness_args),
	nOSC_QUERY_ITEM_METHOD("delay_stiffness", "Stiffness of delay filter", _ptp_delay_stiffness, ptp_stiffness_args),

	// read-only
	nOSC_QUERY_ITEM_METHOD("offset", "Sync offset", _ptp_offset, ptp_offset_args),
	nOSC_QUERY_ITEM_METHOD("delay", "Sync roundtrip delay", _ptp_delay, ptp_delay_args)
};