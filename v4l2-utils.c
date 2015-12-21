/*
 * V4L2 utility functions implementation
 *
 * Copyright (C) 2016 ELVEES NeoTek JSC
 * Author: Anton Leontiev <aleontiev@elvees.com>
 *
 * SPDX-License-Identifier:	GPL-2.0
 */

#include <stdint.h>
#include <math.h>
#include <error.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>

#include "log.h"

static const char *v4l2_field_names[] = {
	[V4L2_FIELD_ANY]        = "any",
	[V4L2_FIELD_NONE]       = "none",
	[V4L2_FIELD_TOP]        = "top",
	[V4L2_FIELD_BOTTOM]     = "bottom",
	[V4L2_FIELD_INTERLACED] = "interlaced",
	[V4L2_FIELD_SEQ_TB]     = "seq-tb",
	[V4L2_FIELD_SEQ_BT]     = "seq-bt",
	[V4L2_FIELD_ALTERNATE]  = "alternate",
	[V4L2_FIELD_INTERLACED_TB] = "interlaced-tb",
	[V4L2_FIELD_INTERLACED_BT] = "interlaced-bt"
};

static const char *v4l2_type_names[] = {
	[V4L2_BUF_TYPE_VIDEO_CAPTURE]      = "vid-cap",
	[V4L2_BUF_TYPE_VIDEO_OVERLAY]      = "vid-overlay",
	[V4L2_BUF_TYPE_VIDEO_OUTPUT]       = "vid-out",
	[V4L2_BUF_TYPE_VBI_CAPTURE]        = "vbi-cap",
	[V4L2_BUF_TYPE_VBI_OUTPUT]         = "vbi-out",
	[V4L2_BUF_TYPE_SLICED_VBI_CAPTURE] = "sliced-vbi-cap",
	[V4L2_BUF_TYPE_SLICED_VBI_OUTPUT]  = "sliced-vbi-out",
	[V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY] = "vid-out-overlay",
	[V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE] = "vid-cap-mplane",
	[V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE] = "vid-out-mplane",
	[V4L2_BUF_TYPE_SDR_CAPTURE]        = "sdr-cap"
};

