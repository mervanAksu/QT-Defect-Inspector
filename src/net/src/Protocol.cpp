#include "inspector/Protocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace inspector::protocol {

namespace {

/// float -> double dönüşümü 572.762939453125 gibi anlamsız basamaklar üretir;
/// JSON okunur ve kısa kalsın diye yuvarlıyoruz.
double rounded(double value, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

bool fail(QString* error, const QString& message)
{
    if (error) {
        *error = message;
    }
    return false;
}

/// Zorunlu sayısal alanı okur. JSON'da tüm sayılar double'dır.
bool readNumber(const QJsonObject& obj, const char* key, double& out, QString* error)
{
    const QJsonValue value = obj.value(QLatin1String(key));
    if (!value.isDouble()) {
        return fail(error, QStringLiteral("missing or non-numeric field '%1'").arg(key));
    }
    out = value.toDouble();
    return true;
}

}  // namespace

Message fromResult(const InspectionResult& result, const QDateTime& sentAt)
{
    Message message;
    message.frameIndex = result.frameIndex;
    message.sentAt = sentAt;
    message.defectCount = static_cast<int>(result.detection.defects.size());
    message.processingMs = result.detection.processingMs;
    message.latencyMs = result.latencyMs;
    message.droppedFrames = result.droppedFrames;
    message.imageSize = result.annotatedImage.size();

    const int listed = std::min(message.defectCount, kMaxListedDefects);
    message.defects.reserve(listed);
    for (int i = 0; i < listed; ++i) {
        const Defect& d = result.detection.defects[static_cast<std::size_t>(i)];
        message.defects.push_back({QRect(d.boundingBox.x, d.boundingBox.y, d.boundingBox.width,
                                         d.boundingBox.height),
                                   QPointF(d.center.x, d.center.y), d.area});
    }
    return message;
}

QByteArray encode(const Message& message)
{
    QJsonArray defects;
    for (const DefectInfo& d : message.defects) {
        defects.append(QJsonObject{
            {"x", d.box.x()},
            {"y", d.box.y()},
            {"width", d.box.width()},
            {"height", d.box.height()},
            {"cx", rounded(d.center.x(), 2)},
            {"cy", rounded(d.center.y(), 2)},
            {"area", rounded(d.area, 1)},
        });
    }
    const QJsonObject root{
        {"type", kTypeInspection},
        {"version", message.version},
        {"frame", static_cast<qint64>(message.frameIndex)},
        {"sent_at", message.sentAt.toUTC().toString(Qt::ISODateWithMs)},
        {"defect_count", message.defectCount},
        {"processing_ms", rounded(message.processingMs, 3)},
        {"latency_ms", rounded(message.latencyMs, 3)},
        {"dropped_frames", static_cast<qint64>(message.droppedFrames)},
        {"image", QJsonObject{{"width", message.imageSize.width()},
                              {"height", message.imageSize.height()}}},
        {"defects", defects},
    };
    // Compact: girinti/yeni satır yok, yani mesajın içinde '\n' asla geçmez.
    return QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
}

std::optional<Message> decode(const QByteArray& line, QString* error)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(error, QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!doc.isObject()) {
        fail(error, QStringLiteral("message is not a JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = doc.object();
    if (root.value("type").toString() != QLatin1String(kTypeInspection)) {
        fail(error, QStringLiteral("unknown message type"));
        return std::nullopt;
    }

    Message message;
    double version = 0;
    double frame = 0;
    double defectCount = 0;
    double dropped = 0;
    if (!readNumber(root, "version", version, error) || !readNumber(root, "frame", frame, error) ||
        !readNumber(root, "defect_count", defectCount, error) ||
        !readNumber(root, "processing_ms", message.processingMs, error) ||
        !readNumber(root, "latency_ms", message.latencyMs, error) ||
        !readNumber(root, "dropped_frames", dropped, error)) {
        return std::nullopt;
    }
    if (static_cast<int>(version) != kVersion) {
        fail(error, QStringLiteral("unsupported protocol version %1").arg(version));
        return std::nullopt;
    }
    message.version = static_cast<int>(version);
    message.frameIndex = static_cast<quint64>(frame);
    message.defectCount = static_cast<int>(defectCount);
    message.droppedFrames = static_cast<quint64>(dropped);
    message.sentAt = QDateTime::fromString(root.value("sent_at").toString(), Qt::ISODateWithMs);

    const QJsonObject image = root.value("image").toObject();
    message.imageSize = QSize(image.value("width").toInt(), image.value("height").toInt());

    const QJsonArray defects = root.value("defects").toArray();
    if (defects.size() > message.defectCount) {
        fail(error, QStringLiteral("more defects listed than defect_count"));
        return std::nullopt;
    }
    for (const QJsonValue& value : defects) {
        const QJsonObject d = value.toObject();
        double x = 0, y = 0, w = 0, h = 0, cx = 0, cy = 0, area = 0;
        if (!readNumber(d, "x", x, error) || !readNumber(d, "y", y, error) ||
            !readNumber(d, "width", w, error) || !readNumber(d, "height", h, error) ||
            !readNumber(d, "cx", cx, error) || !readNumber(d, "cy", cy, error) ||
            !readNumber(d, "area", area, error)) {
            return std::nullopt;
        }
        message.defects.push_back({QRect(static_cast<int>(x), static_cast<int>(y),
                                         static_cast<int>(w), static_cast<int>(h)),
                                   QPointF(cx, cy), area});
    }
    return message;
}

}  // namespace inspector::protocol
