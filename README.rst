
x11clone
========

Description
-----------

x11clone is a tool which connects two independent X11 displays. It
retrieves the image from the "server display" and displays it in an
application window, created on the "client display". Keyboard and
mouse events in the application window are transferred to the "server
display". This allows you to view and interact with another X11
session.

x11clone is based on TigerVNC_ and is a fusion between x0vncserver and
vncviewer.  In some cases, documentation, log messages etc refers to
the "server" and "client", meaning the components of x11clone
connected to the "server display" and "client display". Please refer
to README.tigervnc for legal information and more information about
TigerVNC.

x11clone can be used in conjunction with ThinLinc_. In particular, it
makes it possible to connect to the local Xserver console (typically
:0) from a ThinLinc_ session.

x11clone can also be used to remotely displaying 3D applications with
server side hardware acceleration. From the application point of view,
the best performance is achieved by running the desktop session and
applications on the console. To access this session remotely, x11clone
can be used from a X11 client (using X11 forwarding). Another option
is to run x11clone with Xvnc and connect with a VNC client. In this
context, x11clone provides an alternative to VirtualGL_. However,
x11clone does not provide any GPU sharing: You are restricted to one
console session per machine. Virtual machines with GPU virtualization
can be used to provide multiple sessions on the same server hardware.


Code and Issues
---------------

Please report any issues on the `Github x11clone project page`_.


Build Requirements
------------------

* CMake (http://www.cmake.org) v2.8 or later

* zlib

* FLTK 1.3.3 or later

* If building native language support (NLS):
   * Gnu gettext 0.14.4 or later
   * See "Building Native Language Support" below.

* X11 development kit


Building x11clone
-----------------

To build x11clone, run::

  cd {source_directory}
  cmake -G "Unix Makefiles" [additional CMake flags]
  make x11clone

You can use the build system to install x11clone into a directory of
your choosing.  To do this, add::

  -DCMAKE_INSTALL_PREFIX={install_directory}

to the CMake command line.


Installing x11clone
-------------------

To install x11clone, run::

  make -C unix/x11clone install


Creating Binary Package
------------------------

To create a tarball with binaries, run::

  make x11clone-tarball


.. _x11clone: https://github.com/x11clone/x11clone
.. _ThinLinc: https://www.cendio.com/thinlinc/
.. _TigerVNC: http://tigervnc.org
.. _Github x11clone project page: https://github.com/x11clone/x11clone
.. _VirtualGL: http://www.virtualgl.org/
