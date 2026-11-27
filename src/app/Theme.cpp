#include "Theme.h"

#include "Settings.h"

#include <QMap>

namespace theme {

const Palette& dark() {
  static const Palette p{
      /*appBackground*/ QColor("#16181B"),
      /*barBackground*/ QColor("#0F1113"),
      /*toolbar*/       QColor("#1F2226"),
      /*panel*/         QColor("#23262B"),
      /*panelBorder*/   QColor("#33373D"),
      /*field*/         QColor("#16181B"),
      /*fieldBorder*/   QColor("#3A3F46"),
      /*hover*/         QColor("#2E3238"),
      /*selection*/     QColor("#2F3A4A"),
      /*text*/          QColor("#E6E8EB"),
      /*textMuted*/     QColor("#9AA1AB"),
      /*textDisabled*/  QColor("#6B7280"),
      /*accent*/        QColor("#F0A04B"),
      /*accentText*/    QColor("#16181B"),
      /*highlight*/     QColor("#5AA9FF"),
      /*success*/       QColor("#4CC38A"),
      /*danger*/        QColor("#E5484D"),
      /*viewport*/      QColor("#2A2E34"),
      /*viewportGrid*/  QColor("#3A3F46"),
  };
  return p;
}

const Palette& light() {
  static const Palette p{
      /*appBackground*/ QColor("#E4E7EB"),
      /*barBackground*/ QColor("#F7F8F9"),
      /*toolbar*/       QColor("#F1F3F5"),
      /*panel*/         QColor("#FFFFFF"),
      /*panelBorder*/   QColor("#D5D9DE"),
      /*field*/         QColor("#FFFFFF"),
      /*fieldBorder*/   QColor("#C3C9D0"),
      /*hover*/         QColor("#E4E8ED"),
      /*selection*/     QColor("#D3E5F8"),
      /*text*/          QColor("#1F2328"),
      /*textMuted*/     QColor("#5E6670"),
      /*textDisabled*/  QColor("#A3A9B1"),
      /*accent*/        QColor("#E2852A"),
      /*accentText*/    QColor("#FFFFFF"),
      /*highlight*/     QColor("#1C7CDB"),
      /*success*/       QColor("#2EA66B"),
      /*danger*/        QColor("#D93A3F"),
      /*viewport*/      QColor("#EEF0F2"),
      /*viewportGrid*/  QColor("#C9CED4"),
      /*light*/         true,
  };
  return p;
}

const Palette& current() {
  static const Palette& chosen = prefs::get(prefs::kTheme).toString() == "light" ? light() : dark();
  return chosen;
}

QString styleSheet(const Palette& p) {
  // {token} placeholders are replaced with palette colours below.
  QString qss = R"(
* { color: {text}; font-size: 12px; }
QMainWindow, QWidget#Central { background: {appBackground}; }
QToolTip { background: {panel}; color: {text}; border: 1px solid {panelBorder}; padding: 4px 6px; }

/* Document bar */
#DocumentBar { background: {barBackground}; border-bottom: 1px solid #2A2D32; }
#DocumentBar QToolButton { border: 0; border-radius: 6px; padding: 4px; background: transparent; }
#DocumentBar QToolButton:hover { background: {hover}; }
#AppName { font-weight: 700; font-size: 15px; letter-spacing: 0.5px; }
#ProjectsButton { border: 1px solid {panelBorder}; background: {toolbar}; padding: 4px 10px; }
#DocTab { background: {toolbar}; border-top: 2px solid {accent}; border-top-left-radius: 6px;
          border-top-right-radius: 6px; padding: 6px 12px; }
#DocTab QLabel { background: transparent; }
#SaveState { color: {textMuted}; }
#Avatar { background: #3A3F46; border-radius: 14px; font-weight: 600; }

/* Ribbon */
#Ribbon { background: {toolbar}; border-bottom: 1px solid #2A2D32; }
#WorkspaceSwitcher { background: {field}; border: 1px solid {panelBorder}; border-radius: 6px;
                     padding: 0 14px; font-weight: 700; letter-spacing: 1px; min-width: 150px; min-height: 48px; }
#WorkspaceSwitcher:hover { border-color: {textMuted}; }
#WorkspaceSwitcher QAbstractItemView { background: {panel}; border: 1px solid {panelBorder};
                                       selection-background-color: {selection}; }
#RibbonTabs QTabBar::tab { background: transparent; color: {textMuted}; padding: 4px 10px; font-weight: 700;
                           font-size: 11px; border-bottom: 2px solid transparent; }
