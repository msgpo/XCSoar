/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2011 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

/*

InputEvents

This class is used to control all user and extrnal InputEvents.
This includes some Nmea strings, virtual events (Glide Computer
Evnets) and Keyboard.

What it does not cover is Glide Computer normal processing - this
includes GPS and Vario processing.

What it does include is what to do when an automatic event (switch
to Climb mode) and user events are entered.

It also covers the configuration side of on screen labels.

For further information on config file formats see

source/Common/Data/Input/ALL
doc/html/advanced/input/ALL		http://xcsoar.sourceforge.net/advanced/input/

*/

#include "InputEvents.hpp"
#include "InputConfig.hpp"
#include "InputParser.hpp"
#include "Interface.hpp"
#include "MainWindow.hpp"
#include "Protection.hpp"
#include "LogFile.hpp"
#include "ButtonLabel.hpp"
#include "Profile/Profile.hpp"
#include "LocalPath.hpp"
#include "Asset.hpp"
#include "MenuData.hpp"
#include "IO/ConfiguredFile.hpp"
#include "SettingsMap.hpp"
#include "Screen/Blank.hpp"
#include "Screen/Key.h"
#include "Projection/MapWindowProjection.hpp"
#include "InfoBoxes/InfoBoxManager.hpp"
#include "Language/Language.hpp"
#include "Util/Macros.hpp"

#include <algorithm>
#include <assert.h>
#include <ctype.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>

namespace InputEvents
{
  mode current_mode = InputEvents::MODE_DEFAULT;
  Mutex mutexEventQueue;

  unsigned MenuTimeOut = 0;
  void ProcessMenuTimer();
  void DoQueuedEvents(void);

  bool processGlideComputer_real(unsigned gce_id);
  bool processNmea_real(unsigned key);
};

/**
 * For data generated by xci2cpp.pl.
 */
struct flat_event_map {
  unsigned char mode;

#if defined(ENABLE_SDL) && !defined(ANDROID)
#if defined(SDLK_SCANCODE_MASK) && SDLK_SCANCODE_MASK >= 0x10000
  /* need a bigger type for SDL 1.3+ */
  unsigned key;
#else
  unsigned short key;
#endif
#else
  unsigned char key;
#endif

  unsigned short event;
};

/**
 * For data generated by xci2cpp.pl.
 */
struct flat_label {
  unsigned char mode, location;
  unsigned short event;
  const TCHAR *label;
};

struct flat_gesture_map {
  unsigned char mode;
  unsigned short event;
  const TCHAR *data;
};

static InputConfig input_config;

#define MAX_GCE_QUEUE 10
static int GCE_Queue[MAX_GCE_QUEUE];
#define MAX_NMEA_QUEUE 10
static int NMEA_Queue[MAX_NMEA_QUEUE];

// -----------------------------------------------------------------------
// Initialisation and Defaults
// -----------------------------------------------------------------------

bool InitONCE = false;

// Mapping text names of events to the real thing
struct Text2EventSTRUCT {
  const TCHAR *text;
  pt2Event event;
};

static const Text2EventSTRUCT Text2Event[] = {
#include "InputEvents_Text2Event.cpp"
  { NULL, NULL }
};

// Mapping text names of events to the real thing
static const TCHAR *const Text2GCE[] = {
#include "InputEvents_Text2GCE.cpp"
  NULL
};

// Mapping text names of events to the real thing
static const TCHAR *const Text2NE[] = {
#include "InputEvents_Text2NE.cpp"
  NULL
};

