// Copyright 1996-2018 Cyberbotics Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "WbMainWindow.hpp"

#include "WbAboutBox.hpp"
#include "WbActionManager.hpp"
#include "WbAnimationRecorder.hpp"
#include "WbApplication.hpp"
#include "WbApplicationInfo.hpp"
#include "WbBuildEditor.hpp"
#include "WbClipboard.hpp"
#include "WbConsole.hpp"
#include "WbContextMenuGenerator.hpp"
#include "WbControlledWorld.hpp"
#include "WbDockWidget.hpp"
#include "WbDocumentation.hpp"
#include "WbFileUtil.hpp"
#include "WbGuidedTour.hpp"
#include "WbHtmlExportDialog.hpp"
#include "WbJoystickInterface.hpp"
#include "WbMessageBox.hpp"
#include "WbNewControllerWizard.hpp"
#include "WbNewPhysicsPluginWizard.hpp"
#include "WbNewProjectWizard.hpp"
#include "WbNodeOperations.hpp"
#include "WbNodeUtilities.hpp"
#include "WbOdeDebugger.hpp"
#include "WbOpenSampleWorldDialog.hpp"
#include "WbPerformanceLog.hpp"
#include "WbPerspective.hpp"
#include "WbPhysicsPlugin.hpp"
#include "WbPreferences.hpp"
#include "WbPreferencesDialog.hpp"
#include "WbProject.hpp"
#include "WbProjectRelocationDialog.hpp"
#include "WbProtoList.hpp"
#include "WbRecentFilesList.hpp"
#include "WbRenderingDevice.hpp"
#include "WbRenderingDeviceWindowFactory.hpp"
#include "WbRobot.hpp"
#include "WbRobotWindow.hpp"
#include "WbSaveWarningDialog.hpp"
#include "WbSceneTree.hpp"
#include "WbSelection.hpp"
#include "WbSimulationState.hpp"
#include "WbSimulationView.hpp"
#include "WbSimulationWorld.hpp"
#include "WbStandardPaths.hpp"
#include "WbStreamingServer.hpp"
#include "WbSysInfo.hpp"
#include "WbTemplateManager.hpp"
#include "WbVideoRecorder.hpp"
#include "WbView3D.hpp"
#include "WbWebotsUpdateDialog.hpp"
#include "WbWebotsUpdateManager.hpp"
#include "WbWrenOpenGlContext.hpp"
#include "WbWrenTextureOverlay.hpp"

#ifdef _WIN32
#include "WbVirtualRealityHeadset.hpp"
#endif

#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>

#include <QtNetwork/QHostInfo>

#include <QtGui/QCloseEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QOpenGLFunctions_3_3_Core>
#include <QtGui/QScreen>
#include <QtGui/QWindow>

#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QStyle>

#ifdef _WIN32
#include <QtWebKit/QWebSettings>
#else
#include <QtWebEngineWidgets/QWebEngineProfile>
#endif

WbMainWindow::WbMainWindow(bool minimizedOnStart, QWidget *parent) :
  QMainWindow(parent),
  mExitStatus(0),
  mConsole(NULL),
  mDocumentation(NULL),
  mTextEditor(NULL),
  mSimulationView(NULL),
  mRecentFiles(NULL),
  mOdeDebugger(NULL),
  mOverlayMenu(NULL),
  mWorldLoadingProgressDialog(NULL),
  mIsFullScreenLocked(false),
  mMaximizedWidget(NULL) {
#ifdef __APPLE__
  // This flag is required to hide a second and useless title bar.
  setUnifiedTitleAndToolBarOnMac(true);
#endif

  setObjectName("MainWindow");
  QStatusBar *statusBar = new QStatusBar(this);
  statusBar->showMessage(tr("Welcome to Webots!"));
  setStatusBar(statusBar);

  style()->polish(this);
  QDir::addSearchPath("enabledIcons", WbStandardPaths::resourcesPath() + enabledIconPath());
  QDir::addSearchPath("disabledIcons", WbStandardPaths::resourcesPath() + disabledIconPath());
  QDir::addSearchPath("coreIcons", WbStandardPaths::resourcesPath() + coreIconPath());
  style()->polish(this);

  QApplication::setWindowIcon(QIcon("coreIcons:webots.png"));

  // listen to the application
  connect(WbApplication::instance(), &WbApplication::preWorldLoaded, this, &WbMainWindow::updateBeforeWorldLoading);
  connect(WbApplication::instance(), &WbApplication::postWorldLoaded, this, &WbMainWindow::updateAfterWorldLoading);
  connect(WbApplication::instance(), &WbApplication::createWorldLoadingProgressDialog, this,
          &WbMainWindow::createWorldLoadingProgressDialog);
  connect(WbApplication::instance(), &WbApplication::deleteWorldLoadingProgressDialog, this,
          &WbMainWindow::deleteWorldLoadingProgressDialog);
  connect(WbApplication::instance(), &WbApplication::worldLoadingHasProgressed, this, &WbMainWindow::setWorldLoadingProgress);
  connect(WbApplication::instance(), &WbApplication::worldLoadingStatusHasChanged, this, &WbMainWindow::setWorldLoadingStatus);

  connect(WbSimulationState::instance(), &WbSimulationState::enabledChanged, this, &WbMainWindow::simulationEnabledChanged);

  // listen to log
  connect(WbLog::instance(), &WbLog::logEmitted, this, &WbMainWindow::showStatusBarMessage);

  // world reload or simulation quit shoud not be executed directly (Qt::QueuedConnection)
  // because it is call in a Webots state where events have to be solved
  // (typically packets comming from libController)
  // applying the reload or quit directly may imply a Webots crash
  connect(WbApplication::instance(), &WbApplication::worldReloadRequested, this, &WbMainWindow::reloadWorld,
          Qt::QueuedConnection);
  connect(WbApplication::instance(), &WbApplication::simulationResetRequested, this, &WbMainWindow::resetWorld,
          Qt::QueuedConnection);
  connect(WbApplication::instance(), &WbApplication::simulationQuitRequested, this, &WbMainWindow::simulationQuit,
          Qt::QueuedConnection);
  connect(WbApplication::instance(), &WbApplication::worldLoadRequested, this, &WbMainWindow::loadDifferentWorld,
          Qt::QueuedConnection);

  createMainTools();
  createMenus();

  WbActionManager *actionManager = WbActionManager::instance();
  QAction *action = actionManager->action(WbActionManager::EDIT_CONTROLLER);
  connect(action, &QAction::triggered, this, &WbMainWindow::editRobotController);
  addAction(action);

  action = actionManager->action(WbActionManager::SHOW_ROBOT_WINDOW);
  connect(action, &QAction::triggered, this, &WbMainWindow::showRobotWindow);
  addAction(action);

  restorePreferredGeometry(minimizedOnStart);

  mFactoryLayout = new QByteArray(saveState());

  updateGui();

  if (WbPreferences::instance()->value("General/checkWebotsUpdateOnStartup").toBool() && WbMessageBox::enabled()) {
    WbWebotsUpdateManager *webotsUpdateManager = WbWebotsUpdateManager::instance();
    if (webotsUpdateManager->isTargetVersionAvailable() || webotsUpdateManager->error().size() > 0)
      openWebotsUpdateDialogFromStartup();
    else
      connect(webotsUpdateManager, &WbWebotsUpdateManager::targetVersionAvailable, this,
              &WbMainWindow::openWebotsUpdateDialogFromStartup);
  }

  // toggling the animation icon
  mAnimationRecordingTimer = new QTimer(this);
  connect(mAnimationRecordingTimer, &QTimer::timeout, this, &WbMainWindow::toggleAnimationIcon);
  toggleAnimationAction(false);

  WbAnimationRecorder *recorder = WbAnimationRecorder::instance();
  connect(recorder, &WbAnimationRecorder::initalizedFromStreamingServer, this, &WbMainWindow::disableAnimationAction);
  connect(recorder, &WbAnimationRecorder::cleanedUpFromStreamingServer, this, &WbMainWindow::enableAnimationAction);
  connect(recorder, &WbAnimationRecorder::requestOpenUrl, this, &WbMainWindow::openUrl);

  WbJoystickInterface::setWindowHandle(winId());

  connect(WbTemplateManager::instance(), &WbTemplateManager::preNodeRegeneration, this, &WbMainWindow::prepareNodeRegeneration);
  connect(WbTemplateManager::instance(), &WbTemplateManager::abortNodeRegeneration, this,
          &WbMainWindow::discardNodeRegeneration);
  connect(WbTemplateManager::instance(), &WbTemplateManager::postNodeRegeneration, this,
          &WbMainWindow::finalizeNodeRegeneration);
}

WbMainWindow::~WbMainWindow() {
  delete mFactoryLayout;
}

void WbMainWindow::lockFullScreen(bool isLocked) {
  mIsFullScreenLocked = isLocked;
}

void WbMainWindow::exitFullScreen() {
  if (mIsFullScreenLocked) {
    // stop video recording
    mSimulationView->movieAction()->trigger();
    mIsFullScreenLocked = false;
  }

  mToggleFullScreenAction->setChecked(false);
}

void WbMainWindow::toggleFullScreen(bool enabled) {
  setFullScreen(enabled);
}

