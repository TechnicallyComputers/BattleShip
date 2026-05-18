#include <sys/netpeer_transport.h>

#ifdef PORT

#include <sys/netpeer.h>
#include <sys/netrollback.h>

void syNetPeerPumpIngressBeforeInputRead(void)
{
	if (syNetRollbackIsResimulating() != FALSE)
	{
		return;
	}
	if (syNetPeerIsVSSessionActive() == FALSE)
	{
		return;
	}
	syNetPeerPumpIngressTransport("inactive_pre_read");
	syNetPeerApplyPendingDelayContract();
}

#endif /* PORT */
