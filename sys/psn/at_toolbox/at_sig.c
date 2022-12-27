#include <sys/types.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/mmu.h>
#include <sys/page.h>
#include <sys/region.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/stream.h>

static at_sig_open();
static at_sig_close();
static at_sig_rputq();
static at_sig_wputq();

static struct module_info at_sig_minfo = {99, "at_sig", 0, INFPSZ, 32767, 32767, 0};
static struct qinit at_sig_rinit = {at_sig_rputq, NULL, at_sig_open,
				    at_sig_close, NULL, &at_sig_minfo, 0};
static struct qinit at_sig_winit = {at_sig_wputq, NULL, at_sig_open,
				    at_sig_close, NULL, &at_sig_minfo, 0};
struct streamtab at_sig_info = {&at_sig_rinit, &at_sig_winit, NULL, NULL};

static
at_sig_open(q, dev, flag, sflag)
queue_t *q;
{
	q->q_ptr = (caddr_t)u.u_procp;
	WR(q)->q_ptr = (caddr_t)u.u_procp->p_pid;
	return(0);
}

static 
at_sig_close(q)
queue_t *q;
{
	return(0);
}

static
at_sig_rputq(q, m)
queue_t *q;
mblk_t *m;
{
	struct proc *p;

	if (m->b_datap->db_type == M_DATA) {
		p = (struct proc *)q->q_ptr;
		if (p->p_pid == (int)(WR(q)->q_ptr)) {		/* make sure it's still the same process */
			psignal(p, SIGIO);
		}
	}
	putnext(q, m);
}

static
at_sig_wputq(q, m)
queue_t *q;
mblk_t *m;
{
	putnext(q, m);
}