bool WbMainWindow::setFullScreen(bool isEnabled, bool isRecording, bool showDialog, bool startup) {
  static const QString msgEnterFullScreenMode = tr("You are entering fullscreen mode.") + "<br/><br/>";

  static const QString fullscreenEsc = tr("Press ESC to quit fullscreen mode.") + "<br/>";
  static const QString movieEsc =
    "<strong>" + tr("Press ESC to stop recording the movie and quit fullscreen mode.") + "</strong><br/>";

  static const QString ctrlZero = tr("Press <i>Ctrl+0</i> to pause the simulation.") + "<br/>";
  static const QString ctrlOne = tr("Press <i>Ctrl+1</i> to execute one basic time step.") + "<br/>";
  static const QString ctrlTwo = tr("Press <i>Ctrl+2</i> to run the simulation in real time.") + "<br/>";
  static const QString ctrlThree = tr("Press <i>Ctrl+3</i> to run the simulation as fast as possible.") + "<br/>";
  static const QString ctrlFour =
    tr("Press <i>Ctrl+4</i> to run the simulation as fast as possible without rendering the 3D scene.") + "<br/>" + "<br/>";

  static const QString cmdZero = tr("Press <i>Cmd+0</i> to pause the simulation.") + "<br/>";
  static const QString cmdOne = tr("Press <i>Cmd+1</i> to execute one basic time step.") + "<br/>";
  static const QString cmdTwo = tr("Press <i>Cmd+2</i> to run the simulation in real time.") + "<br/>";
  static const QString cmdThree = tr("Press <i>Cmd+3</i> to run the simulation as fast as possible.") + "<br/>";
  static const QString cmdFour =
    tr("Press <i>Cmd+4</i> to run the simulation as fast as possible without rendering the 3D scene.") + "<br/>" + "<br/>";

  static const QString ctrlScreenshot =
    tr("Press <i>Ctrl+Shift+P</i> to take a screenshot of the 3D screen.") + "<br/>" + "<br/>";
  static const QString cmdScreenshot =
    tr("Press <i>Cmd+Shift+P</i> to take a screenshot of the 3D screen.") + "<br/>" + "<br/>";
  static const QString screenshotNotes = tr("<b>Note:</b> When taking a screenshot in fullscreen mode, the resulting "
                                            "screenshot's save path will be chosen automatically.") +
                                         "<br/>";

  static QString screenshotPath;
  screenshotPath =
    tr("Screenshots will be saved in <i>%1</i>.").arg(WbPreferences::instance()->value("Directories/screenshots").toString());

  static bool macos = WbSysInfo::platform() == WbSysInfo::MACOS_PLATFORM;
  static const QString &zero = macos ? cmdZero : ctrlZero;
  static const QString &one = macos ? cmdOne : ctrlOne;
  static const QString &two = macos ? cmdTwo : ctrlTwo;
  static const QString &three = macos ? cmdThree : ctrlThree;
  static const QString &four = macos ? cmdFour : ctrlFour;
  static const QString &five = macos ? cmdScreenshot : ctrlScreenshot;

  static QByteArray currentPerspective = *mFactoryLayout;

  if (mIsFullScreenLocked)
    return false;

  if (isEnabled) {
    if (showDialog) {
      QString message = msgEnterFullScreenMode;
      if (isRecording)
        message += movieEsc;
      else
        message += fullscreenEsc;
      message += zero;
      message += one;
      message += two;
      message += three;
      message += four;
      message += five;
      message += screenshotNotes;
      message += screenshotPath;
      if (WbMessageBox::question(message, this, tr("Fullscreen mode"), QMessageBox::Ok) == QMessageBox::Cancel) {
        mToggleFullScreenAction->blockSignals(true);
        mToggleFullScreenAction->setChecked(false);
        mToggleFullScreenAction->blockSignals(false);
        return false;
      }

      // store actual window geometry and perspective
      writePreferences();
      currentPerspective = saveState();
    }

    if (startup) {
      // store actual window geometry and perspective
      writePreferences();
      currentPerspective = saveState();
      if (!mToggleFullScreenAction->isChecked()) {
        disconnect(mToggleFullScreenAction, &QAction::toggled, this, &WbMainWindow::toggleFullScreen);
        mToggleFullScreenAction->setChecked(true);
        connect(mToggleFullScreenAction, &QAction::toggled, this, &WbMainWindow::toggleFullScreen);
      }
    }

    // hide docks
    mConsole->hide();
    mDocumentation->hide();
    if (mTextEditor)
      mTextEditor->hide();

    // hide menu bar and status bar
    mMenuBar->hide();
    statusBar()->hide();

    // remove tool bar in WbSimulationView
    mSimulationView->show();
    mSimulationView->setDecorationVisible(false);

    // show main window in fullscreen mode
    showFullScreen();

    // connect exit shortcut
    connect(mExitFullScreenAction, &QAction::triggered, this, &WbMainWindow::exitFullScreen);

  } else {
    // show main window in normal mode
    showNormal();

    // show docks
    mConsole->show();
    mDocumentation->show();
    if (mTextEditor)
      mTextEditor->show();

    // show menu bar and status bar
    mMenuBar->show();
    statusBar()->show();

    // show tool bar in WbSimulationView
    mSimulationView->setDecorationVisible(true);

    restorePreferredGeometry();
    restoreState(currentPerspective);

    // disconnect exit shortcut
    disconnect(mExitFullScreenAction, &QAction::triggered, this, &WbMainWindow::exitFullScreen);
  }

  return true;
}

void WbMainWindow::addDock(QWidget *dock) {
  mDockWidgets.append(dock);
  connect(dock, SIGNAL(needsMaximize()), this, SLOT(maximizeDock()));
  connect(dock, SIGNAL(needsMinimize()), this, SLOT(minimizeDock()));
}

void WbMainWindow::createMainTools() {
  // extend Scene Tree to bottom left corner
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

  // the console is built at first in order to
  // be able to display message boxes if fatal
  // errors come after (ex. initializing WREN)
  mConsole = new WbConsole(this);
  addDockWidget(Qt::BottomDockWidgetArea, mConsole);
  addDock(mConsole);

  mSimulationView = new WbSimulationView(this, toolBarAlign());
  setCentralWidget(mSimulationView);
  addDock(mSimulationView);
  connect(mSimulationView, &WbSimulationView::requestOpenUrl, this, &WbMainWindow::openUrl);
  connect(mSimulationView->view3D(), &WbView3D::showRobotWindowRequest, this, &WbMainWindow::showRobotWindow);
  connect(mSimulationView->selection(), &WbSelection::selectionChangedFromSceneTree, this, &WbMainWindow::updateOverlayMenu);
  connect(mSimulationView->selection(), &WbSelection::selectionChangedFromView3D, this, &WbMainWindow::updateOverlayMenu);
  connect(mSimulationView->sceneTree(), &WbSceneTree::editRequested, this, &WbMainWindow::openFileInTextEditor);
  if (WbStreamingServer::instanceExists()) {
    WbStreamingServer::instance()->setView3D(mSimulationView->view3D());
    WbStreamingServer::instance()->setMainWindow(this);
  }

  mTextEditor = new WbBuildEditor(this, toolBarAlign());
  addDockWidget(Qt::RightDockWidgetArea, mTextEditor, Qt::Vertical);
  addDock(mTextEditor);
  connect(mTextEditor, &WbBuildEditor::reloadRequested, this, &WbMainWindow::reloadWorld, Qt::QueuedConnection);
  connect(mTextEditor, &WbBuildEditor::resetRequested, this, &WbMainWindow::resetWorld, Qt::QueuedConnection);

  mDocumentation = new WbDocumentation(this);
  addDockWidget(Qt::LeftDockWidgetArea, mDocumentation, Qt::Horizontal);
  addDock(mDocumentation);
  connect(mSimulationView->sceneTree(), &WbSceneTree::documentationRequest, mDocumentation, &WbDocumentation::open);
  mDocumentation->open("guide", "index", false);
  // this instruction does nothing but prevents issues resizing QDockWidgets
  // https://stackoverflow.com/questions/48766663/resize-qdockwidget-without-undocking-and-docking
  resizeDocks({mDocumentation}, {20}, Qt::Horizontal);

  mOdeDebugger = new WbOdeDebugger();
  connect(WbVideoRecorder::instance(), &WbVideoRecorder::requestOpenUrl, this, &WbMainWindow::openUrl);
}

QMenu *WbMainWindow::createFileMenu() {
  QMenu *menu = new QMenu(this);
  menu->setTitle(tr("&File"));

  QAction *action;
  WbActionManager *manager = WbActionManager::instance();

  action = manager->action(WbActionManager::NEW_WORLD);
  connect(action, &QAction::triggered, this, &WbMainWindow::newWorld);
  menu->addAction(action);

  action = manager->action(WbActionManager::OPEN_WORLD);
  connect(action, &QAction::triggered, this, &WbMainWindow::openWorld);
  menu->addAction(action);

  mRecentFilesSubMenu = menu->addMenu(tr("&Open Recent World"));
  mRecentFiles = new WbRecentFilesList(10, mRecentFilesSubMenu);
  connect(mRecentFiles, &WbRecentFilesList::fileChosen, this, &WbMainWindow::loadDifferentWorld);

  action = manager->action(WbActionManager::OPEN_SAMPLE_WORLD);
  connect(action, &QAction::triggered, this, &WbMainWindow::openSampleWorld);
  menu->addAction(action);

  action = manager->action(WbActionManager::SAVE_WORLD);
  connect(action, &QAction::triggered, this, &WbMainWindow::saveWorld);
  menu->addAction(action);

  action = manager->action(WbActionManager::SAVE_WORLD_AS);
  connect(action, &QAction::triggered, this, &WbMainWindow::saveWorldAs);
  menu->addAction(action);

  action = manager->action(WbActionManager::RELOAD_WORLD);
  connect(action, &QAction::triggered, this, &WbMainWindow::reloadWorld);
  menu->addAction(action);

  action = manager->action(WbActionManager::RESET_SIMULATION);
  connect(action, &QAction::triggered, this, &WbMainWindow::resetWorld);
  menu->addAction(action);

  menu->addSeparator();

  if (mTextEditor) {
    menu->addAction(manager->action(WbActionManager::NEW_FILE));
    menu->addAction(manager->action(WbActionManager::OPEN_FILE));
    menu->addAction(manager->action(WbActionManager::SAVE_FILE));
    menu->addAction(manager->action(WbActionManager::SAVE_FILE_AS));
    menu->addAction(manager->action(WbActionManager::SAVE_ALL_FILES));
    menu->addAction(manager->action(WbActionManager::REVERT_FILE));

    menu->addSeparator();

    menu->addAction(manager->action(WbActionManager::PRINT_PREVIEW));
    menu->addAction(manager->action(WbActionManager::PRINT));

    menu->addSeparator();
  }
  action = new QAction(this);
  action->setText(tr("&Import VRML97..."));
  action->setStatusTip(tr("Add a VRML97 object to the Scene Tree."));
  action->setToolTip(action->statusTip());
  connect(action, &QAction::triggered, this, &WbMainWindow::importVrml);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("&Export VRML97..."));
  action->setStatusTip(tr("Export the whole Scene Tree as a VRML97 file."));
  action->setToolTip(action->statusTip());
  connect(action, &QAction::triggered, this, &WbMainWindow::exportVrml);
  menu->addAction(action);

  menu->addSeparator();

  menu->addAction(manager->action(WbActionManager::TAKE_SCREENSHOT));
  menu->addAction(mSimulationView->movieAction());
  action = new QAction(this);
  action->setText(tr("&Export HTML5 Model..."));
  action->setStatusTip(tr("Export the whole Scene Tree as an HTML5 file."));
  action->setToolTip(action->statusTip());
  connect(action, &QAction::triggered, this, &WbMainWindow::exportHtml);
  menu->addAction(action);
  menu->addAction(manager->action(WbActionManager::ANIMATION));
  connect(manager->action(WbActionManager::ANIMATION), &QAction::triggered, this, &WbMainWindow::startAnimationRecording);

  menu->addSeparator();

#ifdef _WIN32  // On Windows, applications generally use the "Exit" terminology to terminate.
  const QString terminateWord(tr("Exit"));
#else  // On Linux and macOS, they use "Quit" instead of "Exit".
  const QString terminateWord(tr("Quit"));
#endif

  action = new QAction(terminateWord, this);
  action->setMenuRole(QAction::QuitRole);  // Mac: put the menu respecting the MacOS specifications
  action->setShortcut(Qt::CTRL + Qt::Key_Q);
  action->setStatusTip(tr("Terminate the Webots application."));
  action->setToolTip(action->statusTip());
  connect(action, &QAction::triggered, this, &WbMainWindow::close);
  menu->addAction(action);

  return menu;
}

QMenu *WbMainWindow::createEditMenu() {
  QMenu *menu = new QMenu(this);
  menu->setTitle(tr("&Edit"));

  WbActionManager *manager = WbActionManager::instance();
  menu->addAction(manager->action(WbActionManager::UNDO));
  menu->addAction(manager->action(WbActionManager::REDO));
  menu->addSeparator();
  menu->addAction(manager->action(WbActionManager::CUT));
  menu->addAction(manager->action(WbActionManager::COPY));
  menu->addAction(manager->action(WbActionManager::PASTE));
  menu->addAction(manager->action(WbActionManager::SELECT_ALL));
  menu->addSeparator();
  menu->addAction(manager->action(WbActionManager::FIND));
  menu->addAction(manager->action(WbActionManager::FIND_NEXT));
  menu->addAction(manager->action(WbActionManager::FIND_PREVIOUS));
  menu->addAction(manager->action(WbActionManager::REPLACE));
  menu->addSeparator();
  menu->addAction(manager->action(WbActionManager::GO_TO_LINE));
  menu->addSeparator();
  menu->addAction(manager->action(WbActionManager::TOGGLE_LINE_COMMENT));
  menu->addSeparator();
  menu->addAction(manager->action(WbActionManager::DUPLICATE_SELECTION));
  menu->addAction(manager->action(WbActionManager::TRANSPOSE_LINE));

  return menu;
}

