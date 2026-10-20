#pragma once

namespace prowal::x11 {

// True when running on X11 (xcb) and a compositing manager owns _NET_WM_CM_Sn.
// Without one, translucent windows render on black, so the popup falls back to
// a shaped window.
bool compositorRunning();

// Tries to grab the keyboard for the given window. Fails while another client
// holds a grab - e.g. the window manager's own shortcut grab while the key
// that launched Prowal is still held down - so callers retry.
bool grabKeyboard(unsigned long window);

// Marks the window as drawing its own shadows (an all-zero _GTK_FRAME_EXTENTS,
// the convention GTK uses), so compositors such as picom don't add a rectangular
// shadow around the whole translucent window. Call before the window is shown.
void drawsOwnShadows(unsigned long window);

} // namespace prowal::x11
