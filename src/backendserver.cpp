#include "backend.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <algorithm>

void Backend::setupServer() {
  auto lastTrack = std::make_shared<QVariantMap>();
  auto lastPosition = std::make_shared<qint64>(0);
  auto progressTick = std::make_shared<qint64>(-1);
  connect(this, &Backend::positionChanged, this, [this,lastTrack,lastPosition,progressTick] {
    if(m_media.playbackState()!=QMediaPlayer::StoppedState)*lastPosition=position();
    const auto tick=position()/10000;
    if(!historyPaused() && tick!=*progressTick && playing()) {
      *progressTick=tick;m_server.reportPlayback(current(),position(),false,false);
    }
  });
  connect(this, &Backend::playbackChanged, this, [this,lastPosition] {
    if(!historyPaused())m_server.reportPlayback(current(),m_media.playbackState()==QMediaPlayer::StoppedState?*lastPosition:position(),!playing(),!playing()&&m_media.playbackState()==QMediaPlayer::StoppedState);
  });
  connect(this, &Backend::seeked, this, [this](qint64 pos) {
    if(!historyPaused())m_server.reportPlayback(current(),pos,!playing(),false);
  });
  auto privateMode=std::make_shared<bool>(historyPaused());
  connect(this,&Backend::settingsChanged,this,[this,privateMode] {
    if(*privateMode==historyPaused())return;
    *privateMode=historyPaused();
    m_server.reportPlayback(current(),position(),!playing(),historyPaused());
  });
  connect(this, &Backend::trackChanged, this, [this,lastTrack,lastPosition,progressTick] {
    if(!historyPaused() && !lastTrack->isEmpty())m_server.reportPlayback(*lastTrack,*lastPosition,true,true);
    *lastTrack=current();*lastPosition=0;*progressTick=-1;
    m_server.cancel("cover");
    m_serverArtwork.clear();
    m_serverArtDirectory.reset();
    if (!m_server.owns(current()))
      return;
    const auto root =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(root);
    auto dir = std::make_shared<QTemporaryDir>(root + "/desktop-art-XXXXXX");
    if (!dir->isValid())
      return;
    m_serverArtDirectory = dir;
    const auto token = m_trackToken;
    m_server.cover(
        current(), dir->path() + "/cover.png",
        [this, token, dir](const QVariantMap &d, const QString &) {
          if (token != m_trackToken)
            return;
          m_serverArtwork =
              QUrl::fromLocalFile(d.value("file").toString()).toString();
          emit playbackChanged();
        });
  });

  connect(&m_server, &MusicServer::changed, this, &Backend::libraryChanged);
  connect(&m_server, &MusicServer::message, this, &Backend::toast);
  connect(&m_server, &MusicServer::accountChanged, this, [this] {
    if (isServerSource(current().value("source")))
      stop();
    m_serverArtwork.clear();
    m_serverArtDirectory.reset();
    m_serverListenTimer.stop();
    m_serverListenToken = 0;
    for (int i = m_back.size() - 1; i >= 0; --i)
      if (m_back[i].toMap().value("page") == "server")
        m_back.removeAt(i);
    if (m_page == "server") {
      m_busy = false;
      m_results.assign({});
      m_more = false;
      emit catalogChanged();
    }
  });
  m_serverListenTimer.setInterval(1000);
  connect(this, &Backend::playbackChanged, this, [this] {
    const bool active =
        playing() && m_server.owns(current()) && !historyPaused();
    if (active) {
      if (m_serverListenToken != m_trackToken) {
        m_serverListenToken = m_trackToken;
        m_serverListened = 0;
        m_serverSubmitted = false;
        m_serverStarted = QDateTime::currentMSecsSinceEpoch();
        m_server.scrobble(current(), false, m_serverStarted,
                          [](const QVariantMap &, const QString &) {});
      }
      if (!m_serverListenTimer.isActive()) {
        m_serverElapsed.start();
        m_serverListenTimer.start();
      }
    } else if (m_serverListenTimer.isActive()) {
      m_serverListened += m_serverElapsed.elapsed();
      m_serverListenTimer.stop();
    }
  });
  connect(&m_serverListenTimer, &QTimer::timeout, this, [this] {
    if (!playing() || historyPaused() || !m_server.owns(current())) {
      m_serverListenTimer.stop();
      return;
    }
    m_serverListened += m_serverElapsed.restart();
    // Count listening time, not seek position. Pauses and seeking cannot fake a
    // play.
    const auto threshold =
        qMin<qint64>(240000, qMax<qint64>(1000, duration() / 2));
    if (!m_serverSubmitted && duration() > 0 && m_serverListened >= threshold) {
      m_serverSubmitted = true;
      m_server.scrobble(
          current(), true, m_serverStarted,
          [this](const QVariantMap &, const QString &error) {
            if (!error.isEmpty())
              emit toast("Could not update the server listening history.");
          });
    }
  });
}
void Backend::browseServer(const QString &mode, const QString &query,
                           const QString &filter) {
  serverBrowseRequest({{"mode", mode},
                       {"query", query.trimmed()},
                       {"filter", filter},
                       {"offset", 0}});
}
void Backend::serverBrowseRequest(QVariantMap req, bool push, bool append) {
  const auto key=QString("server:%1:%2:%3:%4:%5").arg(m_server.address(),req.value("mode").toString(),req.value("remoteId").toString(),req.value("query").toString(),req.value("filter").toString());
  if (push || m_page != "server")
    navigate("server", "Music server", push,key);
  else {beginView(key);cancel("catalog");}
  m_request = req;
  m_busy = true;
  dismissError();
  const auto mode = req.value("mode", "albums").toString();
  m_title = req.value("title", mode == "search"      ? "Search server"
                               : mode == "favorites" ? "Server favorites"
                               : mode == "random"    ? "Discover"
                               : mode == "playlists" ? "Server playlists"
                               : mode == "artists"   ? "Artists"
                               : mode == "genres"    ? "Genres"
                                                     : "Albums")
                .toString();
  m_cover = req.value("art").toString();
  if (!append)
    m_results.assign({});
  emit catalogChanged();
  m_server.browse(
      req, [this, append](const QVariantMap &data, const QString &error) {
        m_busy = false;
        if (!error.isEmpty()) {
          notifyError(error, "catalog");
          emit catalogChanged();
          return;
        }
        if (append)
          m_results.append(data.value("items").toList());
        else
          m_results.assign(data.value("items").toList());
        if (data.contains("title"))
          m_title = data.value("title").toString();
        if (data.contains("editable"))
          m_request["editable"] = data.value("editable");
        if(m_request.value("mode")=="album"){m_request["artist"]=data.value("artist");m_request["year"]=data.value("year");}
        m_more = data.value("more").toBool();
        emit catalogChanged();
      });
}
void Backend::addServerPlaylist(const QString &remoteId,
                                const QVariantList &songs) {
  if (songs.isEmpty())
    return;
  Subsonic::Params p;
  for (const auto &v : songs) {
    const auto song = v.toMap();
    if (!m_server.owns(song) || song.value("kind") != "song") {
      notifyError("Server playlists accept songs from the connected server. "
                  "Use a local playlist to mix sources.");
      return;
    }
    p.append(QPair<QString, QString>{"songIdToAdd",
                                     song.value("remoteId").toString()});
  }
  if (p.size() > 5000) {
    notifyError("Add up to 5000 songs at a time to a server playlist.");
    return;
  }
  m_server.editPlaylist(
      remoteId, p, [this, remoteId](const QVariantMap &, const QString &e) {
        if (!e.isEmpty())
          notifyError(e);
        else {
          emit toast("Added to server playlist");
          if (m_page == "server" && m_request.value("remoteId") == remoteId)
            refresh();
        }
      });
}
void Backend::renameServerPlaylist(const QVariantMap &playlist,
                                   const QString &name) {
  if (!m_server.owns(playlist) || name.trimmed().isEmpty())
    return;
  const auto id=playlist.value("remoteId").toString();
  const auto title=name.trimmed().left(120);
  m_server.editPlaylist(id,{{"name",title}},[this,id,title](const QVariantMap &,const QString &e) {
    if(!e.isEmpty())notifyError(e);
    else if(m_page=="server") {
      if(m_request.value("mode")=="playlist" && m_request.value("remoteId")==id) {
        m_title=title;m_request["title"]=title;emit catalogChanged();
      } else refresh();
    }
  });
}
void Backend::deleteServerPlaylist(const QVariantMap &playlist) {
  if (!m_server.owns(playlist))
    return;
  m_server.removePlaylist(playlist.value("remoteId").toString(),
                          [this](const QVariantMap &, const QString &e) {
                            if (!e.isEmpty())
                              notifyError(e);
                            else
                              browseServer("playlists");
                          });
}
void Backend::removeServerRows(const QVariantList &indices) {
  if (!serverPlaylistEditable() || indices.isEmpty())
    return;
  if(m_server.provider()=="jellyfin") {
    const auto original=m_results.rows;const auto request=m_request;
    m_server.browse(request,[this,original,indices,request](const QVariantMap &d,const QString &e){
      if(!e.isEmpty()){notifyError(e);return;}
      if(d.value("items").toList()!=original){notifyError("The server playlist changed. Refresh it before removing songs.");return;}
      Subsonic::Params params{{"playlistId",request.value("remoteId").toString()}};
      for(int i=0;i<original.size();++i)if(!indices.contains(i))params.append(QPair<QString,QString>{"songId",original[i].toMap().value("remoteId").toString()});
      m_server.call("createPlaylist",params,[this,request](const QVariantMap &,const QString &error){if(!error.isEmpty())notifyError(error);else if(m_page=="server"&&m_request.value("remoteId")==request.value("remoteId"))refresh();});
    },"playlist-edit");return;
  }
  QList<int> rows;
  for (const auto &v : indices)
    if (v.toInt() >= 0 && v.toInt() < m_results.count() &&
        !rows.contains(v.toInt()))
      rows.append(v.toInt());
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  Subsonic::Params p;
  for (int i : rows)
    p.append(QPair<QString, QString>{"songIndexToRemove", QString::number(i)});
  const auto id = m_request.value("remoteId").toString();
  m_server.editPlaylist(
      id, p, [this, id](const QVariantMap &, const QString &e) {
        if (!e.isEmpty())
          notifyError(e);
        else if (m_page == "server" && m_request.value("remoteId") == id)
          refresh();
      });
}
void Backend::moveServerRows(const QVariantList &indices, int before) {
  if (!serverPlaylistEditable() || indices.isEmpty())
    return;
  // Refresh before reordering, and reject concurrent edits instead of
  // overwriting them.
  const auto original = m_results.rows;
  const auto id = m_request.value("remoteId").toString();
  const auto req = m_request;
  m_server.browse(req, [this, original, id, indices,
                        before](const QVariantMap &d, const QString &e) {
    if (!e.isEmpty()) {
      notifyError(e);
      return;
    }
    const auto fresh = d.value("items").toList();
    if (fresh != original) {
      notifyError("The server playlist changed. Refresh it before reordering.");
      return;
    }
    QList<int> selected;
    for (const auto &v : indices)
      if (v.toInt() >= 0 && v.toInt() < fresh.size() &&
          !selected.contains(v.toInt()))
        selected.append(v.toInt());
    std::sort(selected.begin(), selected.end());
    auto remaining = fresh;
    QVariantList moving;
    int target = qBound(0, before, int(fresh.size()));
    for (int i : selected) {
      moving.append(fresh[i]);
      if (i < before)
        --target;
    }
    for (auto i = selected.crbegin(); i != selected.crend(); ++i)
      remaining.removeAt(*i);
    for (int i = 0; i < moving.size(); ++i)
      remaining.insert(target + i, moving[i]);
    if (remaining == fresh)
      return;
    if (remaining.size() > 5000) {
      notifyError(
          "Reordering server playlists is limited to 5000 songs per request.");
      return;
    }
    Subsonic::Params p{{"playlistId", id}};
    for (const auto &v : remaining)
      p.append(QPair<QString, QString>{"songId",
                                       v.toMap().value("remoteId").toString()});
    m_server.call("createPlaylist", p,
                  [this, id](const QVariantMap &, const QString &error) {
                    if (!error.isEmpty())
                      notifyError(error);
                    else if (m_page == "server" &&
                             m_request.value("remoteId") == id)
                      refresh();
                  });
  });
}
void Backend::rateServerSong(const QVariantMap &song, int rating) {
  m_server.rate(song, rating,
                [this, song, rating](const QVariantMap &, const QString &e) {
                  if (!e.isEmpty()) {
                    notifyError(e);
                    return;
                  }
                  auto update = [&](Entries &entries) {
                    auto rows = entries.rows;
                    bool changed = false;
                    for (auto &v : rows) {
                      auto row = v.toMap();
                      if (row.value("id") == song.value("id")) {
                        row["rating"] = rating;
                        v = row;
                        changed = true;
                      }
                    }
                    if (changed)
                      entries.assign(rows);
                  };
                  update(m_results);
                  update(m_queue);
                  emit toast(rating ? "Rating saved" : "Rating removed");
                });
}
void Backend::saveServerQueue() {
  Subsonic::Params p;
  for (const auto &v : m_queue.rows) {
    auto t = v.toMap();
    if (!m_server.owns(t)) {
      notifyError(
          "The server queue can only contain songs from the connected server.");
      return;
    }
    p.append(QPair<QString, QString>{"id", t.value("remoteId").toString()});
  }
  if (p.size() > 5000) {
    notifyError("Save up to 5000 songs in the server queue.");
    return;
  }
  if (!current().isEmpty()) {
    p.append(QPair<QString, QString>{"current",
                                     current().value("remoteId").toString()});
    p.append(QPair<QString, QString>{"position", QString::number(position())});
  }
  m_server.call("savePlayQueue", p,
                [this](const QVariantMap &, const QString &e) {
                  if (!e.isEmpty())
                    notifyError(e);
                  else
                    emit toast("Queue saved to server");
                });
}
void Backend::restoreServerQueue() {
  m_server.call(
      "getPlayQueue", {}, [this](const QVariantMap &d, const QString &e) {
        if (!e.isEmpty()) {
          notifyError(e);
          return;
        }
        const auto q = d.value("playQueue").toMap();
        QVariantList rows;
        int index = 0;
        for (const auto &v : q.value("entry").toList()) {
          const auto song = m_server.item(v.toMap(), "song");
          if (song.value("remoteId") == q.value("current"))
            index = rows.size();
          rows.append(song);
        }
        if (rows.isEmpty()) {
          emit toast("The server queue is empty");
          return;
        }
        stop();
        m_queue.assign(rows);
        m_index = index;
        m_savedPosition = qMax<qint64>(0, q.value("position").toLongLong());
        ++m_trackToken;
        clearLyrics();
        emit trackChanged();
        emit libraryChanged();
        emit positionChanged();
        m_saveTimer.start();
        emit toast("Server queue restored");
      });
}
