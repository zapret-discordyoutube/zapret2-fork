// Batched reinjection of unmodified packets.
// winws2 receives packets from windivert in bulk but reinjects them one by one : one syscall per packet.
// Here consecutive unmodified packets of the received bulk are collected and sent by one WinDivertSendEx call.
// Order of packets on the wire is preserved : anything else that is sent (modified packet, fake from lua)
// flushes the collected run first. Next receive flushes it too.
// The run must not outlive the receive loop : on quit and when the logical network disappears upstream returns
// from windivert_recv() before the next receive, frees the receive buffer and closes the filter handle.
// Both exits start with usleep(0), so usleep flushes the run as well. WinDivertClose forgets the run of the handle
// and never touches the collected packets : on quit the receive buffer is already freed when the handle is closed.
// Upstream sources are not changed. WinDivertRecvEx/WinDivertSend/WinDivertClose/usleep are redirected here
// by macros in compat.h.
// Set environment variable WINWS2_NO_BATCH=1 to turn batching off.

#include "windivert/windivert.h"

#undef WinDivertRecvEx
#undef WinDivertSend
#undef WinDivertClose
#undef usleep

static const uint8_t *rcv_buf = NULL;	// last receive buffer
static size_t rcv_size = 0;
static const WINDIVERT_ADDRESS *rcv_addr = NULL;	// last receive address array
static unsigned int rcv_addr_count = 0;
static HANDLE run_handle = NULL;	// collected run
static const uint8_t *run_pkt = NULL;
static size_t run_len = 0;
static const WINDIVERT_ADDRESS *run_addr = NULL;
static unsigned int run_count = 0;
static int batch_off = -1;

static void wn_batch_flush(void)
{
	if (run_count)
	{
		UINT sent;
		WinDivertSendEx(run_handle, run_pkt, (UINT)run_len, &sent, 0, run_addr, run_count * sizeof(WINDIVERT_ADDRESS), NULL);
		run_count = 0;
		run_len = 0;
	}
}

BOOL wn_WinDivertRecvEx(HANDLE handle, VOID *pPacket, UINT packetLen, UINT *pRecvLen, UINT64 flags, WINDIVERT_ADDRESS *pAddr, UINT *pAddrLen, LPOVERLAPPED lpOverlapped)
{
	// receive buffer is about to be overwritten
	wn_batch_flush();
	wn_control_poll();
	if (batch_off < 0) batch_off = getenv("WINWS2_NO_BATCH") != NULL;
	rcv_buf = (const uint8_t*)pPacket;
	rcv_size = packetLen;
	rcv_addr = pAddr;
	rcv_addr_count = (pAddr && pAddrLen) ? *pAddrLen / sizeof(WINDIVERT_ADDRESS) : 0;
	return WinDivertRecvEx(handle, pPacket, packetLen, pRecvLen, flags, pAddr, pAddrLen, lpOverlapped);
}

BOOL wn_WinDivertSend(HANDLE handle, const VOID *pPacket, UINT packetLen, UINT *pSendLen, const WINDIVERT_ADDRESS *pAddr)
{
	const uint8_t *pkt = (const uint8_t*)pPacket;

	// packet that lives inside the receive buffer and has its original address is a reinject of unmodified packet.
	// both buffers stay valid until the next receive
	if (!batch_off && rcv_buf && pkt >= rcv_buf && pkt + packetLen <= rcv_buf + rcv_size &&
		rcv_addr && pAddr >= rcv_addr && pAddr < rcv_addr + rcv_addr_count)
	{
		if (run_count && (handle != run_handle || pkt != run_pkt + run_len || pAddr != run_addr + run_count || run_count >= WINDIVERT_BATCH_MAX))
			wn_batch_flush();
		if (!run_count)
		{
			run_handle = handle;
			run_pkt = pkt;
			run_addr = pAddr;
		}
		run_len += packetLen;
		run_count++;
		if (pSendLen) *pSendLen = packetLen;
		return TRUE;
	}
	wn_batch_flush();
	return WinDivertSend(handle, pPacket, packetLen, pSendLen, pAddr);
}

BOOL wn_WinDivertClose(HANDLE handle)
{
	if (run_handle == handle)
	{
		run_handle = NULL;
		run_count = 0;
		run_len = 0;
	}
	// the receive buffer may be freed after the close. next receive sets it again
	rcv_buf = NULL;
	rcv_addr = NULL;
	rcv_addr_count = 0;
	return WinDivertClose(handle);
}

// upstream calls usleep(0) to "make signals working" before it checks the quit flag and the logical network.
// do what cygwin did there : send what is held and deliver the pending control events.
// the waiting loop "logical network is not present" sleeps by usleep too and gets the stop request this way
int wn_usleep(useconds_t us)
{
	wn_batch_flush();
	int r = usleep(us);
	wn_control_poll();
	return r;
}
