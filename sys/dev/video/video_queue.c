/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/selinfo.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <contrib/v4l/videodev.h>
#include <contrib/v4l/videodev2.h>

#include <dev/video/video_var.h>

MALLOC_DEFINE(M_VIDEO_QUEUE, "video_queue", "Generic video queue");

static void
video_buf_fill_v4l2(struct video_queue *queue, struct v4l2_buffer *buf,
    uint32_t index, uint32_t size, uint32_t len)
{
	buf->index = index;
	buf->m.offset = index * size;
	buf->length = len;
	buf->type = queue->type;
	buf->sequence = 0;
	buf->field = V4L2_FIELD_NONE;
	buf->memory = queue->memory;
	buf->flags = 0;
}

int
video_queue_mmap(struct video_queue *queue, vm_paddr_t *paddr,
    vm_ooffset_t offset)
{
	uint64_t total_size;
	int error;

	if (queue == NULL || paddr == NULL)
		return (EINVAL);

	mtx_lock(&queue->mtx);
	if (queue->mem == NULL) {
		error = ENXIO;
		goto done;
	}
	if (offset < 0 || (offset & PAGE_MASK) != 0) {
		error = EINVAL;
		goto done;
	}
	if (queue->buf_size == 0 || queue->buf_count == 0) {
		error = ENXIO;
		goto done;
	}
	total_size = queue->buf_count * queue->buf_size;
	if ((uint64_t)offset >= total_size) {
		error = EINVAL;
		goto done;
	}

	*paddr = vtophys((uint8_t *)queue->mem + offset);
	error = 0;
done:
	mtx_unlock(&queue->mtx);
	return (error);
}

static void
video_queue_querybuf_locked(struct video_buffer_entry *buf, struct v4l2_buffer *vbuf)
{
	memcpy(vbuf, &buf->vbuf, sizeof(*vbuf));

	switch (buf->status) {
	case VIDEO_BUFFER_STATE_QUEUED:
	case VIDEO_BUFFER_STATE_ACTIVE:
		vbuf->flags |= V4L2_BUF_FLAG_QUEUED;
		break;
	case VIDEO_BUFFER_STATE_DONE:
	case VIDEO_BUFFER_STATE_ERROR:
		vbuf->flags |= V4L2_BUF_FLAG_DONE;
		break;
	case VIDEO_BUFFER_STATE_DEQUEUED:
	default:
		break;
	}
}

int
video_queue_dqbuf(struct video_queue *queue, struct v4l2_buffer *vbuf,
    int nonblock)
{
	struct video_buffer_entry *buf;
	int error;

	error = 0;
	mtx_lock(&queue->mtx);
	do {
		if (!VIDEO_QUEUE_IS_RUNNING(queue)) {
			error = EINVAL;
			break;
		}

		buf = STAILQ_FIRST(&queue->consumer);
		if (buf != NULL) {
			switch (buf->status) {
			case VIDEO_BUFFER_STATE_ERROR:
				error = EIO;
				break;
			case VIDEO_BUFFER_STATE_DONE:
				break;
			default:
				break;
			}
			buf->status = VIDEO_BUFFER_STATE_DEQUEUED;
			STAILQ_REMOVE_HEAD(&queue->consumer, link);
			video_queue_querybuf_locked(buf, vbuf);
			break;
		}

		if (STAILQ_EMPTY(&queue->product)) {
			error = EPIPE;
			break;
		}
		if (nonblock) {
			error = EAGAIN;
			break;
		}

		error = cv_wait_sig(&queue->io_cv, &queue->mtx);
	} while (error == 0 && !nonblock);

	mtx_unlock(&queue->mtx);
	return (error);
}

int
video_queue_qbuf(struct video_queue *queue, struct v4l2_buffer *vbuf)
{
	struct video_buffer_entry *buf;
	int error;

	error = 0;
	mtx_lock(&queue->mtx);
	if (vbuf->index >= queue->buf_count || !VIDEO_QUEUE_IS_RUNNING(queue)) {
		error = EINVAL;
		goto done;
	}

	buf = queue->buf + vbuf->index;
	if (buf->status != VIDEO_BUFFER_STATE_DEQUEUED) {
		error = EINVAL;
		goto done;
	}

	buf->status = VIDEO_BUFFER_STATE_QUEUED;
	buf->vbuf.bytesused = 0;
	STAILQ_INSERT_TAIL(&queue->product, buf, link);
done:
	mtx_unlock(&queue->mtx);
	return (error);
}

int
video_queue_querybuf(struct video_queue *queue, struct v4l2_buffer *vbuf)
{
	int error;

	error = 0;
	mtx_lock(&queue->mtx);
	if (vbuf->index >= queue->buf_count) {
		error = EINVAL;
		goto done;
	}

	video_queue_querybuf_locked(queue->buf + vbuf->index, vbuf);
done:
	mtx_unlock(&queue->mtx);
	return (error);
}

