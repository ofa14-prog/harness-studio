/* SPDX-FileCopyrightText: 2026 Harness Studio contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup harness
 *
 * A self-contained glTF 2.0 / GLB reader for the viewer, so generated models can be inspected
 * without Qt's Assimp importer (an optional system library). Meshes are flattened into world
 * space (node transforms applied), one geometry per primitive, with their PBR materials and
 * embedded textures written to the cache.
 */

#pragma once

#include <QQuick3DGeometry>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVector3D>

namespace harness {

class GltfGeometry : public QQuick3DGeometry {
  Q_OBJECT
 public:
  explicit GltfGeometry(QQuick3DObject *parent = nullptr) : QQuick3DGeometry(parent) {}
  void setMesh(const QByteArray &vertices, const QByteArray &indices, const QVector3D &lo,
               const QVector3D &hi);
};

class GltfModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
  /** [{geometry, baseColor, metalness, roughness, baseColorMap, normalMap, ormMap, emissive,
   *   emissiveMap, opacity}] */
  Q_PROPERTY(QVariantList parts READ parts NOTIFY loaded)
  Q_PROPERTY(QVector3D minimum READ minimum NOTIFY loaded)
  Q_PROPERTY(QVector3D maximum READ maximum NOTIFY loaded)
  Q_PROPERTY(QString error READ error NOTIFY loaded)
  Q_PROPERTY(bool ready READ ready NOTIFY loaded)
  Q_PROPERTY(int vertexCount READ vertexCount NOTIFY loaded)
  Q_PROPERTY(int triangleCount READ triangleCount NOTIFY loaded)
  Q_PROPERTY(int textureCount READ textureCount NOTIFY loaded)

 public:
  explicit GltfModel(QObject *parent = nullptr) : QObject(parent) {}
  ~GltfModel() override;
  QUrl source() const { return source_; }
  void setSource(const QUrl &url);
  QVariantList parts() const { return parts_; }
  QVector3D minimum() const { return lo_; }
  QVector3D maximum() const { return hi_; }
  QString error() const { return error_; }
  bool ready() const { return ready_; }
  int vertexCount() const { return vertices_; }
  int triangleCount() const { return triangles_; }
  int textureCount() const { return textures_; }

 Q_SIGNALS:
  void sourceChanged();
  void loaded();

 private:
  void load();
  void clear();
  QUrl source_;
  QVariantList parts_;
  QList<GltfGeometry *> geometries_;
  QVector3D lo_, hi_;
  QString error_;
  bool ready_ = false;
  int vertices_ = 0, triangles_ = 0, textures_ = 0;
};

}  // namespace harness
