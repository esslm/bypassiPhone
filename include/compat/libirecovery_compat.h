#ifndef TP_LIBIRECOVERY_COMPAT_H
#define TP_LIBIRECOVERY_COMPAT_H

/*
 * libirecovery_compat.h -- libirecovery API drift shim.
 *
 * Older libirecovery releases lacked IRECV_SEND_OPT_DFU_NOTIFY_FINISH,
 * the send-flag that instructs irecv_send_file() to terminate a DFU
 * transfer with a zero-length packet. The Makefile probes for the
 * symbol and passes -DTP_IRECV_HAS_NOTIFY_FINISH when present.
 *
 * When absent we provide a no-op flag value and set
 * TP_IRECV_NEEDS_MANUAL_ZLP=1 so callers can send the ZLP by hand
 * after irecv_send_file() returns.
 */

#include <libirecovery.h>

#ifdef TP_IRECV_HAS_NOTIFY_FINISH
    #define TP_IRECV_NEEDS_MANUAL_ZLP 0
#else
    #ifndef IRECV_SEND_OPT_DFU_NOTIFY_FINISH
        #define IRECV_SEND_OPT_DFU_NOTIFY_FINISH 0
    #endif
    #define TP_IRECV_NEEDS_MANUAL_ZLP 1
#endif

#endif /* TP_LIBIRECOVERY_COMPAT_H */
