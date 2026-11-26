// defect_receiver: Defect Inspector sonuçlarını TCP ya da UDP üzerinden alıp
// konsola basan istemci.
//
//   defect_receiver                          # 127.0.0.1:5555'e TCP ile bağlan
//   defect_receiver --host 192.168.1.20 --port 5555 --only-defects
//   defect_receiver --udp 5556               # UDP datagram'larını dinle
//   defect_receiver --count 10               # 10 mesaj sonra çık (betikler için)

#include "MessageFormat.h"
#include "ReceiverClient.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <csignal>
#include <cstdio>

namespace {

// Sinyal işleyicisinde yalnızca "async-signal-safe" işler yapılabilir: Qt fonksiyonu
// çağırmak, bellek ayırmak, printf... hiçbiri güvenli değil. Sadece bir bayrak
// kaldırıyoruz; olay döngüsündeki bir zamanlayıcı bayrağı görüp düzgünce çıkıyor.
volatile std::sig_atomic_t g_stopRequested = 0;

void onSignal(int)
{
    g_stopRequested = 1;
}

void printLine(const QString& text, std::FILE* stream = stdout)
{
    std::fputs(qPrintable(text + '\n'), stream);
    std::fflush(stream);  // borulara (| tee, | grep) satır satır aksın
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);  // GUI yok: yalnızca olay döngüsü
    QCoreApplication::setApplicationName("defect_receiver");

    QCommandLineParser parser;
    parser.setApplicationDescription("Receives Defect Inspector results over TCP or UDP.");
    parser.addHelpOption();
    const QCommandLineOption hostOption("host", "TCP server address.", "host", "127.0.0.1");
    const QCommandLineOption portOption("port", "TCP server port.", "port", "5555");
    const QCommandLineOption udpOption("udp", "Listen for UDP datagrams on <port> instead.",
                                       "port");
    const QCommandLineOption onlyDefectsOption("only-defects", "Print only frames with defects.");
    const QCommandLineOption countOption("count", "Exit after <n> valid messages.", "n");
    parser.addOptions({hostOption, portOption, udpOption, onlyDefectsOption, countOption});
    parser.process(app);

    const bool onlyDefects = parser.isSet(onlyDefectsOption);
    const quint64 exitAfter = parser.isSet(countOption) ? parser.value(countOption).toULongLong() : 0;

    inspector::ReceiverClient client;

    QObject::connect(&client, &inspector::ReceiverClient::messageReceived, &app,
                     [&](const inspector::protocol::Message& m) {
                         if (!onlyDefects || m.defectCount > 0) {
                             printLine(inspector::formatMessage(m));
                         }
                         if (exitAfter > 0 && client.stats().received >= exitAfter) {
                             QCoreApplication::exit(0);
                         }
                     });
    QObject::connect(&client, &inspector::ReceiverClient::invalidMessage, &app,
                     [](const QString& error) { printLine("invalid message: " + error, stderr); });
    QObject::connect(&client, &inspector::ReceiverClient::connected, &app,
                     [] { printLine("connected", stderr); });
    QObject::connect(&client, &inspector::ReceiverClient::disconnected, &app,
                     [] { printLine("disconnected", stderr); });
    QObject::connect(&client, &inspector::ReceiverClient::errorOccurred, &app,
                     [](const QString& error) { printLine("error: " + error, stderr); });
    QObject::connect(&client, &inspector::ReceiverClient::reconnectScheduled, &app,
                     [](int ms) {
                         printLine(QStringLiteral("reconnecting in %1 s").arg(ms / 1000.0), stderr);
                     });

    if (parser.isSet(udpOption)) {
        const auto port = static_cast<quint16>(parser.value(udpOption).toUInt());
        if (!client.listenUdp(port)) {
            return 1;
        }
        printLine(QStringLiteral("listening for UDP on port %1").arg(port), stderr);
    } else {
        const QString host = parser.value(hostOption);
        const auto port = static_cast<quint16>(parser.value(portOption).toUInt());
        printLine(QStringLiteral("connecting to %1:%2").arg(host).arg(port), stderr);
        client.connectToServer(host, port);
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    QTimer signalPoll;
    QObject::connect(&signalPoll, &QTimer::timeout, &app, [] {
        if (g_stopRequested) {
            QCoreApplication::exit(0);
        }
    });
    signalPoll.start(100);

    const int code = QCoreApplication::exec();

    const auto& s = client.stats();
    printLine(QStringLiteral("received %1 message(s), %2 with defects, %3 invalid")
                  .arg(s.received)
                  .arg(s.defective)
                  .arg(s.invalid),
              stderr);
    return code;
}
