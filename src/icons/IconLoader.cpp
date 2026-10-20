#include "icons/IconLoader.h"

#include "desktop/DesktopEntry.h"

#include <QFileInfo>
#include <QIcon>
#include <QImageReader>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <vector>

namespace prowal {

namespace {

QPixmap fitPixmap(QImage image, int px, qreal dpr)
{
  if (std::max(image.width(), image.height()) != px)
    image = image.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap pm = QPixmap::fromImage(std::move(image));
  pm.setDevicePixelRatio(dpr);
  return pm;
}

QPixmap fromFile(const QString &path, int px, qreal dpr)
{
  QImageReader reader(path);
  const QSize native = reader.size();
  if (native.isValid() && reader.supportsOption(QImageIOHandler::ScaledSize))
    reader.setScaledSize(native.scaled(px, px, Qt::KeepAspectRatio));  // vector: render sharp
  const QImage image = reader.read();
  if (image.isNull())
    return QPixmap();
  return fitPixmap(image, px, dpr);
}

QPixmap fromTheme(const QString &name, int size, int px, qreal dpr)
{
  const QIcon icon = QIcon::fromTheme(name);
  if (icon.isNull())
    return QPixmap();
  const QPixmap pm = icon.pixmap(QSize(size, size), dpr);
  if (pm.isNull())
    return QPixmap();
  // QIcon never upscales; themes without a large enough size would come out small.
  return fitPixmap(pm.toImage(), px, dpr);
}

QPixmap placeholder(int px, qreal dpr)
{
  QPixmap pm(px, px);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const qreal inset = px * 0.12;
  const QRectF r(inset, inset, px - 2 * inset, px - 2 * inset);
  p.setPen(QPen(QColor(255, 255, 255, 170), std::max(1.0, px / 24.0)));
  p.setBrush(QColor(90, 90, 100, 200));
  p.drawRoundedRect(r, px * 0.18, px * 0.18);
  QFont f = p.font();
  f.setPixelSize(int(px * 0.45));
  f.setBold(true);
  p.setFont(f);
  p.drawText(r, Qt::AlignCenter, QStringLiteral("?"));
  p.end();
  pm.setDevicePixelRatio(dpr);
  return pm;
}

// Three box-blur passes approximate a Gaussian. Operates on the alpha channel
// only; `alpha` is width x height.
void blurAlpha(std::vector<int> &alpha, int width, int height, int radius)
{
  const int r = std::max(1, radius / 3);
  std::vector<int> tmp(alpha.size());
  auto pass = [&](std::vector<int> &src, std::vector<int> &dst, int len, int lines, int stride, int step) {
    const int window = 2 * r + 1;
    for (int line = 0; line < lines; ++line)
    {
      const int base = line * stride;
      int sum = 0;
      for (int i = -r; i <= r; ++i)
        sum += src[base + std::clamp(i, 0, len - 1) * step];
      for (int i = 0; i < len; ++i)
      {
        dst[base + i * step] = sum / window;
        sum += src[base + std::min(i + r + 1, len - 1) * step];
        sum -= src[base + std::max(i - r, 0) * step];
      }
    }
  };
  for (int n = 0; n < 3; ++n)
  {
    pass(alpha, tmp, width, height, width, 1);   // rows
    pass(tmp, alpha, height, width, 1, width);   // columns
  }
}

QPixmap shadowFromAlpha(const QImage &silhouette, int pad, const QColor &colour, qreal dpr)
{
  const int w = silhouette.width(), h = silhouette.height();
  std::vector<int> alpha(size_t(w) * size_t(h));
  for (int y = 0; y < h; ++y)
  {
    const auto *line = reinterpret_cast<const QRgb *>(silhouette.constScanLine(y));
    for (int x = 0; x < w; ++x)
      alpha[size_t(y) * size_t(w) + size_t(x)] = qAlpha(line[x]);
  }
  blurAlpha(alpha, w, h, pad);

  QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
  const int ca = colour.alpha();
  for (int y = 0; y < h; ++y)
  {
    auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
    for (int x = 0; x < w; ++x)
    {
      const int a = alpha[size_t(y) * size_t(w) + size_t(x)] * ca / 255;
      line[x] = qPremultiply(qRgba(colour.red(), colour.green(), colour.blue(), a));
    }
  }
  QPixmap pm = QPixmap::fromImage(std::move(out));
  pm.setDevicePixelRatio(dpr);
  return pm;
}

} // namespace

QPixmap shadowFor(const QPixmap &pixmap, int radius, const QColor &colour)
{
  const qreal dpr = pixmap.devicePixelRatio();
  const int pad = int(std::lround(radius * dpr));
  QImage silhouette(pixmap.width() + 2 * pad, pixmap.height() + 2 * pad, QImage::Format_ARGB32);
  silhouette.fill(Qt::transparent);
  QPainter p(&silhouette);
  QPixmap raw = pixmap;
  raw.setDevicePixelRatio(1.0);  // draw in device pixels
  p.drawPixmap(pad, pad, raw);
  p.end();
  return shadowFromAlpha(silhouette, pad, colour, dpr);
}

QPixmap roundedRectShadow(QSize size, qreal cornerRadius, int radius, const QColor &colour, qreal dpr)
{
  const int pad = int(std::lround(radius * dpr));
  const QSize px = (QSizeF(size) * dpr).toSize();
  QImage silhouette(px.width() + 2 * pad, px.height() + 2 * pad, QImage::Format_ARGB32);
  silhouette.fill(Qt::transparent);
  QPainter p(&silhouette);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::black);
  p.drawRoundedRect(QRectF(pad, pad, px.width(), px.height()), cornerRadius * dpr, cornerRadius * dpr);
  p.end();
  return shadowFromAlpha(silhouette, pad, colour, dpr);
}

