#pragma once

#include <stddef.h>
#include <stdint.h>

#define GZ_OK        0
#define GZ_STOPPED   1      // the sink said stop
#define GZ_EIO      -1      // the source failed
#define GZ_EFORMAT  -2      // not gzip at all
#define GZ_ECORRUPT -3
#define GZ_ETRUNC   -4      // ran out of input in the middle
#define GZ_ECRC     -5
#define GZ_ENOMEM   -6

// compressed bytes come from here, bytes read, 0 at the end, <0 on error
typedef long (*gz_source_t)(void *arg, uint8_t *buf, size_t len);

// inflated bytes go here, anything but 0 stops it
typedef int (*gz_sink_t)(void *arg, const uint8_t *buf, size_t len);

// inflates a whole gzip stream (all its members) from src into sink
int gunzip(gz_source_t src, void *src_arg, gz_sink_t sink, void *sink_arg);

// what a GZ_ result means
const char *gunzip_error(int r);
