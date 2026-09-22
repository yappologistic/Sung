#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>

namespace librarydata {
// The saved library repeats songs in queues, playlists and their snapshots.
// Qt's implicitly shared values can represent equal copies with one allocation,
// while an edit still detaches the changed copy. Keep the lookup tables only
// during loading; no second library or process-wide string cache survives it.
// https://doc.qt.io/qt-6/implicit-sharing.html
class Reader {
public:
  QVariant read(const QJsonValue &value) {
    if (value.isString()) return string(value.toString());
    if (value.isArray()) {
      const auto array = value.toArray();
      QVariantList result;
      result.reserve(array.size());
      for (const auto &entry : array) result.append(read(entry));
      return result;
    }
    if (!value.isObject()) return value.toVariant();
    const auto object = value.toObject();
    const auto id = object.value(QLatin1String("id")).toString();
    if (!id.isEmpty()) {
      const auto found = m_objects.constFind(id);
      if (found != m_objects.cend())
        for (const auto &entry : *found)
          if (entry.first == object) return entry.second;
    }
    QVariantMap result;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
      result.insert(string(it.key()), read(it.value()));
    // IDs alone are not equality: a queue entry can carry a different origin,
    // and a saved version can intentionally retain older metadata.
    // Bound comparisons for a damaged file with thousands of different
    // records sharing an ID. Uncached variants still retain all their fields.
    if (!id.isEmpty() && m_objects[id].size() < 8)
      m_objects[id].append({object, result});
    return result;
  }
private:
  QString string(const QString &value) {
    const auto found = m_strings.constFind(value);
    if (found != m_strings.cend()) return *found;
    m_strings.insert(value, value);
    return value;
  }
  QHash<QString, QString> m_strings;
  QHash<QString, QList<QPair<QJsonObject, QVariantMap>>> m_objects;
};
inline QVariantMap read(const QJsonObject &object) {
  return Reader().read(object).toMap();
}
}
