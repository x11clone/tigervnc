/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright (C) 2011 D. R. Commander.  All Rights Reserved.
 * Copyright 2009-2014 Pierre Ossman for Cendio AB
 * Copyright 2017 Peter Astrand <astrand@cendio.se> for Cendio AB
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

#include <assert.h>
#include <unistd.h>

#include <rfb/CMsgWriter.h>
#include <rfb/CSecurity.h>
#include <rfb/LogWriter.h>
#include <rfb/Security.h>
#include <rfb/util.h>
#include <rfb/screenTypes.h>
#include <rfb/fenceTypes.h>
#include <rfb/Timer.h>
#include <rdr/MemInStream.h>
#include <rdr/MemOutStream.h>
#include <network/Socket.h>

#include <FL/Fl.H>
#include <FL/fl_ask.H>

#include "CConn.h"
#include "OptionsDialog.h"
#include "DesktopWindow.h"
#include "PlatformPixelBuffer.h"
#include "i18n.h"
#include "parameters.h"
#include "x11clone.h"


using namespace rdr;
using namespace rfb;
using namespace std;

static rfb::LogWriter vlog("CConn");


CConn::CConn(network::Socket* socket)
  : desktop(NULL),
    frameCount(0), pixelCount(0), pendingPFChange(false),
    lastServerEncoding((unsigned int)-1),
    formatChange(false), encodingChange(false),
    firstUpdate(true), pendingUpdate(false), continuousUpdates(false),
    forceNonincremental(true), supportsSyncFence(false)
{
  sock = socket;

  cp.supportsLocalCursor = true;

  cp.supportsDesktopResize = true;
  cp.supportsExtendedDesktopSize = true;
  cp.supportsDesktopRename = true;

  cp.supportsLEDState = true;

  Fl::add_fd(sock->getFd(), FL_READ | FL_EXCEPT, socketEvent, this);

  // See callback below
  sock->inStream().setBlockCallback(this);

  setStreams(&sock->inStream(), &sock->outStream());

  initialiseProtocol();

  OptionsDialog::addCallback(handleOptions, this);
}

CConn::~CConn()
{
  OptionsDialog::removeCallback(handleOptions);
  Fl::remove_timeout(handleUpdateTimeout, this);

  if (desktop)
    delete desktop;

  if (sock)
    Fl::remove_fd(sock->getFd());

  delete sock;
}

void CConn::refreshFramebuffer()
{
  forceNonincremental = true;

  // Without fences, we cannot safely trigger an update request directly
  // but must wait for the next update to arrive.
  if (supportsSyncFence)
    requestNewUpdate();
}

const char *CConn::connectionInfo()
{
  static char infoText[1024] = "";

  char scratch[100];

  // Crude way of avoiding constant overflow checks
  assert((sizeof(scratch) + 1) * 10 < sizeof(infoText));

  infoText[0] = '\0';

  snprintf(scratch, sizeof(scratch),
           _("Parent display: %.80s"), cp.name());
  strcat(infoText, scratch);
  strcat(infoText, "\n");

  snprintf(scratch, sizeof(scratch),
           _("Size: %d x %d"), cp.width, cp.height);
  strcat(infoText, scratch);
  strcat(infoText, "\n");

  return infoText;
}

unsigned CConn::getFrameCount()
{
  return frameCount;
}

unsigned CConn::getPixelCount()
{
  return pixelCount;
}

unsigned CConn::getPosition()
{
  return sock->inStream().pos();
}

// The RFB core is not properly asynchronous, so it calls this callback
// whenever it needs to block to wait for more data. Since FLTK is
// monitoring the socket, we just make sure FLTK gets to run.

void CConn::blockCallback()
{
  run_mainloop();

  if (should_exit())
    throw rdr::Exception("Termination requested");
}

void CConn::socketEvent(FL_SOCKET fd, void *data)
{
  CConn *cc;
  static bool recursing = false;

  assert(data);
  cc = (CConn*)data;

  // I don't think processMsg() is recursion safe, so add this check
  if (recursing)
    return;

  recursing = true;

  try {
    // processMsg() only processes one message, so we need to loop
    // until the buffers are empty or things will stall.
    do {
      cc->processMsg();

      // Make sure that the FLTK handling and the timers gets some CPU
      // time in case of back to back messages
       Fl::check();
       Timer::checkTimeouts();

       // Also check if we need to stop reading and terminate
       if (should_exit())
         break;
    } while (cc->sock->inStream().checkNoWait(1));
  } catch (rdr::EndOfStream& e) {
    vlog.info("%s", e.str());
    exit_x11clone();
  } catch (rdr::Exception& e) {
    vlog.error("%s", e.str());
    // Somebody might already have requested us to terminate, and
    // might have already provided an error message.
    if (!should_exit())
      exit_x11clone(e.str());
  }

  recursing = false;
}

