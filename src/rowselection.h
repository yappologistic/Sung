#pragma once
#include <QAbstractItemModel>
#include <QSet>
#include <QVariantList>
#include <algorithm>

// Selection belongs to a displayed model, never to recycled delegates.
class RowSelection : public QObject {
  Q_OBJECT
  Q_PROPERTY(QAbstractItemModel* model READ model WRITE setModel NOTIFY modelChanged)
  Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
  using QObject::QObject;
  QAbstractItemModel *model() const {return m_model;}
  void setModel(QAbstractItemModel *model) {
    if(m_model==model)return;
    if(m_model)disconnect(m_model,nullptr,this,nullptr);
    clear();m_model=model;
    if(model){
      connect(model,&QAbstractItemModel::modelReset,this,&RowSelection::clear);
      connect(model,&QAbstractItemModel::layoutChanged,this,&RowSelection::clear);
      connect(model,&QAbstractItemModel::rowsRemoved,this,&RowSelection::clear);
      connect(model,&QAbstractItemModel::rowsInserted,this,&RowSelection::clear);
      connect(model,&QObject::destroyed,this,[this]{m_model=nullptr;clear();emit modelChanged();});
    }
    emit modelChanged();
  }
  int count() const {return m_rows.size();}
  int revision() const {return m_revision;}
  QVariantList rows() const {auto sorted=m_rows.values();std::sort(sorted.begin(),sorted.end());QVariantList result;for(int i:sorted)result.append(i);return result;}
  Q_INVOKABLE bool contains(int i) const {return m_rows.contains(i);}
  Q_INVOKABLE void clear() {m_anchor=-1;if(m_rows.isEmpty())return;m_rows.clear();announce();}
  Q_INVOKABLE void select(int row,int modifiers=0) {
    if(!eligible(row))return;
    if((modifiers&Qt::ShiftModifier)&&m_anchor>=0){
      if(!(modifiers&Qt::ControlModifier))m_rows.clear();
      for(int i=qMin(row,m_anchor);i<=qMax(row,m_anchor);++i)if(eligible(i))m_rows.insert(i);
    }else if(modifiers&Qt::ControlModifier){if(m_rows.contains(row))m_rows.remove(row);else m_rows.insert(row);m_anchor=row;}
    else {m_rows={row};m_anchor=row;}
    announce();
  }
  Q_INVOKABLE void selectAll() {if(!m_model)return;m_rows.clear();for(int i=0;i<m_model->rowCount();++i)if(eligible(i))m_rows.insert(i);m_anchor=0;announce();}
  Q_INVOKABLE QVariantList items() const {QVariantList result;if(m_model)for(const auto &v:rows())result.append(m_model->data(m_model->index(v.toInt(),0),Qt::UserRole));return result;}
  Q_INVOKABLE bool eligible(int row) const {if(!m_model||row<0||row>=m_model->rowCount())return false;auto item=m_model->data(m_model->index(row,0),Qt::UserRole).toMap();return (!item.value("videoId").toString().isEmpty()||!item.value("localPath").toString().isEmpty())&&item.value("available",true).toBool();}
signals:
  void changed();
  void modelChanged();
private:
  QAbstractItemModel *m_model=nullptr;
  QSet<int> m_rows;
  int m_anchor=-1, m_revision=0;
  void announce(){++m_revision;emit changed();}
};
