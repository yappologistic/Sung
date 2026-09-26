#pragma once
#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QVector>
#include "motionartwork.h"
#include "roundedart.h"
#include "scrimcontrast.h"

// The current cover filling the view, sharp in the middle and melting at its
// edges into a blurred copy of itself. The cover is the animated one while
// there is one and the still one otherwise.
//
// Everything is drawn as two textured rectangles, so the software renderer
// draws it too. The frame goes up as it is. The edge is a small picture: the
// part of the frame on screen, shrunk to about 128 pixels, blurred, and given
// an alpha that is opaque at the view's border and clear inside it. Enlarged
// over the frame, it is the blur and the feather at once, and building it
// costs a few hundred microseconds rather than a pass over every pixel.
class MotionBackdrop : public QQuickItem {
  Q_OBJECT
  friend class ArtworkTest;
  Q_PROPERTY(MotionArtwork *animation READ animation WRITE setAnimation NOTIFY animationChanged)
  // What to draw without an animation. A hidden RoundedArt, so the still
  // arrives through the same cache, network and video-frame lookup as every
  // other cover in the application.
  Q_PROPERTY(RoundedArt *still READ still WRITE setStill NOTIFY stillChanged)
  // How far the crossfade from the previous picture has got. Written from
  // QML, so the fade runs on the theme's spring; reaching 1 lets the previous
  // picture go.
  Q_PROPERTY(qreal mix READ mix WRITE setMix NOTIFY mixChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY sceneChanged)
  Q_PROPERTY(bool moving READ moving NOTIFY sceneChanged)
  // Text sits in a band at the top and one at the bottom. Each band's scrim
  // is solved from the picture under it: the smallest cover of `surface`,
  // at least `base`, that holds `contrast` for `ink` everywhere in the band.
  Q_PROPERTY(QColor surface MEMBER m_surface WRITE setSurface NOTIFY scrimInputsChanged)
  Q_PROPERTY(QColor ink MEMBER m_ink WRITE setInk NOTIFY scrimInputsChanged)
  Q_PROPERTY(qreal contrast MEMBER m_contrast WRITE setContrast NOTIFY scrimInputsChanged)
  Q_PROPERTY(qreal base MEMBER m_base WRITE setBase NOTIFY scrimInputsChanged)
  Q_PROPERTY(qreal topBand MEMBER m_topBand WRITE setTopBand NOTIFY scrimInputsChanged)
  Q_PROPERTY(qreal bottomBand MEMBER m_bottomBand WRITE setBottomBand NOTIFY scrimInputsChanged)
  Q_PROPERTY(qreal topScrim READ topScrim NOTIFY scrimChanged)
  Q_PROPERTY(qreal bottomScrim READ bottomScrim NOTIFY scrimChanged)
public:
  explicit MotionBackdrop(QQuickItem *parent = nullptr);
  MotionArtwork *animation() const { return m_animation; }
  void setAnimation(MotionArtwork *animation);
  RoundedArt *still() const { return m_still; }
  void setStill(RoundedArt *still);
  qreal mix() const { return m_mix; }
  void setMix(qreal mix);
  bool ready() const { return !m_current.frame.isNull(); }
  bool moving() const { return m_current.moving; }
  qreal topScrim() const { return m_topScrim; }
  qreal bottomScrim() const { return m_bottomScrim; }
  void setSurface(const QColor &value) { if (value != m_surface) { m_surface = value; scrimInputChanged(); } }
  void setInk(const QColor &value) { if (value != m_ink) { m_ink = value; scrimInputChanged(); } }
  void setContrast(qreal value) { if (value != m_contrast) { m_contrast = value; scrimInputChanged(); } }
  void setBase(qreal value) { if (value != m_base) { m_base = value; scrimInputChanged(); } }
  void setTopBand(qreal value) { if (value != m_topBand) { m_topBand = value; rebuildEdge(); scrimInputChanged(); } }
  void setBottomBand(qreal value) { if (value != m_bottomBand) { m_bottomBand = value; rebuildEdge(); scrimInputChanged(); } }
signals:
  void animationChanged();
  void stillChanged();
  void mixChanged();
  // A different picture has taken over, and the one before it is waiting on
  // `mix` to fade it out.
  void sceneChanged();
  void scrimInputsChanged();
  void scrimChanged();

protected:
  QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
  void geometryChange(const QRectF &current, const QRectF &previous) override;

private:
  struct Scene {
    QString key;
    bool moving = false;
    QImage frame, edge;
    QVector<scrimcontrast::Sample> top, bottom;
    // The most scrim any frame of this scene has asked for. A loop that
    // brightens halfway keeps the scrim it needed there, so the words do
    // not dim and brighten with every pass of the video.
    qreal topNeed = 0, bottomNeed = 0;
  };
  void refresh();
  void rebuildEdge();
  void scrimInputChanged();
  void solveScrims();
  QPointer<MotionArtwork> m_animation;
  QPointer<RoundedArt> m_still;
  Scene m_current, m_previous;
  int m_frames = 0;
  qreal m_mix = 1;
  QColor m_surface = Qt::black, m_ink = Qt::white;
  qreal m_contrast = 4.5, m_base = 0, m_topBand = 0, m_bottomBand = 0;
  qreal m_topScrim = 0, m_bottomScrim = 0;
};
