#pragma once
#include <QSortFilterProxyModel>
#include <QCollator>
#include <QVariantMap>

// A view of a collection: filtering and sorting never change saved playlist order.
class CollectionView : public QSortFilterProxyModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY optionsChanged)
  Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY optionsChanged)
public:
  explicit CollectionView(QObject *parent=nullptr):QSortFilterProxyModel(parent) {
    if(m_collator.locale().language()==QLocale::C)m_collator.setLocale(QLocale(QLocale::English));
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);m_collator.setNumericMode(true);
    connect(this,&QAbstractItemModel::modelReset,this,&CollectionView::countChanged);
    connect(this,&QAbstractItemModel::rowsInserted,this,&CollectionView::countChanged);
    connect(this,&QAbstractItemModel::rowsRemoved,this,&CollectionView::countChanged);
    connect(this,&QAbstractItemModel::layoutChanged,this,&CollectionView::countChanged);
  }
  int count() const {return rowCount();}
  QString query() const {return m_query;}
  QString sortKey() const {return m_sort;}
  void setQuery(const QString &value) {
    if(m_query==value)return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_query=value;m_terms=value.simplified().split(' ',Qt::SkipEmptyParts);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
    emit optionsChanged();
  }
  void setSortKey(const QString &value) {
    if(!QStringList{"original","title","artist","duration"}.contains(value)||m_sort==value)return;
    m_sort=value;invalidate();sort(value=="original"?-1:0);emit optionsChanged();
  }
  Q_INVOKABLE QVariantMap get(int row) const {return row>=0&&row<count()?data(index(row,0),Qt::UserRole).toMap():QVariantMap{};}
  Q_INVOKABLE int sourceIndex(int row) const {return row>=0&&row<count()?mapToSource(index(row,0)).row():-1;}
  QVariantList items() const {QVariantList result;result.reserve(count());for(int i=0;i<count();++i)result.append(get(i));return result;}
  static qint64 seconds(const QVariantMap &item) {
    if(item.value("seconds").toLongLong()>0)return item.value("seconds").toLongLong();
    qint64 result=0;const auto parts=item.value("duration").toString().split(':');
    if(parts.size()>3)return 0;
    for(const auto &p:parts){bool ok=false;const auto n=p.toInt(&ok);if(!ok||n<0)return 0;result=result*60+n;}return result;
  }
signals:
  void countChanged();
  void optionsChanged();
protected:
  bool filterAcceptsRow(int row,const QModelIndex &parent) const override {
    if(m_terms.isEmpty())return true;
    const auto item=sourceModel()->data(sourceModel()->index(row,0,parent),Qt::UserRole).toMap();
    const auto text=item.value("title").toString()+' '+item.value("artist").toString()+' '+item.value("album").toString();
    for(const auto &term:m_terms) {if(!text.contains(term,Qt::CaseInsensitive))return false;}
    return true;
  }
  bool lessThan(const QModelIndex &a,const QModelIndex &b) const override {
    const auto x=sourceModel()->data(a,Qt::UserRole).toMap(),y=sourceModel()->data(b,Qt::UserRole).toMap();
    if(m_sort=="duration") {const auto xs=seconds(x),ys=seconds(y);if(xs!=ys)return xs<ys;}
    else {const int cmp=m_collator.compare(x.value(m_sort).toString(),y.value(m_sort).toString());if(cmp)return cmp<0;}
    return a.row()<b.row();
  }
private:
  QString m_query,m_sort="original";
  QStringList m_terms;
  QCollator m_collator;
};
