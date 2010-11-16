/*
 * Copyright (c) 2010 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>

#include <dect/libdect.h>
#include <dect/raw.h>
#include <dectmon.h>

#define DECT_MAX_CLUSTERS	16
#define DECT_LOCK_TIMEOUT	15

#define cluster_log(priv, fmt, args...) \
	printf("%s: " fmt, \
	       ((struct dect_handle_priv *)dect_handle_priv(dh))->cluster, \
	       ## args)

static LIST_HEAD(dect_handles);
static unsigned int locked;

static struct dect_handle_priv *dect_handle_lookup(const struct dect_ari *pari)
{
	struct dect_handle_priv *priv;

	list_for_each_entry(priv, &dect_handles, list)
		if (!dect_ari_cmp(pari, &priv->pari))
			return priv;
	return NULL;
}

static void dect_lock_timer(struct dect_handle *dh, struct dect_timer *timer)
{
	struct dect_handle_priv *priv = dect_handle_priv(dh);

	cluster_log(dh, "timeout, lock failed\n");
	memset(&priv->pari, 0, sizeof(priv->pari));
	dect_llme_scan_req(dh);
}

static void dect_mac_me_info_ind(struct dect_handle *dh,
				 const struct dect_ari *pari,
				 const struct dect_fp_capabilities *fpc)
{
	struct dect_handle_priv *priv = dect_handle_priv(dh);

	if (pari != NULL) {
		if (dect_handle_lookup(pari) == NULL) {
			cluster_log(dh, "MAC_ME_INFO-ind: EMC: %.4x FPN: %.5x\n",
				    pari->emc, pari->fpn);

			dect_llme_mac_me_info_res(dh, pari);
			priv->pari = *pari;
			dect_timer_start(dh, priv->lock_timer, DECT_LOCK_TIMEOUT);
		}
	} else if (fpc->fpc != 0) {
		if (dect_timer_running(priv->lock_timer)) {
			locked++;
			cluster_log(dh, "locked (%u): EMC: %.4x FPN: %.5x\n",
				    locked, priv->pari.emc, priv->pari.fpn);

			dect_timer_stop(dh, priv->lock_timer);
		}
	} else {
		locked--;
		cluster_log(dh, "unlocked (%u): EMC: %.4x FPN: %.5x\n",
			    locked, priv->pari.emc, priv->pari.fpn);

		memset(&priv->pari, 0, sizeof(priv->pari));
		dect_llme_scan_req(dh);
	}
}

static struct dect_llme_ops_ llme_ops = {
	.mac_me_info_ind	= dect_mac_me_info_ind,
};

static void dect_raw_rcv(struct dect_handle *dh, struct dect_fd *dfd,
			 struct dect_msg_buf *mb)
{
	dect_mac_rcv(dh, mb);
}

static struct dect_raw_ops raw_ops = {
	.raw_rcv		= dect_raw_rcv,
};

static struct dect_ops ops = {
	.priv_size		= sizeof(struct dect_handle_priv),
	.llme_ops		= &llme_ops,
	.raw_ops		= &raw_ops,
};

#define OPTSTRING "c:sm:d:n:p:h"

enum {
	OPT_CLUSTER	= 'c',
	OPT_SCAN	= 's',
	OPT_DUMP_MAC	= 'm',
	OPT_DUMP_DLC	= 'd',
	OPT_DUMP_NWK	= 'n',
	OPT_AUTH_PIN	= 'p',
	OPT_HELP	= 'h',
};

static const struct option dectmon_opts[] = {
	{ .name = "cluster",  .has_arg = true,	.flag = 0, .val = OPT_CLUSTER, },
	{ .name = "scan",     .has_arg = false, .flag = 0, .val = OPT_SCAN, },
	{ .name = "dump-mac", .has_arg = true,  .flag = 0, .val = OPT_DUMP_MAC, },
	{ .name = "dump-dlc", .has_arg = true,  .flag = 0, .val = OPT_DUMP_DLC, },
	{ .name = "dump-nwk", .has_arg = true,  .flag = 0, .val = OPT_DUMP_NWK, },
	{ .name = "auth-pin", .has_arg = true,  .flag = 0, .val = OPT_AUTH_PIN, },
	{ .name = "help",     .has_arg = false, .flag = 0, .val = OPT_HELP, },
	{ },
};

static void pexit(const char *str)
{
	perror(str);
	exit(1);
}

static void dectmon_help(const char *progname)
{
	printf("%s [ options ]\n"
	       "\n"
	       "Options:\n"
	       "  -c/--cluster=NAME		Bind to cluster NAME. May be specified more than once.\n"
	       "  -s/--scan			Scan for FPs and lock dynamically\n"
	       "  -m/--dump-mac=yes/no		Dump MAC layer messages (default: no)\n"
	       "  -d/--dump-dlc=yes/no		Dump DLC layer messages (default: no)\n"
	       "  -n/--dump-nwk=yes/no		Dump NWK layer messages (default: yes)\n"
	       "  -p/--auth-pin=PIN		Authentication PIN for Key Allocation\n"
	       "  -h/--help			Show this help text\n"
	       "\n",
	       progname);
}

static uint32_t opt_yesno(const char *arg, uint32_t opts, uint32_t flag)
{
	if (!strcmp(arg, "yes"))
		opts |= flag;
	else if (!strcmp(arg, "no"))
		opts &= ~flag;
	else
		pexit("invalid argument\n");

	return opts;
}

const char *auth_pin = "0000";
uint32_t dumpopts = DECTMON_DUMP_NWK;

static struct dect_handle *dectmon_open_handle(struct dect_ops *ops,
					       const char *cluster)
{
	struct dect_handle_priv *priv;
	struct dect_handle *dh;

	dh = dect_open_handle(ops, cluster);
	if (dh == NULL)
		pexit("dect_open_handle");

	priv = dect_handle_priv(dh);
	priv->cluster = cluster;

	priv->lock_timer = dect_timer_alloc(dh);
	if (priv->lock_timer == NULL)
		pexit("dect_alloc_timer");
	dect_timer_setup(priv->lock_timer, dect_lock_timer, priv);

	init_list_head(&priv->pt_list);
	list_add_tail(&priv->list, &dect_handles);

	return dh;
}

int main(int argc, char **argv)
{
	const char *cluster[DECT_MAX_CLUSTERS] = {};
	unsigned int ncluster = 0, i;
	struct dect_handle *dh;
	struct dect_fd *dfd;
	bool scan = false;
	int optidx = 0, c;

	for (;;) {
		c = getopt_long(argc, argv, OPTSTRING, dectmon_opts, &optidx);
		if (c == -1)
			break;

		switch (c) {
		case OPT_CLUSTER:
			cluster[ncluster++] = optarg;
			break;
		case OPT_SCAN:
			scan = true;
			break;
		case OPT_DUMP_MAC:
			dumpopts = opt_yesno(optarg, dumpopts, DECTMON_DUMP_MAC);
			break;
		case OPT_DUMP_DLC:
			dumpopts = opt_yesno(optarg, dumpopts, DECTMON_DUMP_DLC);
			break;
		case OPT_DUMP_NWK:
			dumpopts = opt_yesno(optarg, dumpopts, DECTMON_DUMP_NWK);
			break;
		case OPT_AUTH_PIN:
			auth_pin = optarg;
			break;
		case OPT_HELP:
			dectmon_help(argv[0]);
			exit(0);
		case '?':
			dectmon_help(argv[0]);
			exit(1);
		}
	}

	dect_event_ops_init(&ops);
	dect_dummy_ops_init(&ops);

	if (ncluster == 0)
		ncluster = 1;

	for (i = 0; i < ncluster; i++) {
		dh = dectmon_open_handle(&ops, cluster[i]);

		dfd = dect_raw_socket(dh);
		if (dfd == NULL)
			pexit("dect_raw_socket");

		if (scan)
			dect_llme_scan_req(dh);
	}

	dect_event_loop();
	return 0;
}
