/*
 * producer_molten.c -- an MLT producer over a .mltn Molten recording.
 *
 * Structurally this is producer_colour.c's shape (MLT's own from-scratch
 * pixel-generating producer, src/modules/core/producer_colour.c): no
 * wrapped inner producer, get_frame stacks a get_image callback that
 * allocates and fills a fresh RGBA buffer per frame.
 *
 * What actually produces the pixels is one call into libmolten_ffi's
 * session API (molten_session_render_at) per frame - frame number
 * converted to milliseconds is the whole of what this file computes. No
 * .mltn framing, no chunk parsing, no seeking logic lives here; that is
 * deliberate, see molten-ffi's own doc comment on molten_session_render_at
 * and the molten repository's README on why.
 */

#include <framework/mlt_frame.h>
#include <framework/mlt_log.h>
#include <framework/mlt_pool.h>
#include <framework/mlt_producer.h>
#include <framework/mlt_profile.h>

#include <molten.h>

#include <stdlib.h>
#include <string.h>

static int producer_get_image(mlt_frame frame,
                               uint8_t **buffer,
                               mlt_image_format *format,
                               int *width,
                               int *height,
                               int writable)
{
    (void) writable;

    mlt_producer producer = mlt_frame_pop_service(frame);
    mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
    MoltenSession *session = mlt_properties_get_data(properties, "molten_session", NULL);

    mlt_position position = mlt_frame_get_position(frame);
    double fps = mlt_producer_get_fps(producer);
    uint64_t timestamp_ms = (uint64_t) ((double) position * 1000.0 / fps);

    const uint8_t *out_ptr = NULL;
    uint32_t out_width = 0;
    uint32_t out_height = 0;
    enum MoltenStatus status
        = molten_session_render_at(session, timestamp_ms, &out_ptr, &out_width, &out_height);

    if (status != MOLTEN_STATUS_OK || out_ptr == NULL) {
        mlt_log_error(MLT_PRODUCER_SERVICE(producer),
                      "molten: render_at(%llu) failed, status=%d\n",
                      (unsigned long long) timestamp_ms,
                      (int) status);
        return 1;
    }

    /* Whole-number nearest-neighbour magnification, same semantic as
     * molten's own CLI `-scale` - a canvas is pixel art, and smooth
     * interpolation would blur the one thing worth keeping sharp. Clamped
     * to >= 1 here rather than trusted, since this property is directly
     * user-editable. */
    int scale = mlt_properties_get_int(properties, "scale");
    if (scale < 1)
        scale = 1;

    *format = mlt_image_rgba;
    *width = (int) out_width * scale;
    *height = (int) out_height * scale;

    int size = (*width) * (*height) * 4;
    int alpha_size = (*width) * (*height);

    /* molten_session_render_at's buffer belongs to the session and is only
     * valid until the next call that touches it - copy (and, here, expand)
     * immediately, per its own doc comment. */
    uint8_t *image = mlt_pool_alloc(size);
    if (scale == 1) {
        memcpy(image, out_ptr, size);
    } else {
        for (int y = 0; y < *height; y++) {
            const uint8_t *src_row = out_ptr + (size_t) (y / scale) * out_width * 4;
            uint8_t *dst_row = image + (size_t) y * (*width) * 4;
            for (int x = 0; x < *width; x++)
                memcpy(dst_row + (size_t) x * 4, src_row + (size_t) (x / scale) * 4, 4);
        }
    }

    /* The canvas is always fully opaque - see molten-ffi's own
     * MoltenPixel/State: nothing here ever produces a transparent pixel. */
    uint8_t *alpha = mlt_pool_alloc(alpha_size);
    memset(alpha, 255, alpha_size);

    *buffer = image;
    mlt_frame_set_image(frame, image, size, mlt_pool_release);
    mlt_frame_set_alpha(frame, alpha, alpha_size, mlt_pool_release);

    mlt_properties frame_properties = MLT_FRAME_PROPERTIES(frame);
    mlt_properties_set_int(frame_properties, "meta.media.width", *width);
    mlt_properties_set_int(frame_properties, "meta.media.height", *height);

    return 0;
}