static const char *v4l2_memory_names[] = {
	[V4L2_MEMORY_MMAP]    = "mmap",
	[V4L2_MEMORY_USERPTR] = "userptr",
	[V4L2_MEMORY_OVERLAY] = "overlay",
	[V4L2_MEMORY_DMABUF]  = "dmabuf"
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define v4l2_name(a, arr) (((unsigned)(a)) < ARRAY_SIZE(arr) ? arr[a] : "unknown")

const char *v4l2_field_name(enum v4l2_field const field)
{
	return v4l2_name(field, v4l2_field_names);
}

const char *v4l2_type_name(enum v4l2_buf_type const type)
{
	return v4l2_name(type, v4l2_type_names);
}

const char *v4l2_memory_name(enum v4l2_memory const memory)
{
	return v4l2_name(memory, v4l2_memory_names);
}

void v4l2_print_format(struct v4l2_format const *const p)
{
	const struct v4l2_pix_format *pix;
	const struct v4l2_pix_format_mplane *mp;
	const struct v4l2_vbi_format *vbi;
	const struct v4l2_sliced_vbi_format *sliced;
	const struct v4l2_window *win;
	const struct v4l2_sdr_format *sdr;
	unsigned i;

	pr_cont(LOG_DEBUG, "type=%s", v4l2_type_name(p->type));
	switch (p->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		pix = &p->fmt.pix;
		pr_cont(LOG_DEBUG, ", width=%u, height=%u, "
			"pixelformat=%c%c%c%c, field=%s, "
			"bytesperline=%u, sizeimage=%u, colorspace=%d, "
			"flags=0x%x, ycbcr_enc=%u, quantization=%u\n",
			pix->width, pix->height,
			(pix->pixelformat & 0xff),
			(pix->pixelformat >>  8) & 0xff,
			(pix->pixelformat >> 16) & 0xff,
			(pix->pixelformat >> 24) & 0xff,
			v4l2_field_name(pix->field),
			pix->bytesperline, pix->sizeimage,
			pix->colorspace, pix->flags, pix->ycbcr_enc,
			pix->quantization);
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		mp = &p->fmt.pix_mp;
		pr_cont(LOG_DEBUG ,", width=%u, height=%u, "
			"format=%c%c%c%c, field=%s, "
			"colorspace=%d, num_planes=%u, flags=0x%x, "
			"ycbcr_enc=%u, quantization=%u\n",
			mp->width, mp->height,
			(mp->pixelformat & 0xff),
			(mp->pixelformat >>  8) & 0xff,
			(mp->pixelformat >> 16) & 0xff,
			(mp->pixelformat >> 24) & 0xff,
			v4l2_field_name(mp->field),
			mp->colorspace, mp->num_planes, mp->flags,
			mp->ycbcr_enc, mp->quantization);
		for (i = 0; i < mp->num_planes; i++)
			pr_debug("plane %u: bytesperline=%u sizeimage=%u\n", i,
					mp->plane_fmt[i].bytesperline,
					mp->plane_fmt[i].sizeimage);
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		win = &p->fmt.win;
		/* Note: we can't print the clip list here since the clips
		 * pointer is a userspace pointer, not a kernelspace
		 * pointer. */
		pr_cont(LOG_DEBUG, ", wxh=%dx%d, x,y=%d,%d, field=%s, chromakey=0x%08x, clipcount=%u, clips=%p, bitmap=%p, global_alpha=0x%02x\n",
			win->w.width, win->w.height, win->w.left, win->w.top,
			v4l2_field_name(win->field),
			win->chromakey, win->clipcount, win->clips,
			win->bitmap, win->global_alpha);
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		vbi = &p->fmt.vbi;
		pr_cont(LOG_DEBUG, ", sampling_rate=%u, offset=%u, samples_per_line=%u, "
			"sample_format=%c%c%c%c, start=%u,%u, count=%u,%u\n",
			vbi->sampling_rate, vbi->offset,
			vbi->samples_per_line,
			(vbi->sample_format & 0xff),
			(vbi->sample_format >>  8) & 0xff,
			(vbi->sample_format >> 16) & 0xff,
			(vbi->sample_format >> 24) & 0xff,
			vbi->start[0], vbi->start[1],
			vbi->count[0], vbi->count[1]);
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		sliced = &p->fmt.sliced;
		pr_cont(LOG_DEBUG, ", service_set=0x%08x, io_size=%d\n",
				sliced->service_set, sliced->io_size);
		for (i = 0; i < 24; i++)
			pr_debug("line[%02u]=0x%04x, 0x%04x\n", i,
				sliced->service_lines[0][i],
				sliced->service_lines[1][i]);
		break;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		sdr = &p->fmt.sdr;
		pr_cont(LOG_DEBUG, ", pixelformat=%c%c%c%c\n",
			(sdr->pixelformat >>  0) & 0xff,
			(sdr->pixelformat >>  8) & 0xff,
			(sdr->pixelformat >> 16) & 0xff,
			(sdr->pixelformat >> 24) & 0xff);
		break;
	}
}

void v4l2_print_buffer(struct v4l2_buffer const *const p)
{
	pr_debug("%02ld:%02d:%02d.%08ld index=%d, type=%s, "
		"flags=0x%08x, sequence=%d, memory=%s, bytesused=%d, length=%d, "
		"offset=%d",
			p->timestamp.tv_sec / 3600,
			(int)(p->timestamp.tv_sec / 60) % 60,
			(int)(p->timestamp.tv_sec % 60),
			(long)p->timestamp.tv_usec,
			p->index,
			v4l2_type_name(p->type),
			p->flags,
			p->sequence, v4l2_memory_name(p->memory),
			p->bytesused, p->length,
			(p->memory == V4L2_MEMORY_MMAP) ? p->m.offset : 0);
}

int v4l2_open(char const *const device, uint32_t positive, uint32_t negative,
		char card[32])
{
	int ret;
	struct v4l2_capability cap;
	struct stat stat;

	int fd = open(device, O_RDWR, 0);
	if (fd < 0) error(EXIT_FAILURE, errno, "Can not open %s", device);

	pr_verb("V4L2: Device %s descriptor is %d", device, fd);

	if (fstat(fd, &stat) == -1)
		error(EXIT_FAILURE, errno, "Can not stat() %s", device);
	else if (!S_ISCHR(stat.st_mode))
		error(EXIT_FAILURE, 0, "%s is not a character device", device);

	ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	if (ret != 0)
		error(EXIT_FAILURE, errno, "Can not query device capabilities");

	if ((cap.capabilities & positive) != positive)
		error(EXIT_FAILURE, 0, "Device %s does not support required "
				"capabilities: %#08x", device, positive);

	if ((cap.capabilities & negative) != 0)
		error(EXIT_FAILURE, 0, "Device %s supports unsupported "
				"capabilities: %#08x", device, negative);

	if (card) memcpy(card, cap.card, 32);

	return fd;
}

void v4l2_configure(int const fd, enum v4l2_buf_type const type,
		uint32_t const pixelformat, uint32_t const width,
		uint32_t const height)
{
	int rc;
	struct v4l2_format fmt = {
		.type = type,
		.fmt = {
			.pix = {
				.width = width,
				.height = height,
				.pixelformat = pixelformat,
				.field = V4L2_FIELD_ANY
			}
		}
	};

	pr_verb("V4L2: Setup format for %d %s", fd, v4l2_type_name(type));

	rc = ioctl(fd, VIDIOC_S_FMT, &fmt);
	if (rc != 0)
		error(EXIT_FAILURE, 0, "Can not set %s format", v4l2_type_name(type));

	if (fmt.fmt.pix.width != width || fmt.fmt.pix.height != height)
		error(EXIT_FAILURE, 0, "Can not set requested size");

	if (fmt.fmt.pix.pixelformat != pixelformat)
		error(EXIT_FAILURE, 0, "Can not set requested pixel format");

	pr_debug("V4L2: Configured: pixelformat = %.4s, width = %u, height = %u,"
			" sizeimage = %u", (char *)&fmt.fmt.pix.pixelformat,
			fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.sizeimage);

	v4l2_print_format(&fmt);
}

void v4l2_framerate_configure(int const fd, enum v4l2_buf_type const type,
		unsigned const framerate)
{
	int rc;
	struct v4l2_streamparm parm = {
		.type = type
	};

	pr_verb("V4L2: Setup framerate for %d", fd);
	rc = ioctl(fd, VIDIOC_G_PARM, &parm);
	if (rc != 0)
		error(EXIT_FAILURE, 0, "Can not get device streaming parameters");

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (!(parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
			pr_warn("Device %d capture does not support framerate adjustment");
			return;
		}

		parm.parm.capture.timeperframe.numerator = 1;
		parm.parm.capture.timeperframe.denominator = framerate;
	} else if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		if (!(parm.parm.output.capability & V4L2_CAP_TIMEPERFRAME)) {
			pr_warn("Device %d output does not support framerate adjustment");
			return;
		}

		parm.parm.output.timeperframe.numerator = 1;
		parm.parm.output.timeperframe.denominator = framerate;
	}

	rc = ioctl(fd, VIDIOC_S_PARM, &parm);
	if (rc != 0)
		error(EXIT_FAILURE, 0, "Can not set device streaming parameters");

	if ((type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			parm.parm.capture.timeperframe.denominator != framerate) ||
			(type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
			parm.parm.output.timeperframe.denominator != framerate))
		error(EXIT_FAILURE, 0,
				"Device %d %s failed to set requested framerate", fd,
				v4l2_type_name(type));
}

