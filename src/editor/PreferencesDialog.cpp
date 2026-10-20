#include "editor/PreferencesDialog.h"

#include "icons/IconTheme.h"
#include "system/Launch.h"
#include "system/Terminal.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace prowal {

// A button showing a colour swatch. An empty colour means "Prowal's default",
// which `fallback` shows.
class ColorButton : public QPushButton
{
public:
  ColorButton(QColor fallback, QWidget *parent = nullptr) : QPushButton(parent), m_fallback(fallback)
  {
    connect(this, &QPushButton::clicked, this, [this] {
      const QColor chosen = QColorDialog::getColor(shown(), this, tr("Choose a colour"));
      if (chosen.isValid())
        setColour(chosen.name());
    });
    setColour(QString());
  }

  QString colour() const { return m_colour; }
  void setColour(const QString &colour)
  {
    m_colour = colour;
    setText(colour.isEmpty() ? tr("Default") : colour);
    const int s = fontMetrics().height();
    QPixmap swatch(s, s);
    swatch.fill(Qt::transparent);
    QPainter p(&swatch);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(palette().color(QPalette::Text), 1));
    p.setBrush(shown());
    p.drawRoundedRect(QRectF(0.5, 0.5, s - 1, s - 1), 3, 3);
    p.end();
    setIcon(swatch);
  }

private:
  QColor shown() const { return m_colour.isEmpty() ? m_fallback : QColor(m_colour); }

  QColor m_fallback;
  QString m_colour;
};

namespace {

// These match the popup's built-in defaults (PopupWindow.cpp).
const QColor kDefaultAccent(130, 175, 255);
const QColor kDefaultPanel(28, 31, 38);

QLabel *hint(const QString &text)
{
  auto *label = new QLabel(text);
  label->setWordWrap(true);
  QFont f = label->font();
  f.setPointSizeF(f.pointSizeF() * 0.9);
  label->setFont(f);
  label->setForegroundRole(QPalette::PlaceholderText);
  return label;
}

QWidget *withHint(QWidget *field, const QString &text)
{
  auto *box = new QWidget;
  auto *layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);
  layout->addWidget(field);
  layout->addWidget(hint(text));
  return box;
}

// The speed slider is logarithmic so that normal (1×) sits in the middle:
// -100..100 maps to 0.25×..4×.
double speedFromSlider(int v)
{
  return std::pow(2.0, v / 50.0);
}

int sliderFromSpeed(double speed)
{
  return int(std::lround(std::log2(std::clamp(speed, 0.25, 4.0)) * 50.0));
}

QComboBox *choices(std::initializer_list<std::pair<QString, QString>> items)
{
  auto *box = new QComboBox;
  for (const auto &[text, value] : items)
    box->addItem(text, value);
  return box;
}

void select(QComboBox *box, const QString &value)
{
  const int index = box->findData(value);
  box->setCurrentIndex(index >= 0 ? index : 0);
}

QString selected(const QComboBox *box)
{
  return box->currentData().toString();
}

} // namespace

