#include "editor/PropertiesPanel.h"

#include "icons/IconLoader.h"
#include "system/Launch.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace prowal {

namespace {

QLabel *problemLabel()
{
  auto *label = new QLabel;
  label->setWordWrap(true);
  label->setStyleSheet(QStringLiteral("color: #e0a030;"));
  label->hide();
  return label;
}

// Only touch fields whose value really changed, so a refresh after an edit
// doesn't disturb the cursor; show long values from their start.
void setText(QLineEdit *edit, const QString &text)
{
  if (edit->text() == text)
    return;
  edit->setText(text);
  edit->setCursorPosition(0);
}

void showProblem(QLabel *label, const QString &text)
{
  label->setText(text.isEmpty() ? QString() : QStringLiteral("⚠ ") + text);
  label->setVisible(!text.isEmpty());
}

QWidget *withButton(QLineEdit *edit, QPushButton *button)
{
  auto *row = new QWidget;
  auto *layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(edit, 1);
  layout->addWidget(button);
  return row;
}

} // namespace

PropertiesPanel::PropertiesPanel(QWidget *parent) : QWidget(parent)
{
  m_pages = new QStackedWidget;
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(m_pages);

  // Page 0: nothing / an empty cell.
  auto *empty = new QWidget;
  auto *emptyLayout = new QVBoxLayout(empty);
  m_emptyText = new QLabel;
  m_emptyText->setWordWrap(true);
  m_emptyText->setAlignment(Qt::AlignCenter);
  m_addButton = new QPushButton(tr("Add launcher here"));
  m_appsButton = new QPushButton(tr("Add from installed apps…"));
  m_addGroupButton = new QPushButton(tr("Add group here"));
  emptyLayout->addStretch();
  emptyLayout->addWidget(m_emptyText);
  emptyLayout->addWidget(m_addButton, 0, Qt::AlignCenter);
  emptyLayout->addWidget(m_appsButton, 0, Qt::AlignCenter);
  emptyLayout->addWidget(m_addGroupButton, 0, Qt::AlignCenter);
  emptyLayout->addStretch();
  connect(m_addButton, &QPushButton::clicked, this, &PropertiesPanel::addRequested);
  connect(m_appsButton, &QPushButton::clicked, this, &PropertiesPanel::appsRequested);
  connect(m_addGroupButton, &QPushButton::clicked, this, &PropertiesPanel::addGroupRequested);
  m_pages->addWidget(empty);

  // Page 1: an item.
  auto *page = new QWidget;
  auto *form = m_form = new QFormLayout(page);
  form->setRowWrapPolicy(QFormLayout::WrapAllRows);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  m_position = new QLabel;
  form->addRow(tr("Position"), m_position);

  m_name = new QLineEdit;
  form->addRow(tr("&Name"), m_name);

  m_groupInfo = new QLabel;
  m_groupInfo->setWordWrap(true);
  m_openGroup = new QPushButton(tr("&Open group"));
  m_openGroup->setToolTip(tr("Edit what's inside (or double-click the group)"));
  connect(m_openGroup, &QPushButton::clicked, this, &PropertiesPanel::openGroupRequested);
  auto *groupBox = new QWidget;
  auto *groupLayout = new QVBoxLayout(groupBox);
  groupLayout->setContentsMargins(0, 0, 0, 0);
  groupLayout->addWidget(m_groupInfo);
  groupLayout->addWidget(m_openGroup, 0, Qt::AlignLeft);
  form->addRow(tr("Group"), groupBox);

  m_command = new QLineEdit;
  m_command->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_command->setPlaceholderText(tr("program --and arguments"));
  m_commandProblem = problemLabel();
  auto *commandBox = new QWidget;
  auto *commandLayout = new QVBoxLayout(commandBox);
  commandLayout->setContentsMargins(0, 0, 0, 0);
  commandLayout->addWidget(m_command);
  commandLayout->addWidget(m_commandProblem);
  m_shell = new QCheckBox(tr("Run through the shell (for pipes, &&, $VARIABLES)"));
  m_terminalBox = new QCheckBox;
  commandLayout->addWidget(m_shell);
  commandLayout->addWidget(m_terminalBox);
  form->addRow(tr("&Command"), commandBox);
  m_commandRow = commandBox;

  m_folder = new QLineEdit;
  m_folder->setPlaceholderText(tr("(home folder)"));
  auto *folderButton = new QPushButton(tr("Browse…"));
  m_folderProblem = problemLabel();
  auto *folderBox = new QWidget;
  auto *folderLayout = new QVBoxLayout(folderBox);
  folderLayout->setContentsMargins(0, 0, 0, 0);
  folderLayout->addWidget(withButton(m_folder, folderButton));
  folderLayout->addWidget(m_folderProblem);
  form->addRow(tr("Start in &folder"), folderBox);
  m_folderRow = folderBox;

  m_icon = new QLineEdit;
  m_icon->setPlaceholderText(tr("icon name (e.g. firefox) or image file"));
  auto *iconButton = new QPushButton(tr("Choose…"));
  m_iconPreview = new QLabel;
  m_iconPreview->setFixedSize(52, 52);
  m_iconPreview->setAlignment(Qt::AlignCenter);
  m_iconProblem = problemLabel();
  auto *iconBox = new QWidget;
  auto *iconGrid = new QHBoxLayout(iconBox);
  iconGrid->setContentsMargins(0, 0, 0, 0);
  auto *iconFields = new QVBoxLayout;
  iconFields->addWidget(withButton(m_icon, iconButton));
  iconFields->addWidget(m_iconProblem);
  iconGrid->addWidget(m_iconPreview);
  iconGrid->addLayout(iconFields, 1);
  form->addRow(tr("&Icon"), iconBox);

  m_env = new QPlainTextEdit;
  m_env->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_env->setPlaceholderText(tr("NAME=value, one per line"));
  m_env->setFixedHeight(64);
  m_env->installEventFilter(this);
  form->addRow(tr("&Environment"), m_env);

  m_test = new QPushButton(tr("&Test launch"));
  form->addRow(m_test);
  connect(m_test, &QPushButton::clicked, this, &PropertiesPanel::testRequested);

  m_pages->addWidget(page);

  for (QLineEdit *edit : {m_name, m_command, m_folder, m_icon})
  {
    connect(edit, &QLineEdit::editingFinished, this, &PropertiesPanel::commit);
    connect(edit, &QLineEdit::textChanged, this, &PropertiesPanel::updateProblems);
  }
  for (QCheckBox *box : {m_shell, m_terminalBox})
    connect(box, &QCheckBox::toggled, this, &PropertiesPanel::commit);
  connect(iconButton, &QPushButton::clicked, this, &PropertiesPanel::browseIcon);
  connect(folderButton, &QPushButton::clicked, this, &PropertiesPanel::browseFolder);

  setItem(nullptr, std::nullopt, QString());
}

