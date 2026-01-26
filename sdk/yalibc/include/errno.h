/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ERRNO_H
#define _ERRNO_H

#include <features.h>

#define __STDC_VERSION_ERRNO_H__ 202311L

/* Provided by the platform layer.
 * Returns a pointer to the current hardware thread's errno variable. */
int *__errno_location(void);
#define errno (*__errno_location())

/* C23 required errno values (7.5 Errors <errno.h>)
 * Only EDOM, EILSEQ, and ERANGE are required by C23.
 * All values match Linux/POSIX for consistency. */

/* C23 required errors */
#define EDOM		33	/* Math argument out of domain of func */
#define EILSEQ		84	/* Illegal byte sequence */
#define ERANGE		34	/* Math result not representable */

/* Additional errors used in BareMetal codebase */

/* Argument errors */
#define EINVAL		22	/* Invalid argument */
#define ENAMETOOLONG	36	/* File name too long */

/* Memory errors */
#define ENOMEM		12	/* Out of memory */
#define ENOSPC		28	/* No space left on device */

/* Device/resource errors */
#define ENODEV		19	/* No such device */
#define ENOSYS		38	/* Function not implemented */
#define ENOTSUP		95	/* Not supported */
#define EBUSY		16	/* Device or resource busy */

/* I/O errors */
#define EIO		 5	/* I/O error */
#define ETIME		62	/* Timer expired */
#define EAGAIN		11	/* Try again */

/* Network/protocol errors */
#define EPROTO		71	/* Protocol error */
#define EBADMSG		74	/* Not a data message */
#define EMSGSIZE	90	/* Message too long */
#define ENOMSG		42	/* No message of desired type */
#define EOVERFLOW	75	/* Value too large for defined data type */
#define ENODATA		61	/* No data available */
#define ENOBUFS		105	/* No buffer space available */

/* Network address errors */
#define EADDRINUSE	98	/* Address already in use */
#define EADDRNOTAVAIL	99	/* Cannot assign requested address */
#define EDESTADDRREQ	89	/* Destination address required */
#define EHOSTDOWN	112	/* Host is down */
#define EHOSTUNREACH	113	/* No route to host */

/* Connection errors */
#define ECONNRESET	104	/* Connection reset by peer */

#endif /* _ERRNO_H */