QMenu *WbMainWindow::createViewMenu() {
  QMenu *menu = new QMenu(this);
  QMenu *subMenu;
  menu->setTitle(tr("&View"));

  WbActionManager *actionManager = WbActionManager::instance();
  menu->addAction(actionManager->action(WbActionManager::FOLLOW_OBJECT));
  menu->addAction(actionManager->action(WbActionManager::FOLLOW_OBJECT_AND_ROTATE));
  menu->addAction(actionManager->action(WbActionManager::RESTORE_VIEWPOINT));
  menu->addAction(actionManager->action(WbActionManager::MOVE_VIEWPOINT_TO_OBJECT));

  QIcon icon = QIcon();
  icon.addFile("enabledIcons:front_view.png", QSize(), QIcon::Normal);
  icon.addFile("disabledIcons:front_view.png", QSize(), QIcon::Disabled);
  subMenu = menu->addMenu(icon, tr("Change View"));
  subMenu->addAction(actionManager->action(WbActionManager::FRONT_VIEW));
  subMenu->addAction(actionManager->action(WbActionManager::BACK_VIEW));
  subMenu->addAction(actionManager->action(WbActionManager::LEFT_VIEW));
  subMenu->addAction(actionManager->action(WbActionManager::RIGHT_VIEW));
  subMenu->addAction(actionManager->action(WbActionManager::TOP_VIEW));
  subMenu->addAction(actionManager->action(WbActionManager::BOTTOM_VIEW));
  menu->addSeparator();

  mToggleFullScreenAction = new QAction(this);
  mToggleFullScreenAction->setText(tr("&Fullscreen"));
  mToggleFullScreenAction->setStatusTip(tr("Show the simulation view in fullscreen mode."));
  mToggleFullScreenAction->setToolTip(mToggleFullScreenAction->statusTip());
  mToggleFullScreenAction->setShortcut(Qt::CTRL + Qt::SHIFT + Qt::Key_F);
  mToggleFullScreenAction->setCheckable(true);
  connect(mToggleFullScreenAction, &QAction::toggled, this, &WbMainWindow::toggleFullScreen);
  menu->addAction(mToggleFullScreenAction);

  mExitFullScreenAction = new QAction(this);
  mExitFullScreenAction->setCheckable(false);
  mExitFullScreenAction->setShortcut(Qt::Key_Escape);

  // add fullscreen actions to the current widget too
  // otherwise it will be disabled when hiding the menu
  addAction(mToggleFullScreenAction);
  addAction(mExitFullScreenAction);

#ifdef _WIN32
  menu->addSeparator();
  subMenu = menu->addMenu(tr("&Virtual Reality Headset"));
  if (!WbVirtualRealityHeadset::isSteamVRInstalled()) {
    subMenu->setStatusTip(tr("Please install SteamVR to use a virtual reality headset."));
    subMenu->setToolTip(subMenu->statusTip());
    subMenu->setEnabled(false);
  } else if (!WbVirtualRealityHeadset::isHeadsetConnected()) {
    subMenu->setStatusTip(tr("No virtual reality headset connected."));
    subMenu->setToolTip(subMenu->statusTip());
    subMenu->setEnabled(false);
  }

  subMenu->addAction(actionManager->action(WbActionManager::VIRTUAL_REALITY_HEADSET_ENABLE));
  subMenu->addSeparator();
  subMenu->addAction(actionManager->action(WbActionManager::VIRTUAL_REALITY_HEADSET_POSITION));
  subMenu->addAction(actionManager->action(WbActionManager::VIRTUAL_REALITY_HEADSET_ORIENTATION));
  subMenu->addSeparator();
  subMenu->addAction(actionManager->action(WbActionManager::VIRTUAL_REALITY_HEADSET_LEFT_EYE));
  subMenu->addAction(actionManager->action(WbActionManager::VIRTUAL_REALITY_HEADSET_RIGHT_EYE));
  subMenu->addAction(actionManager->action(WbActionManager::VIRTUAL_REALITY_HEADSET_NO_EYE));
  subMenu->addSeparator();
  subMenu->addAction(actionManager->action(WbActionManager::VIRTUAL_REALITY_HEADSET_ANTI_ALIASING));
#endif

  menu->addSeparator();
  menu->addAction(actionManager->action(WbActionManager::PERSPECTIVE_PROJECTION));
  menu->addAction(actionManager->action(WbActionManager::ORTHOGRAPHIC_PROJECTION));
  menu->addSeparator();
  menu->addAction(actionManager->action(WbActionManager::PLAIN_RENDERING));
  menu->addAction(actionManager->action(WbActionManager::WIREFRAME_RENDERING));
  menu->addSeparator();

  subMenu = menu->addMenu(tr("&Optional Rendering"));
  subMenu->addAction(actionManager->action(WbActionManager::COORDINATE_SYSTEM));
  subMenu->addAction(actionManager->action(WbActionManager::BOUNDING_OBJECT));
  subMenu->addAction(actionManager->action(WbActionManager::CONTACT_POINTS));
  subMenu->addAction(actionManager->action(WbActionManager::CONNECTOR_AXES));
  subMenu->addAction(actionManager->action(WbActionManager::JOINT_AXES));
  subMenu->addAction(actionManager->action(WbActionManager::RANGE_FINDER_FRUSTUMS));
  subMenu->addAction(actionManager->action(WbActionManager::LIDAR_RAYS_PATH));
  subMenu->addAction(actionManager->action(WbActionManager::LIDAR_POINT_CLOUD));
  subMenu->addAction(actionManager->action(WbActionManager::CAMERA_FRUSTUM));
  subMenu->addAction(actionManager->action(WbActionManager::DISTANCE_SENSOR_RAYS));
  subMenu->addAction(actionManager->action(WbActionManager::LIGHT_SENSOR_RAYS));
  subMenu->addAction(actionManager->action(WbActionManager::LIGHT_POSITIONS));
  subMenu->addAction(actionManager->action(WbActionManager::PEN_PAINTING_RAYS));
  subMenu->addAction(actionManager->action(WbActionManager::SKIN_SKELETON));
  subMenu->addAction(actionManager->action(WbActionManager::RADAR_FRUSTUMS));

  if (!WbSysInfo::environmentVariable("WEBOTS_DEBUG").isEmpty()) {
    subMenu->addSeparator();
    subMenu->addAction(actionManager->action(WbActionManager::BOUNDING_SPHERE));
    subMenu->addAction(actionManager->action(WbActionManager::PHYSICS_CLUSTERS));
  }

  // these optional renderings are selection dependent
  subMenu->addSeparator();
  subMenu->addAction(actionManager->action(WbActionManager::CENTER_OF_MASS));
  subMenu->addAction(actionManager->action(WbActionManager::CENTER_OF_BUOYANCY));
  subMenu->addAction(actionManager->action(WbActionManager::SUPPORT_POLYGON));

  menu->addSeparator();
  QAction *action = actionManager->action(WbActionManager::DISABLE_SELECTION);
  menu->addAction(action);

  action = actionManager->action(WbActionManager::LOCK_VIEWPOINT);
  menu->addAction(action);

  return menu;
}

QMenu *WbMainWindow::createSimulationMenu() {
  WbActionManager *manager = WbActionManager::instance();

  QMenu *menu = new QMenu(this);
  menu->setTitle(tr("&Simulation"));
  menu->addAction(manager->action(WbActionManager::PAUSE));
  menu->addAction(manager->action(WbActionManager::STEP));
  menu->addAction(manager->action(WbActionManager::REAL_TIME));
  menu->addAction(manager->action(WbActionManager::RUN));
  menu->addAction(manager->action(WbActionManager::FAST));
  return menu;
}

QMenu *WbMainWindow::createBuildMenu() {
  QMenu *menu = new QMenu(this);
  menu->setTitle(tr("&Build"));
  menu->addAction(mTextEditor->buildAction());
  menu->addAction(mTextEditor->cleanAction());
  menu->addAction(mTextEditor->makeJarAction());
  menu->addAction(mTextEditor->crossCompileAction());
  menu->addAction(mTextEditor->cleanCrossCompilationAction());
  return menu;
}

QMenu *WbMainWindow::createOverlayMenu() {
  mOverlayMenu = new QMenu(this);
  mOverlayMenu->setTitle(tr("&Overlays"));

  mRobotCameraMenu = mOverlayMenu->addMenu(tr("Ca&mera Devices"));
  mRobotRangeFinderMenu = mOverlayMenu->addMenu(tr("&RangeFinder Devices"));
  mRobotDisplayMenu = mOverlayMenu->addMenu(tr("&Display Devices"));

  WbContextMenuGenerator::setRobotCameraMenu(mRobotCameraMenu);
  WbContextMenuGenerator::setRobotRangeFinderMenu(mRobotRangeFinderMenu);
  WbContextMenuGenerator::setRobotDisplayMenu(mRobotDisplayMenu);

  mOverlayMenu->addAction(WbActionManager::instance()->action(WbActionManager::HIDE_ALL_CAMERA_OVERLAYS));
  mOverlayMenu->addAction(WbActionManager::instance()->action(WbActionManager::HIDE_ALL_RANGE_FINDER_OVERLAYS));
  mOverlayMenu->addAction(WbActionManager::instance()->action(WbActionManager::HIDE_ALL_DISPLAY_OVERLAYS));

  return mOverlayMenu;
}

void WbMainWindow::enableToolsWidgetItems(bool enabled) {
  WbActionManager::setActionEnabledSilently(mSimulationView->toggleView3DAction(), enabled);
  WbActionManager::setActionEnabledSilently(mSimulationView->toggleSceneTreeAction(), enabled);
  if (mTextEditor)
    WbActionManager::setActionEnabledSilently(mTextEditor->toggleViewAction(), enabled);
  WbActionManager::setActionEnabledSilently(mConsole->toggleViewAction(), enabled);
  WbActionManager::setActionEnabledSilently(mDocumentation->toggleViewAction(), enabled);
}

// we need this function because WbDockWidget and WbSimulationView don't have a common base class
void WbMainWindow::setWidgetMaximized(QWidget *widget, bool maximized) {
  WbDockWidget *dock = dynamic_cast<WbDockWidget *>(widget);
  WbSimulationView *view = dynamic_cast<WbSimulationView *>(widget);
  if (dock)
    dock->setMaximized(maximized);
  else
    view->setMaximized(maximized);
}

// maximize the sender widget
void WbMainWindow::maximizeDock() {
  mMinimizedDockState = saveState();
  mMaximizedWidget = static_cast<QWidget *>(sender());

  // close every other dock widget
  foreach (QWidget *dock, mDockWidgets) {
    WbDockWidget *dockWidget = dynamic_cast<WbDockWidget *>(dock);
    if (dock != mMaximizedWidget && (dockWidget == NULL || !dockWidget->isFloating()))
      dock->close();
  }
  enableToolsWidgetItems(false);
  setWidgetMaximized(mMaximizedWidget, true);
}

