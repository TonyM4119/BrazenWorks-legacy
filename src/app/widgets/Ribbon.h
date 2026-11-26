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
  };
  struct Group {
    QString label;
    QVector<Tool> tools;
  };
  struct Workspace {
    QString name;
    QStringList tabs;
    QVector<Group> groups;  // groups of the first tab (other tabs: later)
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

  QComboBox* workspaceBox_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QHBoxLayout* toolRow_ = nullptr;
  QVector<Workspace> workspaces_;
  QHash<QString, QToolButton*> buttons_;
  QString activeTool_;
};
