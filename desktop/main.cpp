
/**
 * Copyright (C) 2016, Canonical Ltd.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

// #define BUILD_FOR_BUNDLE

#include <QCommandLineParser>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QProcess>
#include <QQuickView>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "attachedproperties.h"
#include "reactitem.h"
#include "rootview.h"
#include "utilities.h"

#include "exceptionglobalhandler.h"

Q_DECLARE_LOGGING_CATEGORY(JSSERVER)
Q_DECLARE_LOGGING_CATEGORY(STATUS)
Q_LOGGING_CATEGORY(JSSERVER, "jsserver")
Q_LOGGING_CATEGORY(STATUS, "status")

static QStringList consoleOutputStrings;
static QMutex consoleOutputMutex;

#ifdef BUILD_FOR_BUNDLE
bool nodeJsServerStarted = false;
QProcess *g_nodeJsServerProcess = nullptr;
#endif

const int MAIN_WINDOW_WIDTH = 1024;
const int MAIN_WINDOW_HEIGHT = 768;
const QString CRASH_REPORT_EXECUTABLE = QStringLiteral("reportApp");
const QString CRASH_REPORT_EXECUTABLE_RELATIVE_PATH =
    QStringLiteral("/../reportApp");

const char *ENABLE_LOG_FILE_ENV_VAR_NAME = "STATUS_LOG_FILE_ENABLED";
const char *LOG_FILE_PATH_ENV_VAR_NAME = "STATUS_LOG_PATH";

// TODO: some way to change while running
class ReactNativeProperties : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool liveReload READ liveReload WRITE setLiveReload NOTIFY
                 liveReloadChanged)
  Q_PROPERTY(QUrl codeLocation READ codeLocation WRITE setCodeLocation NOTIFY
                 codeLocationChanged)
  Q_PROPERTY(QString pluginsPath READ pluginsPath WRITE setPluginsPath NOTIFY
                 pluginsPathChanged)
  Q_PROPERTY(
      QString executor READ executor WRITE setExecutor NOTIFY executorChanged)
public:
  ReactNativeProperties(QObject *parent = nullptr) : QObject(parent) {
    m_codeLocation = m_packagerTemplate.arg(m_packagerHost).arg(m_packagerPort);
  }
  bool liveReload() const { return m_liveReload; }
  void setLiveReload(bool liveReload) {
    if (m_liveReload == liveReload)
      return;
    m_liveReload = liveReload;
    Q_EMIT liveReloadChanged();
  }
  QUrl codeLocation() const { return m_codeLocation; }
  void setCodeLocation(const QUrl &codeLocation) {
    if (m_codeLocation == codeLocation)
      return;
    m_codeLocation = codeLocation;
    Q_EMIT codeLocationChanged();
  }
  QString pluginsPath() const { return m_pluginsPath; }
  void setPluginsPath(const QString &pluginsPath) {
    if (m_pluginsPath == pluginsPath)
      return;
    m_pluginsPath = pluginsPath;
    Q_EMIT pluginsPathChanged();
  }
  QString executor() const { return m_executor; }
  void setExecutor(const QString &executor) {
    if (m_executor == executor)
      return;
    m_executor = executor;
    Q_EMIT executorChanged();
  }
  QString packagerHost() const { return m_packagerHost; }
  void setPackagerHost(const QString &packagerHost) {
    if (m_packagerHost == packagerHost)
      return;
    m_packagerHost = packagerHost;
    setCodeLocation(m_packagerTemplate.arg(m_packagerHost).arg(m_packagerPort));
  }
  QString packagerPort() const { return m_packagerPort; }
  void setPackagerPort(const QString &packagerPort) {
    if (m_packagerPort == packagerPort)
      return;
    m_packagerPort = packagerPort;
    setCodeLocation(m_packagerTemplate.arg(m_packagerHost).arg(m_packagerPort));
  }
  void setLocalSource(const QString &source) {
    if (m_localSource == source)
      return;

    // overrides packager*
    if (source.startsWith("file:")) {
      setCodeLocation(source);
    } else {
      QFileInfo fi(source);
      if (!fi.exists()) {
        qCWarning(STATUS) << "Attempt to set non-existent local source file";
        return;
      }
      setCodeLocation(QUrl::fromLocalFile(fi.absoluteFilePath()));
      setLiveReload(false);
    }
  }
Q_SIGNALS:
  void liveReloadChanged();
  void codeLocationChanged();
  void pluginsPathChanged();
  void executorChanged();

private:
  bool m_liveReload = false;
  QString m_packagerHost = "localhost";
  QString m_packagerPort = "8081";
  QString m_localSource;
  QString m_packagerTemplate =
      "http://%1:%2/index.desktop.bundle?platform=desktop&dev=true";
  QUrl m_codeLocation;
  QString m_pluginsPath;
#ifdef BUILD_FOR_BUNDLE
  QString m_executor = "RemoteServerConnection";
#else
  QString m_executor = "LocalServerConnection";
#endif
};

void saveMessage(QtMsgType type, const QMessageLogContext &context,
                 const QString &msg);
void writeLogsToFile();
void writeLogFromJSServer(const QString &msg);
void writeSingleLineLogFromJSServer(const QString &msg);

#ifdef BUILD_FOR_BUNDLE

void killZombieJsServer();
void runNodeJsServer();

#endif

void loadFontsFromResources() {

  QDirIterator it(":", QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QString resourceFile = it.next();
    if (resourceFile.endsWith(".otf", Qt::CaseInsensitive) ||
        resourceFile.endsWith(".ttf", Qt::CaseInsensitive)) {
      QFontDatabase::addApplicationFont(resourceFile);
    }
  }
}

void exceptionPostHandledCallback() {
#ifdef BUILD_FOR_BUNDLE
  if (g_nodeJsServerProcess) {
    g_nodeJsServerProcess->kill();
  }
#endif
}

bool redirectLogIntoFile() {
#ifdef BUILD_FOR_BUNDLE
  return true;
#else
  return qEnvironmentVariable(ENABLE_LOG_FILE_ENV_VAR_NAME, "") ==
         QStringLiteral("1");
#endif
}

QString getDataStoragePath() {
  QString dataStoragePath =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir dir(dataStoragePath);
  if (!dir.exists()) {
    dir.mkpath(".");
  }
  return dataStoragePath;
}

int main(int argc, char **argv) {
  QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QGuiApplication app(argc, argv);

  QCoreApplication::setApplicationName("Status");

  QString appPath = QCoreApplication::applicationDirPath();
  QString dataStoragePath = getDataStoragePath();
#ifndef BUILD_FOR_BUNDLE
  appPath.append(CRASH_REPORT_EXECUTABLE_RELATIVE_PATH);
  dataStoragePath = "";

  killZombieJsServer();
#endif

  ExceptionGlobalHandler exceptionHandler(
      appPath + QDir::separator() + CRASH_REPORT_EXECUTABLE,
      exceptionPostHandledCallback, dataStoragePath);

  Q_INIT_RESOURCE(react_resources);

  loadFontsFromResources();

  QLoggingCategory::setFilterRules(QStringLiteral("UIManager=false\nFlexbox=false\nViewManager=false\nNetworking=false\nWebSocketModule=false"));

  if (redirectLogIntoFile()) {
    qInstallMessageHandler(saveMessage);
  }

#ifdef BUILD_FOR_BUNDLE
  runNodeJsServer();

  app.setWindowIcon(QIcon(":/icon.png"));
#endif

  QQuickView view;
  ReactNativeProperties *rnp = new ReactNativeProperties(&view);
#ifdef BUILD_FOR_BUNDLE
  rnp->setCodeLocation("file:" + QGuiApplication::applicationDirPath() +
                       "/assets");
#endif

  utilities::registerReactTypes();

  QCommandLineParser p;
  p.setApplicationDescription("React Native host application");
  p.addHelpOption();
  p.addOptions({
      {{"R", "live-reload"}, "Enable live reload."},
      {{"H", "host"}, "Set packager host address.", rnp->packagerHost()},
      {{"P", "port"}, "Set packager port number.", rnp->packagerPort()},
      {{"L", "local"}, "Set path to the local packaged source", "not set"},
      {{"M", "plugins-path"}, "Set path to node modules", "./plugins"},
      {{"E", "executor"}, "Set Javascript executor", rnp->executor()},
  });
  p.process(app);
  rnp->setLiveReload(p.isSet("live-reload"));
  if (p.isSet("host"))
    rnp->setPackagerHost(p.value("host"));
  if (p.isSet("port"))
    rnp->setPackagerPort(p.value("port"));
  if (p.isSet("local"))
    rnp->setLocalSource(p.value("local"));
  if (p.isSet("plugins-path"))
    rnp->setPluginsPath(p.value("plugins-path"));
  if (p.isSet("executor"))
    rnp->setExecutor(p.value("executor"));

  view.rootContext()->setContextProperty("ReactNativeProperties", rnp);
  view.setSource(QUrl("qrc:///main.qml"));
  view.setResizeMode(QQuickView::SizeRootObjectToView);
  view.resize(MAIN_WINDOW_WIDTH, MAIN_WINDOW_HEIGHT);
  view.show();

  QTimer flushLogsToFileTimer;
  if (redirectLogIntoFile()) {
    flushLogsToFileTimer.setInterval(500);
    QObject::connect(&flushLogsToFileTimer, &QTimer::timeout,
                     [=]() { writeLogsToFile(); });
    flushLogsToFileTimer.start();
  }

  return app.exec();
}

QString getLogFilePath() {
  QString logFilePath;
#ifdef BUILD_FOR_BUNDLE
  logFilePath = getDataStoragePath() + "/Status.log";
#else
  logFilePath = qEnvironmentVariable(LOG_FILE_PATH_ENV_VAR_NAME, "");
  if (logFilePath.isEmpty()) {
    logFilePath = getDataStoragePath() + "/StatusDev.log";
  }
#endif
  return logFilePath;
}

void writeLogsToFile() {
  QMutexLocker locker(&consoleOutputMutex);
  QFile logFile(getLogFilePath());
  if (logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
    for (QString message : consoleOutputStrings) {
      logFile.write(message.toStdString().c_str());
    }
    consoleOutputStrings.clear();

    logFile.flush();
    logFile.close();
  }
}

#ifdef BUILD_FOR_BUNDLE
#ifdef Q_OS_WIN
#include <shellapi.h>
#include <combaseapi.h>

#include <cstdio>
#include <windows.h>
#include <tlhelp32.h>

/*!
\brief Check if a process is running
\param [in] processName Name of process to check if is running
\returns \c True if the process is running, or \c False if the process is not running
*/
bool IsProcessRunning(const wchar_t *processName) {
    bool exists = false;
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != NULL) {
      if (::Process32First(snapshot, &entry)) {
          do {
              if (!wcsicmp(entry.szExeFile, processName)) {
                exists = true;
                break;
              }
          } while (::Process32Next(snapshot, &entry));
      }

      ::CloseHandle(snapshot);
    }
    return exists;
}

