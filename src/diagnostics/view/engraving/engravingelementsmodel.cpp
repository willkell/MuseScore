/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "engravingelementsmodel.h"

#include <QTextStream>

#include "engraving/libmscore/engravingobject.h"
#include "engraving/libmscore/score.h"
#include "engraving/libmscore/masterscore.h"
#include "dataformatter.h"

#include "log.h"

using namespace mu::diagnostics;

EngravingElementsModel::EngravingElementsModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex EngravingElementsModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!m_rootItem) {
        return QModelIndex();
    }

    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    Item* parentItem = nullptr;

    if (!parent.isValid()) {
        parentItem = m_rootItem;
    } else {
        parentItem = itemByModelIndex(parent);
    }

    if (!parentItem) {
        return QModelIndex();
    }

    Item* childItem = parentItem->child(row);
    if (childItem) {
        return createIndex(row, column, childItem->key());
    }

    return QModelIndex();
}

QModelIndex EngravingElementsModel::parent(const QModelIndex& child) const
{
    if (!m_rootItem) {
        return QModelIndex();
    }

    if (!child.isValid()) {
        return QModelIndex();
    }

    Item* childItem = itemByModelIndex(child);
    if (!childItem) {
        return QModelIndex();
    }

    Item* parentItem = childItem->parent();
    if (parentItem == m_rootItem) {
        return QModelIndex();
    }

    return createIndex(parentItem->row(), 0, parentItem->key());
}

int EngravingElementsModel::rowCount(const QModelIndex& parent) const
{
    if (!m_rootItem) {
        return 0;
    }

    Item* parentItem = nullptr;

    if (!parent.isValid()) {
        parentItem = m_rootItem;
    } else {
        parentItem = itemByModelIndex(parent);
    }

    if (!parentItem) {
        return 0;
    }

    return parentItem->childCount();
}

int EngravingElementsModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant EngravingElementsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() && role != rItemData) {
        return QVariant();
    }

    Item* item = itemByModelIndex(index);
    if (!item) {
        return QVariant();
    }

    if (item->data().isEmpty()) {
        item->setData(makeData(item->element()));
    }

    return item->data();
}

QHash<int, QByteArray> EngravingElementsModel::roleNames() const
{
    return { { rItemData, "itemData" } };
}

EngravingElementsModel::Item* EngravingElementsModel::createItem(Item* parent)
{
    Item* item = new Item(parent);
    m_allItems.insert(item->key(), item);
    return item;
}

EngravingElementsModel::Item* EngravingElementsModel::itemByModelIndex(const QModelIndex& index) const
{
    return m_allItems.value(index.internalId(), nullptr);
}

QVariantMap EngravingElementsModel::makeData(const Ms::EngravingObject* el) const
{
    TRACEFUNC;
    if (!el) {
        return QVariantMap();
    }

    auto formatRect = [](const mu::RectF& r) {
        QString str = "[";
        str += DataFormatter::formatReal(r.x(), 1) + ", ";
        str += DataFormatter::formatReal(r.y(), 1) + ", ";
        str += DataFormatter::formatReal(r.width(), 1) + ", ";
        str += DataFormatter::formatReal(r.height(), 1) + "]";
        return str;
    };

    auto formatPoint= [](const mu::PointF& p) {
        QString str = "[";
        str += DataFormatter::formatReal(p.x(), 1) + ", ";
        str += DataFormatter::formatReal(p.y(), 1) + "]";
        return str;
    };

    QString name;
    if (el->isScore()) {
        const Ms::Score* score = Ms::toScore(el);
        if (score->isMaster()) {
            name = "MasterScore: " + score->title();
        } else {
            name = "Score: " + score->title();
        }
    } else if (el->isDummy()) {
        if (el->isType(Ms::ElementType::INVALID)) {
            name = "Dummy";
        } else {
            name = QString("Dummy: ") + el->name();
        }
    } else {
        name = el->name();
    }

    QString info = name + ": ";
    info += "children: " + QString::number(el->children().size());
    if (!el->isDummy()) {
        info += ", treechildren: " + QString::number(el->treeChildCount());
    }
    info += "\n";
    if (el->isEngravingItem()) {
        const Ms::EngravingItem* item = Ms::toEngravingItem(el);
        info += "pagePos: " + formatPoint(item->pagePos()) + ", bbox: " + formatRect(item->bbox());
    }

    QVariantMap d;
    d["selected"] = elementsProvider()->isSelected(el);
    d["info"] = info;

    if (el->children().size() != size_t(el->treeChildCount())) {
        d["color"] = "#ff0000";
    }

    return d;
}

void EngravingElementsModel::init()
{
}

