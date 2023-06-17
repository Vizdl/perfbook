/*
 * route_hazptr.c: Trivial linked-list routing table protected by hazard
 *	pointers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright (c) 2016-2019 Paul E. McKenney, IBM Corporation.
 * Copyright (c) 2019 Paul E. McKenney, Facebook.
 */

#include "../api.h"
#include "hazptr.h"

struct route_entry {
	// 危险指针节点,用来释放内存的。
	struct hazptr_head hh;
	struct route_entry *re_next;
	unsigned long addr;
	unsigned long iface;
	int re_freed;
};

struct route_entry route_list;
// 保护多写冲突
DEFINE_SPINLOCK(routelock);
// 这里将会被初始化为指向 HP 数组
hazard_pointer __thread *my_hazptr;

/*
 * Look up a route entry, return the corresponding interface.
 */
unsigned long route_lookup(unsigned long addr)
{
	int offset = 0;
	struct route_entry *rep;
	struct route_entry **repp;

retry:
	repp = &route_list.re_next;
	do {
// =================[ READ BGN ]======================
		// 这里在试图读取 next 指针
		// 访问 next 指针存在两个层面的问题
		// 1. next 指针指向的内存已经被释放(与释放的冲突),通过危险指针延期释放解决。
		// 2. next 指针指向的内存已经被解链但还未被释放,重新遍历链表
		// rep = my_hazptr[offset].p = *repp
		// 下面其实做了两件事
		// 1. 标记 repp 正在被读
		// 2. 安全地获取到 *repp
		rep = hp_try_record(repp, &my_hazptr[offset]);
		// 访问到最后一个节点(没找到)
		if (!rep)
			return ULONG_MAX;
		// 访问到了一个已经被解链
		if ((uintptr_t)rep == HAZPTR_POISON)
			goto retry; /* element deleted. */
// =================[ READ END ]======================
		repp = &rep->re_next;
	} while (rep->addr != addr);

	if (READ_ONCE(rep->re_freed))
		abort();
	return rep->iface;
}

/*
 * Add an element to the route table.
 */
int route_add(unsigned long addr, unsigned long interface)
{
	struct route_entry *rep;

	rep = malloc(sizeof(*rep));
	if (!rep)
		return -ENOMEM;
	rep->addr = addr;
	rep->iface = interface;
	rep->re_freed = 0;
// =================[ WRTE BGN ]======================
	spin_lock(&routelock);
	rep->re_next = route_list.re_next;
	route_list.re_next = rep;
	spin_unlock(&routelock);
// =================[ WRTE END ]======================
	return 0;
}

/*
 * Remove the specified element from the route table.
 */
int route_del(unsigned long addr)
{
	struct route_entry *rep;
	struct route_entry **repp;

// =================[ WRTE BGN ]======================
	spin_lock(&routelock);
	repp = &route_list.re_next;
	for (;;) {
		rep = *repp;
		if (rep == NULL)
			break;
		if (rep->addr == addr) {
			// 这里在解除联系,这里不需要 WRITE_ONCE 的原因可能是两个语句之间有绝对的依赖关系
			// 这里将前一个元素的 next 指针指向 next 节点
			*repp = rep->re_next;
			// 这里将当前节点 next 指向 HAZPTR_POISON,标记已解除关系。
			rep->re_next = (struct route_entry *)HAZPTR_POISON; //\lnlbl{poison}
			spin_unlock(&routelock);
// =================[ WRTE END ]======================
// =================[ FREE BGN ]======================
			// 延迟释放
			hazptr_free_later(&rep->hh);
// =================[ FREE END ]======================
			return 0;
		}
		repp = &rep->re_next;
	}
	spin_unlock(&routelock);
// =================[ WRTE END ]======================
	return -ENOENT;
}

/*
 * Clear all elements from the route table.
 */
void route_clear(void)
{
	struct route_entry *rep;
	struct route_entry *rep1;

	spin_lock(&routelock);
	rep = route_list.re_next;
	// 这里需要 WRITE_ONCE 是担心会延期处理 route_list.re_next = NULL?
	WRITE_ONCE(route_list.re_next, NULL);
	while (rep != NULL) {
		rep1 = rep->re_next;
		rep->re_next = (struct route_entry *)HAZPTR_POISON;
		hazptr_free_later(&rep->hh);
		rep = rep1;
	}
	spin_unlock(&routelock);
}

void route_register_thread(void)
{
	my_hazptr = &HP[K * smp_thread_id()];
}
#define route_register_thread route_register_thread

#define route_unregister_thread() hazptr_thread_exit()

#define quiescent_state() do { } while (0)

#define synchronize_rcu() do { } while (0)

#define other_init() hazptr_init()

// 危险指针释放函数
void hazptr_free(void *p)
{
	struct route_entry *rep = p;

	WRITE_ONCE(rep->re_freed, 1);
	free(p);
}

#include "routetorture.h"
