/*
 * RequestReaderHandler.cpp
 *
 *  Created on: 02-Jan-2015
 *      Author: sumeetc
 */

#include "RequestReaderHandler.h"

RequestReaderHandler::RequestReaderHandler(ServiceHandler* shi, const SOCKET& listenerSock) {
	this->shi = shi;
	this->listenerSock = listenerSock;
	this->isNotRegisteredListener = (listenerSock == INVALID_SOCKET);
	this->run = false;
	this->siIdentifierSeries = 1;
	this->sf = NULL;
}

void RequestReaderHandler::switchReaders(SocketInterface* prev, SocketInterface* next) {
	readerSwitchedSocks.push(next);
}

void RequestReaderHandler::registerRead(SocketInterface* si) {
	selector.registerForEvent(si->getDescriptor());
}

void RequestReaderHandler::start() {
	if(!run) {
		cMutex.lock();
		if(run) {
			cMutex.unlock();
			return;
		}
		run = true;
		selector.initialize(listenerSock, -1);
		Thread thr(&handle, this);
		thr.execute();
		Thread hthr(&handleTimeouts, this);
		hthr.execute();
		cMutex.unlock();
	}
}

void RequestReaderHandler::stop() {
	cMutex.lock();
	run = false;
	cMutex.unlock();
}

bool RequestReaderHandler::isActive() {
	bool fl = false;
	cMutex.lock();
	fl = run;
	cMutex.unlock();
	return fl;
}

void RequestReaderHandler::registerSocketInterfaceFactory(const SocketInterfaceFactory& f) {
	this->sf = f;
}

void RequestReaderHandler::addSf(SocketInterface* psi) {
	psi->t.start();
	psi->sockUtil->sel = &selector;
	psi->setIdentifier(siIdentifierSeries++);
	connections[psi->getDescriptor()] = psi;
	if(psi->getTimeout()>0)
	{
		addToTimeoutSocks.push(psi);
	}
	selector.registerForEvent(psi->getDescriptor());
	shi->addOpenRequest(psi);
	psi->onOpen();
}

RequestReaderHandler::~RequestReaderHandler() {
	stop();
}

void* RequestReaderHandler::handleTimeouts(void* inp) {
	Logger logger = LoggerFactory::getLogger("RequestReaderHandler");
	RequestReaderHandler* ins  = static_cast<RequestReaderHandler*>(inp);
	std::map<int, SocketInterface*>::iterator it;
	while(ins->isActive())
	{
		Timer cdt;

		SocketInterface* asi = NULL;
		cdt.start();
		while(ins->remFromTimeoutSocks.pop(asi) && cdt.elapsedMilliSeconds()<1000)
		{
			ins->connectionsWithTimeouts.erase(asi->getDescriptor());
		}

		asi = NULL;
		cdt.start();
		while(ins->addToTimeoutSocks.pop(asi) && cdt.elapsedMilliSeconds()<1000)
		{
			ins->connectionsWithTimeouts[asi->getDescriptor()] = asi;
		}

		ins->shi->cleanSif(ins->connectionsWithTimeouts);

		if(!ins->connectionsWithTimeouts.empty())
		{
			for(it=ins->connectionsWithTimeouts.begin();it!=ins->connectionsWithTimeouts.end();)
			{
				if(it->second->t.elapsedMilliSeconds()>=it->second->getTimeout())
				{
					logger << "timedout connection " << it->second->getDescriptor() << " " << it->second->identifier << std::endl;
					ins->timedoutSocks.push(it->second);
					ins->connectionsWithTimeouts.erase(it++);
				}
				else
				{
					it++;
				}
			}
		}
		Thread::sSleep(1);
	}
	return NULL;
}