////////////////////// CConnection callback methods //////////////////////

// serverInit() is called when the serverInit message has been received.  At
// this point we create the desktop window and display it.  We also tell the
// server the pixel format and encodings to use and request the first update.
void CConn::serverInit()
{
  CConnection::serverInit();

  serverPF = cp.pf();

  desktop = new DesktopWindow(cp.width, cp.height, cp.name(), serverPF, this);
  fullColourPF = desktop->getPreferredPF();

  // Force a switch to the format and encoding we'd like
  formatChange = encodingChange = true;

  // And kick off the update cycle
  requestNewUpdate();

  // This initial update request is a bit of a corner case, so we need
  // to help out setting the correct format here.
  assert(pendingPFChange);
  cp.setPF(pendingPF);
  pendingPFChange = false;
}

// setDesktopSize() is called when the desktop size changes (including when
// it is set initially).
void CConn::setDesktopSize(int w, int h)
{
  CConnection::setDesktopSize(w,h);
  resizeFramebuffer();
}

// setExtendedDesktopSize() is a more advanced version of setDesktopSize()
void CConn::setExtendedDesktopSize(unsigned reason, unsigned result,
                                   int w, int h, const rfb::ScreenSet& layout)
{
  CConnection::setExtendedDesktopSize(reason, result, w, h, layout);

  if ((reason == reasonClient) && (result != resultSuccess)) {
    vlog.error(_("SetDesktopSize failed: %d"), result);
    return;
  }

  resizeFramebuffer();
}

// setName() is called when the desktop name changes
void CConn::setName(const char* name)
{
  CConnection::setName(name);
  if (desktop)
    desktop->setName(name);
}

// framebufferUpdateStart() is called at the beginning of an update.
// Here we try to send out a new framebuffer update request so that the
// next update can be sent out in parallel with us decoding the current
// one.
void CConn::framebufferUpdateStart()
{
  CConnection::framebufferUpdateStart();

  // Note: This might not be true if sync fences are supported
  pendingUpdate = false;

  requestNewUpdate();

  // Update the screen prematurely for very slow updates
  Fl::add_timeout(1.0, handleUpdateTimeout, this);
}

// framebufferUpdateEnd() is called at the end of an update.
// For each rectangle, the FdInStream will have timed the speed
// of the connection, allowing us to select format and encoding
// appropriately, and then request another incremental update.
void CConn::framebufferUpdateEnd()
{
  CConnection::framebufferUpdateEnd();

  frameCount++;

  Fl::remove_timeout(handleUpdateTimeout, this);
  desktop->updateWindow();

  if (firstUpdate) {
    // We need fences to make extra update requests and continuous
    // updates "safe". See fence() for the next step.
    if (cp.supportsFence)
      writer()->writeFence(fenceFlagRequest | fenceFlagSyncNext, 0, NULL);

    firstUpdate = false;
  }

  // A format change has been scheduled and we are now past the update
  // with the old format. Time to active the new one.
  if (pendingPFChange) {
    cp.setPF(pendingPF);
    pendingPFChange = false;
  }

}

// The rest of the callbacks are fairly self-explanatory...

void CConn::setColourMapEntries(int firstColour, int nColours, rdr::U16* rgbs)
{
  vlog.error(_("Invalid SetColourMapEntries from server!"));
}

void CConn::bell()
{
  fl_beep();
}

void CConn::serverCutText(const char* str, rdr::U32 len)
{
  char *buffer;
  int size, ret;

  if (!acceptClipboard)
    return;

  size = fl_utf8froma(NULL, 0, str, len);
  if (size <= 0)
    return;

  size++;

  buffer = new char[size];

  ret = fl_utf8froma(buffer, size, str, len);
  assert(ret < size);

  vlog.debug("Got clipboard data (%d bytes)", (int)strlen(buffer));

  // RFB doesn't have separate selection and clipboard concepts, so we
  // dump the data into both variants.
  if (setPrimary)
    Fl::copy(buffer, ret, 0);
  Fl::copy(buffer, ret, 1);

  delete [] buffer;
}

void CConn::dataRect(const Rect& r, int encoding)
{
  sock->inStream().startTiming();

  if (encoding != encodingCopyRect)
    lastServerEncoding = encoding;

  CConnection::dataRect(r, encoding);

  sock->inStream().stopTiming();

  pixelCount += r.area();
}

void CConn::setCursor(int width, int height, const Point& hotspot,
                      const rdr::U8* data)
{
  desktop->setCursor(width, height, hotspot, data);
}

