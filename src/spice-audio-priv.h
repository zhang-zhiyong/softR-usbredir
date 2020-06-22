/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010 Red Hat, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __SPICE_AUDIO_PRIVATE_H__
#define __SPICE_AUDIO_PRIVATE_H__

#include <glib.h>
#include <gio/gio.h>
#include "spice-session.h"

G_BEGIN_DECLS

struct _SpiceAudioPrivate {
    SpiceSession            *session;
    GMainContext            *main_context;
};

SpiceAudio *spice_audio_new_priv(SpiceSession *session, GMainContext *context,
                                 const char *name);

void spice_audio_get_playback_volume_info_async(SpiceAudio *audio, GCancellable *cancellable,
        SpiceMainChannel *main_channel, GAsyncReadyCallback callback, gpointer user_data);
gboolean spice_audio_get_playback_volume_info_finish(SpiceAudio *audio, GAsyncResult *res,
        gboolean *mute, guint8 *nchannels, guint16 **volume, GError **error);
void spice_audio_get_record_volume_info_async(SpiceAudio *audio, GCancellable *cancellable,
        SpiceMainChannel *main_channel, GAsyncReadyCallback callback, gpointer user_data);
gboolean spice_audio_get_record_volume_info_finish(SpiceAudio *audio, GAsyncResult *res,
        gboolean *mute, guint8 *nchannels, guint16 **volume, GError **error);
G_END_DECLS

#endif /* __SPICE_AUDIO_PRIVATE_H__ */