// minimize the maximized widget
void WbMainWindow::minimizeDock() {
  setWidgetMaximized(mMaximizedWidget, false);
  mMaximizedWidget = NULL;
  mSimulationView->show();
  restoreState(mMinimizedDockState);
  enableToolsWidgetItems(true);
}

QMenu *WbMainWindow::createToolsMenu() {
  QMenu *menu = new QMenu(this);
  menu->setTitle(tr("&Tools"));

  menu->addAction(mSimulationView->toggleView3DAction());
  menu->addAction(mSimulationView->toggleSceneTreeAction());
  if (mTextEditor)
    menu->addAction(mTextEditor->toggleViewAction());
  menu->addAction(mConsole->toggleViewAction());
  menu->addAction(mDocumentation->toggleViewAction());

  QAction *action = new QAction(this);
  action->setText(tr("Restore &Layout"));
  action->setShortcut(Qt::CTRL + Qt::Key_J);
  action->setStatusTip(tr("Restore windows factory layout."));
  action->setToolTip(action->statusTip());
  connect(action, &QAction::triggered, this, &WbMainWindow::restoreLayout);
  menu->addAction(action);

  menu->addSeparator();

  menu->addAction(WbActionManager::instance()->action(WbActionManager::CLEAR_CONSOLE));

  action = new QAction(this);
  action->setText(tr("Edit &Physics Plugin"));
  action->setStatusTip(tr("Open this simulation's physics plugin in the text editor."));
  action->setToolTip(action->statusTip());
  connect(action, &QAction::triggered, this, &WbMainWindow::editPhysicsPlugin);
  menu->addAction(action);

  menu->addSeparator();

  action = new QAction(this);
  action->setMenuRole(QAction::PreferencesRole);  // Mac: put the menu respecting the MacOS specifications
  action->setText(tr("&Preferences..."));
  action->setStatusTip(tr("Open the Preferences window."));
  connect(action, &QAction::triggered, this, &WbMainWindow::openPreferencesDialog);
  menu->addAction(action);

  action = new QAction(this);
  action->setMenuRole(QAction::ApplicationSpecificRole);  // Mac: put the menu respecting the MacOS specifications
  action->setText(tr("&Check for updates..."));
  action->setStatusTip(tr("Open the Webots update dialog."));
  connect(action, &QAction::triggered, this, &WbMainWindow::openWebotsUpdateDialogFromMenu);
  menu->addAction(action);

  return menu;
}

QMenu *WbMainWindow::createWizardsMenu() {
  QMenu *menu = new QMenu(this);
  menu->setTitle(tr("&Wizards"));

  QAction *action = new QAction(this);
  action->setText(tr("New Project &Directory..."));
  action->setStatusTip(tr("Create a new project directory."));
  connect(action, &QAction::triggered, this, &WbMainWindow::newProjectDirectory);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("New Robot &Controller..."));
  action->setStatusTip(tr("Create a new controller program."));
  connect(action, &QAction::triggered, this, &WbMainWindow::newRobotController);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("New &Physics Plugin..."));
  action->setStatusTip(tr("Create a new physics plugin."));
  connect(action, &QAction::triggered, this, &WbMainWindow::newPhysicsPlugin);
  menu->addAction(action);

  return menu;
}

QMenu *WbMainWindow::createHelpMenu() {
  QMenu *menu = new QMenu(this);
  menu->setTitle(tr("&Help"));

  QAction *action = new QAction(this);
  action->setMenuRole(QAction::AboutRole);  // Mac: put the menu respecting the MacOS specifications
  action->setText(tr("&About..."));
  action->setStatusTip(tr("Display information about Webots."));
  connect(action, &QAction::triggered, this, &WbMainWindow::showAboutBox);
  menu->addAction(action);

  if (WbGuidedTour::isAvailable()) {
    action = new QAction(this);
    action->setText(tr("Webots &Guided Tour..."));
    action->setStatusTip(tr("Start a guided tour demonstrating Webots capabilities."));
    connect(action, &QAction::triggered, this, &WbMainWindow::showGuidedTour);
    menu->addAction(action);
  }

  menu->addSeparator();

  action = new QAction(this);
  action->setText(tr("How do I &navigate in 3D?"));
  action->setStatusTip(tr("Show information about navigation in the 3D window."));
  connect(action, &QAction::triggered, this, &WbMainWindow::show3DViewingInfo);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("How do I &move an object?"));
  action->setStatusTip(tr("Show information about moving an object in the 3D window."));
  connect(action, &QAction::triggered, this, &WbMainWindow::show3DMovingInfo);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("How do I &apply a force or a torque to an object?"));
  action->setStatusTip(tr("Show information about applying a force or a torque to an object in the 3D window."));
  connect(action, &QAction::triggered, this, &WbMainWindow::show3DForceInfo);
  menu->addAction(action);

  menu->addSeparator();

  action = new QAction(this);
  action->setText(tr("&OpenGL Information..."));
  action->setStatusTip(tr("Show information about the current OpenGL hardware and driver."));
  connect(action, &QAction::triggered, this, &WbMainWindow::showOpenGlInfo);
  menu->addAction(action);

  menu->addSeparator();

  action = new QAction(this);
  action->setText(tr("&User Guide"));
  action->setStatusTip(tr("Open the Webots user guide online."));
  action->setShortcut(Qt::Key_F1);
  connect(action, &QAction::triggered, this, &WbMainWindow::showUserGuide);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("&Reference manual"));
  action->setStatusTip(tr("Open the Webots reference manual online."));
  action->setShortcut(Qt::Key_F2);
  connect(action, &QAction::triggered, this, &WbMainWindow::showReferenceManual);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("&Webots for automobiles"));
  action->setStatusTip(tr("Open the Webots for automobiles book online."));
  connect(action, &QAction::triggered, this, &WbMainWindow::showAutomobileDocumentation);
  menu->addAction(action);

  QMenu *offlineDocumentationMenu = new QMenu(tr("&Offline documentation"), this);

  action = new QAction(this);
  action->setText(tr("&User Guide"));
  action->setStatusTip(tr("Open the Webots user guide in the documentation tool."));
  connect(action, &QAction::triggered, this, &WbMainWindow::showOfflineUserGuide);
  offlineDocumentationMenu->addAction(action);

  action = new QAction(this);
  action->setText(tr("&Reference manual"));
  action->setStatusTip(tr("Open the Webots reference manual in the documentation tool."));
  connect(action, &QAction::triggered, this, &WbMainWindow::showOfflineReferenceManual);
  offlineDocumentationMenu->addAction(action);

  action = new QAction(this);
  action->setText(tr("&Webots for automobiles"));
  action->setStatusTip(tr("Open the Webots for automobiles book in the documentation tool."));
  connect(action, &QAction::triggered, this, &WbMainWindow::showOfflineAutomobileDocumentation);
  offlineDocumentationMenu->addAction(action);

  menu->addMenu(offlineDocumentationMenu);

  menu->addSeparator();

  action = new QAction(this);
  action->setText(tr("&Bug Report..."));
  action->setStatusTip(tr("Report a bug to Cyberbotics."));
  connect(action, &QAction::triggered, this, &WbMainWindow::openBugReport);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("&Support Ticket..."));
  action->setStatusTip(tr("Open a Support Ticket with Cyberbotics."));
  connect(action, &QAction::triggered, this, &WbMainWindow::openSupportTicket);
  menu->addAction(action);

  action = new QAction(this);
  action->setText(tr("Cyberbotics &Website..."));
  action->setStatusTip(tr("Open the Cyberbotics website."));
  connect(action, &QAction::triggered, this, &WbMainWindow::showCyberboticsWebsite);
  menu->addAction(action);

  return menu;
}

void WbMainWindow::createMenus() {
  mMenuBar = new QMenuBar(this);

  QMenu *menu = createFileMenu();
  mMenuBar->addAction(menu->menuAction());

  menu = createEditMenu();
  mMenuBar->addAction(menu->menuAction());

  menu = createViewMenu();
  mMenuBar->addAction(menu->menuAction());

  mSimulationMenu = createSimulationMenu();
  mMenuBar->addAction(mSimulationMenu->menuAction());
  mSimulationMenu->addAction(WbActionManager::instance()->action(WbActionManager::RUN));
  mSimulationMenu->addAction(WbActionManager::instance()->action(WbActionManager::FAST));

  menu = createBuildMenu();
  mMenuBar->addAction(menu->menuAction());

  menu = createOverlayMenu();
  mMenuBar->addAction(menu->menuAction());

  menu = createToolsMenu();
  mMenuBar->addAction(menu->menuAction());

  menu = createWizardsMenu();
  mMenuBar->addAction(menu->menuAction());

  menu = createHelpMenu();
  mMenuBar->addAction(menu->menuAction());

  setMenuBar(mMenuBar);
}

void WbMainWindow::restorePreferredGeometry(bool minimizedOnStart) {
  WbPreferences *prefs = WbPreferences::instance();
#ifdef __linux__
  if (minimizedOnStart && prefs->value("MainWindow/maximized", false).toBool())
    return;
#endif

  if (prefs->value("MainWindow/maximized", false).toBool()) {
    showMaximized();
    return;
  }

  QRect desktopRect = QApplication::desktop()->availableGeometry();
  QRect preferedRect(prefs->value("MainWindow/position", QPoint(0, 0)).toPoint(),
                     prefs->value("MainWindow/size", QSize(0, 0)).toSize());

  if (preferedRect == QRect(0, 0, 0, 0) || !desktopRect.contains(preferedRect)) {
    preferedRect.setTopLeft(desktopRect.topLeft());
    preferedRect.setSize(desktopRect.size() - frameGeometry().size() + geometry().size());
  }

  resize(preferedRect.size());
  move(preferedRect.topLeft());
}

void WbMainWindow::writePreferences() const {
  WbPreferences *prefs = WbPreferences::instance();
  prefs->setValue("MainWindow/maximized", isMaximized());
  prefs->setValue("MainWindow/size", size());
  prefs->setValue("MainWindow/position", pos());
  prefs->sync();
}

void WbMainWindow::simulationQuit(int exitStatus) {
  mExitStatus = exitStatus;
  emit close();
}

bool WbMainWindow::event(QEvent *event) {
  if (event->type() == QEvent::ScreenChangeInternal)
    mSimulationView->internalScreenChangedCallback();
  return QMainWindow::event(event);
}

void WbMainWindow::closeEvent(QCloseEvent *event) {
  if (!proposeToSaveWorld()) {
    event->ignore();
    return;
  }

  logActiveControllersTermination();

  // disconnect from file changed signal before saving the perspective
  // if the perspective file is open, a segmentation fault is generated
  // due to double free of the reload QMessageBox on Linux
  if (mTextEditor)
    mTextEditor->ignoreFileChangedEvent();

  // perspective need to be saved before deleting the
  // simulationView (and therefore the sceneTree), otherwise
  // the perspective of the node editor is not correctly saved
  if (WbApplication::instance())
    savePerspective(false, true);

  // the scene tree qt model should be cleaned first
  // otherwise some signals can be fired after the
  // QCoreApplication::exit() call
  // A better fix would be to move this code in a higher
  // level class deleting the mainwindow before deleting
  // WbApplication (and so WbWorld)
  mSimulationView->view3D()->logWrenStatistics();
  mSimulationView->view3D()->cleanupOptionalRendering();
  mSimulationView->view3D()->cleanupFullScreenOverlay();
  mSimulationView->cleanup();

  // really close
  if (WbApplication::instance()) {
    if (mTextEditor)
      mTextEditor->closeAllBuffers();

    writePreferences();
    delete WbApplication::instance();
  }

  WbRenderingDeviceWindowFactory::deleteInstance();
  WbPerformanceLog::deleteInstance();

  event->accept();
  QCoreApplication::exit(mExitStatus);
}

