/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Li-Wen Hsu
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

#ifndef _DEV_VIDEO_VIDEO_VAR_H_
#define _DEV_VIDEO_VIDEO_VAR_H_

#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/selinfo.h>

#define	VIDEO_MAX_BUFFERS		8
#define VIDEO_QUEUE_IS_RUNNING(vq)	((vq)->buf_count != 0)

enum video_buffer_state {
	VIDEO_BUFFER_STATE_DEQUEUED = 0,
	VIDEO_BUFFER_STATE_QUEUED = 1,
	VIDEO_BUFFER_STATE_ACTIVE = 2,
	VIDEO_BUFFER_STATE_DONE = 3,
	VIDEO_BUFFER_STATE_ERROR = 4,
};

#define VIDEO_BUFFER_QUEUE_WORKING		(1 << 0)
#define VIDEO_BUFFER_QUEUE_DISCONNECTED		(1 << 1)
#define VIDEO_BUFFER_QUEUE_DROP_INCOMPLETE	(1 << 2)

struct video_buffer_entry {
	void *mem;
	uint64_t offset;
	uint64_t status;
	uint64_t index;
	struct v4l2_buffer vbuf;

	STAILQ_ENTRY(video_buffer_entry) link;
};

struct video_queue {
	struct mtx mtx;
	struct cv io_cv;
	struct selinfo sel;

	void *priv;
	void *mem;
	uint64_t status;
	uint64_t flags;
	uint64_t seq;
	uint64_t buf_size;
	uint64_t buf_count;
	enum v4l2_buf_type type;
	enum v4l2_memory memory;
	uint32_t max_buffers;
	struct video_buffer_entry buf[VIDEO_MAX_BUFFERS];

	STAILQ_HEAD(, video_buffer_entry) consumer;
	STAILQ_HEAD(, video_buffer_entry) product;
};

void video_queue_init(struct video_queue *queue, void *priv,
    enum v4l2_buf_type type, enum v4l2_memory memory, uint32_t max_buffers);
int video_queue_enable(struct video_queue *queue);
int video_queue_disable(struct video_queue *queue);
int video_queue_poll(struct video_queue *queue, int events,
    struct thread *td);
int video_queue_reqbufs(struct video_queue *queue, uint32_t *count,
    uint32_t size);
void video_queue_free_bufs(struct video_queue *queue);
void video_queue_set_drop_flag(struct video_queue *queue);
int video_queue_querybuf(struct video_queue *queue,
    struct v4l2_buffer *vbuf);
int video_queue_mmap(struct video_queue *queue, vm_paddr_t *paddr,
    vm_ooffset_t offset);
int video_queue_qbuf(struct video_queue *queue,
    struct v4l2_buffer *vbuf);
int video_queue_dqbuf(struct video_queue *queue,
    struct v4l2_buffer *vbuf, int nonblock);

#endif
