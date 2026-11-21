#include "NetworkAccess.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSocketNotifier>

#include <csignal>
#include <sys/eventfd.h>
#include <unistd.h>

namespace {

int quitFd = -1;

void requestQuit(int)
{
    const quint64 one = 1;
    [[maybe_unused]] auto written = ::write(quitFd, &one, sizeof(one));
}

// systemd stops the unit with SIGTERM. Turn it into a normal quit, so the shell
// hands the screen back to Steam on the way out (BaseLayer's destructor).
void quitOnTermination(QGuiApplication &app)
{
    quitFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (quitFd < 0)
        return;
    auto *notifier = new QSocketNotifier(quitFd, QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);

    struct sigaction action = {};
    action.sa_handler = requestQuit;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

} // namespace

int main(int argc, char *argv[])
{
    // Inside Gaming Mode only X11 windows can be put on screen (see docs/gabecube-findings.md).
    if (qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY") && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tvshell"));
    quitOnTermination(app);

    QQmlApplicationEngine engine;
    // Adds the Immich API key to requests for the Immich server; disk cache.
    static NetworkAccessManagerFactory networkFactory;
    engine.setNetworkAccessManagerFactory(&networkFactory);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("TvShell", "Main");

    return app.exec();
}