void WbMainWindow::restoreLayout() {
  mSimulationView->show();
  restoreState(*mFactoryLayout);
  mMaximizedWidget = NULL;
  foreach (QWidget *dock, mDockWidgets)
    setWidgetMaximized(dock, false);
  mSimulationView->restoreFactoryLayout();
  enableToolsWidgetItems(true);
}

void WbMainWindow::editPhysicsPlugin() {
  WbSimulationWorld *world = WbSimulationWorld::instance();

  const QString &pluginName = world->worldInfo()->physics();
  if (pluginName.isEmpty()) {
    if (WbMessageBox::question(
          tr("This simulation does not currently use a physics plugin.") + "\n" + tr("Would you like to create one?"), this,
          tr("Question")) == QMessageBox::Ok)
      newPhysicsPlugin();
    return;
  }

  QString filename = WbPhysicsPlugin::findSourceFileForPlugin(pluginName);
  if (filename.isEmpty()) {
    WbMessageBox::info(tr("Could not find the source file of the '%1' physics plugin.").arg(pluginName), this);
    return;
  }

  openFileInTextEditor(filename);
}

void WbMainWindow::savePerspective(bool reloading, bool saveToFile) {
  bool savingIsAllowed = true;

  if (!qgetenv("WEBOTS_DISABLE_SAVE_PERSPECTIVE_ON_CLOSE").isEmpty())
    savingIsAllowed = false;

  if (!savingIsAllowed && saveToFile)
    return;

  const WbWorld *world = WbWorld::instance();
  if (!world || world->isUnnamed() || WbFileUtil::isLocatedInInstallationDirectory(world->fileName()))
    return;

  WbPerspective *perspective = world->perspective();
  if (reloading) {
    // load previous settings
    // for example the perspectives of devices that have been deleted since the
    // last world save have to be loaded from the existing perspective file
    perspective->load(true);
    perspective->clearEnabledOptionalRenderings();
    perspective->clearRenderingDevicesPerspectiveList();
  }
  perspective->setMainWindowState(saveState());
  perspective->setMinimizedState(mMinimizedDockState);
  const int id = mDockWidgets.indexOf(mMaximizedWidget);
  perspective->setMaximizedDockId(id);
  perspective->setCentralWidgetVisible(mSimulationView->isVisible());
  perspective->setSimulationViewState(mSimulationView->saveState());
  if (mTextEditor) {
    perspective->setFilesList(mTextEditor->openFiles());
    perspective->setSelectedTab(mTextEditor->selectedTab());
  }
  if (mDocumentation->isVisible()) {
    perspective->setDocumentationBook(mDocumentation->book());
    perspective->setDocumentationPage(mDocumentation->page());
  } else {
    perspective->setDocumentationBook("");
    perspective->setDocumentationPage("");
  }
  perspective->setOrthographicViewHeight(world->orthographicViewHeight());

  QStringList robotWindowNodeNames;
  foreach (QWidget *dock, mDockWidgets) {
    WbRobotWindow *w = dynamic_cast<WbRobotWindow *>(dock);
    if (!w || !(w->isVisible()))
      continue;
    robotWindowNodeNames << w->robot()->computeUniqueName();
  }
  perspective->setRobotWindowNodeNames(robotWindowNodeNames);

  QStringList centerOfMassEnabledNodeNames, centerOfBuoyancyEnabledNodeNames, supportPolygonEnabledNodeNames;
  world->retrieveNodeNamesWithOptionalRendering(centerOfMassEnabledNodeNames, centerOfBuoyancyEnabledNodeNames,
                                                supportPolygonEnabledNodeNames);
  perspective->setEnabledOptionalRendering(centerOfMassEnabledNodeNames, centerOfBuoyancyEnabledNodeNames,
                                           supportPolygonEnabledNodeNames);

  // save rendering devices perspective
  const QList<WbRenderingDevice *> renderingDevices = WbRenderingDevice::renderingDevices();
  foreach (const WbRenderingDevice *device, renderingDevices) {
    if (device->overlay() != NULL)
      perspective->setRenderingDevicePerspective(device->computeShortUniqueName(), device->perspective());
  }

  // save rendering devices perspective of external window
  WbRenderingDeviceWindowFactory::instance()->saveWindowsPerspective(*perspective);

  // save our new perspective in the file
  if (savingIsAllowed && saveToFile)
    perspective->save();
}

void WbMainWindow::restorePerspective(bool reloading, bool firstLoad, bool loadingFromMemory) {
  WbWorld *world = WbWorld::instance();
  WbPerspective *perspective = world->perspective();
  bool meansOfLoading = false;
  if (loadingFromMemory)
    meansOfLoading = true;
  else {
    meansOfLoading = world->reloadPerspective();
    perspective = world->perspective();
  }

  if (meansOfLoading) {
    if (!perspective->enabledRobotWindowNodeNames().isEmpty()) {
      const QList<WbRobot *> &robots = world->robots();
      foreach (WbRobot *robot, robots) {
        if (perspective->enabledRobotWindowNodeNames().contains(robot->computeUniqueName()))
          showHtmlRobotWindow(robot);
      }
    }
    restoreState(perspective->mainWindowState());
    mMinimizedDockState = perspective->minimizedState();
    const int id = perspective->maximizedDockId();
    mMaximizedWidget = (id >= 0 && id < mDockWidgets.size()) ? mDockWidgets.at(id) : NULL;
    enableToolsWidgetItems(mMaximizedWidget == NULL);
    if (!reloading) {
      mSimulationView->setVisible(perspective->centralWidgetVisible());
      mSimulationView->restoreState(perspective->simulationViewState(), firstLoad);
      if (mTextEditor)
        mTextEditor->openFiles(perspective->filesList(), perspective->selectedTab());
      if (!perspective->documentationBook().isEmpty())
        mDocumentation->open(perspective->documentationBook(), perspective->documentationPage());
      else {  // the documentation dock is not specified, reset it to its default location and contents
        mDocumentation->open("guide", "index", false);
        removeDockWidget(mDocumentation);
        addDockWidget(Qt::LeftDockWidgetArea, mDocumentation, Qt::Horizontal);
        mDocumentation->hide();
      }
    }
    // update icons
    foreach (QWidget *dock, mDockWidgets)
      setWidgetMaximized(dock, dock == mMaximizedWidget);
  } else if (firstLoad)
    // set default simulation view perspective
    mSimulationView->restoreFactoryLayout();

  const double ovh = perspective->orthographicViewHeight();
  world->setOrthographicViewHeight(ovh);

  mSimulationView->view3D()->restoreOptionalRendering(perspective->enabledCenterOfMassNodeNames(),
                                                      perspective->enabledCenterOfBuoyancyNodeNames(),
                                                      perspective->enabledSupportPolygonNodeNames());

  if (firstLoad)  // for the first load we can't restore the rendering devices perspective now because the size of the wren
                  // window has not be set yet
    connect(mSimulationView->view3D(), &WbView3D::resized, this, &WbMainWindow::restoreRenderingDevicesPerspective);
  else
    restoreRenderingDevicesPerspective();

  // Refreshing
  mSimulationView->repaintView3D();
}

void WbMainWindow::restoreRenderingDevicesPerspective() {
  const WbPerspective *perspective = WbWorld::instance()->perspective();
  const QList<WbRenderingDevice *> devices = WbRenderingDevice::renderingDevices();
  for (int i = 0; i < devices.size(); ++i) {
    WbRenderingDevice *device = devices[i];
    QStringList devicePerspective = perspective->renderingDevicePerspective(device->computeShortUniqueName());
    if (!devicePerspective.isEmpty())
      device->restorePerspective(devicePerspective);
  }
  disconnect(mSimulationView->view3D(), &WbView3D::resized, this, &WbMainWindow::restoreRenderingDevicesPerspective);
}

bool WbMainWindow::loadDifferentWorld(const QString &fileName) {
  return loadWorld(fileName, false);
}

bool WbMainWindow::proposeToSaveWorld(bool reloading) {
  const WbWorld *world = WbWorld::instance();
  if (world != NULL && world->needSaving() && !WbProject::current()->isReadOnly() && WbMessageBox::enabled()) {
    WbSaveWarningDialog *dialog = new WbSaveWarningDialog(world->fileName(), world->isModifiedFromSceneTree(), reloading, this);
    int result = dialog->exec();
    if (result == QMessageBox::Cancel)
      return false;
    if (result == QMessageBox::Save)
      saveWorld();
  }
  return true;
}

bool WbMainWindow::loadWorld(const QString &fileName, bool reloading) {
  if (!proposeToSaveWorld(reloading))
    return true;
  mSimulationView->cancelSupervisorMovieRecording();
  logActiveControllersTermination();
  return WbApplication::instance()->loadWorld(fileName, reloading);
}

void WbMainWindow::updateBeforeWorldLoading(bool reloading) {
  WbLog::instance()->setPopUpPostponed(true);
  savePerspective(reloading, true);
  foreach (QWidget *dock, mDockWidgets) {
    WbRobotWindow *w = dynamic_cast<WbRobotWindow *>(dock);
    if (!w)
      continue;
    w->close();
    mDockWidgets.removeOne(w);
    delete w;
  }
  mSimulationView->view3D()->logWrenStatistics();
  if (!reloading && WbClipboard::instance()->type() == WB_SF_NODE)
    WbClipboard::instance()->replaceAllExternalDefNodesInString();
  mSimulationView->prepareWorldLoading();
}

void WbMainWindow::updateAfterWorldLoading(bool reloading, bool firstLoad) {
  const WbWorld *world = WbWorld::instance();
  if (!world->isUnnamed())
    mRecentFiles->makeRecent(world->fileName());

  mSimulationView->setWorld(WbSimulationWorld::instance());

  // update 'view' menu
  const WbPerspective *perspective = world->perspective();
  WbActionManager::instance()->action(WbActionManager::DISABLE_SELECTION)->setChecked(perspective->isSelectionDisabled());
  WbActionManager::instance()->action(WbActionManager::LOCK_VIEWPOINT)->setChecked(perspective->isViewpointLocked());

#ifdef _WIN32
  QWebSettings::globalSettings()->clearMemoryCaches();
#else
  QWebEngineProfile::defaultProfile()->clearHttpCache();
#endif
  WbRenderingDeviceWindowFactory::reset();
  restorePerspective(reloading, firstLoad, false);

  emit splashScreenCloseRequested();

  foreach (WbRobot *const robot, WbControlledWorld::instance()->robots())
    handleNewRobotInsertion(robot);

  connect(world, &WbWorld::robotAdded, this, &WbMainWindow::handleNewRobotInsertion);
  connect(world, &WbWorld::modificationChanged, this, &WbMainWindow::updateWindowTitle);

  updateGui();

  if (!reloading)
    WbActionManager::instance()->resetApplicationActionsState();
  // reset focus widget used to identify the actions target widget
  WbActionManager::instance()->setFocusObject(mSimulationView->view3D());

  WbLog::setPopUpPostponed(false);
  WbLog::showPostponedPopUpMessages();
  connect(WbProject::current(), &WbProject::pathChanged, this, &WbMainWindow::updateProjectPath);
}