static void
apply_defaults(const TCHAR *const* default_modes,
               const InputConfig::Event *default_events,
               unsigned num_default_events,
               const flat_gesture_map *default_gesture2event,
               const flat_event_map *default_key2event,
               const flat_event_map *default_gc2event,
               const flat_event_map *default_n2event,
               const flat_label *default_labels)
{
  assert(num_default_events <= InputConfig::MAX_EVENTS);

  input_config.clear_mode_map();
  while (*default_modes != NULL)
    input_config.append_mode(*default_modes++);

  input_config.Events_count = num_default_events + 1;
  std::copy(default_events, default_events + num_default_events,
            input_config.Events + 1);

  while (default_gesture2event->event > 0) {
    input_config.Gesture2Event[default_gesture2event->mode].add(default_gesture2event->data,default_gesture2event->event);
    ++default_gesture2event;
  }
  
  while (default_key2event->event > 0) {
    input_config.Key2Event[default_key2event->mode][default_key2event->key] =
      default_key2event->event;
    ++default_key2event;
  }

  while (default_gc2event->event > 0) {
    input_config.GC2Event[default_gc2event->mode][default_gc2event->key] =
      default_gc2event->event;
    ++default_gc2event;
  }

  while (default_n2event->event > 0) {
    input_config.N2Event[default_n2event->mode][default_n2event->key] =
      default_n2event->event;
    ++default_n2event;
  }

  while (default_labels->label != NULL) {
    InputEvents::makeLabel((InputEvents::mode)default_labels->mode,
                           default_labels->label,
                           default_labels->location, default_labels->event);
    ++default_labels;
  }
}

// Read the data files
void
InputEvents::readFile()
{
  LogStartUp(_T("Loading input events file"));

  // clear the GCE and NMEA queues
  mutexEventQueue.Lock();
  std::fill(GCE_Queue, GCE_Queue + MAX_GCE_QUEUE, -1);
  std::fill(NMEA_Queue, NMEA_Queue + MAX_NMEA_QUEUE, -1);
  mutexEventQueue.Unlock();

  // Get defaults
  if (!InitONCE) {
    if (is_altair()) {
      #include "InputEvents_altair.cpp"
      apply_defaults(default_modes,
                     default_events,
                     ARRAY_SIZE(default_events),
                     default_gesture2event,
                     default_key2event, default_gc2event, default_n2event,
                     default_labels);
    } else {
      #include "InputEvents_default.cpp"
      apply_defaults(default_modes,
                     default_events,
                     ARRAY_SIZE(default_events),
                     default_gesture2event,
                     default_key2event, default_gc2event, default_n2event,
                     default_labels);
    }

    InitONCE = true;
  }

  // Read in user defined configuration file
  TLineReader *reader = OpenConfiguredTextFile(szProfileInputFile);
  if (reader != NULL) {
    ::ParseInputFile(input_config, *reader);
    delete reader;
  }
}

struct string_to_key {
  const TCHAR *name;
  unsigned key;
};

static const struct string_to_key string_to_key[] = {
  { _T("APP1"), VK_APP1 },
  { _T("APP2"), VK_APP2 },
  { _T("APP3"), VK_APP3 },
  { _T("APP4"), VK_APP4 },
  { _T("APP5"), VK_APP5 },
  { _T("APP6"), VK_APP6 },
  { _T("F1"), VK_F1 },
  { _T("F2"), VK_F2 },
  { _T("F3"), VK_F3 },
  { _T("F4"), VK_F4 },
  { _T("F5"), VK_F5 },
  { _T("F6"), VK_F6 },
  { _T("F7"), VK_F7 },
  { _T("F8"), VK_F8 },
  { _T("F9"), VK_F9 },
  { _T("F10"), VK_F10 },
  { _T("F11"), VK_F11 },
  { _T("F12"), VK_F12 },
  { _T("LEFT"), VK_LEFT },
  { _T("RIGHT"), VK_RIGHT },
  { _T("UP"), VK_UP },
  { _T("DOWN"), VK_DOWN },
  { _T("RETURN"), VK_RETURN },
  { _T("ESCAPE"), VK_ESCAPE },
  { _T("MENU"), VK_MENU },
  { NULL }
};

unsigned
InputEvents::findKey(const TCHAR *data)
{
  for (const struct string_to_key *p = string_to_key; p->name != NULL; ++p)
    if (_tcscmp(data, p->name) == 0)
      return p->key;

  if (_tcslen(data) == 1)
    return _totupper(data[0]);

  else
    return 0;

}

pt2Event
InputEvents::findEvent(const TCHAR *data)
{
  for (unsigned i = 0; Text2Event[i].text != NULL; ++i)
    if (_tcscmp(data, Text2Event[i].text) == 0)
      return Text2Event[i].event;

  return NULL;
}

int
InputEvents::findGCE(const TCHAR *data)
{
  int i;
  for (i = 0; i < GCE_COUNT; i++) {
    if (_tcscmp(data, Text2GCE[i]) == 0)
      return i;
  }

  return -1;
}

int
InputEvents::findNE(const TCHAR *data)
{
  int i;
  for (i = 0; i < NE_COUNT; i++) {
    if (_tcscmp(data, Text2NE[i]) == 0)
      return i;
  }

  return -1;
}

// Make a new label (add to the end each time)
// NOTE: String must already be copied (allows us to use literals
// without taking up more data - but when loading from file must copy string
void
InputEvents::makeLabel(mode mode_id, const TCHAR* label,
                       unsigned location, unsigned event_id)
{
  input_config.append_menu(mode_id, label, location, event_id);
}

void
InputEvents::setMode(mode mode)
{
  assert((unsigned)mode < input_config.mode_map_count);

  if (mode == current_mode)
    return;

  current_mode = mode;

  // TODO code: Enable this in debug modes
  // for debugging at least, set mode indicator on screen
  /*
  if (thismode == 0) {
    ButtonLabel::SetLabelText(0, NULL);
  } else {
    ButtonLabel::SetLabelText(0, mode);
  }
  */
  ButtonLabel::SetLabelText(0, NULL);

  drawButtons(current_mode);
}

void
InputEvents::setMode(const TCHAR *mode)
{
  int m = input_config.lookup_mode(mode);
  if (m >= 0)
    setMode((InputEvents::mode)m);
}

void
InputEvents::drawButtons(mode Mode)
{
  if (!globalRunningEvent.Test())
    return;

  const Menu &menu = input_config.menus[Mode];
  for (unsigned i = 0; i < menu.MAX_ITEMS; ++i) {
    const MenuItem &item = menu[i];

    ButtonLabel::SetLabelText(i, item.label);
  }
}

InputEvents::mode
InputEvents::getModeID()
{
  return current_mode;
}

// -----------------------------------------------------------------------
// Processing functions - which one to do
// -----------------------------------------------------------------------

// Input is a via the user touching the label on a touch screen / mouse
bool
InputEvents::processButton(unsigned bindex)
{
  if (!globalRunningEvent.Test())
    return false;

  if (bindex >= Menu::MAX_ITEMS)
    return false;

  mode lastMode = getModeID();
  const MenuItem &item = input_config.menus[lastMode][bindex];
  if (!item.defined())
    return false;

  /* JMW illegal, should be done by gui handler loop
  // JMW need a debounce method here..
  if (!Debounce()) return true;
  */

  processGo(item.event);

  // experimental: update button text, macro may change the label
  if (lastMode == getModeID() && item.label != NULL)
    drawButtons(lastMode);

  return true;
}

unsigned
InputEvents::key_to_event(mode mode, unsigned key_code)
{
  if (key_code >= InputConfig::MAX_KEY)
    return 0;

  unsigned event_id = input_config.Key2Event[mode][key_code];
  if (event_id == 0)
    /* not found in this mode - try the default binding */
    event_id = input_config.Key2Event[0][key_code];

  return event_id;
}

