/*
 *  $Id: device.c,v 1.1.1.1 2004/12/30 22:44:00 lordjaxom Exp $
 */
 
#include "client/device.h"
#include "client/setup.h"
#include "client/assembler.h"
#include "client/filter.h"

#include "tools/select.h"
#include "tools/string.h"

#include <vdr/channels.h>
#include <vdr/ringbuffer.h>
#include <vdr/eit.h>
#include <vdr/timers.h>

#include <time.h>
#include <iostream>

using namespace std;

#define VIDEOBUFSIZE MEGABYTE(3)

cStreamdevDevice *cStreamdevDevice::m_Device = NULL;

cStreamdevDevice::cStreamdevDevice(void) {
	m_Channel    = NULL;
	m_TSBuffer   = NULL;
	m_Assembler  = NULL;

#if VDRVERSNUM < 10300
#	if defined(HAVE_AUTOPID)
	(void)new cSIProcessor(new cSectionsScanner(""));
#	else
	(void)new cSIProcessor("");
# endif
	cSIProcessor::Read();
#else
	m_Filters    = new cStreamdevFilters;
	StartSectionHandler();
	cSchedules::Read();
#endif

	m_Device = this;

	if (StreamdevClientSetup.SyncEPG)	
		ClientSocket.SynchronizeEPG();
}

cStreamdevDevice::~cStreamdevDevice() {
	Dprintf("Device gets destructed\n");
	m_Device = NULL;
	delete m_TSBuffer;
	delete m_Assembler;
#if VDRVERSNUM >= 10300
	delete m_Filters;
#endif
}

bool cStreamdevDevice::ProvidesSource(int Source) const {
	Dprintf("ProvidesSource, Source=%d\n", Source);
	return false;
}

bool cStreamdevDevice::ProvidesChannel(const cChannel *Channel, int Priority, 
		bool *NeedsDetachReceivers) const {
	bool res = false;
	bool prio = Priority < 0 || Priority > this->Priority();
	bool ndr = false;
	Dprintf("ProvidesChannel, Channel=%s, Prio=%d\n", Channel->Name(), Priority);

	if (ClientSocket.DataSocket(siLive) != NULL 
			&& TRANSPONDER(Channel, m_Channel))
		res = true;
	else {
		res = prio && ClientSocket.ProvidesChannel(Channel, Priority);
		ndr = true;
	}
	
	if (NeedsDetachReceivers)
		*NeedsDetachReceivers = ndr;
	Dprintf("prov res = %d, ndr = %d\n", res, ndr);
	return res;
}

bool cStreamdevDevice::SetChannelDevice(const cChannel *Channel, 
		bool LiveView) {
	Dprintf("SetChannelDevice Channel: %s, LiveView: %s\n", Channel->Name(),
			LiveView ? "true" : "false");

	if (LiveView)
		return false;

	if (ClientSocket.DataSocket(siLive) != NULL 
			&& TRANSPONDER(Channel, m_Channel))
		return true;

	m_Channel = Channel;
	return ClientSocket.SetChannelDevice(m_Channel);
}

bool cStreamdevDevice::SetPid(cPidHandle *Handle, int Type, bool On) {
	Dprintf("SetPid, Pid=%d, Type=%d, On=%d, used=%d\n", Handle->pid, Type, On,
			Handle->used);
	if (Handle->pid && (On || !Handle->used))
		return ClientSocket.SetPid(Handle->pid, On);
	return true;
}

bool cStreamdevDevice::OpenDvr(void) {
	Dprintf("OpenDvr\n");
	if (ClientSocket.CreateDataConnection(siLive)) {
		m_Assembler = new cStreamdevAssembler(ClientSocket.DataSocket(siLive));
		m_TSBuffer = new cTSBuffer(m_Assembler->ReadPipe(), MEGABYTE(2), 
				CardIndex() + 1);
		Dprintf("waiting\n");
		m_Assembler->WaitForFill();
		Dprintf("resuming\n");
		return true;
	}
	return false;
}

void cStreamdevDevice::CloseDvr(void) {
	Dprintf("CloseDvr\n");

	ClientSocket.CloseDvr();
	DELETENULL(m_TSBuffer);
	DELETENULL(m_Assembler);
}

bool cStreamdevDevice::GetTSPacket(uchar *&Data) {
  if (m_TSBuffer) {
    int r;
    while ((r = m_TSBuffer->Read()) >= 0) {
      Data = m_TSBuffer->Get();
#if VDRVERSNUM >= 10300
			if (Data != NULL) {
				u_short pid = (((u_char)Data[1] & PID_MASK_HI) << 8) | Data[2];
				u_char tid = Data[3];
				if (m_Filters->Matches(pid, tid)) {
					m_Filters->Put(Data);
					continue;
				}
			}
#endif
      return true;
    } 
		if (FATALERRNO) {
      LOG_ERROR;
    	return false;
    }
  	return true;
	}
  return false;
}

#if VDRVERSNUM >= 10300
int cStreamdevDevice::OpenFilter(u_short Pid, u_char Tid, u_char Mask) {
	Dprintf("OpenFilter\n");
	if (StreamdevClientSetup.StreamFilters 
			&& ClientSocket.SetFilter(Pid, Tid, Mask, true)) {
		return m_Filters->OpenFilter(Pid, Tid, Mask);
	} else
		return -1;
}
#endif

bool cStreamdevDevice::Init(void) {
	if (m_Device == NULL && StreamdevClientSetup.StartClient)
		new cStreamdevDevice;
	return true;
}

bool cStreamdevDevice::ReInit(void) {
	ClientSocket.Quit();
	ClientSocket.Reset();
	if (m_Device != NULL) {
		DELETENULL(m_Device->m_TSBuffer);
		DELETENULL(m_Device->m_Assembler);
	}
	return StreamdevClientSetup.StartClient ? Init() : true;
}

