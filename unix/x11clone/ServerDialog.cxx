/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2012 Samuel Mannehed <samuel@cendio.se> for Cendio AB
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

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>

#include "ServerDialog.h"
#include "OptionsDialog.h"
#include "vncviewer/fltk_layout.h"
#include "i18n.h"
#include "x11clone.h"
#include "parameters.h"
#include "rfb/Exception.h"

ServerDialog::ServerDialog()
  : Fl_Window(450, 160, _("x11clone: Connection Details"))
{
  int x, y;
  Fl_Button *button;
  Fl_Box *divider;

  int margin = 20;
  int server_label_width = gui_str_len(_("Parent display: "));

  x = margin + server_label_width;
  y = margin;
  
  serverName = new Fl_Input(x, y, w() - margin*2 - server_label_width, INPUT_HEIGHT, _("Parent display: "));

  int adjust = (w() - 20) / 4;
  int button_width = adjust - margin/2;

  x = margin;
  y = margin + margin/2 + INPUT_HEIGHT;

  y += margin/2;

  button = new Fl_Button(x, y, button_width, BUTTON_HEIGHT, _("Options..."));
  button->callback(this->handleOptions, this);
  
  x = 0;
  y += margin/2 + BUTTON_HEIGHT;

  divider = new Fl_Box(x, y, w(), 2);
  divider->box(FL_THIN_DOWN_FRAME);
  
  x += margin;
  y += margin/2;

  button = new Fl_Button(x, y, button_width, BUTTON_HEIGHT, _("About..."));
  button->callback(this->handleAbout, this);

  x = w() - margin - adjust - button_width - 20;

  button = new Fl_Button(x, y, button_width, BUTTON_HEIGHT, _("Cancel"));
  button->callback(this->handleCancel, this);

  x += adjust;

  button = new Fl_Return_Button(x, y, button_width+20, BUTTON_HEIGHT, _("Connect"));
  button->callback(this->handleConnect, this);

  callback(this->handleCancel, this);

  set_modal();
}


ServerDialog::~ServerDialog()
{
}


void ServerDialog::run(const char* servername, char *newservername)
{
  ServerDialog dialog;

  dialog.serverName->value(servername);
  
  dialog.show();
  while (dialog.shown()) Fl::wait();

  if (dialog.serverName->value() == NULL) {
    newservername[0] = '\0';
    return;
  }

  strncpy(newservername, dialog.serverName->value(), SERVERNAMELEN);
  newservername[SERVERNAMELEN - 1] = '\0';
}

void ServerDialog::handleOptions(Fl_Widget *widget, void *data)
{
  OptionsDialog::showDialog();
}


void ServerDialog::handleAbout(Fl_Widget *widget, void *data)
{
  about_x11clone();
}


void ServerDialog::handleCancel(Fl_Widget *widget, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  dialog->serverName->value(NULL);
  dialog->hide();
}


void ServerDialog::handleConnect(Fl_Widget *widget, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  dialog->hide();
  
}