PreferencesDialog::PreferencesDialog(const Config &config, QString autoTerminal, QWidget *parent)
  : QDialog(parent)
  , m_behaviour(config.behaviour)
  , m_appearance(config.appearance)
  , m_autoTerminal(std::move(autoTerminal))
{
  setWindowTitle(tr("Prowal Preferences"));

  // --- General
  auto *general = new QWidget;
  auto *gForm = new QFormLayout(general);
  gForm->setRowWrapPolicy(QFormLayout::DontWrapRows);

  m_terminal = new QComboBox;
  m_terminal->setEditable(true);
  m_terminal->setInsertPolicy(QComboBox::NoInsert);
  m_terminal->addItem(m_autoTerminal.isEmpty() ? tr("Automatic (none found)")
                                               : tr("Automatic (%1)").arg(m_autoTerminal),
                      QString());
  for (const QString &prefix : installedTerminals())
    m_terminal->addItem(prefix, prefix);
  auto *tryButton = new QPushButton(tr("Try it"));
  tryButton->setToolTip(tr("Open the terminal with a short test message"));
  connect(tryButton, &QPushButton::clicked, this, &PreferencesDialog::tryTerminal);
  auto *terminalRow = new QWidget;
  auto *terminalLayout = new QHBoxLayout(terminalRow);
  terminalLayout->setContentsMargins(0, 0, 0, 0);
  terminalLayout->addWidget(m_terminal, 1);
  terminalLayout->addWidget(tryButton);
  gForm->addRow(tr("&Terminal"),
                withHint(terminalRow, tr("For launchers set to \"Run in a terminal\". The command goes after "
                                         "this, e.g. konsole -e htop. Pick one, or type your own "
                                         "(with its flags).")));

  m_timeout = new QDoubleSpinBox;
  m_timeout->setRange(0.1, 600);
  m_timeout->setDecimals(1);
  m_timeout->setSingleStep(0.5);
  m_timeout->setSuffix(tr(" s"));
  gForm->addRow(tr("Close &after"),
                withHint(m_timeout, tr("How long the popup waits after the pointer leaves it. "
                                       "Using the arrow keys stops the countdown.")));

  m_stayOpen = new QCheckBox(tr("Keep the popup open after launching"));
  gForm->addRow(QString(), withHint(m_stayOpen, tr("Middle click (or Shift+Enter) always keeps it open.")));

  // --- Look
  auto *look = new QWidget;
  auto *lForm = new QFormLayout(look);

  m_mode = choices({{tr("Floating icons"), QStringLiteral("floating")}, {tr("Panel behind the icons"), QStringLiteral("panel")}});
  lForm->addRow(tr("&Style"), m_mode);
  m_panelColor = new ColorButton(kDefaultPanel);
  m_panelOpacity = new QSlider(Qt::Horizontal);
  m_panelOpacity->setRange(0, 100);
  m_panelOpacityLabel = new QLabel;
  m_panelOpacityLabel->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("100 %")));
  auto *panelRow = new QWidget;
  auto *panelLayout = new QHBoxLayout(panelRow);
  panelLayout->setContentsMargins(0, 0, 0, 0);
  panelLayout->addWidget(m_panelColor);
  panelLayout->addWidget(m_panelOpacity, 1);
  panelLayout->addWidget(m_panelOpacityLabel);
  lForm->addRow(tr("&Panel"), panelRow);
  connect(m_panelOpacity, &QSlider::valueChanged, this,
          [this](int v) { m_panelOpacityLabel->setText(tr("%1 %").arg(v)); });

  m_iconSize = new QSpinBox;
  m_iconSize->setRange(16, 256);
  m_iconSize->setSuffix(tr(" px"));
  lForm->addRow(tr("Icon si&ze"), m_iconSize);
  m_spacing = new QSpinBox;
  m_spacing->setRange(0, 64);
  m_spacing->setSuffix(tr(" px"));
  lForm->addRow(tr("Spa&cing"), m_spacing);

  m_labels = choices({{tr("When pointed at"), QStringLiteral("hover")},
                      {tr("Always"), QStringLiteral("always")},
                      {tr("Never"), QStringLiteral("never")}});
  lForm->addRow(tr("&Labels"), m_labels);

  m_groupStyle = choices({{tr("Icon with a grid badge"), QStringLiteral("badge")},
                          {tr("Preview of what's inside"), QStringLiteral("preview")},
                          {tr("Plain icon"), QStringLiteral("plain")}});
  lForm->addRow(tr("&Group icons"), m_groupStyle);

  m_iconTheme = new QComboBox;
  const QString desktop = desktopIconTheme();
  m_iconTheme->addItem(desktop.isEmpty() ? tr("Follow the desktop") : tr("Follow the desktop (%1)").arg(desktop),
                       QString());
  for (const IconThemeInfo &t : installedIconThemes())
    m_iconTheme->addItem(t.display == t.name ? t.name : QStringLiteral("%1 (%2)").arg(t.display, t.name), t.name);
  lForm->addRow(tr("Icon &theme"),
                withHint(m_iconTheme, tr("For icons given by name. The editor switches too.")));

  m_accent = new ColorButton(kDefaultAccent);
  auto *accentReset = new QPushButton(tr("Use default"));
  connect(accentReset, &QPushButton::clicked, this, [this] { m_accent->setColour(QString()); });
  auto *accentRow = new QWidget;
  auto *accentLayout = new QHBoxLayout(accentRow);
  accentLayout->setContentsMargins(0, 0, 0, 0);
  accentLayout->addWidget(m_accent);
  accentLayout->addWidget(accentReset);
  accentLayout->addStretch();
  lForm->addRow(tr("&Highlight colour"), accentRow);

  m_shadows = new QCheckBox(tr("Soft shadows under icons"));
  lForm->addRow(QString(), m_shadows);

  // --- Motion
  auto *motion = new QWidget;
  auto *mForm = new QFormLayout(motion);
  m_motion = choices({{tr("Full: icons bloom out, hover lifts"), QStringLiteral("full")},
                      {tr("Reduced: fades only"), QStringLiteral("reduced")},
                      {tr("Off"), QStringLiteral("off")}});
  mForm->addRow(tr("&Animation"),
                withHint(m_motion, tr("Without a compositor there is no animation whatever this says.")));
  m_speed = new QSlider(Qt::Horizontal);
  m_speed->setRange(-100, 100);
  m_speed->setSingleStep(5);
  m_speed->setPageStep(25);
  m_speed->setTickPosition(QSlider::TicksBelow);
  m_speed->setTickInterval(50);
  m_speedLabel = new QLabel;
  m_speedLabel->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("4.00× (normal)")));
  auto *speedRow = new QWidget;
  auto *speedLayout = new QHBoxLayout(speedRow);
  speedLayout->setContentsMargins(0, 0, 0, 0);
  speedLayout->addWidget(new QLabel(tr("slower")));
  speedLayout->addWidget(m_speed, 1);
  speedLayout->addWidget(new QLabel(tr("faster")));
  speedLayout->addWidget(m_speedLabel);
  mForm->addRow(tr("S&peed"), speedRow);
  connect(m_speed, &QSlider::valueChanged, this, [this](int v) {
    m_speedLabel->setText(v == 0 ? tr("1.00× (normal)") : tr("%1×").arg(speedFromSlider(v), 0, 'f', 2));
  });

  auto *tabs = new QTabWidget;
  tabs->addTab(general, tr("General"));
  tabs->addTab(look, tr("Look"));
  tabs->addTab(motion, tr("Motion"));

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply |
                                       QDialogButtonBox::RestoreDefaults);
  QPushButton *preview = buttons->addButton(tr("&Preview"), QDialogButtonBox::ActionRole);
  preview->setToolTip(tr("Show the popup with these settings, around the pointer"));
  connect(preview, &QPushButton::clicked, this, &PreferencesDialog::previewRequested);
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    emit applied();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &PreferencesDialog::applied);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
          &PreferencesDialog::restoreDefaults);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(tabs);
  layout->addWidget(hint(tr("Changes are part of the layout file: Save in the editor to keep them.")));
  layout->addWidget(buttons);

  connect(m_mode, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updateEnabled);
  load(m_behaviour, m_appearance);
  resize(560, sizeHint().height());
}