void WbMainWindow::handleNewRobotInsertion(WbRobot *robot) {
  if (robot->isShowWindowFieldEnabled()) {
    if (robot->windowFile().isEmpty())
      robot->showWindow();
    else
      showHtmlRobotWindow(robot);
  }
}

void WbMainWindow::newWorld() {
  loadWorld(WbStandardPaths::emptyProjectPath() + "worlds/" + WbProject::newWorldFileName());
}

void WbMainWindow::openWorld() {
  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();

  QString fileName = QFileDialog::getOpenFileName(this, tr("Open World File"), WbProject::current()->worldsPath(),
                                                  tr("World Files (*.wbt *.WBT)"));
  if (!fileName.isEmpty())
    loadWorld(fileName);

  simulationState->resumeSimulation();
}

void WbMainWindow::openSampleWorld() {
  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();

  WbOpenSampleWorldDialog dialog(this);
  if (dialog.exec())
    loadWorld(dialog.selectedWorld());

  simulationState->resumeSimulation();
}

bool WbMainWindow::runSimulationHasRunWarningMessage() {
  const QString message(tr("The simulation has run!") + "\n" +
                        tr("Saving the .wbt file will store the current world state: the objects position and rotation and "
                           "other fields may differ from the original file!") +
                        "\n" + tr("Do you want to save this modified world?"));
  return WbMessageBox::question(message, this, tr("Question"), QMessageBox::Cancel, QMessageBox::Cancel | QMessageBox::Save) ==
         QMessageBox::Save;
}

void WbMainWindow::saveWorld() {
  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();

  WbSimulationWorld *world = WbSimulationWorld::instance();
  if (WbSimulationState::instance()->hasStarted() && !runSimulationHasRunWarningMessage()) {
    simulationState->resumeSimulation();
    return;
  }

  QString worldFilename = world->fileName();
  QString previousWorldFileName = worldFilename;
  bool locationValidated = WbProjectRelocationDialog::validateLocation(this, worldFilename);
  if (!locationValidated) {
    simulationState->resumeSimulation();
    return;
  }

  if ((previousWorldFileName == worldFilename) && (world->isUnnamed() || world->simulationHasRunAfterSave())) {
    saveWorldAs(true);
    return;
  }

  mSimulationView->applyChanges();
  world->save();
  savePerspective(false, true);
  updateWindowTitle();
  simulationState->resumeSimulation();
}

void WbMainWindow::saveWorldAs(bool skipSimulationHasRunWarning) {
  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();

  mSimulationView->applyChanges();

  if (!skipSimulationHasRunWarning && WbSimulationState::instance()->hasStarted() && !runSimulationHasRunWarningMessage()) {
    simulationState->resumeSimulation();
    return;
  }

  WbWorld *world = WbWorld::instance();

  QString fileName = QFileDialog::getSaveFileName(
    this, tr("Save World File"), WbProject::computeBestPathForSaveAs(world->fileName()), tr("World Files (*.wbt *.WBT)"));

  if (fileName.isEmpty()) {
    simulationState->resumeSimulation();
    return;
  }

  if (!fileName.endsWith(".wbt", Qt::CaseInsensitive))
    fileName.append(".wbt");

  if (WbProjectRelocationDialog::validateLocation(this, fileName)) {
    mRecentFiles->makeRecent(fileName);
    world->saveAs(fileName);
    savePerspective(false, true);
    updateWindowTitle();
  }

  simulationState->resumeSimulation();
}

void WbMainWindow::reloadWorld() {
  toggleAnimationAction(false);
  if (!WbWorld::instance() || WbWorld::instance()->isUnnamed())
    newWorld();
  else
    loadWorld(WbWorld::instance()->fileName(), true);
}

void WbMainWindow::resetWorld() {
  toggleAnimationAction(false);
  if (!WbWorld::instance())
    newWorld();
  else {
    mSimulationView->cancelSupervisorMovieRecording();
    WbWorld::instance()->reset();
  }
  mSimulationView->view3D()->renderLater();
}

void WbMainWindow::importVrml() {
  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();

  QString worldFilename = WbSimulationWorld::instance()->fileName();
  if (!WbProjectRelocationDialog::validateLocation(this, worldFilename, true)) {
    simulationState->resumeSimulation();
    return;
  }

  // first time: suggest import in user's home directory
  static QString suggestedPath = QDir::homePath();

  QString fileName = QFileDialog::getOpenFileName(this, tr("Import VRML97"), suggestedPath, tr("VRML97 Files (*.wrl *.WRL)"));
  if (!fileName.isEmpty()) {
    // next time: remember last import directory
    suggestedPath = QFileInfo(fileName).path();

    if (WbNodeOperations::instance()->importVrml(fileName) == WbNodeOperations::SUCCESS)
      WbWorld::instance()->setModified();

    mSimulationView->view3D()->refresh();
  }

  simulationState->resumeSimulation();
}

void WbMainWindow::exportVrml() {
  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();
  WbWorld *world = WbWorld::instance();

  QString path = WbPreferences::instance()->value("Directories/vrml").toString();
  QString fileName =
    QFileDialog::getSaveFileName(this, tr("Export World as VRML97"),
                                 WbProject::computeBestPathForSaveAs(path + QFileInfo(world->fileName()).baseName() + ".wrl"),
                                 tr("VRML97 Files (*.wrl *.WRL)"));
  if (!fileName.isEmpty()) {
    WbPreferences::instance()->setValue("Directories/vrml", QFileInfo(fileName).absolutePath() + "/");
    world->exportAsVrml(fileName);
  }

  simulationState->resumeSimulation();
}

void WbMainWindow::exportHtml() {
  WbSimulationState::Mode currentMode = WbSimulationState::instance()->mode();
  WbSimulationState::instance()->setMode(WbSimulationState::PAUSE);
  WbWorld *world = WbWorld::instance();

  WbHtmlExportDialog parametersDialog(tr("Export HTML5 Model"), world->fileName(), this);
  bool accept = parametersDialog.exec();
  if (accept) {
    const QString &fileName = parametersDialog.fileName();
    assert(!fileName.isEmpty());
    world->exportAsHtml(fileName, false);
    WbPreferences::instance()->setValue("Directories/www", QFileInfo(fileName).absolutePath() + "/");
    openUrl(fileName,
            tr("The HTML5 model has been created:\n%1\n\nDo you want to view it locally now?\n\nNote: HTML5 models can not be "
               "viewed locally on Google Chrome.")
              .arg(fileName),
            tr("Export HTML5 Model"));
  }

  WbSimulationState::instance()->setMode(currentMode);
}

void WbMainWindow::showAboutBox() {
  WbAboutBox *box = new WbAboutBox(this);
  box->exec();
}

void WbMainWindow::showGuidedTour() {
  if (!WbGuidedTour::isAvailable())
    return;
  WbGuidedTour *tour = WbGuidedTour::instance(this);
  tour->show();
  tour->raise();
  connect(tour, &WbGuidedTour::worldLoaded, this, &WbMainWindow::loadDifferentWorld);
}

void WbMainWindow::setView3DSize(const QSize &size) {
  mSimulationView->enableView3DFixedSize(size);
}

void WbMainWindow::show3DViewingInfo() {
  const QString info =
    tr("<strong>Rotate:</strong><br/>"
       "To rotate the camera around the x and y axis, you have to set the mouse pointer in the 3D scene, press the left mouse "
       "button and drag the mouse:<br/>"
       "- if you clicked on an object, the rotation will be centered around the picked point on this object.<br/>"
       "- if you clicked outside of any object, the rotation will be centered around the position of the camera.<br/><br/>"
       "<strong>Translate:</strong><br/>"
       "To translate the camera in the x and y directions, you have to set the mouse pointer in the 3D scene, press the right "
       "mouse button and drag the mouse.<br/><br/>"
       "<strong>Zoom / Tilt:</strong><br/>"
       "Set the mouse pointer in the 3D scene, then:\n"
       "- if you press both left and right mouse buttons (or the middle button) and drag the mouse vertically, the camera will "
       "zoom in or out.<br/>"
       "- if you press both left and right mouse buttons (or the middle button) and drag the mouse horizontally, the camera "
       "will rotate around its z axis (tilt movement).<br/>"
       "- if you use the wheel of the mouse, the camera will zoom in or out.");
  WbMessageBox::info(info, this, tr("How do I navigate in 3D?"));
}

void WbMainWindow::show3DMovingInfo() {
  const QString info =
    tr("In order to move an object: first <strong>select the object</strong> with a left mouse button click.<br/><br/>"
       "Then <strong>click and drag the arrow-shaped handles</strong> to translate or "
       "rotate the object along the corresponding axis.<br/><br/>"
       "Alternatively, you can hold the shift key and use the mouse:<br/>"
       "<em>Horizontal translation:</em><br/>"
       "Use the left mouse button while the shift key is down to drag an object parallel to the ground.<br/>"
       "<em>Vertical rotation:</em><br/>"
       "Use the right mouse button while the shift key is down to rotate an object around the world's vertical axis.<br/>"
       "<em>Lift:</em><br/>"
       "Press both left and right mouse buttons, press the middle mouse button, or roll the mouse wheel "
       "while the shift key is down to raise or lower the selected object.");
  WbMessageBox::info(info, this, tr("How do I move an object?"));
}

void WbMainWindow::show3DForceInfo() {
  static const QString infoLinux(
    tr("<strong>Force:</strong><br/> Place the mouse pointer where the force will apply and hold down the Alt key"
       ", the Control key (Ctrl)"
       " and the left mouse button together while dragging the mouse.<br/><br/> <strong>Torque:</strong><br/>"
       "Place the mouse pointer on the object and hold down the Alt key"
       ", the Control key (Ctrl)"
       " and the right mouse button together while dragging the mouse."));

  static const QString infoWindows(
    tr("<strong>Force:</strong><br/> Place the mouse pointer where the force will apply and hold down the Alt key"
       " and the left mouse button together while dragging the mouse.<br/><br/> <strong>Torque:</strong><br/>"
       "Place the mouse pointer on the object and hold down the Alt key"
       " and the right mouse button together while dragging the mouse."));

  static const QString infoMac(
    tr("<strong>Force:</strong><br/> Place the mouse pointer where the force will apply and hold down the Alt key"
       " and the left mouse button together while dragging the mouse.<br/><br/> <strong>Torque:</strong><br/>"
       "Place the mouse pointer on the object and hold down the Alt key"
       " and the right mouse button together while dragging the mouse."
       "<br/><br/>If you have a one-button mouse, hold down also the Control key (Ctrl) to emulate the right mouse button."));

  QString info;

  switch (WbSysInfo::platform()) {
    case WbSysInfo::LINUX_PLATFORM:
      info = infoLinux;
      break;
    case WbSysInfo::MACOS_PLATFORM:
      info = infoMac;
      break;
    case WbSysInfo::WIN32_PLATFORM:
      info = infoWindows;
    default:
      assert(false);
  }
  WbMessageBox::info(info, this, tr("How do I apply a force or a torque to an object?"));
}

