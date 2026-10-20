#pragma once

#include "config/Model.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QSpinBox;

namespace prowal {

class ColorButton;

// Edits the behaviour and appearance settings. Nothing is changed until
// applied(): the editor turns that into one undoable step, saved with the layout.
class PreferencesDialog : public QDialog
{
  Q_OBJECT

public:
  // autoTerminal: what an empty terminal setting resolves to on this machine.
  PreferencesDialog(const Config &config, QString autoTerminal, QWidget *parent = nullptr);

  Behaviour behaviour() const;
  Appearance appearance() const;

signals:
  void applied();
  void previewRequested();

private:
  void load(const Behaviour &behaviour, const Appearance &appearance);
  void restoreDefaults();
  void tryTerminal();
  void updateEnabled();

  Behaviour m_behaviour;  // kept for unknown keys (extra) and anything not shown
  Appearance m_appearance;
  QString m_autoTerminal;

  QComboBox *m_terminal;
  QDoubleSpinBox *m_timeout;
  QCheckBox *m_stayOpen;

  QComboBox *m_mode;
  ColorButton *m_panelColor;
  QSlider *m_panelOpacity;
  QLabel *m_panelOpacityLabel;
  QSpinBox *m_iconSize;
  QSpinBox *m_spacing;
  QComboBox *m_labels;
  QComboBox *m_groupStyle;
  QComboBox *m_iconTheme;
  ColorButton *m_accent;
  QCheckBox *m_shadows;

  QComboBox *m_motion;
  QSlider *m_speed;
  QLabel *m_speedLabel;
};

} // namespace prowal
