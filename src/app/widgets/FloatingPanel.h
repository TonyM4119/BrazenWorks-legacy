#pragma once

#include <QFrame>

class QLabel;
class QToolButton;
class QVBoxLayout;

// A rounded panel that floats over the 3D viewport (browser, command dialogs).
// Header with a title and a collapse button; any widget as content.
class FloatingPanel : public QFrame {
  Q_OBJECT
 public:
  explicit FloatingPanel(const QString& title, QWidget* parent = nullptr);

  void setContent(QWidget* content);
  QWidget* content() const { return content_; }

  void setAccentDot(bool on);
  void setCollapsible(bool on);
  bool isCollapsed() const { return collapsed_; }
  void setCollapsed(bool collapsed);

 signals:
  void collapsedChanged(bool collapsed);

 protected:
  QVBoxLayout* bodyLayout() const { return body_; }
  QWidget* header() const { return header_; }

 private:
  QWidget* header_ = nullptr;
  QWidget* dot_ = nullptr;
  QLabel* title_ = nullptr;
  QToolButton* collapse_ = nullptr;
  QVBoxLayout* body_ = nullptr;
  QWidget* content_ = nullptr;
  bool collapsed_ = false;
};
