#pragma once

#include <QFrame>

class QToolButton;

// The pill of view controls at the bottom of the viewport.
class NavBar : public QFrame {
  Q_OBJECT
 public:
  enum class DragMode { Select, Orbit, Pan, Zoom };
  enum class DisplayMode { ShadedWithEdges, Shaded, Wireframe };

  explicit NavBar(QWidget* parent = nullptr);

 signals:
  void homeRequested();
  void fitRequested();
  void dragModeChanged(NavBar::DragMode mode);
  void displayModeChanged(NavBar::DisplayMode mode);
  void gridToggled(bool visible);

 public:
  void setGridChecked(bool on);

 private:
  class QToolButton* gridButton_ = nullptr;

 private:
  QToolButton* addButton(const QString& icon, const QString& tip, bool checkable = false);
  void addSeparator();
};