float v4l2_framerate_get(int const fd, enum v4l2_buf_type const type)
{
	int rc;
	struct v4l2_streamparm parm = {
		.type = type
	};

	rc = ioctl(fd, VIDIOC_G_PARM, &parm);

	if (rc != 0) {
		pr_warn("Can not get device %d %s streaming parameters", fd,
				v4l2_type_name(type));
		return NAN;
	}

	return (float)parm.parm.capture.timeperframe.denominator /
			parm.parm.capture.timeperframe.numerator;
}

uint32_t v4l2_buffers_request(int const fd, enum v4l2_buf_type const type,
		uint32_t const num, enum v4l2_memory const memory)
{
	int rc;

	pr_verb("V4L2: Obtaining %d %s buffers for %d %s", num,
			v4l2_memory_name(memory), fd, v4l2_type_name(type));

	struct v4l2_requestbuffers reqbuf = {
		.count = num,
		.type = type,
		.memory = memory
	};

	rc = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);

	if (rc != 0)
		error(EXIT_FAILURE, errno, "Can not request %s buffers",
				v4l2_type_name(type));

	if (reqbuf.count == 0)
		error(EXIT_FAILURE, 0, "Device gives zero %s buffers",
				v4l2_type_name(type));

	if (reqbuf.count != num)
		error(EXIT_FAILURE, 0, "Device gives %u %s buffers, but %u is requested",
				reqbuf.count, v4l2_type_name(type), num);

	pr_debug("V4L2: Got %d %s buffers", reqbuf.count,
			v4l2_type_name(type));

	return reqbuf.count;
}

