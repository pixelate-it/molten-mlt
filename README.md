# molten-mlt

An MLT producer for [Molten](https://github.com/pixelate-it/molten) `.mltn`
recordings - play a collaborative pixel canvas season back as a clip inside
Kdenlive, or any other MLT-based editor, the same way you'd load a video
file.

```
melt molten:season.mltn -consumer sdl2:
```

## What this is

A single MLT service (`molten`), registered like any other producer, backed
by [`molten-ffi`](https://github.com/pixelate-it/molten/tree/feat/rust-core/molten-ffi) -
the Rust implementation of the format, not a reimplementation here. This
repo's C is deliberately thin: `producer_molten.c` converts a frame number to
a millisecond timestamp and makes one call,
`molten_session_render_at(session, timestamp_ms, ...)`, which hands back a
ready-to-use RGBA8 buffer. No `.mltn` framing, no chunk parsing, no seek
logic lives here - all of that, including fast arbitrary-timestamp seeking
via a sidecar index, is `molten-ffi`/`molten-utils`'s job.

## Building

Requires MLT's development headers (`mlt-framework-7` via pkg-config - MLT
7.x; this was built and tested against 7.38.0) and a built `molten-ffi`.

```bash
# 1. Build molten-ffi (from a checkout of pixelate-it/molten's Rust branch):
cd /path/to/molten
cargo build -p molten-ffi --release

# 2. Build this module against it:
cd /path/to/molten-mlt
cmake -B build \
  -DMOLTEN_FFI_INCLUDE_DIR=/path/to/molten/molten-ffi/include \
  -DMOLTEN_FFI_LIB_DIR=/path/to/molten/target/release
cmake --build build
```

This produces `build/mltmolten.so`. Verified end to end against the real MLT
7.38.0 (nixpkgs) and a real `.mltn` fixture: `melt` discovers the service,
serves its YAML metadata (`melt -query "producer=molten"`), and renders every
frame of a recording through to a `null:` consumer with no errors, for
exactly the frame count its `Footer` timestamp implies.

**Not yet automated**: there is no CI, no packaged release, and no
`FetchContent`/submodule step to pull `molten-ffi` automatically - the
`molten` repo's Rust code does not have a public release yet, so
`MOLTEN_FFI_INCLUDE_DIR`/`MOLTEN_FFI_LIB_DIR` have to point at a local build
for now. That is the first thing to fix once `molten`'s Rust branch is
published.

## Installing into MLT

```bash
cmake --install build --prefix <wherever cmake found MLT installed>
```

installs `mltmolten.so` and `producer_molten.yml` into MLT's own module and
data directories (read from `mlt-framework-7.pc`'s `moduledir`/`mltdatadir`),
the same layout every other MLT module uses - Kdenlive picks it up
automatically from there, no separate registration step.

## Using it

```
molten:<path/to/season.mltn>
```

as a resource, anywhere MLT accepts a producer - a `.mlt` XML file's
`<producer>`, a Kdenlive project's clip bin, or directly on `melt`'s command
line. If `<path/to/season.mltn>.idx` exists beside the recording (see
`molten-utils`'s seek index), scrubbing is fast; if it doesn't, every seek
falls back to a full replay from the start of the file - slower, never
wrong.

A resize mid-recording is a new segment, not a mid-clip frame-size change
(MLT producers can't do that, and neither can any video encoder) - split the
timeline at each resize the same way `molten render`'s CLI numbers its output
files, one `molten:` producer per window.

## License

MPL-2.0, matching [pixelate-it/molten](https://github.com/pixelate-it/molten).
