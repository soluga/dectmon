#ifndef _DECTMON_H
#define _DECTMON_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include <dect/libdect.h>
#include <dect/timer.h>
#include <dect/auth.h>
#include <phl.h>

enum {
	DECTMON_DUMP_MAC	= 0x1,
	DECTMON_DUMP_DLC	= 0x2,
	DECTMON_DUMP_NWK	= 0x4,
};

extern const char *auth_pin;
extern uint32_t dumpopts;
extern uint32_t debug_mask;

struct dect_ops;
extern int dect_event_ops_init(struct dect_ops *ops);
extern void dect_event_loop_stop(void);
extern void dect_event_loop(void);
extern void dect_event_ops_cleanup(void);
extern void dect_dummy_ops_init(struct dect_ops *ops);

extern void dectmon_log(const char *fmt, ...);
extern void dect_hexdump(const char *prefix, const uint8_t *buf, size_t size);

extern struct list_head dect_handles;

struct dect_handle_priv {
	struct list_head			list;
	const char				*cluster;
	struct dect_handle			*dh;

	struct dect_timer			*lock_timer;
	bool					locked;
	struct dect_ari				pari;

	struct list_head			pt_list;
	struct dect_tbc				*slots[DECT_FRAME_SIZE];
};

enum dect_mm_procedures {
	DECT_MM_NONE,
	DECT_MM_KEY_ALLOCATION,
	DECT_MM_AUTHENTICATION,
	DECT_MM_CIPHERING,
};

struct dect_pt {
	struct list_head			list;
	struct dect_ie_portable_identity	*portable_identity;
	struct dect_dl				*dl;

	uint8_t					uak[DECT_AUTH_KEY_LEN];
	uint8_t					dck[DECT_CIPHER_KEY_LEN];

	struct dect_audio_handle		*ah;

	enum dect_mm_procedures			procedure;
	uint8_t					last_msg;

	struct dect_ie_auth_type		*auth_type;
	struct dect_ie_auth_value		*rand_f;
	struct dect_ie_auth_value		*rs;
	struct dect_ie_auth_res			*res;
};

/* DLC */

struct dect_dl {
	struct dect_pt				*pt;
	struct dect_tbc				*tbc;
};

struct dect_msg_buf;
extern void dect_dl_data_ind(struct dect_handle *dh, struct dect_dl *dl,
			     struct dect_msg_buf *mb);

extern void dect_dl_u_data_ind(struct dect_handle *dh, struct dect_dl *dl,
			       bool dir, struct dect_msg_buf *mb);

struct dect_lc {
	uint16_t				lsig;
	struct dect_msg_buf			*rx_buf;
	uint8_t					rx_len;
};

struct dect_mac_con {
	struct dect_lc				*lc;
	struct dect_tbc				*tbc;
};

enum dect_data_channels;
extern void dect_mac_co_data_ind(struct dect_handle *dh,
				 struct dect_mac_con *mc,
				 enum dect_data_channels chan,
				 struct dect_msg_buf *mb);

/* MAC */

struct dect_mbc {
	bool					cs_seq;
	bool					cf_seq;
	struct dect_mac_con			mc;
};

struct dect_tbc {
	uint8_t					slot1;
	uint8_t					slot2;

	uint16_t				fmid;
	uint32_t				pmid;

	struct dect_timer			*timer;
	struct dect_mbc				mbc[2];

	bool					ciphered;
	uint8_t					ks[2 * 45];

	struct dect_dl				dl;
};

extern void dect_mac_rcv(struct dect_handle *dh, struct dect_msg_buf *mb);

/* DSC */

extern void dect_dsc_keystream(uint64_t iv, const uint8_t *key,
			       uint8_t *output, unsigned int len);
extern uint64_t dect_dsc_iv(uint32_t mfn, uint8_t framenum);

/* Audio */

#include "../src/ccitt-adpcm/g72x.h"

struct dect_audio_handle {
	struct g72x_state	codec[2];
	struct dect_msg_buf	*queue[2];
};

extern int dect_audio_init(void);
extern struct dect_audio_handle *dect_audio_open(void);
extern void dect_audio_close(struct dect_audio_handle *ah);
extern void dect_audio_queue(struct dect_audio_handle *ah, unsigned int queue,
			     struct dect_msg_buf *mb);

/* Raw dump */

struct dect_raw_frame_hdr {
	uint8_t		len;
	uint8_t		slot;
	uint8_t		frame;
	uint8_t		pad;
	uint32_t	mfn;
};

#endif /* _DECTMON_H */
