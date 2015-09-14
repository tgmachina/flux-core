#ifndef _BROKER_HEARTBEAT_H
#define _BROKER_HEARTBEAT_H

/* Manage the session heartbeat.
 *
 * Rank 0 should call heartbeat_start() to begin sending heartbeat events.
 * This registers a reactor timer watcher.
 *
 * On all ranks, "hb" event messages should be passed to heartbeat_recvmsg(),
 * which will decode the received epoch, set it internally, and call the
 * heartbeat_cb_f, if any.
 *
 * The heartbeat_get_epoch() getter obtains the most recently processed epoch.
 *
 * Note: rank 0's epoch update and callback are driven by the receipt of the
 * heartbeat event, not its generation.
 */

typedef struct heartbeat_struct heartbeat_t;
typedef void (*heartbeat_cb_f)(heartbeat_t *hb, void *arg);

heartbeat_t *heartbeat_create (void);
void heartbeat_destroy (heartbeat_t *hb);

/* Default heart rate (seconds) can be set from a command line argument
 * that includes an optional "s" or "ms" unit suffix.
 * Returns -1, EINVAL if rate is out of range (0.1, 30).
 */
int heartbeat_set_ratestr (heartbeat_t *hb, const char *s);

int heartbeat_set_rate (heartbeat_t *hb, double rate);
double heartbeat_get_rate (heartbeat_t *hb);

void heartbeat_set_flux (heartbeat_t *hb, flux_t h);
void heartbeat_set_callback (heartbeat_t *hb, heartbeat_cb_f cb, void *arg);

void heartbeat_set_epoch (heartbeat_t *hb, int epoch);
int heartbeat_get_epoch (heartbeat_t *hb);

int heartbeat_start (heartbeat_t *hb); /* rank 0 only */
void heartbeat_stop (heartbeat_t *hb);

int heartbeat_recvmsg (heartbeat_t *hb, const flux_msg_t *msg);

#endif /* !_BROKER_HEARTBEAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */