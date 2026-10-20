#include "platform/X11.h"

#include <QByteArray>
#include <QGuiApplication>

#include <xcb/xcb.h>

#include <cstdlib>

namespace prowal::x11 {

namespace {

int screenNumber()
{
  // ":0.1" -> 1. Without a screen part X uses screen 0.
  const QByteArray display = qgetenv("DISPLAY");
  const int dot = display.lastIndexOf('.');
  const int colon = display.lastIndexOf(':');
  if (dot > colon && colon >= 0)
    return std::atoi(display.constData() + dot + 1);
  return 0;
}

} // namespace

bool compositorRunning()
{
  auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
  if (!x11 || !x11->connection())
    return false;
  xcb_connection_t *c = x11->connection();

  const QByteArray name = "_NET_WM_CM_S" + QByteArray::number(screenNumber());
  xcb_intern_atom_reply_t *atom =
    xcb_intern_atom_reply(c, xcb_intern_atom(c, 1, uint16_t(name.size()), name.constData()), nullptr);
  if (!atom)
    return false;
  const xcb_atom_t selection = atom->atom;
  std::free(atom);
  if (selection == XCB_ATOM_NONE)
    return false;  // atom never interned: nobody has ever owned it

  xcb_get_selection_owner_reply_t *owner =
    xcb_get_selection_owner_reply(c, xcb_get_selection_owner(c, selection), nullptr);
  if (!owner)
    return false;
  const bool running = owner->owner != XCB_NONE;
  std::free(owner);
  return running;
}

bool grabKeyboard(unsigned long window)
{
  auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
  if (!x11 || !x11->connection())
    return true;  // not X11: nothing to do
  xcb_connection_t *c = x11->connection();
  xcb_grab_keyboard_reply_t *reply = xcb_grab_keyboard_reply(
    c, xcb_grab_keyboard(c, 1, xcb_window_t(window), XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC,
                         XCB_GRAB_MODE_ASYNC),
    nullptr);
  if (!reply)
    return false;
  const bool ok = reply->status == XCB_GRAB_STATUS_SUCCESS;
  std::free(reply);
  return ok;
}

void drawsOwnShadows(unsigned long window)
{
  auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
  if (!x11 || !x11->connection())
    return;
  xcb_connection_t *c = x11->connection();
  static const char name[] = "_GTK_FRAME_EXTENTS";
  xcb_intern_atom_reply_t *atom =
    xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, uint16_t(sizeof(name) - 1), name), nullptr);
  if (!atom)
    return;
  const uint32_t extents[4] = {0, 0, 0, 0};
  xcb_change_property(c, XCB_PROP_MODE_REPLACE, xcb_window_t(window), atom->atom, XCB_ATOM_CARDINAL, 32, 4,
                      extents);
  std::free(atom);
}

} // namespace prowal::x11