void
video_queue_set_drop_flag(struct video_queue *queue)
{
	queue->flags |= VIDEO_BUFFER_QUEUE_DROP_INCOMPLETE;
}

static void
video_queue_free_bufs_locked(struct video_queue *queue)
{
	if (queue->mem != NULL) {
		free(queue->mem, M_VIDEO_QUEUE);
		queue->mem = NULL;
		queue->buf_count = 0;
	}
}

void
video_queue_free_bufs(struct video_queue *queue)
{
	mtx_lock(&queue->mtx);
	video_queue_free_bufs_locked(queue);
	mtx_unlock(&queue->mtx);
}

int
video_queue_reqbufs(struct video_queue *queue, uint32_t *count,
    uint32_t size)
{
	struct video_buffer_entry *buf;
	void *mem;
	unsigned long rounded_size;
	int error, i, num;

	error = 0;
	mem = NULL;
	rounded_size = round_page(size);
	num = *count;
	if (num > queue->max_buffers)
		num = queue->max_buffers;

	if (num != 0) {
		mem = malloc(num * rounded_size, M_VIDEO_QUEUE,
		    M_WAITOK | M_ZERO);
		if (mem == NULL)
			return (ENOMEM);
	}

	mtx_lock(&queue->mtx);
	video_queue_free_bufs_locked(queue);
	if (num == 0)
		goto done;

	for (i = 0; i < num; i++) {
		buf = queue->buf + i;
		buf->index = i;
		buf->mem = mem;
		buf->offset = i * rounded_size;
		buf->status = VIDEO_BUFFER_STATE_DEQUEUED;
		video_buf_fill_v4l2(queue, &buf->vbuf, i, rounded_size, size);
	}

	queue->mem = mem;
	queue->buf_count = num;
	queue->buf_size = rounded_size;
	*count = num;
done:
	mtx_unlock(&queue->mtx);
	return (error);
}

static void
video_queue_init_buf(struct video_buffer_entry *buf, int index)
{
	memset(buf, 0, sizeof(*buf));
	buf->index = index;
	buf->status = VIDEO_BUFFER_STATE_DEQUEUED;
}

int
video_queue_poll(struct video_queue *queue, int events, struct thread *td)
{
	struct video_buffer_entry *buf;
	int revents;

	revents = 0;
	mtx_lock(&queue->mtx);
	if (queue->flags == 0) {
		mtx_unlock(&queue->mtx);
		return (POLLHUP);
	}

	buf = STAILQ_FIRST(&queue->consumer);
	if (buf != NULL) {
		if (buf->status == VIDEO_BUFFER_STATE_DONE ||
		    buf->status == VIDEO_BUFFER_STATE_ERROR)
			revents |= (events & (POLLIN | POLLRDNORM));
	} else {
		selrecord(td, &queue->sel);
	}
	mtx_unlock(&queue->mtx);
	return (revents);
}

int
video_queue_disable(struct video_queue *queue)
{
	int i;

	mtx_lock(&queue->mtx);
	if ((queue->flags & VIDEO_BUFFER_QUEUE_WORKING) == 0) {
		mtx_unlock(&queue->mtx);
		return (EINVAL);
	}

	STAILQ_INIT(&queue->consumer);
	STAILQ_INIT(&queue->product);
	for (i = 0; i < queue->max_buffers; i++)
		video_queue_init_buf(queue->buf + i, i);

	cv_broadcast(&queue->io_cv);
	queue->flags = 0;
	mtx_unlock(&queue->mtx);
	return (0);
}

int
video_queue_enable(struct video_queue *queue)
{
	mtx_lock(&queue->mtx);
	if ((queue->flags & VIDEO_BUFFER_QUEUE_WORKING) != 0) {
		mtx_unlock(&queue->mtx);
		return (EBUSY);
	}

	queue->seq = 0;
	queue->flags |= VIDEO_BUFFER_QUEUE_WORKING;
	mtx_unlock(&queue->mtx);
	return (0);
}

void
video_queue_init(struct video_queue *queue, void *priv,
    enum v4l2_buf_type type, enum v4l2_memory memory, uint32_t max_buffers)
{
	int i;

	queue->priv = priv;
	queue->type = type;
	queue->memory = memory;
	queue->max_buffers = min(max_buffers, (uint32_t)VIDEO_MAX_BUFFERS);
	mtx_init(&queue->mtx, "video queue lock", NULL, MTX_DEF);
	cv_init(&queue->io_cv, "videoqueuecv");
	STAILQ_INIT(&queue->consumer);
	STAILQ_INIT(&queue->product);

	for (i = 0; i < queue->max_buffers; i++)
		video_queue_init_buf(queue->buf + i, i);
}
