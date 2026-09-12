#include "backend.h"
#include <algorithm>

void Backend::setCompactDensity(bool value){if(value==compactDensity())return;m_settings.setValue("compactDensity",value);emit presentationChanged();}
void Backend::setStartPage(const QString &value){if(!QStringList{"home","files","server","favorites"}.contains(value)||value==startPage())return;m_settings.setValue("startPage",value);emit presentationChanged();}
void Backend::openStartPage(){const auto page=startPage();if(page=="files"||page=="favorites"||page=="server")library(page);else home();}
QVariantList Backend::homeSections(bool includeHidden) const {
  if(m_page!="home")return m_sections;
  auto sections=m_sections;const auto pinned=pins();
  if(!pinned.isEmpty())sections.prepend(QVariantMap{{"title","Pinned"},{"items",pinned}});
  const auto order=homeOrder(),hidden=hiddenHomeSections();
  std::stable_sort(sections.begin(),sections.end(),[&](const QVariant &a,const QVariant &b){
    const auto rank=[&](const QVariant &v){const int i=order.indexOf(v.toMap().value("title").toString());return i<0?order.size():i;};
    return rank(a)<rank(b);
  });
  QVariantList result;for(const auto &v:sections){auto section=v.toMap();const bool shown=!hidden.contains(section.value("title").toString());if(includeHidden||shown){section["shown"]=shown;result<<section;}}
  return result;
}
void Backend::moveHomeSection(const QString &title,int offset){
  if(m_page!="home" || (offset!=-1&&offset!=1))return;
  QStringList order;for(const auto &v:homeSections(true)){const auto key=v.toMap().value("title").toString();if(!order.contains(key))order<<key;}
  const int from=order.indexOf(title),to=from+offset;if(from<0||to<0||to>=order.size())return;
  order.swapItemsAt(from,to);for(const auto &key:homeOrder())if(!order.contains(key)&&order.size()<64)order<<key;
  m_settings.setValue("homeOrder",order.mid(0,64));emit presentationChanged();
}
void Backend::showHomeSection(const QString &title,bool show){
  if(m_page!="home")return;
  bool found=false;for(const auto &v:homeSections(true))if(v.toMap().value("title")==title)found=true;
  if(!found)return;
  auto hidden=hiddenHomeSections();if(show)hidden.removeAll(title);else if(!hidden.contains(title)&&hidden.size()<64)hidden<<title;
  m_settings.setValue("hiddenHomeSections",hidden);emit presentationChanged();
}
void Backend::resetHomeLayout(){m_settings.remove("homeOrder");m_settings.remove("hiddenHomeSections");emit presentationChanged();}

QVariantList Backend::queueWithOrigin(const QVariantList &items,const QString &origin){
  QVariantList result;result.reserve(items.size());for(const auto &v:items){auto item=v.toMap();item["_queueOrigin"]=origin;result<<item;}return result;
}
int Backend::viewDensity() const {return qBound(-1,m_settings.value("viewLayouts").toMap().value(m_viewKey).toMap().value("density",-1).toInt(),1);}
void Backend::setViewDensity(int value){
  if(value < -1 || value > 1 || value==viewDensity())return;
  auto layouts=m_settings.value("viewLayouts").toMap(),options=layouts.value(m_viewKey).toMap();options["density"]=value;layouts[m_viewKey]=options;
  while(layouts.size()>64){auto it=layouts.begin();if(it.key()==m_viewKey)++it;layouts.erase(it);}m_settings.setValue("viewLayouts",layouts);emit presentationChanged();
}
bool Backend::viewSupportsGrid() const {return m_page=="library" && (m_libraryId=="local-albums" || m_libraryId=="local-artists" || m_libraryId=="playlists");}
QString Backend::viewMode() const {
  if(!viewSupportsGrid())return "list";
  const auto value=m_settings.value("viewLayouts").toMap().value(m_viewKey).toMap().value("mode").toString();
  return value=="grid"||value=="list"?value:m_libraryId=="playlists"?"list":"grid";
}
void Backend::setViewMode(const QString &value){
  if(!viewSupportsGrid() || (value!="grid"&&value!="list") || value==viewMode())return;
  auto layouts=m_settings.value("viewLayouts").toMap(),options=layouts.value(m_viewKey).toMap();options["mode"]=value;layouts[m_viewKey]=options;
  while(layouts.size()>64){auto it=layouts.begin();if(it.key()==m_viewKey)++it;layouts.erase(it);}m_settings.setValue("viewLayouts",layouts);emit presentationChanged();
}
int Backend::lyricGapSeconds() const {
  const qint64 now=position()+lyricOffset();int low=0,high=m_lyricLines.size();
  while(low<high){const int mid=(low+high)/2;if(m_lyricLines[mid].toMap().value("start").toLongLong()<=now)low=mid+1;else high=mid;}
  qint64 beginning=0;
  if(low>0){const auto line=m_lyricLines[low-1].toMap();beginning=line.value("text").toString().trimmed().isEmpty()?line.value("start").toLongLong():line.value("end").toLongLong();if((beginning<=0 && !line.value("text").toString().trimmed().isEmpty()) || beginning>now)return 0;}
  while(low<m_lyricLines.size() && m_lyricLines[low].toMap().value("text").toString().trimmed().isEmpty())++low;
  if(low==m_lyricLines.size())return 0;
  const auto next=m_lyricLines[low].toMap().value("start").toLongLong();
  return next-beginning>=5000 && next>now ? int((next-now+999)/1000) : 0;
}