void EngravingElementsModel::reload()
{
    TRACEFUNC;

    beginResetModel();

    m_allItems.clear();
    delete m_rootItem;
    m_rootItem = createItem(nullptr);

    const EngravingObjectList& elements = elementsProvider()->elements();
    for (const Ms::EngravingObject* el : elements) {
        if (el == Ms::gpaletteScore) {
            continue;
        }

        if (el->isScore() && el->score() == el->masterScore()) {
            Item* scoreItem = createItem(m_rootItem);
            scoreItem->setElement(el);
            load(elements, scoreItem);
        }
    }

    Item* lostItem = createItem(m_rootItem);
    QVariantMap lostData;
    lostData["info"] = "Lost items";
    lostItem->setData(lostData);
    findAndAddLost(elements, lostItem);

    endResetModel();

    updateInfo();
}

void EngravingElementsModel::load(const EngravingObjectList& elements, Item* root)
{
    TRACEFUNC;
    for (const Ms::EngravingObject* el : elements) {
        if (el == Ms::gpaletteScore) {
            continue;
        }

        Ms::EngravingObject* parent = nullptr;
        if (isUseTreeParent()) {
            parent = el->treeParent();
        } else {
            parent = el->parent(true);
        }

        if (parent == root->element()) {
            Item* item = createItem(root);
            item->setElement(el);
            load(elements, item);
        }
    }
}

const EngravingElementsModel::Item* EngravingElementsModel::findItem(const Ms::EngravingObject* el, const Item* root) const
{
    if (root->element() == el) {
        return root;
    }

    for (int i = 0; i < root->childCount(); ++i) {
        const Item* ch = findItem(el, root->child(i));
        if (ch) {
            return ch;
        }
    }
    return nullptr;
}

void EngravingElementsModel::findAndAddLost(const EngravingObjectList& elements, Item* lossRoot)
{
    TRACEFUNC;

    QSet<const Ms::EngravingObject*> used;
    for (auto it = m_allItems.begin(); it != m_allItems.end(); ++it) {
        const Item* item = it.value();
        used.insert(item->element());
    }

    for (const Ms::EngravingObject* el : elements) {
        if (used.contains(el)) {
            continue;
        }

        Item* item = createItem(lossRoot);
        item->setElement(el);
    }
}

void EngravingElementsModel::select(QModelIndex index, bool arg)
{
    Item* item = itemByModelIndex(index);
    if (!item) {
        return;
    }

    elementsProvider()->select(item->element(), arg);
    item->setData(QVariantMap());

    emit dataChanged(index, index, { rItemData });

    dispatcher()->dispatch("diagnostic-notationview-redraw");
}

void EngravingElementsModel::click1(QModelIndex index)
{
    //! NOTE For debugging purposes
    Item* item = itemByModelIndex(index);
    if (!item) {
        return;
    }

    const Ms::EngravingObject* el = item->element();
    if (!el) {
        return;
    }

    const Ms::EngravingObject* parent = el->parent();
    UNUSED(parent);

    const Ms::EngravingObject* parent2 = el->parent(true);
    UNUSED(parent2);

    const Ms::EngravingObject* treeParent = el->treeParent();
    UNUSED(treeParent);

    size_t children = el->children().size();
    UNUSED(children);

    int treeChildren = el->treeChildCount();
    UNUSED(treeChildren);

    if (parent2 != treeParent) {
        int k = 1;
        UNUSED(k);
    }
}

void EngravingElementsModel::updateInfo()
{
    const EngravingObjectList& elements = elementsProvider()->elements();
    QHash<QString, int> els;
    for (const Ms::EngravingObject* el : elements) {
        els[el->name()] += 1;
    }

    {
        m_info.clear();
        QTextStream stream(&m_info);
        for (auto it = els.constBegin(); it != els.constEnd(); ++it) {
            stream << it.key() << ": " << it.value() << "\n";
        }
    }

    {
        m_summary.clear();
        QTextStream stream(&m_summary);
        stream << "Total: " << elements.size();
    }

    emit infoChanged();
    emit summaryChanged();
}

QString EngravingElementsModel::info() const
{
    return m_info;
}

QString EngravingElementsModel::summary() const
{
    return m_summary;
}

// Item ===============
EngravingElementsModel::Item::Item(Item* parent)
    : m_parent(parent)
{
    if (m_parent) {
        m_parent->addChild(this);
    }
}

EngravingElementsModel::Item::~Item()
{
    for (Item* item : m_children) {
        delete item;
    }
}

int EngravingElementsModel::Item::row() const
{
    if (m_parent) {
        return m_parent->m_children.indexOf(const_cast<Item*>(this));
    }
    return 0;
}

bool EngravingElementsModel::isUseTreeParent() const
{
    return m_isUseTreeParent;
}

void EngravingElementsModel::setIsUseTreeParent(bool arg)
{
    if (m_isUseTreeParent == arg) {
        return;
    }
    m_isUseTreeParent = arg;
    emit isUseTreeParentChanged();

    reload();
}