/*
  InputEvent::processKey(KeyID);
  Process keys normally brought in by hardware or keyboard presses
  Future will also allow for long and double click presses...
  Return = We had a valid key (even if nothing happens because of Bounce)
*/
bool
InputEvents::processKey(unsigned dWord)
{
  if (!globalRunningEvent.Test())
    return false;

  // get current mode
  InputEvents::mode mode = InputEvents::getModeID();

  // Which key - can be defined locally or at default (fall back to default)
  unsigned event_id = key_to_event(mode, dWord);
  if (event_id == 0)
    return false;

  int bindex = -1;
  InputEvents::mode lastMode = mode;
  const TCHAR *pLabelText = NULL;

  // JMW should be done by gui handler
  // if (!Debounce()) return true;

  const Menu &menu = input_config.menus[mode];
  int i = menu.FindByEvent(event_id);
  if (i >= 0 && menu[i].defined()) {
    bindex = i;
    pLabelText = menu[i].label;
  }

  if (bindex < 0 || ButtonLabel::IsEnabled(bindex))
    InputEvents::processGo(event_id);

  // experimental: update button text, macro may change the value
  if (lastMode == getModeID() && bindex > 0 && pLabelText != NULL)
    drawButtons(lastMode);

  return true;
}

unsigned
InputEvents::gesture_to_event(mode mode, const TCHAR *data)
{
  unsigned event_id = input_config.Gesture2Event[mode].get(data, 0);
  if (event_id == 0)
    /* not found in this mode - try the default binding */
    event_id = input_config.Gesture2Event[0].get(data, 0);
  
  return event_id;
}

bool
InputEvents::processGesture(const TCHAR *data)
{
  // get current mode
  InputEvents::mode mode = InputEvents::getModeID();
  unsigned event_id = gesture_to_event(mode, data);
  if (event_id)
  {
    InputEvents::processGo(event_id);
    return true;
  }
  return false;
}

bool
InputEvents::processNmea(unsigned ne_id)
{
  // add an event to the bottom of the queue
  mutexEventQueue.Lock();
  for (int i = 0; i < MAX_NMEA_QUEUE; i++) {
    if (NMEA_Queue[i] == -1) {
      NMEA_Queue[i] = ne_id;
      break;
    }
  }
  mutexEventQueue.Unlock();
  return true; // ok.
}

/*
  InputEvent::processNmea(TCHAR* data)
  Take hard coded inputs from NMEA processor.
  Return = TRUE if we have a valid key match
*/
bool
InputEvents::processNmea_real(unsigned ne_id)
{
  if (!globalRunningEvent.Test())
    return false;

  int event_id = 0;

  // Valid input ?
  if (ne_id >= NE_COUNT)
    return false;

  // get current mode
  InputEvents::mode mode = InputEvents::getModeID();

  // Which key - can be defined locally or at default (fall back to default)
  event_id = input_config.N2Event[mode][ne_id];
  if (event_id == 0) {
    // go with default key..
    event_id = input_config.N2Event[0][ne_id];
  }

  if (event_id > 0) {
    InputEvents::processGo(event_id);
    return true;
  }

  return false;
}


// This should be called ONLY by the GUI thread.
void
InputEvents::DoQueuedEvents(void)
{
  int GCE_Queue_copy[MAX_GCE_QUEUE];
  int NMEA_Queue_copy[MAX_NMEA_QUEUE];
  int i;

  // copy the queue first, blocking
  mutexEventQueue.Lock();
  std::copy(GCE_Queue, GCE_Queue + MAX_GCE_QUEUE, GCE_Queue_copy);
  std::fill(GCE_Queue, GCE_Queue + MAX_GCE_QUEUE, -1);
  std::copy(NMEA_Queue, NMEA_Queue + MAX_NMEA_QUEUE, NMEA_Queue_copy);
  std::fill(NMEA_Queue, NMEA_Queue + MAX_NMEA_QUEUE, -1);
  mutexEventQueue.Unlock();

  // process each item in the queue
  for (i = 0; i < MAX_GCE_QUEUE; i++) {
    if (GCE_Queue_copy[i] != -1) {
      processGlideComputer_real(GCE_Queue_copy[i]);
    }
  }
  for (i = 0; i < MAX_NMEA_QUEUE; i++) {
    if (NMEA_Queue_copy[i] != -1) {
      processNmea_real(NMEA_Queue_copy[i]);
    }
  }
}