void PropertiesPanel::setItem(const Item *item, std::optional<QPoint> cell, const QString &terminalPrefix)
{
  m_terminal = terminalPrefix;
  m_updating = true;
  if (!item)
  {
    m_pages->setCurrentIndex(0);
    m_addButton->setVisible(cell.has_value());
    m_appsButton->setVisible(cell.has_value());
    m_addGroupButton->setVisible(cell.has_value());
    m_emptyText->setText(cell ? tr("Empty cell (%1, %2)").arg(cell->x()).arg(cell->y())
                              : tr("Select a cell in the grid."));
    m_updating = false;
    return;
  }

  m_item = *item;
  m_pages->setCurrentIndex(1);
  m_position->setText(tr("(%1, %2)%3")
                        .arg(item->x)
                        .arg(item->y)
                        .arg(item->x == 0 && item->y == 0 ? tr(" — under the pointer") : QString()));
  setText(m_name, item->name);
  setText(m_command, item->command);
  m_shell->setChecked(item->runInShell);
  m_terminalBox->setChecked(item->terminal);
  m_terminalBox->setText(terminalPrefix.isEmpty()
                           ? tr("Run in a terminal (no terminal configured)")
                           : tr("Run in a terminal (%1)").arg(terminalPrefix.section(QLatin1Char(' '), 0, 0)));
  setText(m_folder, item->workingDir);
  setText(m_icon, item->icon);
  QStringList env;
  for (auto it = item->env.begin(); it != item->env.end(); ++it)
    env << it.key() + QLatin1Char('=') + it.value();
  m_env->setPlainText(env.join(QLatin1Char('\n')));

  const bool launcher = item->type == ItemType::Launcher;
  for (QWidget *w : std::initializer_list<QWidget *>{m_commandRow, m_folderRow, m_env, m_test})
    m_form->setRowVisible(w, launcher);
  m_form->setRowVisible(m_groupInfo->parentWidget(), !launcher);
  m_icon->setPlaceholderText(launcher ? tr("icon name (e.g. firefox) or image file")
                                      : tr("folder (the default for groups)"));
  if (!launcher)
  {
    int groups = 0;
    for (const Item &child : item->children)
      if (child.type == ItemType::Group)
        ++groups;
    const int launchers = int(item->children.size()) - groups;
    m_groupInfo->setText(item->children.empty()
                           ? tr("Empty. Open it to add launchers (or more groups).")
                           : tr("%n launcher(s)", nullptr, launchers) +
                               (groups > 0 ? tr(", %n group(s)", nullptr, groups) : QString()) +
                               tr(" — opens around this icon in the popup."));
  }
  m_updating = false;
  updateProblems();
}