void PreferencesDialog::load(const Behaviour &b, const Appearance &a)
{
  const int known = m_terminal->findData(b.terminal);
  if (known >= 0)
    m_terminal->setCurrentIndex(known);
  else
    m_terminal->setEditText(b.terminal);
  m_timeout->setValue(b.timeoutMs / 1000.0);
  m_stayOpen->setChecked(b.stayOpenAfterLaunch);

  select(m_mode, a.mode);
  m_panelColor->setColour(a.panelColor);
  m_panelOpacity->setValue(a.panelOpacity);
  m_panelOpacityLabel->setText(tr("%1 %").arg(a.panelOpacity));
  m_iconSize->setValue(a.iconSize);
  m_spacing->setValue(a.spacing);
  select(m_labels, a.labels);
  select(m_groupStyle, a.groupStyle);
  if (!a.iconTheme.isEmpty() && m_iconTheme->findData(a.iconTheme) < 0)
    m_iconTheme->addItem(tr("%1 (not installed)").arg(a.iconTheme), a.iconTheme);
  select(m_iconTheme, a.iconTheme);
  m_accent->setColour(a.accentColor);
  m_shadows->setChecked(a.shadows);
  select(m_motion, a.motion);
  m_speed->setValue(sliderFromSpeed(a.animationSpeed));
  emit m_speed->valueChanged(m_speed->value());  // label, even when the value didn't change
  updateEnabled();
}

Behaviour PreferencesDialog::behaviour() const
{
  Behaviour b = m_behaviour;
  const QString text = m_terminal->currentText().trimmed();
  const int index = m_terminal->findText(text);
  b.terminal = index >= 0 ? m_terminal->itemData(index).toString() : text;
  b.timeoutMs = int(std::lround(m_timeout->value() * 1000));
  b.stayOpenAfterLaunch = m_stayOpen->isChecked();
  return b;
}

Appearance PreferencesDialog::appearance() const
{
  Appearance a = m_appearance;
  a.mode = selected(m_mode);
  a.panelColor = m_panelColor->colour();
  a.panelOpacity = m_panelOpacity->value();
  a.iconSize = m_iconSize->value();
  a.spacing = m_spacing->value();
  a.labels = selected(m_labels);
  a.groupStyle = selected(m_groupStyle);
  a.iconTheme = selected(m_iconTheme);
  a.accentColor = m_accent->colour();
  a.shadows = m_shadows->isChecked();
  a.motion = selected(m_motion);
  // Untouched keeps a hand-written value the slider can't hit exactly; otherwise
  // round so the file holds 1.41, not 1.4142...
  if (m_speed->value() != sliderFromSpeed(m_appearance.animationSpeed))
    a.animationSpeed = std::round(speedFromSlider(m_speed->value()) * 100) / 100;
  return a;
}

void PreferencesDialog::restoreDefaults()
{
  Behaviour b;
  b.extra = m_behaviour.extra;
  Appearance a;
  a.extra = m_appearance.extra;
  load(b, a);
}

void PreferencesDialog::updateEnabled()
{
  const bool panel = selected(m_mode) == QLatin1String("panel");
  m_panelColor->setEnabled(panel);
  m_panelOpacity->setEnabled(panel);
  m_panelOpacityLabel->setEnabled(panel);
}

void PreferencesDialog::tryTerminal()
{
  const QString prefix = behaviour().terminal.isEmpty() ? m_autoTerminal : behaviour().terminal;
  if (prefix.isEmpty())
  {
    QMessageBox::information(this, tr("Try the terminal"), tr("No terminal is installed that Prowal knows; "
                                                              "type the command for yours."));
    return;
  }
  Item test;
  test.name = tr("Terminal test");
  test.command = QStringLiteral("sh -c 'echo \"Prowal: this terminal works.\"; echo; "
                                "echo \"This window closes in 5 seconds.\"; sleep 5'");
  test.terminal = true;
  QString error;
  if (!launchItem(test, prefix, &error))
    QMessageBox::warning(this, tr("Try the terminal"), tr("Couldn't start \"%1\":\n%2").arg(prefix, error));
}

} // namespace prowal
