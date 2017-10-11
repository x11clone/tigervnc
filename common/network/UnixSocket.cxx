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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define closesocket close

#include <unistd.h>
#include <network/UnixSocket.h>
#include <rfb/util.h>

using namespace network;
using namespace rdr;

UnixSocket::UnixSocket(int sock, bool close, int bufSize_)
  : Socket(new FdInStream(sock), new FdOutStream(sock, true, -1, bufSize_), true), closeFd(close)
{
}

UnixSocket::~UnixSocket() {
  if (closeFd) {
    closesocket(getFd());
  }
}

void UnixSocket::shutdown()
{
  Socket::shutdown();
  ::shutdown(getFd(), 2);
}

bool UnixSocket::cork(bool enable)
{
  return false;
}

int UnixSocket::getMyPort() {
  return 0;
}

char* UnixSocket::getPeerAddress() {
  return rfb::strDup("UNIX Socket");
}

int UnixSocket::getPeerPort() {
  return 0;
}

char* UnixSocket::getPeerEndpoint() {
  return rfb::strDup("UNIX Socket");
}

bool UnixSocket::sameMachine() {
  return true;
}
