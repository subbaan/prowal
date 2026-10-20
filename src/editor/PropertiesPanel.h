#pragma once

#include "config/Model.h"

#include <QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace prowal {

// Edits one item. Changes are committed (as one undo step each) when a field
// loses focus or Enter is pressed; problems are shown live while typing.
class PropertiesPanel : public QWidget
{
  Q_OBJECT

public:
  explicit PropertiesPanel(QWidget *parent = nullptr);

  // item == nullptr shows the empty-cell page for `cell` (or nothing if no cell).
  void setItem(const Item *item, std::optional<QPoint> cell, const QString &terminalPrefix);
  void focusName();

  // Opens the icon picker on the current value; returns the choice or empty.
  std::function<QString(const QString &)> iconChooser;

signals:
  void itemEdited(const Item &item);
  void testRequested();
  void addRequested();
  void addGroupRequested();
  void appsRequested();
  void openGroupRequested();

protected:
  bool eventFilter(QObject *watched, QEvent *e) override;

private:
  Item currentValue() const;
  void commit();
  void updateProblems();
  void browseIcon();
  void browseFolder();

  Item m_item;
  QString m_terminal;
  bool m_updating = false;

  QStackedWidget *m_pages;
  QLabel *m_emptyText;
  QPushButton *m_addButton;
  QPushButton *m_appsButton;
  QPushButton *m_addGroupButton;

  QFormLayout *m_form;
  QLabel *m_position;
  QLabel *m_groupInfo;
  QPushButton *m_openGroup;
  QWidget *m_commandRow;
  QWidget *m_folderRow;
  QLineEdit *m_name;
  QLineEdit *m_command;
  QLabel *m_commandProblem;
  QCheckBox *m_shell;
  QCheckBox *m_terminalBox;
  QLineEdit *m_folder;
  QLabel *m_folderProblem;
  QLineEdit *m_icon;
  QLabel *m_iconPreview;
  QLabel *m_iconProblem;
  QPlainTextEdit *m_env;
  QPushButton *m_test;
};

} // namespace prowal
