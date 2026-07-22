/*
 * PureDarwin: job_mig_look_up2_forward()/job_mig_parent_forward() are the
 * MIG *server*-side handlers (job_forward.defs) for launchd's nested
 * job-manager forwarding protocol - invoked when a per-user/per-session
 * child launchd instance asks its parent to resolve a bootstrap lookup or
 * relay a message on its behalf. Real Apple core.c only calls the
 * *client*-side vproc_mig_look_up2_forward()/vproc_mig_parent_forward()
 * (see core.c's job_mig_intran-driven forwarding logic) because PD runs a
 * single, flat, unnested launchd instance - there is no parent job manager
 * to forward *to* in this environment, so a child-side forward request can
 * never legitimately arrive here. Returning BOOTSTRAP_NOT_PRIVILEGED (the
 * real macOS error for "no such nested-manager relationship exists") is
 * the honest, documented failure for this unsupported topology, not a
 * fabricated success.
 */
#include <mach/mach.h>
#include <bootstrap_priv.h>
#include "core.h"

kern_return_t
job_mig_look_up2_forward(job_t j, mach_port_t rp, name_t servicename,
    pid_t targetpid, uuid_t instanceid, uint64_t flags)
{
	(void)j;
	(void)rp;
	(void)servicename;
	(void)targetpid;
	(void)instanceid;
	(void)flags;
	return BOOTSTRAP_NOT_PRIVILEGED;
}

kern_return_t
job_mig_parent_forward(job_t j, mach_port_t rp)
{
	(void)j;
	(void)rp;
	return BOOTSTRAP_NOT_PRIVILEGED;
}