#RibbonTabs QTabBar::tab:selected { color: {text}; border-bottom: 2px solid {accent}; }
#RibbonTabs QTabBar::tab:hover:!selected { color: {text}; }
#ToolGroup { border-right: 1px solid {panelBorder}; }
#ToolButton { border: 1px solid transparent; border-radius: 6px; background: transparent; }
/* #ToolButton hover is painted (and faded) by AnimatedToolButton */
#ToolButton:pressed, #ToolButton:checked { border-color: {accent}; }
#ToolButton[active="true"] { border-color: {accent}; background: {selection}; }
#PrimaryToolButton { border: 0; border-radius: 6px; background: {success}; color: {accentText};
  font-weight: 600; font-size: 10px; padding: 4px 6px; }
#PrimaryToolButton:hover { background: {successHover}; }
#SketchValueEdit { background: {field}; color: {text}; border: 1px solid {accent}; border-radius: 4px;
  padding: 2px 6px; font-size: 12px; selection-background-color: {accent}; selection-color: {accentText}; }
#GroupButton { border: 0; background: transparent; color: {textMuted}; font-size: 10px; font-weight: 700;
               letter-spacing: 0.8px; padding: 2px 4px; }
#GroupButton:hover { color: {text}; }
#GroupButton::menu-indicator { image: none; width: 0; }

/* Menus */
QMenu { background: {panel}; border: 1px solid {panelBorder}; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 10px; border-radius: 4px; }
QMenu::item:selected { background: {selection}; }
QMenu::item:disabled { color: {textDisabled}; }
QMenu::separator { height: 1px; background: {panelBorder}; margin: 4px 6px; }

/* Floating panels over the viewport */
#FloatingPanel { background: {panel}; border: 1px solid {panelBorder}; border-radius: 8px; }
#PanelHeader { border-bottom: 1px solid {panelBorder}; }
#PanelTitle { font-weight: 700; font-size: 11px; letter-spacing: 1px; background: transparent; }
#PanelHeader QToolButton { border: 0; background: transparent; border-radius: 4px; }
#PanelHeader QToolButton:hover { background: {hover}; }
#AccentDot { background: {accent}; border-radius: 4px; }

/* Browser tree */
#Browser { background: transparent; border: 0; outline: 0; show-decoration-selected: 1; }
#Browser::item { height: 24px; border: 0; padding-left: 2px; }
#Browser::item:hover, #Browser::branch:hover { background: {hover}; }
#Browser::item:selected, #Browser::branch:selected { background: {selection}; color: {text}; }
#Browser::branch { background: transparent; }
#Browser::branch:has-children:closed { image: url(:/icons/branch-closed.svg); }
#Browser::branch:has-children:open { image: url(:/icons/branch-open.svg); }

/* Command dialog */
#DialogLabel { color: {textMuted}; background: transparent; }
#QuickButton { background: transparent; border: 0; border-radius: 4px; padding: 3px; min-width: 26px; min-height: 26px; }
#QuickButton:hover { background: {hover}; }
#QuickButton:pressed { background: {selection}; }
#QuickButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; right: 2px; }
#QuickSeparator { background: {panelBorder}; }
#VersionLabel { color: {textMuted}; font-size: 12px; padding: 0 2px; }
QDialog#Preferences, QDialog#InfoDialog { background: {panel}; }
#PrefsCategories { background: {appBackground}; border: 1px solid {panelBorder}; border-radius: 6px; padding: 4px; outline: 0; }
#PrefsCategories::item { padding: 7px 10px; border-radius: 4px; border-left: 3px solid transparent; }
#PrefsCategories::item:hover { background: {hover}; }
#PrefsCategories::item:selected { background: {selection}; color: {text}; border-left: 3px solid {accent}; }
#PrefsGroup { border: 1px solid {panelBorder}; border-radius: 6px; margin-top: 10px; padding-top: 6px; font-weight: 600; }
#PrefsGroup::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: {text}; }
#PrefsGroup QLabel, #PrefsGroup QCheckBox { font-weight: 400; }
#PrefsNote { color: {textMuted}; font-weight: 400; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid {fieldBorder}; border-radius: 4px; background: {field}; }
QCheckBox::indicator:checked { background: {highlight}; border-color: {highlight}; image: url(:/icons/check-white.svg); }
QSpinBox::up-button, QSpinBox::down-button { width: 14px; border: 0; }
#InfoTable { background: {appBackground}; border: 1px solid {panelBorder}; border-radius: 6px; gridline-color: {panelBorder}; }
#InfoTable QHeaderView::section { background: {toolbar}; color: {textMuted}; border: 0; padding: 4px 8px; }
#PaletteScroll, #PaletteScroll > QWidget > QWidget { background: transparent; }
#PaletteHeading { color: {textMuted}; font-weight: 600; font-size: 11px; padding-top: 4px; }
QToolButton#PaletteHeading { background: transparent; border: none; padding: 4px 0 0 0; }
QToolButton#PaletteHeading:hover { color: {text}; }
#PaletteDof { font-weight: 600; }
#PaletteDof[state="full"] { color: {success}; }
#PaletteDof[state="conflict"] { color: {danger}; }
#PaletteButton { padding: 3px 8px; border-radius: 5px; }
#PaletteTool { background: transparent; border: 1px solid transparent; border-radius: 6px; }
#PaletteTool:hover { background: {hover}; }
#PaletteTool[active="true"] { border-color: {accent}; background: {selection}; }
#PaletteToolTitle { font-weight: 700; }
#PaletteHint { color: {textMuted}; }
#DialogInfo { color: {text}; background: transparent; font-weight: 600; }
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox#DialogCombo {
  background: {field}; border: 1px solid {fieldBorder}; border-radius: 4px; padding: 3px 6px; min-height: 20px;
  selection-background-color: {selection}; }
QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus { border-color: {highlight}; }
QLineEdit[invalid="true"] { border-color: {danger}; }
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: 0; }
QPushButton { background: transparent; border: 1px solid {fieldBorder}; border-radius: 6px; padding: 6px 16px; }
QPushButton:hover { background: {hover}; }
QPushButton#PrimaryButton { background: {accent}; color: {accentText}; border: 0; font-weight: 700; padding: 6px 20px; }
QPushButton#PrimaryButton:hover { background: {accentHover}; }

/* Navigation bar */
#NavBar { background: {panel}; border: 1px solid {panelBorder}; border-radius: 8px; }
#NavBar QToolButton { border: 0; border-radius: 6px; background: transparent; padding: 6px; }
#NavBar QToolButton:checked { background: {selection}; }
#NavSeparator { background: {panelBorder}; }

/* Timeline */
#Timeline { background: {toolbar}; border-top: 1px solid #2A2D32; }
#Timeline QToolButton { border: 0; border-radius: 4px; background: transparent; padding: 4px; }
#Timeline QToolButton:hover { background: {hover}; }
#Timeline #FeatureChip { background: {hover}; border: 1px solid {fieldBorder}; border-radius: 5px; padding: 0; }
#Timeline #FeatureChip[sketch="true"] { background: {selection}; }
#Timeline #FeatureChip:hover { border-color: {textMuted}; }
#Timeline #FeatureChip[rolledBack="true"] { background: transparent; border: 1px dashed {fieldBorder}; }
#Timeline #FeatureChip[failed="true"] { border-color: {danger}; }
#TimelineMarker { background: {accent}; border-radius: 2px; }
#StatusText { color: {textMuted}; }
#UnitsButton { color: {textMuted}; border: 1px solid {panelBorder}; border-radius: 4px; padding: 2px 8px; font-weight: 600; }
#UnitsButton:hover { color: {text}; border-color: {textMuted}; }
#UnitsButton::menu-indicator { image: none; width: 0; }

QScrollBar:horizontal, QScrollBar:vertical { background: transparent; height: 8px; width: 8px; }
QScrollBar::handle { background: {fieldBorder}; border-radius: 4px; min-width: 24px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QMessageBox { background: {panel}; }
)";

  const QMap<QString, QColor> tokens{
      {"appBackground", p.appBackground}, {"barBackground", p.barBackground},
      {"toolbar", p.toolbar},             {"panel", p.panel},
      {"panelBorder", p.panelBorder},     {"field", p.field},
      {"fieldBorder", p.fieldBorder},     {"hover", p.hover},
      {"selection", p.selection},         {"text", p.text},
      {"textMuted", p.textMuted},         {"textDisabled", p.textDisabled},
      {"accent", p.accent},               {"accentText", p.accentText},
      {"accentHover", p.accent.lighter(110)},
      {"highlight", p.highlight},         {"success", p.success},
      {"danger", p.danger},              {"successHover", p.success.lighter(112)},
  };
  for (auto it = tokens.cbegin(); it != tokens.cend(); ++it) {
    qss.replace("{" + it.key() + "}", it.value().name());
  }
  return qss;
}

}  // namespace theme
