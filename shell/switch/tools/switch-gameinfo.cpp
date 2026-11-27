// switch-gameinfo: what the shell reads from a Switch game file, for trying the reader on the
// machine without the shell.
//
//   switch-gameinfo <game.xci|game.nsp> [icon.jpg]
//
// Prints the title id, name, publisher and version; writes the icon if a path is given.
// Keys come from the newest folder in ~/Games/Emulators/Switch/keys/; they are never printed.

#include "format/GameFile.h"
#include "format/Keys.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (args.size() < 2 || args.size() > 3) {
        err << "usage: switch-gameinfo <game.xci|game.nsp> [icon.jpg]\n";
        return 2;
    }

    SwitchFormat::Keys keys;
    QString error;
    const QString folder = SwitchFormat::Keys::newestFolder(QDir::homePath() + QStringLiteral("/Games/Emulators/Switch/keys"));
    if (folder.isEmpty() || !keys.load(folder, &error)) {
        err << "keys: " << (folder.isEmpty() ? QStringLiteral("no keys folder") : error) << '\n';
        return 1;
    }

    const auto info = SwitchFormat::readGame(args[1], keys, {QStringLiteral("BritishEnglish"), QStringLiteral("AmericanEnglish")}, &error);
    if (!info) {
        err << args[1] << ": " << error << '\n';
        return 1;
    }
    out << "title id:  " << QStringLiteral("%1").arg(info->titleId, 16, 16, QLatin1Char('0')).toUpper() << '\n'
        << "name:      " << info->name << '\n'
        << "publisher: " << info->publisher << '\n'
        << "version:   " << info->version << '\n'
        << "icon:      " << info->icon.size() << " bytes\n";

    if (args.size() == 3) {
        QFile icon(args[2]);
        if (!icon.open(QIODevice::WriteOnly) || icon.write(info->icon) != info->icon.size()) {
            err << args[2] << ": " << icon.errorString() << '\n';
            return 1;
        }
    }
    return 0;
}
