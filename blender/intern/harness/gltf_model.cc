/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 */

#include "gltf_model.hh"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QStandardPaths>
#include <QtEndian>

#include <cstring>
#include <functional>
#include <limits>

namespace harness {

void GltfGeometry::setMesh(const QByteArray &vertices, const QByteArray &indices,
                           const QVector3D &lo, const QVector3D &hi)
{
  clear();
  setVertexData(vertices);
  setIndexData(indices);
  setStride(8 * sizeof(float));
  setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
  addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 24, QQuick3DGeometry::Attribute::F32Type);
  addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
  setBounds(lo, hi);
  update();
}

GltfModel::~GltfModel()
{
  clear();
}

void GltfModel::clear()
{
  parts_.clear();
  for (GltfGeometry *g : geometries_) g->deleteLater();
  geometries_.clear();
  vertices_ = triangles_ = textures_ = 0;
  lo_ = hi_ = QVector3D();
  ready_ = false;
}

void GltfModel::setSource(const QUrl &url)
{
  if (url == source_) return;
  source_ = url;
  Q_EMIT sourceChanged();
  load();
}

namespace {

struct Doc {
  QJsonObject json;
  QList<QByteArray> buffers;
  QString base_dir;
};

QByteArray read_uri(const QString &uri, const QString &base_dir)
{
  if (uri.startsWith("data:")) {
    const int comma = uri.indexOf(',');
    return QByteArray::fromBase64(uri.mid(comma + 1).toLatin1());
  }
  QFile f(QDir(base_dir).filePath(QUrl::fromPercentEncoding(uri.toUtf8())));
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/** Bytes of a buffer view. */
QByteArray view_bytes(const Doc &d, int view)
{
  const QJsonObject v = d.json["bufferViews"].toArray()[view].toObject();
  const int buf = v["buffer"].toInt();
  if (buf < 0 || buf >= d.buffers.size()) return {};
  return d.buffers[buf].mid(v["byteOffset"].toInt(), v["byteLength"].toInt());
}

int components(const QString &type)
{
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT4") return 16;
  return 0;
}

/** An accessor as floats (normalised integers mapped to 0..1). */
std::vector<float> read_floats(const Doc &d, int accessor, int &comps, int &count)
{
  const QJsonObject a = d.json["accessors"].toArray()[accessor].toObject();
  comps = components(a["type"].toString());
  count = a["count"].toInt();
  std::vector<float> out(size_t(comps) * size_t(count), 0.0f);
  if (!a.contains("bufferView")) return out;
  const QJsonObject v = d.json["bufferViews"].toArray()[a["bufferView"].toInt()].toObject();
  const QByteArray bytes = view_bytes(d, a["bufferView"].toInt());
  const int ct = a["componentType"].toInt();
  const int csize = (ct == 5126 || ct == 5125) ? 4 : (ct == 5123 || ct == 5122) ? 2 : 1;
  const int stride = v["byteStride"].toInt(comps * csize);
  const int offset = a["byteOffset"].toInt();
  const bool norm = a["normalized"].toBool();
  const char *p = bytes.constData();
  for (int i = 0; i < count; i++) {
    for (int c = 0; c < comps; c++) {
      const qsizetype at = offset + qsizetype(i) * stride + c * csize;
      if (at + csize > bytes.size()) return out;
      float f = 0.0f;
      switch (ct) {
        case 5126: std::memcpy(&f, p + at, 4); f = qFromLittleEndian(f); break;
        case 5125: f = float(qFromLittleEndian<quint32>(p + at)); break;
        case 5123: f = float(qFromLittleEndian<quint16>(p + at)); if (norm) f /= 65535.0f; break;
        case 5122: f = float(qFromLittleEndian<qint16>(p + at)); if (norm) f = std::max(-1.0f, f / 32767.0f); break;
        case 5121: f = float(quint8(p[at])); if (norm) f /= 255.0f; break;
        case 5120: f = float(qint8(p[at])); if (norm) f = std::max(-1.0f, f / 127.0f); break;
      }
      out[size_t(i) * comps + c] = f;
    }
  }
  return out;
}

std::vector<quint32> read_indices(const Doc &d, int accessor)
{
  int comps = 0, count = 0;
  const std::vector<float> f = read_floats(d, accessor, comps, count);
  std::vector<quint32> out(f.size());
  for (size_t i = 0; i < f.size(); i++) out[i] = quint32(f[i]);
  return out;
}

QMatrix4x4 node_matrix(const QJsonObject &n)
{
  QMatrix4x4 m;
  if (n.contains("matrix")) {
    const QJsonArray a = n["matrix"].toArray();
    float v[16];
    for (int i = 0; i < 16; i++) v[i] = float(a[i].toDouble());
    return QMatrix4x4(v).transposed(); /* glTF stores column-major */
  }
  const QJsonArray t = n["translation"].toArray();
  const QJsonArray r = n["rotation"].toArray();
  const QJsonArray s = n["scale"].toArray();
  if (t.size() == 3) m.translate(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
  if (r.size() == 4) m.rotate(QQuaternion(float(r[3].toDouble()), float(r[0].toDouble()), float(r[1].toDouble()), float(r[2].toDouble())));
  if (s.size() == 3) m.scale(float(s[0].toDouble()), float(s[1].toDouble()), float(s[2].toDouble()));
  return m;
}

}  // namespace

void GltfModel::load()
{
  clear();
  error_.clear();
  const QString path = source_.isLocalFile() ? source_.toLocalFile() : source_.toString();
  QFile f(path);
  if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) {
    error_ = path.isEmpty() ? QString() : tr("Can't open %1").arg(QFileInfo(path).fileName());
    Q_EMIT loaded();
    return;
  }
  const QByteArray data = f.readAll();
  Doc d;
  d.base_dir = QFileInfo(path).absolutePath();
  QByteArray bin;
  if (data.startsWith("glTF")) {
    /* GLB: header, then a JSON chunk and an optional BIN chunk. */
    qsizetype at = 12;
    while (at + 8 <= data.size()) {
      const quint32 len = qFromLittleEndian<quint32>(data.constData() + at);
      const quint32 type = qFromLittleEndian<quint32>(data.constData() + at + 4);
      const QByteArray chunk = data.mid(at + 8, len);
      if (type == 0x4E4F534A) d.json = QJsonDocument::fromJson(chunk).object();
      else if (type == 0x004E4942) bin = chunk;
      at += 8 + len;
    }
  }
  else {
    d.json = QJsonDocument::fromJson(data).object();
  }
  if (d.json.isEmpty()) {
    error_ = tr("Only glTF 2.0 files (.glb, .gltf) can be shown here; place it in the scene to see it.");
    Q_EMIT loaded();
    return;
  }
  for (const auto &bv : d.json["buffers"].toArray()) {
    const QJsonObject b = bv.toObject();
    d.buffers << (b.contains("uri") ? read_uri(b["uri"].toString(), d.base_dir) : bin);
  }
  for (const auto &e : d.json["extensionsRequired"].toArray()) {
    if (e.toString().contains("draco") || e.toString().contains("meshopt")) {
      error_ = tr("This model is compressed (%1); place it in the scene to see it.").arg(e.toString());
      Q_EMIT loaded();
      return;
    }
  }

  /* Textures: embedded images go to the cache so Qt Quick 3D can load them by file. */
  const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                        "/HarnessStudio/viewer";
  QDir().mkpath(cache);
  std::map<int, QString> texture_files;
  auto texture_url = [&](const QJsonObject &ref) -> QString {
    if (!ref.contains("index")) return {};
    const int ti = ref["index"].toInt();
    if (texture_files.count(ti)) return texture_files[ti];
    const QJsonObject tex = d.json["textures"].toArray()[ti].toObject();
    const QJsonObject img = d.json["images"].toArray()[tex["source"].toInt()].toObject();
    QByteArray bytes = img.contains("bufferView") ? view_bytes(d, img["bufferView"].toInt())
                                                  : read_uri(img["uri"].toString(), d.base_dir);
    if (bytes.isEmpty()) return {};
    const QString mime = img["mimeType"].toString();
    const QString ext = mime.contains("jpeg") ? "jpg" : mime.contains("webp") ? "webp" : "png";
    const QString file = cache + "/" + QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(20) + "." + ext;
    if (!QFile::exists(file)) {
      QFile out(file);
      if (out.open(QIODevice::WriteOnly)) out.write(bytes);
    }
    textures_++;
    return texture_files[ti] = QUrl::fromLocalFile(file).toString();
  };

  QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
  QVector3D hi = -lo;
  const QJsonArray meshes = d.json["meshes"].toArray();
  const QJsonArray nodes = d.json["nodes"].toArray();
  const QJsonArray materials = d.json["materials"].toArray();

  std::function<void(int, const QMatrix4x4 &)> visit = [&](int ni, const QMatrix4x4 &parent) {
    const QJsonObject n = nodes[ni].toObject();
    const QMatrix4x4 world = parent * node_matrix(n);
    if (n.contains("mesh")) {
      const QMatrix3x3 nm = world.normalMatrix();
      for (const auto &pv : meshes[n["mesh"].toInt()].toObject()["primitives"].toArray()) {
        const QJsonObject prim = pv.toObject();
        if (prim["mode"].toInt(4) != 4) continue;
        if (prim["extensions"].toObject().contains("KHR_draco_mesh_compression")) {
          error_ = tr("This model is compressed (Draco); place it in the scene to see it.");
          continue;
        }
        const QJsonObject at = prim["attributes"].toObject();
        if (!at.contains("POSITION")) continue;
        int pc = 0, count = 0, nc = 0, ncount = 0, uc = 0, ucount = 0;
        const std::vector<float> pos = read_floats(d, at["POSITION"].toInt(), pc, count);
        std::vector<float> nor = at.contains("NORMAL") ? read_floats(d, at["NORMAL"].toInt(), nc, ncount) : std::vector<float>();
        const std::vector<float> uv = at.contains("TEXCOORD_0") ? read_floats(d, at["TEXCOORD_0"].toInt(), uc, ucount) : std::vector<float>();
        std::vector<quint32> idx = prim.contains("indices") ? read_indices(d, prim["indices"].toInt()) : std::vector<quint32>();
        if (idx.empty()) {
          idx.resize(size_t(count));
          for (int i = 0; i < count; i++) idx[size_t(i)] = quint32(i);
        }
        if (pc != 3 || count == 0) continue;
        /* World-space positions. */
        std::vector<QVector3D> P(static_cast<size_t>(count)), N(static_cast<size_t>(count));
        for (int i = 0; i < count; i++) {
          P[size_t(i)] = world.map(QVector3D(pos[size_t(i) * 3], pos[size_t(i) * 3 + 1], pos[size_t(i) * 3 + 2]));
        }
        if (nc == 3 && ncount == count) {
          for (int i = 0; i < count; i++) {
            const float *v = &nor[size_t(i) * 3];
            N[size_t(i)] = QVector3D(nm(0, 0) * v[0] + nm(0, 1) * v[1] + nm(0, 2) * v[2],
                                     nm(1, 0) * v[0] + nm(1, 1) * v[1] + nm(1, 2) * v[2],
                                     nm(2, 0) * v[0] + nm(2, 1) * v[1] + nm(2, 2) * v[2]).normalized();
          }
        }
        else {
          /* No normals: smooth ones from the faces. */
          for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            if (idx[t] >= quint32(count) || idx[t + 1] >= quint32(count) || idx[t + 2] >= quint32(count)) continue;
            const QVector3D fn = QVector3D::crossProduct(P[idx[t + 1]] - P[idx[t]], P[idx[t + 2]] - P[idx[t]]);
            N[idx[t]] += fn; N[idx[t + 1]] += fn; N[idx[t + 2]] += fn;
          }
          for (auto &v : N) v.normalize();
        }
        QByteArray vb(qsizetype(count) * 8 * qsizetype(sizeof(float)), Qt::Uninitialized);
        float *o = reinterpret_cast<float *>(vb.data());
        QVector3D plo = P[0], phi = P[0];
        for (int i = 0; i < count; i++) {
          const QVector3D &p = P[size_t(i)];
          plo = QVector3D(std::min(plo.x(), p.x()), std::min(plo.y(), p.y()), std::min(plo.z(), p.z()));
          phi = QVector3D(std::max(phi.x(), p.x()), std::max(phi.y(), p.y()), std::max(phi.z(), p.z()));
          *o++ = p.x(); *o++ = p.y(); *o++ = p.z();
          *o++ = N[size_t(i)].x(); *o++ = N[size_t(i)].y(); *o++ = N[size_t(i)].z();
          *o++ = (uc == 2 && ucount == count) ? uv[size_t(i) * 2] : 0.0f;
          *o++ = (uc == 2 && ucount == count) ? uv[size_t(i) * 2 + 1] : 0.0f;
        }
        QByteArray ib(qsizetype(idx.size()) * 4, Qt::Uninitialized);
        std::memcpy(ib.data(), idx.data(), size_t(ib.size()));
        lo = QVector3D(std::min(lo.x(), plo.x()), std::min(lo.y(), plo.y()), std::min(lo.z(), plo.z()));
        hi = QVector3D(std::max(hi.x(), phi.x()), std::max(hi.y(), phi.y()), std::max(hi.z(), phi.z()));

        auto *geom = new GltfGeometry();
        geom->setMesh(vb, ib, plo, phi);
        geometries_ << geom;
        vertices_ += count;
        triangles_ += int(idx.size() / 3);

        /* Material (defaults per the glTF spec). */
        QVariantMap part{{"geometry", QVariant::fromValue(static_cast<QObject *>(geom))},
                         {"baseColor", QColor::fromRgbF(1, 1, 1, 1)}, {"metalness", 1.0}, {"roughness", 1.0},
                         {"opacity", 1.0}, {"emissive", QColor(0, 0, 0)}, {"doubleSided", false}};
        if (prim.contains("material")) {
          const QJsonObject m = materials[prim["material"].toInt()].toObject();
          const QJsonObject pbr = m["pbrMetallicRoughness"].toObject();
          const QJsonArray bc = pbr["baseColorFactor"].toArray();
          if (bc.size() == 4) {
            part["baseColor"] = QColor::fromRgbF(float(bc[0].toDouble()), float(bc[1].toDouble()), float(bc[2].toDouble()), 1.0f);
            part["opacity"] = bc[3].toDouble();
          }
          part["metalness"] = pbr["metallicFactor"].toDouble(1.0);
          part["roughness"] = pbr["roughnessFactor"].toDouble(1.0);
          part["baseColorMap"] = texture_url(pbr["baseColorTexture"].toObject());
          part["ormMap"] = texture_url(pbr["metallicRoughnessTexture"].toObject());
          part["normalMap"] = texture_url(m["normalTexture"].toObject());
          part["emissiveMap"] = texture_url(m["emissiveTexture"].toObject());
          const QJsonArray em = m["emissiveFactor"].toArray();
          if (em.size() == 3) part["emissive"] = QColor::fromRgbF(float(em[0].toDouble()), float(em[1].toDouble()), float(em[2].toDouble()));
          part["doubleSided"] = m["doubleSided"].toBool();
          part["name"] = m["name"].toString();
        }
        parts_ << part;
      }
    }
    for (const auto &c : n["children"].toArray()) visit(c.toInt(), world);
  };

  const QJsonArray scenes = d.json["scenes"].toArray();
  QJsonArray roots = scenes.isEmpty() ? QJsonArray() : scenes[d.json["scene"].toInt(0)].toObject()["nodes"].toArray();
  if (roots.isEmpty()) {
    for (int i = 0; i < nodes.size(); i++) roots << i;
  }
  for (const auto &r : roots) visit(r.toInt(), QMatrix4x4());

  if (parts_.isEmpty()) {
    if (error_.isEmpty()) error_ = tr("The file has no meshes to show.");
  }
  else {
    lo_ = lo;
    hi_ = hi;
    ready_ = true;
    error_.clear();
  }
  Q_EMIT loaded();
}

}  // namespace harness