LoadedIcon loadIcon(const QString &spec, int size, qreal dpr)
{
  const int px = int(std::lround(size * dpr));
  LoadedIcon r;

  if (spec.contains(QLatin1Char('/')))
  {
    const QFileInfo fi(spec);
    if (fi.isFile())
      r.pixmap = fromFile(spec, px, dpr);
    if (r.pixmap.isNull())
      r.pixmap = fromTheme(fi.completeBaseName(), size, px, dpr);
  }
  else if (!spec.isEmpty())
    r.pixmap = fromTheme(spec, size, px, dpr);

  r.found = !r.pixmap.isNull();
  if (!r.found)
  {
    r.pixmap = fromTheme(QStringLiteral("application-x-executable"), size, px, dpr);
    if (r.pixmap.isNull())
      r.pixmap = placeholder(px, dpr);
  }
  return r;
}

namespace {

QPixmap blank(int size, qreal dpr)
{
  QPixmap pm(QSize(size, size) * dpr);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  return pm;
}

void drawCentred(QPainter &p, const QPixmap &pm, const QRectF &box)
{
  const QSizeF logical = QSizeF(pm.size()) / pm.devicePixelRatio();
  p.drawPixmap(QPointF(box.center().x() - logical.width() / 2, box.center().y() - logical.height() / 2), pm);
}

QString iconSpecOf(const Item &item)
{
  if (item.icon.isEmpty() && item.type == ItemType::Group)
    return QStringLiteral("folder");
  return item.icon;
}

} // namespace

