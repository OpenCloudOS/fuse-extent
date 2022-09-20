#ifndef _FUSE_SIGNAL_H
#define _FUSE_SIGNAL_H

#include "fuse_log.h"
#include "fuse_session.h"

#include <stdlib.h>
#include <signal.h>

int fuse_set_signal_handlers(struct fuse_session *se);

void fuse_remove_signal_handlers(struct fuse_session *se);

int fuse_set_signal_ignore();

void fuse_remove_signal_ignore();

#endif