void PropertiesPanel::focusName()
{
  m_name->setFocus();
  m_name->selectAll();
}

Item PropertiesPanel::currentValue() const
{
  Item item = m_item;
  item.name = m_name->text().trimmed();
  item.command = m_command->text().trimmed();
  item.runInShell = m_shell->isChecked();
  item.terminal = m_terminalBox->isChecked();
  item.workingDir = m_folder->text().trimmed();
  item.icon = m_icon->text().trimmed();
  item.env.clear();
  const QStringList lines = m_env->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString &line : lines)
  {
    const qsizetype eq = line.indexOf(QLatin1Char('='));
    if (eq > 0)
      item.env.insert(line.left(eq).trimmed(), line.mid(eq + 1));
  }
  return item;
}

void PropertiesPanel::commit()
{
  if (m_updating || m_pages->currentIndex() != 1)
    return;
  const Item value = currentValue();
  m_item = value;
  emit itemEdited(value);
}

void PropertiesPanel::updateProblems()
{
  if (m_updating || m_pages->currentIndex() != 1)
    return;
  const Item value = currentValue();
  showProblem(m_commandProblem, commandProblem(value, m_terminal));
  showProblem(m_folderProblem, workingDirProblem(value));

  const bool defaultFolder = value.type == ItemType::Group && value.icon.isEmpty();
  const LoadedIcon icon = loadIcon(defaultFolder ? QStringLiteral("folder") : value.icon, 48, devicePixelRatioF());
  QString problem;
  if (!icon.found && !value.icon.isEmpty())
  {
    const LoadedIcon fallback = loadItemIcon(value, 48, devicePixelRatioF());
    if (fallback.found)
    {
      m_iconPreview->setPixmap(fallback.pixmap);
      problem = tr("This icon is gone; the app's current icon is shown instead. Choose… to pick one.");
    }
    else
    {
      m_iconPreview->setPixmap(icon.pixmap);
      problem = tr("No such icon file or theme icon; a placeholder will be shown.");
    }
  }
  else
    m_iconPreview->setPixmap(icon.pixmap);
  showProblem(m_iconProblem, problem);
}

void PropertiesPanel::browseIcon()
{
  if (iconChooser)
  {
    const QString chosen = iconChooser(m_icon->text());
    if (!chosen.isEmpty())
    {
      m_icon->setText(chosen);
      m_icon->setCursorPosition(0);
      commit();
    }
    return;
  }
  QString start = QFileInfo(m_icon->text()).absolutePath();
  if (!m_icon->text().contains(QLatin1Char('/')) || !QFileInfo::exists(start))
    start = QStringLiteral("/usr/share/icons");
  const QString file = QFileDialog::getOpenFileName(
    this, tr("Choose an icon"), start, tr("Images (*.png *.svg *.svgz *.xpm *.jpg *.jpeg *.gif *.ico *.webp);;All files (*)"));
  if (file.isEmpty())
    return;
  m_icon->setText(file);
  commit();
}

void PropertiesPanel::browseFolder()
{
  const QString start = m_folder->text().isEmpty() ? QDir::homePath() : expandHome(m_folder->text());
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Start in folder"), start);
  if (dir.isEmpty())
    return;
  m_folder->setText(dir);
  commit();
}

bool PropertiesPanel::eventFilter(QObject *watched, QEvent *e)
{
  if (watched == m_env && e->type() == QEvent::FocusOut)
    commit();
  return QWidget::eventFilter(watched, e);
}

} // namespace prowal