bool
InputEvents::processGlideComputer(unsigned gce_id)
{
  // add an event to the bottom of the queue
  mutexEventQueue.Lock();
  for (int i = 0; i < MAX_GCE_QUEUE; i++) {
    if (GCE_Queue[i] == -1) {
      GCE_Queue[i] = gce_id;
      break;
    }
  }
  mutexEventQueue.Unlock();
  return true;
}

/*
  InputEvents::processGlideComputer
  Take virtual inputs from a Glide Computer to do special events
*/
bool
InputEvents::processGlideComputer_real(unsigned gce_id)
{
  if (!globalRunningEvent.Test())
    return false;
  int event_id = 0;

  // TODO feature: Log glide computer events to IGC file

  // Valid input ?
  if (gce_id >= GCE_COUNT)
    return false;

  // get current mode
  InputEvents::mode mode = InputEvents::getModeID();

  // Which key - can be defined locally or at default (fall back to default)
  event_id = input_config.GC2Event[mode][gce_id];
  if (event_id == 0) {
    // go with default key..
    event_id = input_config.GC2Event[0][gce_id];
  }

  if (event_id > 0) {
    InputEvents::processGo(event_id);
    return true;
  }

  return false;
}

// EXECUTE an Event - lookup event handler and call back - no return
void
InputEvents::processGo(unsigned eventid)
{
  if (!globalRunningEvent.Test())
    return;

  // TODO feature: event/macro recorder
  /*
  if (LoggerActive) {
    LoggerNoteEvent(Events[eventid].);
  }
  */

  // eventid 0 is special for "noop" - otherwise check event
  // exists (pointer to function)
  if (eventid) {
    if (input_config.Events[eventid].event) {
      input_config.Events[eventid].event(input_config.Events[eventid].misc);
      MenuTimeOut = 0;
    }
    if (input_config.Events[eventid].next > 0)
      InputEvents::processGo(input_config.Events[eventid].next);
  }
}

void
InputEvents::HideMenu()
{
  MenuTimeOut = XCSoarInterface::menu_timeout_max;
  ProcessMenuTimer();
  ResetDisplayTimeOut();
}

void
InputEvents::ResetMenuTimeOut()
{
  ResetDisplayTimeOut();
  MenuTimeOut = 0;
}

void
InputEvents::ShowMenu()
{
  if (CommonInterface::IsPanning())
    /* disable pan mode before displaying the normal menu; leaving pan
       mode enabled would be confusing for the user, and doesn't look
       consistent */
    sub_Pan(0);

  #if !defined(GNAV) && !defined(PCGNAV)
  // Popup exit button if in .xci
  // setMode(_T("Exit"));
  setMode(MODE_MENU); // VENTA3
  #endif

  ResetDisplayTimeOut();
  MenuTimeOut = 0;
  ProcessMenuTimer();
}

void
InputEvents::ProcessMenuTimer()
{
  if (CommonInterface::main_window.has_dialog())
    /* no menu updates while a dialog is visible */
    return;

  if (MenuTimeOut == XCSoarInterface::menu_timeout_max) {
    if (CommonInterface::IsPanning()) {
      setMode(MODE_PAN);
    } else {
      setMode(MODE_DEFAULT);
    }
  }
  // refresh visible buttons if still visible
  drawButtons(getModeID());

  MenuTimeOut++;
}

void
InputEvents::ProcessTimer()
{
  if (globalRunningEvent.Test()) {
    DoQueuedEvents();
  }
  ProcessMenuTimer();
}
