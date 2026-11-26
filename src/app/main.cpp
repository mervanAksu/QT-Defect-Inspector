#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Organizasyon + uygulama adı QSettings'in dosya yolunu belirler:
    // ~/.config/DefectInspector/Defect Inspector.conf
    QApplication::setOrganizationName("DefectInspector");
    QApplication::setApplicationName("Defect Inspector");
    QApplication::setApplicationVersion(PROJECT_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("Camera monitoring and fabric defect detection");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("source", "Camera index (0), image folder or video file.");
    const QCommandLineOption thresholdOption({"t", "threshold"}, "Detection threshold (0-255).",
                                             "value");
    const QCommandLineOption autostartOption({"s", "start"}, "Start inspecting immediately.");
    const QCommandLineOption screenshotOption(
        "screenshot", "Start, wait <ms>, save the window to <file> and quit (for docs).",
        "file");
    const QCommandLineOption delayOption("screenshot-delay", "Delay before the screenshot.", "ms",
                                         "3000");
    const QCommandLineOption tcpOption("tcp", "Publish results via a TCP server on <port>.",
                                       "port");
    const QCommandLineOption udpOption("udp", "Send results as UDP datagrams to <host:port>.",
                                       "host:port");
    parser.addOption(thresholdOption);
    parser.addOption(autostartOption);
    parser.addOption(tcpOption);
    parser.addOption(udpOption);
    parser.addOption(screenshotOption);
    parser.addOption(delayOption);
    parser.process(app);

    // Ekran görüntüsü modunda kullanıcının kayıtlı ayarlarını ne oku ne yaz: README
    // görüntüleri her makinede aynı çıksın. Ayarları geçici, boş bir klasöre yönlendir.
    QTemporaryDir isolatedSettings;
    if (parser.isSet(screenshotOption)) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolatedSettings.path());
    }

    inspector::MainWindow window;  // kaydedilmiş ayarları yükler

    // Öncelik: komut satırı > kaydedilmiş ayar > depodaki örnek klasör.
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        window.setSource(args.first());
    } else if (window.source().isEmpty() && QDir(INSPECTOR_SAMPLES_DIR).exists()) {
        window.setSource(INSPECTOR_SAMPLES_DIR);
    }
    if (parser.isSet(thresholdOption)) {
        window.setThreshold(parser.value(thresholdOption).toInt());
    }
    if (parser.isSet(tcpOption)) {
        window.enableTcp(static_cast<quint16>(parser.value(tcpOption).toUInt()));
    }
    if (parser.isSet(udpOption)) {
        // "host:port" -> son ':' işaretinden böl
        const QString value = parser.value(udpOption);
        const qsizetype colon = value.lastIndexOf(':');
        if (colon <= 0) {
            std::fprintf(stderr, "--udp expects host:port\n");
            return 2;
        }
        window.enableUdp(value.left(colon), static_cast<quint16>(value.mid(colon + 1).toUInt()));
    }

    window.show();
    if (parser.isSet(autostartOption) || parser.isSet(screenshotOption)) {
        window.start();
    }

    // README ekran görüntülerini tekrarlanabilir şekilde üretmek için:
    //   QT_QPA_PLATFORM=offscreen ./defect_inspector --screenshot docs/screenshot.png
    if (parser.isSet(screenshotOption)) {
        const QString file = parser.value(screenshotOption);
        QTimer::singleShot(parser.value(delayOption).toInt(), &window, [&window, file] {
            const bool ok = window.grab().save(file);
            std::fprintf(ok ? stdout : stderr, "%s %s\n", ok ? "saved" : "cannot save",
                         qPrintable(file));
            QApplication::exit(ok ? 0 : 1);
        });
    }
    return QApplication::exec();  // olay döngüsü: pencere kapanana kadar burada döner
}