void v4l2_buffers_mmap(int const fd, enum v4l2_buf_type const type,
		uint32_t const num, void *bufs[], int const prot)
{
	int rc;

	for (int i = 0; i < num; ++i) {
		struct v4l2_buffer buf = {
			.index = i,
			.type = type
		};

		rc = ioctl(fd, VIDIOC_QUERYBUF, &buf);
		if (rc != 0) error(EXIT_FAILURE, errno, "Can not query buffer");

		pr_debug("V4L2: Got %s buffer #%u: length = %u",
				v4l2_type_name(type), i, buf.length);

		//! \todo size field is not needed
		bufs[i] = mmap(NULL, buf.length, prot, MAP_SHARED, fd,
				buf.m.offset);

		if (bufs[i] == MAP_FAILED)
			error(EXIT_FAILURE, errno, "Can not mmap %s buffer",
					v4l2_type_name(type));
	}
}

void v4l2_buffers_export(int const fd, enum v4l2_buf_type const type,
		uint32_t const num, int bufs[])
{
	int rc;

	for (int i = 0; i < num; ++i) {
		struct v4l2_exportbuffer ebuf = {
			.index = i,
			.type = type
		};

		rc = ioctl(fd, VIDIOC_EXPBUF, &ebuf);
		if (rc != 0)
			error(EXIT_FAILURE, errno, "Can not export %s buffer",
					v4l2_type_name(type));

		pr_debug("V4L2: Exported %s buffer #%u: fd = %d",
				v4l2_type_name(type), i, ebuf.fd);

		bufs[i] = ebuf.fd;
	}
}

void v4l2_dqbuf(int const fd, struct v4l2_buffer *const restrict buf)
{
	int rc;

	rc = ioctl(fd, VIDIOC_DQBUF, buf);
	if (rc != 0)
		error(EXIT_FAILURE, errno, "Can not dequeue %s buffer from %d",
				v4l2_type_name(buf->type), fd);
}

void v4l2_qbuf(int const fd, struct v4l2_buffer *const restrict buf)
{
	int rc;

	pr_debug("Enqueuing buffer #%d to %d %s", buf->index, fd,
			v4l2_type_name(buf->type));

	v4l2_print_buffer(buf);
	rc = ioctl(fd, VIDIOC_QBUF, buf);

	if (rc != 0)
		error(EXIT_FAILURE, errno, "Can not enqueue %s buffer to %d",
				v4l2_type_name(buf->type), fd);
}

void v4l2_streamon(int const fd, enum v4l2_buf_type const type)
{
	int rc;
	pr_verb("V4L2: Stream on for %d %s", fd, v4l2_type_name(type));

	rc = ioctl(fd, VIDIOC_STREAMON, &type);
	if (rc != 0)
		error(EXIT_FAILURE, errno, "Failed to start %s stream",
				v4l2_type_name(type));
}