void* RequestReaderHandler::handle(void* inp) {
	Logger logger = LoggerFactory::getLogger("RequestReaderHandler");
	RequestReaderHandler* ins  = static_cast<RequestReaderHandler*>(inp);
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	while(ins->isActive())
	{
		//Timer t0;
		//t0.start();

		Timer cdt(false);

		if(ins->isNotRegisteredListener)
		{
			cdt.start();
			std::vector<SocketInterface*> pds;
			SocketInterface* psi = NULL;
			while(ins->pendingSocks.pop(psi) && cdt.elapsedMilliSeconds()<100)
			{
				if(ins->connections.find(psi->getDescriptor())==ins->connections.end()) {
					ins->addSf(psi);
				} else {
					pds.push_back(psi);
				}
			}
			for (int var = 0; var < (int)pds.size(); ++var) {
				ins->pendingSocks.push(pds.at(var));
				logger << "existing connection " << pds.at(var)->getDescriptor() << std::endl;
			}
		}

		cdt.start();
		SocketInterface* tsi = NULL;
		while(ins->timedoutSocks.pop(tsi) && cdt.elapsedMilliSeconds()<100)
		{
			if(!tsi->isClosed())tsi->close();
			ins->selector.unRegisterForEvent(tsi->getDescriptor());
			ins->connections.erase(tsi->getDescriptor());
			ins->shi->addCloseRequest(tsi);
		}

		cdt.start();
		SocketInterface* rsi = NULL;
		while(ins->readerSwitchedSocks.pop(rsi) && cdt.elapsedMilliSeconds()<100)
		{
			logger << "Swicthing protocols.." << std::endl;
			if(ins->connections[rsi->getDescriptor()]->getTimeout()>0)
			{
				ins->remFromTimeoutSocks.push(rsi);
			}
			delete ins->connections[rsi->getDescriptor()];
			ins->connections[rsi->getDescriptor()] = rsi;
			if(rsi->getTimeout()>0)
			{
				ins->addToTimeoutSocks.push(rsi);
			}
		}
		//t0.end();
		//CommonUtils::tsPoll1 += t0.timerMilliSeconds();

		//Timer t1;
		//t1.start();
		int num = ins->selector.getEvents();
		//t1.end();
		//CommonUtils::tsPoll += t1.timerMilliSeconds();

		if (num<=0)
		{
			if(num==-1) {
				//print errors
			}
			continue;
		}

		Timer t2;
		t2.start();
		for(int n=0;n<num;n++)
		{
			SOCKET descriptor = ins->selector.getDescriptor(n);
			if(descriptor!=-1)
			{
				if(ins->selector.isListeningDescriptor(descriptor))
				{
#if defined USE_EPOLL && defined(USE_EPOLL_ET)
					while (true) {
						sin_size = sizeof their_addr;
						SOCKET newSocket = accept(ins->listenerSock, (struct sockaddr *)&(their_addr), &sin_size);
						if (newSocket == -1)
						{
						  if ((errno == EAGAIN) ||
							  (errno == EWOULDBLOCK))
							{
							  /* We have processed all incoming
								 connections. */
							  break;
							}
						}
						SocketUtil* sockUtil = new SocketUtil(newSocket);
						SocketInterface* sockIntf = ins->sf(sockUtil);
						ins->addSf(sockIntf);

						//CommonUtils::cSocks += 1;
					}
#else
					sin_size = sizeof their_addr;
					SOCKET newSocket = accept(ins->listenerSock, (struct sockaddr *)&(their_addr), &sin_size);
					SocketUtil* sockUtil = new SocketUtil(newSocket);
					SocketInterface* sockIntf = ins->sf(sockUtil);
					ins->addSf(sockIntf);

					//CommonUtils::cSocks += 1;
#endif
				}
				else
				{
					if(ins->connections.find(descriptor)==ins->connections.end()) {
						//logger << "IDHAR KAISE AAYA@@@@@@@@" << std::endl;
						ins->selector.unRegisterForEvent(descriptor);
						continue;
					}
					SocketInterface* si = ins->connections[descriptor];
					int pending = 1;
					while(pending>0 && ins->shi->isAvailable(si))
					{
						void* context = NULL;
						int reqPos = 0;
						void* request = si->readRequest(context, pending, reqPos);
						if(si->isClosed()) {
							si->onClose();
							ins->selector.unRegisterForEvent(si->getDescriptor());
							ins->connections.erase(si->getDescriptor());
							ins->remFromTimeoutSocks.push(si);
							ins->shi->addCloseRequest(si);
							pending = 0;
							break;
						} else if(request!=NULL) {
							//ins->selector.unRegisterForEvent(si->getDescriptor());
							ins->shi->registerServiceRequest(request, si, context, reqPos, ins);
							//CommonUtils::cReqs += 1;
						}
					}
				}
			}
		}

		//t2.end();
		//CommonUtils::tsProcess += t2.timerMilliSeconds();
	}

	Thread::mSleep(600);
	std::map<int, bool> donelist;
	SocketInterface* si = NULL;
	while(ins->pendingSocks.pop(si))
	{
		donelist[si->getDescriptor()] = true;
		si->close();
		delete si->sockUtil;
		delete si;
	}
	std::map<int, SocketInterface*>::iterator it;
	for(it=ins->connections.begin();it!=ins->connections.end();++it) {
		if(donelist.find(it->first)==donelist.end()) {
			it->second->close();
			delete it->second->sockUtil;
			delete it->second;
		}
	}
	ins->connections.clear();
	ins->connectionsWithTimeouts.clear();
	si = NULL;
	while(ins->timedoutSocks.pop(si))
	{
	}
	si = NULL;
	while(ins->readerSwitchedSocks.pop(si))
	{
	}
	si = NULL;
	while(ins->addToTimeoutSocks.pop(si))
	{
	}
	si = NULL;
	while(ins->remFromTimeoutSocks.pop(si))
	{
	}
	Thread::mSleep(500);
	return 0;
}
