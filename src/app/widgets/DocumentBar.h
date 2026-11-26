#pragma once

#include <QFrame>

class QLabel;
class QMenu;
class QToolButton;

// Top bar: app menu, Projects, open document tabs, save state, help, account.
class DocumentBar : public QFrame {
  Q_OBJECT
 public:
  explicit DocumentBar(QWidget* parent = nullptr);

  QMenu* appMenu() const { return appMenu_; }
  void setDocumentName(const QString& name);
  void setSaveState(const QString& text);

 signals:
  void helpRequested();

 private:
  QMenu* appMenu_ = nullptr;
  QLabel* docName_ = nullptr;
  QLabel* saveState_ = nullptr;
};
