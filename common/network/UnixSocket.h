/* Copyright (C) 2017 Peter Åstrand for Cendio AB.  All Rights Reserved.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifndef __NETWORK_UNIX_SOCKET_H__
#define __NETWORK_UNIX_SOCKET_H__

#include <network/Socket.h>

#include <sys/socket.h> /* for socklen_t */
#include <netinet/in.h> /* for struct sockaddr_in */

namespace network {

  class UnixSocket : public Socket {
  public:
    UnixSocket(int sock, bool close=true, int bufSize=0);
    virtual ~UnixSocket();

    virtual int getMyPort();

    virtual char* getPeerAddress();
    virtual int getPeerPort();
    virtual char* getPeerEndpoint();
    virtual bool sameMachine();

    virtual void shutdown();
    virtual bool cork(bool enable);
  private:
    bool closeFd;
  };

}

#endif // __NETWORK_UNIX_SOCKET_H__