#endif
void killZombieJsServer() {
  // Ensure that a zombie ubuntu-server is not still running in the background before we spawn a new one 
  const char* cmd = NULL;
#ifdef Q_OS_LINUX
  cmd = "pkill -f ubuntu-server";
#elif defined(Q_OS_MAC)
  cmd = "killall -9 ubuntu-server";
#elif defined(Q_OS_WIN)
  if (IsProcessRunning(L"ubuntu-server.exe")) {
    qCDebug(STATUS) << "ubuntu-server is running, killing it";
    ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ::ShellExecute(NULL, NULL, L"taskkill", L"/IM \"ubuntu-server.exe\"", NULL, SW_HIDE);
  } else {
    qCDebug(STATUS) << "ubuntu-server is not running";
  }
#endif

  if (cmd != nullptr) {
    qCDebug(STATUS) << "Running " << cmd;
    //QDesktopServices::openUrl(QUrl::fromLocalFile(cmd));
    system(cmd);
  }
}

void runNodeJsServer() {
  g_nodeJsServerProcess = new QProcess();
  g_nodeJsServerProcess->setWorkingDirectory(getDataStoragePath());
  g_nodeJsServerProcess->setProgram(QGuiApplication::applicationDirPath() + QDir::separator() + "ubuntu-server");
  QObject::connect(g_nodeJsServerProcess, &QProcess::errorOccurred,
                   [=](QProcess::ProcessError) {
                     qCWarning(JSSERVER) << "process name: "
                                         << qUtf8Printable(g_nodeJsServerProcess->program());
                     qCWarning(JSSERVER) << "process error: "
                                         << qUtf8Printable(g_nodeJsServerProcess->errorString());
                   });

  QObject::connect(
      g_nodeJsServerProcess, &QProcess::readyReadStandardOutput, [=] {
        writeLogFromJSServer(g_nodeJsServerProcess->readAllStandardOutput().trimmed());
      });
  QObject::connect(
      g_nodeJsServerProcess, &QProcess::readyReadStandardError, [=] {
        QString output =
            g_nodeJsServerProcess->readAllStandardError().trimmed();
        writeLogFromJSServer(output);
        if (output.contains("Server starting")) {
          nodeJsServerStarted = true;
        }
      });

  QObject::connect(QGuiApplication::instance(), &QCoreApplication::aboutToQuit,
                   [=]() {
                     qCDebug(STATUS) << "Kill node.js server process";
                     g_nodeJsServerProcess->kill();
                   });

  qCDebug(STATUS) << "starting node.js server process...";
  g_nodeJsServerProcess->start();
  qCDebug(STATUS) << "wait for started...";

  while (!nodeJsServerStarted) {
    QGuiApplication::processEvents();
  }

  qCDebug(STATUS) << "waiting finished";
}
#endif

