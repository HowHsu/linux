#ifndef INTERNAL_IO_WQ_H
#define INTERNAL_IO_WQ_H

#include <linux/refcount.h>
#include <linux/io_uring_types.h>
#include <linux/list_nulls.h>

struct io_wq;

/*
 * One for each thread in a wqe pool
 */
struct io_worker {
	refcount_t ref;
	unsigned flags;
	struct hlist_nulls_node nulls_node;
	struct list_head all_list;
	struct task_struct *task;
	struct io_wqe *wqe;

	struct io_wq_work *cur_work;
	struct io_wq_work *next_work;
	raw_spinlock_t lock;

	struct completion ref_done;

	unsigned long create_state;
	struct callback_head create_work;
	int create_index;

	union {
		struct rcu_head rcu;
		struct work_struct work;
	};
};

enum {
	IO_WQ_WORK_CANCEL	= 1,
	IO_WQ_WORK_HASHED	= 2,
	IO_WQ_WORK_UNBOUND	= 4,
	IO_WQ_WORK_CONCURRENT	= 16,

	IO_WQ_HASH_SHIFT	= 24,	/* upper 8 bits are used for hash key */
};

enum io_wq_cancel {
	IO_WQ_CANCEL_OK,	/* cancelled before started */
	IO_WQ_CANCEL_RUNNING,	/* found, running, and attempted cancelled */
	IO_WQ_CANCEL_NOTFOUND,	/* work not found */
};

enum io_uringlet_state {
	IO_URINGLET_INLINE,
	IO_URINGLET_EMPTY,
	IO_URINGLET_SCHEDULED,
};

enum {
	IO_WORKER_F_UP		= 1,	/* up and active */
	IO_WORKER_F_RUNNING	= 2,	/* account as running */
	IO_WORKER_F_FREE	= 4,	/* worker on free list */
	IO_WORKER_F_BOUND	= 8,	/* is doing bounded work */
	IO_WORKER_F_SCHEDULED	= 16,	/* worker had been scheduled out before */
	IO_WORKER_F_SUBMIT	= 32,	/* uringlet worker is submitting sqes */
};

typedef struct io_wq_work *(free_work_fn)(struct io_wq_work *);
typedef int (io_wq_work_fn)(struct io_wq_work *);

struct io_wq_hash {
	refcount_t refs;
	unsigned long map;
	struct wait_queue_head wait;
};

static inline void io_wq_put_hash(struct io_wq_hash *hash)
{
	if (refcount_dec_and_test(&hash->refs))
		kfree(hash);
}

struct io_wq_data {
	struct io_wq_hash *hash;
	struct task_struct *task;
	io_wq_work_fn *do_work;
	free_work_fn *free_work;
	void *private;
};

struct io_wq *io_wq_create(unsigned bounded, struct io_wq_data *data);
void io_wq_exit_start(struct io_wq *wq);
void io_wq_put_and_exit(struct io_wq *wq);

void io_wq_enqueue(struct io_wq *wq, struct io_wq_work *work);
void io_wq_hash_work(struct io_wq_work *work, void *val);

int io_wq_cpu_affinity(struct io_wq *wq, cpumask_var_t mask);
int io_wq_max_workers(struct io_wq *wq, int *new_count);

static inline bool io_wq_is_hashed(struct io_wq_work *work)
{
	return work->flags & IO_WQ_WORK_HASHED;
}

typedef bool (work_cancel_fn)(struct io_wq_work *, void *);

enum io_wq_cancel io_wq_cancel_cb(struct io_wq *wq, work_cancel_fn *cancel,
					void *data, bool cancel_all);

#if defined(CONFIG_IO_WQ)
extern void io_wq_worker_sleeping(struct task_struct *);
extern void io_wq_worker_running(struct task_struct *);
#else
static inline void io_wq_worker_sleeping(struct task_struct *tsk)
{
}
static inline void io_wq_worker_running(struct task_struct *tsk)
{
}
#endif

static inline bool io_wq_current_is_worker(void)
{
	return in_task() && (current->flags & PF_IO_WORKER) &&
		current->worker_private;
}

static inline void io_worker_set_scheduled(struct io_worker *worker)
{
	worker->flags |= IO_WORKER_F_SCHEDULED;
}

static inline void io_worker_clean_scheduled(struct io_worker *worker)
{
	worker->flags &= ~IO_WORKER_F_SCHEDULED;
}

static inline bool io_worker_test_scheduled(struct io_worker *worker)
{
	return worker->flags & IO_WORKER_F_SCHEDULED;
}

extern struct io_wq *io_init_wq_offload(struct io_ring_ctx *ctx,
					struct task_struct *task);
extern int io_uringlet_offload(struct io_wq *wq);
extern int let_get_owner(struct io_wq *wq);
extern bool let_owner_is_transmit(struct io_wq *let);
extern bool let_owner_is_not_null(struct io_wq *let);
extern void io_worker_set_submit(struct io_worker *worker);
extern void io_worker_clean_submit(struct io_worker *worker);
#endif