static int producer_get_frame(mlt_producer producer, mlt_frame_ptr frame, int index)
{
    (void) index;

    *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));

    if (*frame != NULL) {
        mlt_properties properties = MLT_FRAME_PROPERTIES(*frame);
        mlt_profile profile = mlt_service_profile(MLT_PRODUCER_SERVICE(producer));

        mlt_frame_set_position(*frame, mlt_producer_position(producer));

        mlt_properties_set_int(properties, "progressive", 1);
        mlt_properties_set_double(properties, "aspect_ratio", mlt_profile_sar(profile));
        mlt_properties_set_int(properties, "format", mlt_image_rgba);

        /* A resize mid-recording is a new segment, not a new frame size
         * within this one - see the format's README on why a resize cuts
         * segments. A single producer instance here covers one window. */
        mlt_frame_push_service(*frame, producer);
        mlt_frame_push_get_image(*frame, producer_get_image);
    }

    mlt_producer_prepare_next(producer);

    return 0;
}

static void producer_close(mlt_producer producer)
{
    mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
    MoltenSession *session = mlt_properties_get_data(properties, "molten_session", NULL);
    if (session)
        molten_session_free(session);

    producer->close = NULL;
    mlt_producer_close(producer);
    free(producer);
}

mlt_producer producer_molten_init(mlt_profile profile,
                                   mlt_service_type type,
                                   const char *id,
                                   char *arg)
{
    (void) type;
    (void) id;

    if (!arg || !*arg)
        return NULL;

    if (molten_abi_version() != MOLTEN_ABI_VERSION) {
        mlt_log_error(NULL,
                      "molten: built against ABI v%u, loaded library is v%u - rebuild against a "
                      "matching libmolten_ffi\n",
                      (unsigned) MOLTEN_ABI_VERSION,
                      (unsigned) molten_abi_version());
        return NULL;
    }

    /* Optional ?scale=N suffix on the resource itself, e.g.
     * molten:season.mltn?scale=10 - the "scale" property (see
     * producer_get_image) is not reliably editable through a host's UI for
     * a producer type it doesn't recognise, but the resource/path field
     * almost always is. Mirrors producer_framebuffer_init's own ?speed
     * parsing in MLT's kdenlive module - strdup, split on the last '?',
     * parse the copy, leave the original arg untouched. */
    char *path = strdup(arg);
    int scale = 1;
    char *query = strrchr(path, '?');
    if (query) {
        *query++ = '\0';
        char *eq = strstr(query, "scale=");
        if (eq) {
            scale = atoi(eq + 6);
            if (scale < 1)
                scale = 1;
        }
    }

    MoltenSession *session = molten_session_open(path);
    if (!session) {
        mlt_log_error(NULL, "molten: could not open recording '%s'\n", path);
        free(path);
        return NULL;
    }
    free(path);

    mlt_producer producer = calloc(1, sizeof(struct mlt_producer_s));
    if (!producer || mlt_producer_init(producer, NULL) != 0) {
        molten_session_free(session);
        free(producer);
        return NULL;
    }

    mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
    /* No destructor here - producer_close frees the session explicitly via
     * molten_session_free, not plain free(), since it owns an open file
     * handle and (maybe) a loaded seek index. */
    mlt_properties_set_data(properties, "molten_session", session, 0, NULL, NULL);
    mlt_properties_set(properties, "resource", arg);
    mlt_properties_set_int(properties, "scale", scale);

    uint64_t duration_ms = 0;
    if (molten_session_duration_ms(session, &duration_ms) == MOLTEN_STATUS_OK) {
        double fps = mlt_profile_fps(profile);
        mlt_position length = (mlt_position) (duration_ms / 1000.0 * fps) + 1;
        mlt_properties_set_position(properties, "length", length);
        mlt_properties_set_position(properties, "out", length - 1);
    }

    producer->get_frame = producer_get_frame;
    producer->close = (mlt_destructor) producer_close;

    return producer;
}