void CConn::fence(rdr::U32 flags, unsigned len, const char data[])
{
  CMsgHandler::fence(flags, len, data);

  if (flags & fenceFlagRequest) {
    // We handle everything synchronously so we trivially honor these modes
    flags = flags & (fenceFlagBlockBefore | fenceFlagBlockAfter);

    writer()->writeFence(flags, len, data);
    return;
  }

  if (len == 0) {
    // Initial probe
    if (flags & fenceFlagSyncNext) {
      supportsSyncFence = true;

      if (cp.supportsContinuousUpdates) {
        vlog.info(_("Enabling continuous updates"));
        continuousUpdates = true;
        writer()->writeEnableContinuousUpdates(true, 0, 0, cp.width, cp.height);
      }
    }
  } else {
    // Pixel format change
    rdr::MemInStream memStream(data, len);
    PixelFormat pf;

    pf.read(&memStream);

    cp.setPF(pf);
  }
}

void CConn::setLEDState(unsigned int state)
{
  CConnection::setLEDState(state);

  desktop->setLEDState(state);
}


////////////////////// Internal methods //////////////////////

void CConn::resizeFramebuffer()
{
  if (!desktop)
    return;

  if (continuousUpdates)
    writer()->writeEnableContinuousUpdates(true, 0, 0, cp.width, cp.height);

  desktop->resizeFramebuffer(cp.width, cp.height);
}

// checkEncodings() sends a setEncodings message if one is needed.
void CConn::checkEncodings()
{
  if (encodingChange && writer()) {
    vlog.info(_("Using %s encoding"),encodingName(encodingRaw));

    int nEncodings = 0;
    rdr::U32 encodings[encodingMax+3];

    if (cp.supportsLocalCursor) {
	encodings[nEncodings++] = pseudoEncodingCursorWithAlpha;
	encodings[nEncodings++] = pseudoEncodingCursor;
	encodings[nEncodings++] = pseudoEncodingXCursor;
    }
    if (cp.supportsDesktopResize)
	encodings[nEncodings++] = pseudoEncodingDesktopSize;
    if (cp.supportsExtendedDesktopSize)
	encodings[nEncodings++] = pseudoEncodingExtendedDesktopSize;
    if (cp.supportsDesktopRename)
	encodings[nEncodings++] = pseudoEncodingDesktopName;
    if (cp.supportsLEDState)
	encodings[nEncodings++] = pseudoEncodingLEDState;

    encodings[nEncodings++] = pseudoEncodingLastRect;
    encodings[nEncodings++] = pseudoEncodingContinuousUpdates;
    encodings[nEncodings++] = pseudoEncodingFence;
    encodings[nEncodings++] = pseudoEncodingQEMUKeyEvent;

    encodings[nEncodings++] = encodingRaw;
    encodings[nEncodings++] = encodingCopyRect;
    encodings[nEncodings++] = encodingRRE;

    writer()->writeSetEncodings(nEncodings, encodings);

    encodingChange = false;
  }
}

// requestNewUpdate() requests an update from the server, having set the
// format and encoding appropriately.
void CConn::requestNewUpdate()
{
  if (formatChange) {
    PixelFormat pf;

    /* Catch incorrect requestNewUpdate calls */
    assert(!pendingUpdate || supportsSyncFence);

    pf = fullColourPF;
    if (supportsSyncFence) {
      // We let the fence carry the pixel format and switch once we
      // get the response back. That way we will be synchronised with
      // when the server switches.
      rdr::MemOutStream memStream;

      pf.write(&memStream);

      writer()->writeFence(fenceFlagRequest | fenceFlagSyncNext,
                           memStream.length(), (const char*)memStream.data());
    } else {
      // New requests are sent out at the start of processing the last
      // one, so we cannot switch our internal format right now (doing so
      // would mean misdecoding the current update).
      pendingPFChange = true;
      pendingPF = pf;
    }

    char str[256];
    pf.print(str, 256);
    vlog.info(_("Using pixel format %s"),str);
    writer()->writeSetPixelFormat(pf);

    formatChange = false;
  }

  checkEncodings();

  if (forceNonincremental || !continuousUpdates) {
    pendingUpdate = true;
    writer()->writeFramebufferUpdateRequest(Rect(0, 0, cp.width, cp.height),
                                            !forceNonincremental);
  }
 
  forceNonincremental = false;
}

void CConn::handleOptions(void *data)
{
  CConn *self = (CConn*)data;

  self->cp.supportsLocalCursor = true;

  // Format changes refreshes the entire screen though and are therefore
  // very costly. It's probably worth the effort to see if it is necessary
  // here.
  PixelFormat pf;

  pf = self->fullColourPF;

  if (!pf.equal(self->cp.pf())) {
    self->formatChange = true;

    // Without fences, we cannot safely trigger an update request directly
    // but must wait for the next update to arrive.
    if (self->supportsSyncFence)
      self->requestNewUpdate();
  }
}

void CConn::handleUpdateTimeout(void *data)
{
  CConn *self = (CConn *)data;

  assert(self);

  self->desktop->updateWindow();

  Fl::repeat_timeout(1.0, handleUpdateTimeout, data);
}
