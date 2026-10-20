#pragma once

#include "config/Model.h"

#include <QColor>
#include <QPixmap>
#include <QString>

namespace prowal {

struct LoadedIcon
{
  QPixmap pixmap;  // fits within size x size logical pixels, device pixel ratio set
  bool found = false;
};

// spec is an icon theme name ("firefox") or a file path. A missing file falls back
// to the theme icon named after it ("/x/y/gimp.png" -> "gimp"); anything not found
// gets a generic placeholder with found = false.
LoadedIcon loadIcon(const QString &spec, int size, qreal dpr);

// A Group's icon in the given style: "plain" (its icon as is), "badge" (with a
// small grid badge in the corner) or "preview" (a 2x2 tile of the first child
// icons, nearest the origin first). A Group without an icon uses "folder".
QPixmap groupIcon(const Item &group, const QString &style, int size, qreal dpr, const QColor &accent);

// The "back to the parent" button shown in a Group's empty origin cell.
QPixmap backIcon(int size, qreal dpr, const QColor &accent);

// A launcher's icon. If its own icon is gone and it came from an installed app
// (desktopId), uses the icon that app's .desktop file names now - apps rename
// their icons on updates.
LoadedIcon loadItemIcon(const Item &item, int size, qreal dpr);

// Whether a file looks like an image Prowal can use as an icon (by extension).
bool isImageFile(const QString &path);

// Greyed-out version for items whose program isn't installed.
QPixmap desaturated(const QPixmap &pixmap);

// A soft shadow of the pixmap's silhouette, blurred by radius logical pixels.
// The result is larger than the input by radius on every side.
QPixmap shadowFor(const QPixmap &pixmap, int radius, const QColor &colour);

// A soft shadow of a rounded rectangle of the given logical size.
QPixmap roundedRectShadow(QSize size, qreal cornerRadius, int radius, const QColor &colour, qreal dpr);

} // namespace prowal
