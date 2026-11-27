#pragma once

#include <QFrame>
#include <QHash>
#include <QVector>

class QComboBox;
class QHBoxLayout;
class QTabBar;
class QToolButton;

// Workspace switcher, tabs and icon tool groups, Fusion-style. Every tool also
// appears in its group's dropdown menu (click the group name).
class Ribbon : public QFrame {
  Q_OBJECT
 public:
  struct Tool {
    QString id;       // stable id passed to toolTriggered, e.g. "design.box"
    QString label;
    QString icon;
    QString tip;      // empty = enabled; otherwise shown and the tool is disabled
    bool primary = false;  // big coloured button with its label (e.g. Finish Sketch)
    bool menuOnly = false; // only in the group's drop-down menu, as Fusion keeps rarer tools
    QString submenu;       // non-empty: inside this submenu of the group's menu (e.g. "Rectangle")
    bool separator = false;  // a line above it in the menu
    QString help;          // hover help: what it does and how to use it
    QString shortcut;      // shown in menus and the hover help, e.g. "L"
  };
  struct Group {
    QString label;
    QVector<Tool> tools;
    bool compact = false;  // small icons in two rows
    int collapse = 0;      // narrow windows fold the highest numbers into a menu button first; 0 never
  };
  struct Workspace {
    QString name;
    QStringList tabs;
    QVector<Group> groups;  // groups of the active tab (other tabs: later)
    int activeTab = 0;
  };

  explicit Ribbon(QWidget* parent = nullptr);

  void setWorkspaces(const QVector<Workspace>& workspaces);
  void setCurrentWorkspace(int index);
  int currentWorkspace() const;
  // Highlights the tool that is running (empty = none).
  void setActiveTool(const QString& id);

 signals:
  void toolTriggered(const QString& id);
  void workspaceChanged(int index);

 private:
  void rebuildTools();
  void updateCollapse();
  void resizeEvent(QResizeEvent* e) override;
  struct Built {
    QWidget* buttons;    // the expanded icons
    QToolButton* folded; // one button with the group's menu, when folded
    int collapse;
  };
  QVector<Built> built_;
  QWidget* toolHolder_ = nullptr;

  QComboBox* workspaceBox_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QHBoxLayout* toolRow_ = nullptr;
  QVector<Workspace> workspaces_;
  QHash<QString, QToolButton*> buttons_;
  QString activeTool_;
};