void WbMainWindow::showOpenGlInfo() {
  QOpenGLFunctions_3_3_Core gl;
  gl.initializeOpenGLFunctions();
  QString info;
  info += tr("Host name: ") + QHostInfo::localHostName() + "\n";
  info += tr("System: ") + WbSysInfo::sysInfo() + "\n";
  info += tr("OpenGL vendor: ") + (const char *)gl.glGetString(GL_VENDOR) + "\n";
  info += tr("OpenGL renderer: ") + (const char *)gl.glGetString(GL_RENDERER) + "\n";
  info += tr("OpenGL version: ") + (const char *)gl.glGetString(GL_VERSION) + "\n";
  WbMessageBox::info(info, this, tr("OpenGL information"));
}

void WbMainWindow::showDocument(const QString &url) {
  bool ret;
  if (url.startsWith("http") || url.startsWith("www"))
    ret = QDesktopServices::openUrl(QUrl(url));
  else {
#ifdef __linux__  // on linux, the '/lib' directory need to be removed from the LD_LIBRARY_PATH,
                  // otherwise their is some libraries conflicts when trying to open pdf with Evince
    QString WEBOTS_HOME(QDir::toNativeSeparators(WbStandardPaths::webotsHomePath()));
    QByteArray ldLibraryPathBackup = qgetenv("LD_LIBRARY_PATH");
    QByteArray newLdLibraryPath = ldLibraryPathBackup;
    newLdLibraryPath.replace(WEBOTS_HOME + "lib/", "");
    newLdLibraryPath.replace(WEBOTS_HOME + "lib", "");
    qputenv("LD_LIBRARY_PATH", newLdLibraryPath);
#endif
    QUrl u("file:///" + url);
    ret = QDesktopServices::openUrl(u);
#ifdef __linux__
    qputenv("LD_LIBRARY_PATH", ldLibraryPathBackup);
#endif
  }
  if (!ret)
    WbMessageBox::warning(tr("Cannot open the document: '%1'.").arg(url), this, tr("Internal error"));
}

void WbMainWindow::showOnlineBook(const QString &book) {
  QString versionString = WbApplicationInfo::version().toString();
  versionString.replace(" revision ", "-rev");
  const QString url = WbStandardPaths::cyberboticsUrl() + "/doc/" + book + "/index?version=" + versionString;
  showDocument(url);
}

void WbMainWindow::showUserGuide() {
  showOnlineBook("guide");
}

void WbMainWindow::showReferenceManual() {
  showOnlineBook("reference");
}

void WbMainWindow::showAutomobileDocumentation() {
  showOnlineBook("automobile");
}

void WbMainWindow::showOfflineUserGuide() {
  mDocumentation->open("guide");
}

void WbMainWindow::showOfflineReferenceManual() {
  mDocumentation->open("reference");
}

void WbMainWindow::showOfflineAutomobileDocumentation() {
  mDocumentation->open("automobile");
}

void WbMainWindow::openBugReport() {
  openSupport("bug");
}

void WbMainWindow::openSupportTicket() {
  openSupport("ticket");
}

void WbMainWindow::openSupport(const QString &type) {
  QString url = WbStandardPaths::cyberboticsUrl() + '/';
  if (type == "bug")
    url.append("bug_report.php?");
  else
    url.append("support_ticket.php?");
  url.append("&os=");
  url.append(WbSysInfo::sysInfo());
  url.append("&graphics=");
  QOpenGLFunctions_3_3_Core gl;
  gl.initializeOpenGLFunctions();
  url.append((const char *)gl.glGetString(GL_VENDOR));
  url.append(" - ");
  url.append((const char *)gl.glGetString(GL_RENDERER));
  url.append(" - ");
  url.append((const char *)gl.glGetString(GL_VERSION));
  url.append("&version=");
  url.append(WbApplicationInfo::version().toString());
  url.append("&type=");
  url.append(type);
  showDocument(url);
}

void WbMainWindow::showCyberboticsWebsite() {
  showDocument(WbStandardPaths::cyberboticsUrl());
}

void WbMainWindow::newProjectDirectory() {
  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();

  WbNewProjectWizard wizard(this);
  wizard.exec();

  simulationState->resumeSimulation();

  if (wizard.isValidProject())
    loadWorld(wizard.newWorldFile());
}

void WbMainWindow::newRobotController() {
  QString controllersPath = WbProject::current()->path() + "controllers";
  if (!WbProjectRelocationDialog::validateLocation(this, controllersPath))
    return;

  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();

  WbNewControllerWizard wizard(this);
  wizard.exec();
  if (wizard.needsEdit())
    openFileInTextEditor(wizard.controllerName());

  simulationState->resumeSimulation();
}

void WbMainWindow::newPhysicsPlugin() {
  QString pluginsPhysicsPath = WbProject::current()->path() + "plugins/physics";
  if (!WbProjectRelocationDialog::validateLocation(this, pluginsPhysicsPath))
    return;

  WbSimulationState *simulationState = WbSimulationState::instance();
  simulationState->pauseSimulation();

  WbNewPhysicsPluginWizard wizard(this);
  wizard.exec();
  if (wizard.needsEdit())
    openFileInTextEditor(wizard.physicsPluginName());

  simulationState->resumeSimulation();
}

void WbMainWindow::openPreferencesDialog() {
  WbPreferencesDialog dialog(this);
  connect(&dialog, &WbPreferencesDialog::restartRequested, this, &WbMainWindow::restartRequested);
  dialog.exec();
}

void WbMainWindow::openWebotsUpdateDialogFromStartup() {
  WbWebotsUpdateManager *webotsUpdateManager = WbWebotsUpdateManager::instance();

  if (!webotsUpdateManager->isTargetVersionAvailable() || webotsUpdateManager->error().size() > 0)
    return;

  if (WbApplicationInfo::version() < webotsUpdateManager->targetVersion()) {
    WbWebotsUpdateDialog dialog(true, this);
    dialog.exec();
  }
}

void WbMainWindow::openWebotsUpdateDialogFromMenu() {
  WbWebotsUpdateDialog dialog(false, this);
  dialog.exec();
}

void WbMainWindow::updateWindowTitle() {
  QString webotsNameAndVersion("Webots " + WbApplicationInfo::version().toString());
  QString title;

  if (WbWorld::instance()) {
    title = QDir::toNativeSeparators(WbWorld::instance()->fileName());
    QString projectName = WbProject::projectNameFromWorldFile(WbWorld::instance()->fileName());
    if (!projectName.isEmpty())
      title += " (" + projectName + ") - ";
    else
      title += " (No Project) - ";

    title += webotsNameAndVersion;
  } else
    title = webotsNameAndVersion;

  setWindowTitle(title);
}

void WbMainWindow::updateGui() {
  updateWindowTitle();
  updateOverlayMenu();
}

void WbMainWindow::simulationEnabledChanged(bool e) {
  mSimulationMenu->setEnabled(e);
}

void WbMainWindow::showStatusBarMessage(WbLog::Level level, const QString &message) {
  if (level == WbLog::STATUS)
    statusBar()->showMessage(message);
}

void WbMainWindow::editRobotController() {
  WbRobot *robot = mSimulationView->selectedRobot();
  if (!robot)
    return;

  QString controllerName = robot->controllerName();
  if (controllerName.isEmpty()) {
    WbMessageBox::info(tr("Could not find the controller name of the '%1' robot.").arg(robot->name().toUpper()), this);
    return;
  }

  QString filename = WbProject::current()->controllerPathFromDir(robot->controllerDir());
  if (filename.isEmpty()) {
    WbMessageBox::info(tr("Could not find the source file of the '%1' controller.").arg(controllerName), this);
    return;
  }

  openFileInTextEditor(filename);
}

void WbMainWindow::showRobotWindow() {
  WbRobot *robot = mSimulationView->selectedRobot();
  if (robot) {
    if (robot->windowFile().isEmpty())
      robot->showWindow();  // not a HTML robot window
    else
      showHtmlRobotWindow(robot);  // show HTML robot window as a dock
  }
}

void WbMainWindow::showHtmlRobotWindow(WbRobot *robot) {  // shows the HTML robot window
  foreach (QWidget *dock, mDockWidgets) {
    WbRobotWindow *w = dynamic_cast<WbRobotWindow *>(dock);
    if (w && w->robot() == robot) {
      w->show();
      return;
    }
  }
  WbRobotWindow *robotWindow = new WbRobotWindow(robot, this);
  connect(robot, &WbBaseNode::isBeingDestroyed, this, &WbMainWindow::removeHtmlRobotWindow);
  addDockWidget(Qt::LeftDockWidgetArea, robotWindow, Qt::Horizontal);
  addDock(robotWindow);
  robotWindow->show();
}

void WbMainWindow::removeHtmlRobotWindow(WbNode *node) {
  for (int i = 0; i < mDockWidgets.size(); ++i) {
    WbRobotWindow *w = dynamic_cast<WbRobotWindow *>(mDockWidgets[i]);
    if (w && w->robot() == node) {
      mDockWidgets.removeAt(i);
      delete w;
      return;
    }
  }
}

static bool isRobotNode(WbBaseNode *node) {
  return dynamic_cast<WbRobot *>(node);
}

void WbMainWindow::updateOverlayMenu() {
  QList<QAction *> actions;
  WbRobot *selectedRobot = mSimulationView->selectedRobot();

  // remove camera and display item list
  actions = mRobotCameraMenu->actions();
  for (int i = 0; i < actions.size(); ++i) {
    mRobotCameraMenu->removeAction(actions[i]);
    delete actions[i];
  }
  actions = mRobotRangeFinderMenu->actions();
  for (int i = 0; i < actions.size(); ++i) {
    mRobotRangeFinderMenu->removeAction(actions[i]);
    delete actions[i];
  }
  actions = mRobotDisplayMenu->actions();
  for (int i = 0; i < actions.size(); ++i) {
    mRobotDisplayMenu->removeAction(actions[i]);
    delete actions[i];
  }

  // add current robot and descendant robot specific camera and display items
  if (selectedRobot) {
    QAction *action = NULL;
    bool hasDisplay = false;
    bool hasCamera = false;
    bool hasRangeFinder = false;
    QList<WbNode *> robotList = WbNodeUtilities::findDescendantNodesOfType(selectedRobot, isRobotNode, true);
    robotList.prepend(selectedRobot);
    foreach (WbNode *robotNode, robotList) {
      WbRobot *robot = reinterpret_cast<WbRobot *>(robotNode);
      QList<WbRenderingDevice *> devices = robot->renderingDevices();
      for (int i = 0; i < devices.size(); ++i) {
        const WbRenderingDevice *device = devices[i];
        QString deviceName = device->name();
#ifdef __linux__
        // fix Unity bug with underscores in menu item text
        if (qgetenv("XDG_CURRENT_DESKTOP") == "Unity")
          deviceName.replace("_", "__");
#endif
        action = new QAction(this);
        if (robot != selectedRobot)
          action->setText(tr("Show '%2' overlay of robot '%1'").arg(robot->name()).arg(deviceName));
        else
          action->setText(tr("Show '%1' overlay").arg(deviceName));
        if (device->nodeType() == WB_NODE_CAMERA) {
          action->setStatusTip(tr("Show overlay of camera device '%1'.").arg(deviceName));
          mRobotCameraMenu->addAction(action);
          hasCamera = true;
        } else if (device->nodeType() == WB_NODE_RANGE_FINDER) {
          action->setStatusTip(tr("Show overlay of range-finder device '%1'.").arg(deviceName));
          mRobotRangeFinderMenu->addAction(action);
          hasRangeFinder = true;
        } else if (device->nodeType() == WB_NODE_DISPLAY) {
          action->setStatusTip(tr("Show overlay of display device '%1'.").arg(deviceName));
          mRobotDisplayMenu->addAction(action);
          hasDisplay = true;
        } else {
          delete action;
          continue;
        }
        action->setToolTip(mToggleFullScreenAction->statusTip());
        action->setCheckable(true);
        action->setChecked(device->isOverlayEnabled());
        action->setEnabled(!device->isWindowActive());
        action->setProperty("renderingDevice", QVariant::fromValue((void *)device));
        connect(action, &QAction::toggled, mSimulationView->view3D(), &WbView3D::setShowRenderingDevice);
        connect(device, &WbRenderingDevice::overlayVisibilityChanged, action, &QAction::setChecked);
        connect(device, &WbRenderingDevice::overlayStatusChanged, action, &QAction::setEnabled);
      }
    }

    mRobotDisplayMenu->setEnabled(hasDisplay);
    mRobotCameraMenu->setEnabled(hasCamera);
    mRobotRangeFinderMenu->setEnabled(hasRangeFinder);
  }

  actions = mOverlayMenu->actions();
  for (int i = 0; i < actions.size() - 3; ++i)
    actions[i]->setVisible(selectedRobot != NULL);
}