void writeLogFromJSServer(const QString &msg) {
  if (msg.contains("\\n")) {
    QStringList lines = msg.split("\\n");
    foreach (const QString &line, lines) {
      writeSingleLineLogFromJSServer(line);
    }
  } else {
    writeSingleLineLogFromJSServer(msg);
  }
}

QString extractJSServerMessage(const QString& msg, int prefixLength) {
  return msg.mid(prefixLength);
}

void writeSingleLineLogFromJSServer(const QString &msg) {
  if (msg.startsWith("TRACE "))
    qCDebug(JSSERVER) << qUtf8Printable(extractJSServerMessage(msg, 6));
  else if (msg.startsWith("DEBUG "))
    qCDebug(JSSERVER) << qUtf8Printable(extractJSServerMessage(msg, 6));
  else if (msg.startsWith("INFO "))
    qCInfo(JSSERVER) << qUtf8Printable(extractJSServerMessage(msg, 5));
  else if (msg.startsWith("WARN "))
    qCWarning(JSSERVER) << qUtf8Printable(extractJSServerMessage(msg, 5));
  else if (msg.startsWith("ERROR "))
    qCWarning(JSSERVER) << qUtf8Printable(extractJSServerMessage(msg, 6));
  else if (msg.startsWith("FATAL "))
    qCCritical(JSSERVER) << qUtf8Printable(extractJSServerMessage(msg, 6));
  else
    qCDebug(JSSERVER) << qUtf8Printable(msg);
}

void appendConsoleString(const QString &msg) {
  QMutexLocker locker(&consoleOutputMutex);
  consoleOutputStrings << msg;
}

void saveMessage(QtMsgType type, const QMessageLogContext &context,
                 const QString &msg) {
  Q_UNUSED(context);
  QByteArray localMsg = msg.toLocal8Bit();
  QString message = localMsg + "\n";
  QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
  QString typeStr;

  switch (type) {
  case QtDebugMsg:
    typeStr = "D";
    break;
  case QtInfoMsg:
    typeStr = "I";
    break;
  case QtWarningMsg:
    typeStr = "W";
    break;
  case QtCriticalMsg:
    typeStr = "C";
    break;
  case QtFatalMsg:
    typeStr = "F";
  }
  appendConsoleString(QString("%1 - %2 - [%3] - %4").arg(timestamp, typeStr, context.category, message));
  if (type == QtFatalMsg) {
    writeLogsToFile();
    abort();
  }
}

#include "main.moc"
