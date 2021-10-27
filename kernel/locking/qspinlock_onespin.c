// SPDX-License-Identifier: GPL-2.0-or-later
// Catnap spinlock implemented by Youngjoo Woo
// qspinlock_onespin.c is implemented based on qspinlock.c
// SPDX-FileCopyrightText: Copyright (c) 2019 Sungkyunkwan University

#if !defined(_GEN_CATNAP_LOCK_C2)

#define CATNAP_ONESPIN_TARGET_CSTATE 0x00
#define queued_spin_lock_slowpath_onespin	queued_spin_lock_slowpath_onespin_c1

#endif

void queued_spin_lock_slowpath_onespin(struct qspinlock *lock, u32 val)
{
	struct mcs_spinlock *prev, *next, *node;
	u32 old, tail;
	int idx;
	
	BUILD_BUG_ON(CONFIG_NR_CPUS >= (1U << _Q_TAIL_CPU_BITS));
	
	if (pv_enabled())
		goto pv_queue;
	
	if (virt_spin_lock(lock))
		return;
	
	if (val == _Q_PENDING_VAL) {
		int cnt = _Q_PENDING_LOOPS;
		val = atomic_cond_read_relaxed(&lock->val,
									   (VAL != _Q_PENDING_VAL) || !cnt--);
	}
	
	if (val & ~_Q_LOCKED_MASK)
		goto queue;
	
	val = queued_fetch_set_pending_acquire(lock);
	
	if (unlikely(val & ~_Q_LOCKED_MASK)) {
		if (!(val & _Q_PENDING_MASK))
		clear_pending(lock);
		goto queue;
	}
	
	if (val & _Q_LOCKED_MASK)
		atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_MASK));
	
	clear_pending_set_locked(lock);
	qstat_inc(qstat_lock_pending, true);
	return;
	
queue:
	qstat_inc(qstat_lock_slowpath, true);
pv_queue:
	node = this_cpu_ptr(&mcs_nodes[0]);
	idx = node->count++;
	tail = encode_tail(smp_processor_id(), idx);
	
	node += idx;
	
	barrier();
	
	node->locked = 0;
	node->next = NULL;
	pv_init_node(node);
	
	if (queued_spin_trylock(lock))
		goto release;
	
	smp_wmb();
	
	old = xchg_tail(lock, tail);
	next = NULL;
	
	if (old & _Q_TAIL_MASK) {
		prev = decode_tail(old);
		
		WRITE_ONCE(prev->next, node);
		
		pv_wait_node(node, prev);
		arch_mcs_spin_lock_contended_mwait(&node->locked, CATNAP_ONESPIN_TARGET_CSTATE);
		
		next = READ_ONCE(node->next);
		if (next)
			prefetchw(next);
	}
	
	if ((val = pv_wait_head_or_lock(lock, node)))
		goto locked;
	
	val = atomic_cond_read_acquire(&lock->val, !(VAL & _Q_LOCKED_PENDING_MASK));
	
locked:
	
	if (((val & _Q_TAIL_MASK) == tail) &&
		atomic_try_cmpxchg_relaxed(&lock->val, &val, _Q_LOCKED_VAL))
		goto release; /* No contention */
	
	set_locked(lock);
	
	if (!next)
		next = smp_cond_load_relaxed(&node->next, (VAL));
	
	arch_mcs_spin_unlock_contended(&next->locked);
	pv_kick_node(lock, next);
	
release:
	__this_cpu_dec(mcs_nodes[0].count);
}
EXPORT_SYMBOL(queued_spin_lock_slowpath_onespin);

#if !defined(_GEN_CATNAP_LOCK_C2)
#define _GEN_CATNAP_LOCK_C2

#undef CATNAP_ONESPIN_TARGET_CSTATE
#define CATNAP_ONESPIN_TARGET_CSTATE 0x10

#undef  queued_spin_lock_slowpath_onespin
#define queued_spin_lock_slowpath_onespin	queued_spin_lock_slowpath_onespin_c2

#include "qspinlock_onespin.c"

#endif
/*

#if !defined(_GEN_CATNAP_LOCK_C2) && !defined(_GEN_CATNAP_LOCK_C0)
#define _GEN_CATNAP_LOCK_C2

#undef CATNAP_ONESPIN_TARGET_CSTATE
#define CATNAP_ONESPIN_TARGET_CSTATE 0x20

#undef  queued_spin_lock_slowpath_onespin
#define queued_spin_lock_slowpath_onespin	queued_spin_lock_slowpath_onespin_c2

#include "qspinlock_onespin.c"
#endif
*/