void WbMainWindow::updateProjectPath(const QString &oldPath, const QString &newPath) {
  updateWindowTitle();
  if (mTextEditor)
    mTextEditor->updateProjectPath(oldPath, newPath);
  mRecentFiles->makeRecent(WbWorld::instance()->fileName());
}

void WbMainWindow::openFileInTextEditor(const QString &fileName) {
  if (!mTextEditor)
    return;

  bool success = mTextEditor->openFile(fileName);
  if (success)
    mTextEditor->show();
}

void WbMainWindow::createWorldLoadingProgressDialog() {
  if (mWorldLoadingProgressDialog)
    return;

  if (isMinimized())
    return;

#ifdef __APPLE__
  // Note: this platform dependent cases are caused by the fact that
  // the event loop and the OpenGL context management is slightly different
  // between the different OS and that Windows uses QtWebKit and the other OS use
  // QtWebEngine (OpengGL). These callbacks about updating the world loading dialog
  // are called during the object finalization, and the OpenGL context should
  // be dealt in a clean way.
  const bool needToChangeContext = WbWrenOpenGlContext::isCurrent();
  QOpenGLContext context;
  if (needToChangeContext) {
    WbWrenOpenGlContext::doneWren();
    context.makeCurrent(windowHandle());
  }
#endif

  mWorldLoadingProgressDialog = new QProgressDialog(tr("Opening world file"), tr("Cancel"), 0, 101, this);
  mWorldLoadingProgressDialog->setModal(true);
  mWorldLoadingProgressDialog->setAutoClose(false);
  mWorldLoadingProgressDialog->show();
  mWorldLoadingProgressDialog->setValue(0);
  mWorldLoadingProgressDialog->setWindowTitle(tr("Loading world"));
  connect(mWorldLoadingProgressDialog, &QProgressDialog::canceled, WbApplication::instance(),
          &WbApplication::setWorldLoadingCanceled);

#ifdef __APPLE__
  if (needToChangeContext) {
    context.doneCurrent();
    WbWrenOpenGlContext::makeWrenCurrent();
  }
#else
  QApplication::processEvents();
#endif
}

void WbMainWindow::deleteWorldLoadingProgressDialog() {
  if (mWorldLoadingProgressDialog) {
    disconnect(mWorldLoadingProgressDialog, &QProgressDialog::canceled, WbApplication::instance(),
               &WbApplication::setWorldLoadingCanceled);
    delete mWorldLoadingProgressDialog;
    mWorldLoadingProgressDialog = NULL;
  }
}

void WbMainWindow::setWorldLoadingProgress(const int progress) {
  if (mWorldLoadingProgressDialog) {
#ifdef __APPLE__
    // This function can be called when the WREN OpenGL context is active.
    // When Qt updates the GUI, it can change the OpenGL context.
    // Therefore it's important to handle correctly the OpenGL context here.
    const bool needToChangeContext = WbWrenOpenGlContext::isCurrent();
    QOpenGLContext context;
    if (needToChangeContext) {
      WbWrenOpenGlContext::doneWren();
      context.makeCurrent(windowHandle());
    }
#endif

    mWorldLoadingProgressDialog->setValue(progress);

#ifdef __APPLE__
    if (needToChangeContext) {
      context.doneCurrent();
      WbWrenOpenGlContext::makeWrenCurrent();
    }
#else
    QApplication::processEvents();
#endif
  }
}

void WbMainWindow::setWorldLoadingStatus(const QString &status) {
  if (mWorldLoadingProgressDialog) {
#ifdef __APPLE__
    const bool needToChangeContext = WbWrenOpenGlContext::isCurrent();
    QOpenGLContext context;
    if (needToChangeContext) {
      WbWrenOpenGlContext::doneWren();
      context.makeCurrent(windowHandle());
    }
#endif

    mWorldLoadingProgressDialog->setLabelText(status);

#ifdef __APPLE__
    if (needToChangeContext) {
      context.doneCurrent();
      WbWrenOpenGlContext::makeWrenCurrent();
    }
#else
    QApplication::processEvents();
#endif
  }
}

void WbMainWindow::startAnimationRecording() {
  WbSimulationState::Mode currentMode = WbSimulationState::instance()->mode();
  WbSimulationState::instance()->setMode(WbSimulationState::PAUSE);
  const QString &worldFileName = WbWorld::instance()->fileName();

  WbHtmlExportDialog parametersDialog(tr("Export as HTML5 animation"), worldFileName, this);
  bool accept = parametersDialog.exec();
  if (accept) {
    const QString &fileName = parametersDialog.fileName();
    assert(!fileName.isEmpty());
    WbAnimationRecorder::instance()->setStartFromGuiFlag(true);
    WbAnimationRecorder::instance()->start(fileName);
    WbPreferences::instance()->setValue("Directories/www", QFileInfo(fileName).absolutePath() + "/");
    toggleAnimationAction(true);
  }

  WbSimulationState::instance()->setMode(currentMode);
}

void WbMainWindow::stopAnimationRecording() {
  WbAnimationRecorder::instance()->stop();
  WbAnimationRecorder::instance()->setStartFromGuiFlag(false);
  toggleAnimationAction(false);
}

void WbMainWindow::toggleAnimationIcon() {
  static bool isRecOn = false;

  QAction *action = WbActionManager::instance()->action(WbActionManager::ANIMATION);
  if (!isRecOn) {
    action->setIcon(QIcon("enabledIcons:animation_red_button.png"));
    isRecOn = true;
  } else {
    action->setIcon(QIcon("enabledIcons:animation_black_button.png"));
    isRecOn = false;
  }
}

void WbMainWindow::toggleAnimationAction(bool isRecording) {
  QAction *action = WbActionManager::instance()->action(WbActionManager::ANIMATION);
  if (isRecording) {
    action->setText(tr("Stop HTML5 &Animation..."));
    action->setStatusTip(tr("Stop HTML5 animation recording."));
    action->setIcon(QIcon("enabledIcons:animation_red_button.png"));
    disconnect(action, &QAction::triggered, this, &WbMainWindow::startAnimationRecording);
    connect(action, &QAction::triggered, this, &WbMainWindow::stopAnimationRecording, Qt::UniqueConnection);
    mAnimationRecordingTimer->start(800);
  } else {
    action->setText(tr("Make HTML5 &Animation..."));
    action->setStatusTip(tr("Start HTML5 animation recording."));
    action->setIcon(QIcon("enabledIcons:animation_black_button.png"));
    disconnect(action, &QAction::triggered, this, &WbMainWindow::stopAnimationRecording);
    connect(action, &QAction::triggered, this, &WbMainWindow::startAnimationRecording, Qt::UniqueConnection);
    mAnimationRecordingTimer->stop();
  }

  action->setToolTip(action->statusTip());
}

void WbMainWindow::enableAnimationAction() {
  WbActionManager::instance()->action(WbActionManager::ANIMATION)->setEnabled(true);
}

void WbMainWindow::disableAnimationAction() {
  WbActionManager::instance()->action(WbActionManager::ANIMATION)->setEnabled(false);
}

void WbMainWindow::logActiveControllersTermination() {
  WbControlledWorld *controlledWorld = WbControlledWorld::instance();
  if (controlledWorld) {
    QStringList activeControllers = controlledWorld->activeControllersNames();
    foreach (QString controllerName, activeControllers)
      WbLog::info(tr("%1: Terminating.").arg(controllerName));
    QCoreApplication::processEvents();
  }
}

void WbMainWindow::openUrl(const QString &fileName, const QString &message, const QString &title) {
  if (WbMessageBox::question(message, this, title) == QMessageBox::Ok)
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
}

void WbMainWindow::prepareNodeRegeneration(WbNode *node) {
  // save devices perspective if node contains a rendering device
  // the device identification method could fail if the PROTO contains many
  // robots using the same device names, but usual node unique id cannot be used
  // because won't match before and after regeneration
  WbRenderingDeviceWindowFactory *factory = WbRenderingDeviceWindowFactory::instance();
  WbRenderingDevice *device;
  const QList<WbNode *> nodes = QList<WbNode *>() << const_cast<WbNode *>(node) << node->subNodes(true);
  foreach (WbNode *n, nodes) {
    device = dynamic_cast<WbRenderingDevice *>(n);
    if (device) {
      QStringList perspective = factory->windowPerspective(device);
      if (perspective.isEmpty())
        perspective = device->perspective();
      const WbRobot *robot = dynamic_cast<const WbRobot *>(WbNodeUtilities::findTopNode(node));
      mTemporaryProtoPerspectives.insert(robot->name() + "\n" + device->name(), perspective);
    }
  }
}

void WbMainWindow::finalizeNodeRegeneration(WbNode *node) {
  if (WbTemplateManager::isRegenerating())
    return;

  if (node != NULL && !mTemporaryProtoPerspectives.isEmpty()) {
    // apply temporary saved perspectives
    const QList<WbNode *> nodes = QList<WbNode *>() << node << node->subNodes(true);
    const WbRenderingDeviceWindowFactory *factory = WbRenderingDeviceWindowFactory::instance();
    foreach (WbNode *n, nodes) {
      WbRenderingDevice *device = dynamic_cast<WbRenderingDevice *>(n);
      if (device != NULL) {
        factory->listenToRenderingDevice(device);
        const WbRobot *robot = dynamic_cast<const WbRobot *>(WbNodeUtilities::findTopNode(node));
        const QString key = robot->name() + "\n" + device->name();
        QStringList perspective = mTemporaryProtoPerspectives.value(key);
        if (!perspective.isEmpty())
          device->restorePerspective(perspective);
        mTemporaryProtoPerspectives.remove(key);
      }
    }
    mTemporaryProtoPerspectives.clear();
  }
}