QPixmap groupIcon(const Item &group, const QString &style, int size, qreal dpr, const QColor &accent)
{
  const QPixmap base = loadIcon(iconSpecOf(group), size, dpr).pixmap;
  if (style == QLatin1String("plain"))
    return base;

  if (style == QLatin1String("preview") && !group.children.empty())
  {
    std::vector<const Item *> children;
    for (const Item &c : group.children)
      children.push_back(&c);
    std::stable_sort(children.begin(), children.end(), [](const Item *a, const Item *b) {
      const int da = a->x * a->x + a->y * a->y, db = b->x * b->x + b->y * b->y;
      return da != db ? da < db : (a->y != b->y ? a->y < b->y : a->x < b->x);
    });
    QPixmap pm = blank(size, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF tile(1, 1, size - 2, size - 2);
    QColor edge = accent;
    edge.setAlpha(150);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(36, 40, 50, 215));
    p.drawRoundedRect(tile, size * 0.22, size * 0.22);
    const qreal gap = size * 0.09;
    const int mini = int((size - 3 * gap) / 2);
    for (size_t i = 0; i < std::min<size_t>(4, children.size()); ++i)
    {
      const QRectF box(gap + (i % 2) * (mini + gap), gap + (i / 2) * (mini + gap), mini, mini);
      drawCentred(p, loadIcon(iconSpecOf(*children[i]), mini, dpr).pixmap, box);
    }
    return pm;
  }

  // Badge: a small 2x2 grid in a dark disc, bottom right.
  QPixmap pm = blank(size, dpr);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  drawCentred(p, base, QRectF(0, 0, size, size));
  const qreal d = std::max(12.0, size * 0.42);
  const QRectF disc(size - d - 0.5, size - d - 0.5, d, d);
  QColor ring = accent;
  ring.setAlpha(230);
  p.setPen(QPen(ring, std::max(1.0, size / 32.0)));
  p.setBrush(QColor(30, 33, 40, 240));
  p.drawEllipse(disc);
  const qreal sq = d * 0.18, g = d * 0.1;
  const QPointF c = disc.center();
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(240, 242, 248));
  for (int i = 0; i < 4; ++i)
    p.drawRoundedRect(QRectF(c.x() - g / 2 - sq + (i % 2) * (sq + g), c.y() - g / 2 - sq + (i / 2) * (sq + g), sq, sq),
                      sq * 0.25, sq * 0.25);
  return pm;
}

QPixmap backIcon(int size, qreal dpr, const QColor &accent)
{
  QPixmap pm = blank(size, dpr);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const qreal r = size * 0.36;
  const QPointF c(size / 2.0, size / 2.0);
  QColor ring = accent;
  ring.setAlpha(200);
  p.setPen(QPen(ring, std::max(1.0, size / 24.0)));
  p.setBrush(QColor(30, 33, 40, 225));
  p.drawEllipse(c, r, r);
  QPen chevron(QColor(240, 242, 248), std::max(2.0, size / 12.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(chevron);
  p.setBrush(Qt::NoBrush);
  const QPointF pts[] = {c + QPointF(r * 0.16, -r * 0.42), c + QPointF(-r * 0.26, 0), c + QPointF(r * 0.16, r * 0.42)};
  p.drawPolyline(pts, 3);
  return pm;
}

LoadedIcon loadItemIcon(const Item &item, int size, qreal dpr)
{
  LoadedIcon icon = loadIcon(item.icon, size, dpr);
  if (icon.found || item.desktopId.isEmpty())
    return icon;
  const auto app = findApplication(item.desktopId, applicationDirs());
  if (app && !app->icon.isEmpty() && app->icon != item.icon)
  {
    LoadedIcon current = loadIcon(app->icon, size, dpr);
    if (current.found)
      return current;
  }
  return icon;
}

bool isImageFile(const QString &path)
{
  static const QStringList exts{QStringLiteral("png"), QStringLiteral("svg"), QStringLiteral("svgz"),
                                QStringLiteral("xpm"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                QStringLiteral("gif"), QStringLiteral("ico"), QStringLiteral("webp"),
                                QStringLiteral("bmp")};
  return exts.contains(QFileInfo(path).suffix().toLower());
}

QPixmap desaturated(const QPixmap &pixmap)
{
  QImage img = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < img.height(); ++y)
  {
    auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
    for (int x = 0; x < img.width(); ++x)
    {
      const QRgb c = line[x];
      const int g = qGray(c);
      line[x] = qRgba(g, g, g, qAlpha(c) * 55 / 100);
    }
  }
  QPixmap pm = QPixmap::fromImage(std::move(img));
  pm.setDevicePixelRatio(pixmap.devicePixelRatio());
  return pm;
}

} // namespace prowal
