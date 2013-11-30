/*
	Copyright 2009-2012, Sumeet Chhetri 
  
    Licensed under the Apache License, Version 2.0 (the "License"); 
    you may not use this file except in compliance with the License. 
    You may obtain a copy of the License at 
  
        http://www.apache.org/licenses/LICENSE-2.0 
  
    Unless required by applicable law or agreed to in writing, software 
    distributed under the License is distributed on an "AS IS" BASIS, 
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
    See the License for the specific language governing permissions and 
    limitations under the License.  
*/
/*
 * DistoCacheHandler.cpp
 *
 *  Created on: Mar 26, 2010
 *      Author: sumeet
 */

#include "DistoCacheHandler.h"

DistoCacheHandler* _dstcache_instance = NULL;

DistoCacheHandler::DistoCacheHandler()
{
	logger = LoggerFactory::getLogger("DistoCacheHandler");
}

DistoCacheHandler::~DistoCacheHandler()
{

}

void DistoCacheHandler::init()
{
	if(_dstcache_instance==NULL)
	{
		_dstcache_instance = new DistoCacheHandler();
		_dstcache_instance->running = false;
	}
}

void* DistoCacheHandler::service(void* arg)
{
	int fd = *(int*)arg;
	DistoCacheServiceHandler *task = new DistoCacheServiceHandler(fd);
	task->run();
	close(fd);
	return NULL;
}

void DistoCacheHandler::trigger(string port, int poolSize)
{
	init();
	if(_dstcache_instance->running)
		return;
	NBServer serv(port,500,&service);
	_dstcache_instance->server = serv;
	_dstcache_instance->server.start();
	_dstcache_instance->running = true;
	CacheMap::init();
	PooledDistoCacheConnectionFactory::init("localhost", CastUtil::lexical_cast<int>(port), poolSize);
	_dstcache_instance->logger << ("Distocache running on port" + port) << endl;
	return;
}

void DistoCacheHandler::stop()
{
	_dstcache_instance->server.stop();
	PooledDistoCacheConnectionFactory::destroy();
}