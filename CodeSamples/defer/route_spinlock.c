#include "../api.h"

struct route_entry {
	struct cds_list_head re_next;
	unsigned long addr;
	unsigned long iface;
};

DEFINE_SPINLOCK(routelock);		
CDS_LIST_HEAD(route_list);

unsigned long route_lookup(unsigned long addr)
{
	struct route_entry *rep;
	unsigned long ret;

	spin_lock(&routelock);
	cds_list_for_each_entry(rep, &route_list, re_next) {
		if (rep->addr == addr) {
			ret = rep->iface;
			spin_unlock(&routelock);
			return ret;
		}
	}
	spin_unlock(&routelock);
	return ULONG_MAX;
}

int route_add(unsigned long addr, unsigned long interface)
{
	struct route_entry *rep;

	rep = malloc(sizeof(*rep));
	if (!rep)
		return -ENOMEM;
	rep->addr = addr;
	rep->iface = interface;
	spin_lock(&routelock);
	cds_list_add(&rep->re_next, &route_list);
	spin_unlock(&routelock);
	return 0;
}

int route_del(unsigned long addr)
{
	struct route_entry *rep;

	spin_lock(&routelock);
	cds_list_for_each_entry(rep, &route_list, re_next) {
		if (rep->addr == addr) {
			cds_list_del(&rep->re_next);
			free(rep);
			spin_unlock(&routelock);
			return 0;
		}
	}
	spin_unlock(&routelock);
	return -ENOENT;
}

void route_clear(void)
{
	struct route_entry *rep;
	struct route_entry *rep1;

	spin_lock(&routelock);
	cds_list_for_each_entry_safe(rep, rep1, &route_list, re_next) {
		cds_list_del(&rep->re_next);
		free(rep);
	}
	spin_unlock(&routelock);
}


#define route_register_thread() do { } while (0)
#define route_unregister_thread() do { } while (0)

#define quiescent_state() do { } while (0)

#define synchronize_rcu() do { } while (0)

#include "routetorture.h"
