/************************************************************************
**
**  Copyright (C) 2015-2025 Kevin B. Hendricks Stratford, ON, Canada 
**  Copyright (C) 2009-2011 Strahinja Markovic  <strahinja.markovic@gmail.com>
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include <memory>

#include <QFileInfo>
#include <QString>
// #include <QDebug>

#include "BookManipulation/CleanSource.h"
#include "BookManipulation/XhtmlDoc.h"
#include "Misc/Utility.h"
#include "Parsers/GumboInterface.h"
#include "Parsers/HTMLStyleInfo.h"
#include "ResourceObjects/HTMLResource.h"
#include "BookManipulation/FolderKeeper.h"
#include "sigil_exception.h"

static const QString LOADED_CONTENT_MIMETYPE = "application/xhtml+xml";
const QString XML_NAMESPACE_CRUFT = "xmlns=\"http://www.w3.org/1999/xhtml\"";
const QString REPLACE_SPANS = "<span class=\"SigilReplace_\\d*\"( id=\"SigilReplace_\\d*\")*>";

const QString XML_TAG = "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>";

const QStringList RTL_LC = QStringList() << "ar" << "arc" << "dv" << "div" << "fa" <<
                                            "fas" << "per" << "ha" << "hau" << "he" << 
                                            "heb" << "khw" << "ks" << "kas" << "ku" <<
                                            "kur" << "ps" << "pus" << "snd" << "sd" <<
                                            "urd" << "ur" << "yi" << "yid"; 

HTMLResource::HTMLResource(const QString &mainfolder, const QString &fullfilepath, FolderKeeper* Keeper,
                           QObject *parent)
    :
    XMLResource(mainfolder, fullfilepath, parent),
    m_Keeper(Keeper),
    m_LinkedBookPaths(QStringList()),
    m_TOCCache("")
{
}


Resource::ResourceType HTMLResource::Type() const
{
    return Resource::HTMLResourceType;
}

bool HTMLResource::LoadFromDisk()
{
    try {
        const QString &text = Utility::ReadUnicodeTextFile(GetFullPath());
        SetText(text);
        emit LoadedFromDisk();
        return true;
    } catch (CannotOpenFile&) {
        //
    }

    return false;
}

void HTMLResource::SetText(const QString &text)
{
    emit TextChanging();

    XMLResource::SetText(text);

    // Track resources whose change will necessitate an update of the BV and PV.
    // At present this only applies to css files and images.
    TrackNewResources();
}

QString HTMLResource::GetTOCCache()
{
    if (m_TOCCache.isEmpty()) {
        m_TOCCache = TextResource::GetText();
    }
    return m_TOCCache;
}

void HTMLResource::SetTOCCache(const QString & text)
{
    m_TOCCache = text;
}

void HTMLResource::SaveToDisk(bool book_wide_save)
{
    SetText(GetText());
    XMLResource::SaveToDisk(book_wide_save);
}


QStringList HTMLResource::GetLinkedStylesheets()
{
    QStringList hreflist = XhtmlDoc::GetLinkedStylesheets(GetText());
    QString startdir = GetFolder();
    QStringList stylesheet_bookpaths;
    foreach(QString ahref, hreflist) {
        if (ahref.indexOf(":") == -1) {
            std::pair<QString, QString> parts = Utility::parseRelativeHREF(ahref);
            stylesheet_bookpaths << Utility::buildBookPath(parts.first, startdir);
        }
    }
    return stylesheet_bookpaths;
}


QStringList HTMLResource::GetLinkedJavascripts()
{
    QStringList srclist = XhtmlDoc::GetLinkedJavascripts(GetText());
    QString startdir = GetFolder();
    QStringList javascript_bookpaths;
    foreach(QString src, srclist) {
        if (src.indexOf(":") == -1) {
            std::pair<QString, QString> parts = Utility::parseRelativeHREF(src);
            javascript_bookpaths << Utility::buildBookPath(parts.first, startdir);
        }
    }
    return javascript_bookpaths;
}


QStringList HTMLResource::GetManifestProperties() const
{
    QStringList properties;
    QReadLocker locker(&GetLock());
    GumboInterface gi = GumboInterface(GetText(), GetEpubVersion());
    gi.parse();
    QStringList props = gi.get_all_properties();
    props.removeDuplicates();
    if (props.contains("math")) properties.append("mathml");
    if (props.contains("svg")) properties.append("svg");
    // nav as a property should only be used on the nav document and no where else
    // if (props.contains("nav")) properties.append("nav");
    if (props.contains("script")) properties.append("scripted");
    if (props.contains("epub:switch")) properties.append("switch");
    if (props.contains("remote-resources")) properties.append("remote-resources");
    return properties;
}


QStringList HTMLResource::SplitOnSGFSectionMarkers()
{
    QStringList sections = XhtmlDoc::GetSGFSectionSplits(GetText());
    SetText(CleanSource::Mend(sections.takeFirst(),GetEpubVersion()));
    return sections;
}


// returns a list of book paths
QStringList HTMLResource::GetPathsToLinkedResources()
{
    QStringList linked_resources;
    // Can NOT grab Read Lock here as this is also invoked in SetText which has write lock!
    // leading to instant lockup when renaming any resource
    // QReadLocker locker(&GetLock());
    GumboInterface gi = GumboInterface(GetText(),GetEpubVersion());
    gi.parse();
    QList<GumboTag> tags;
    tags << GUMBO_TAG_IMG << GUMBO_TAG_LINK << GUMBO_TAG_AUDIO << GUMBO_TAG_VIDEO;
    const QList<GumboNode*> linked_rsc_nodes = gi.get_all_nodes_with_tags(tags);
    for (int i = 0; i < linked_rsc_nodes.count(); ++i) {
        GumboNode* node = linked_rsc_nodes.at(i);

        // We skip the link elements that are not stylesheets
        if (node->v.element.tag == GUMBO_TAG_LINK) {
            GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, "rel");
            if (attr && (QString::fromUtf8(attr->value) != "stylesheet")) { 
                continue;
            }
        }
        GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, "href");
        if (attr) {
            QString href = QString::fromUtf8(attr->value);
            if (href.indexOf(":") == -1) {
                QUrl target_url(href);
                QString attpath = target_url.path();
                linked_resources.append(Utility::buildBookPath(attpath,GetFolder()));
            }
            continue;
        }
        attr = gumbo_get_attribute(&node->v.element.attributes, "src");
        if (attr) {
            QString href = QString::fromUtf8(attr->value);
            if (href.indexOf(":") == -1) {
                QUrl target_url(href);
                QString attpath = target_url.path();
                linked_resources.append(Utility::buildBookPath(attpath,GetFolder()));
            }
        }
    }
    return linked_resources;
}

QString HTMLResource::GetLanguageAttribute()
{
    GumboInterface gi = GumboInterface(GetText(),GetEpubVersion());
    gi.parse();
    QList<GumboNode*> htmltags = gi.get_all_nodes_with_tag(GUMBO_TAG_HTML);
    if (htmltags.count() != 1) return "";
    GumboNode* node = htmltags.at(0);
    QString lang="";
    GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, "xml:lang");
    if (attr) {
        lang = QString::fromUtf8(attr->value);
        return lang;
    }
    attr = gumbo_get_attribute(&node->v.element.attributes, "lang");
    if (attr) {
        lang = QString::fromUtf8(attr->value);
    }
    return lang;
}

void HTMLResource::SetLanguageAttribute(const QString& langcode)
{
    QString lc(langcode);
    QString version = GetEpubVersion();
    GumboInterface gi = GumboInterface(GetText(),version);
    gi.parse();
    QList<GumboNode*> htmltags = gi.get_all_nodes_with_tag(GUMBO_TAG_HTML);
    if (htmltags.count() != 1) return;
    GumboNode* node = htmltags.at(0);
    if (lc.isEmpty()) {
        // remove any xml:lang or lang attributes on the html node
        GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, "lang");
        GumboElement* element = &node->v.element;
        if (attr) {
            gumbo_element_remove_attribute(element, attr);
        }
        attr = gumbo_get_attribute(&node->v.element.attributes, "xml:lang");
        if (attr) {
            gumbo_element_remove_attribute(element, attr);
        }
        // remove any dir attribute as well
        attr = gumbo_get_attribute(&node->v.element.attributes, "dir");
        if (attr) {
            gumbo_element_remove_attribute(element, attr);
        }
        SetText(gi.getxhtml());
        return;
    }
    // we are adding or changing existing lang xml:lang attributes
    QString sc(lc);
    if (sc.length() > 3) sc=sc.mid(0,2);
    if (version.startsWith("3")) {
        // set the lang attribute (is not valid by spec on epub2 no matter what epubcheck says)
        GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, "lang");
        GumboElement* element = &node->v.element;
        if (attr) {
            // already exists so change its value
            gumbo_attribute_set_value(attr, lc.toUtf8().constData());
        } else {
            // doesn't exist yet so add it
            gumbo_element_set_attribute(element, "lang", lc.toUtf8().constData());
        }
    }
    // set the xml:lang attribute on both epub2 and epub3
    {
        GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, "xml:lang");
        GumboElement* element = &node->v.element;
        if (attr) {
            // already exists so change its value
            gumbo_attribute_set_value(attr, lc.toUtf8().constData());
        } else {
            // doesn't exist yet so add it
            gumbo_element_set_attribute(element, "xml:lang", lc.toUtf8().constData());
        }
    }
    // set the dir attribute only if RTL language code
    if (RTL_LC.contains(sc)){
        GumboAttribute* attr = gumbo_get_attribute(&node->v.element.attributes, "dir");
        GumboElement* element = &node->v.element;
        if (attr) {
            // already exists so change its value
            gumbo_attribute_set_value(attr, "rtl");
        } else {
            // doesn't exist yet so add it
            gumbo_element_set_attribute(element, "dir", "rtl");
        }
    }
    SetText(gi.getxhtml());
}


void HTMLResource::TrackNewResources()
{
    if (!m_LinkedBookPaths.isEmpty()) {
        if (m_Keeper) {
            QList<Resource*> linked_resources = m_Keeper->GetLinkedResources(m_LinkedBookPaths);
            foreach(Resource* resource, linked_resources) {
                disconnect(resource, SIGNAL(ResourceUpdatedOnDisk()),    this, SIGNAL(LinkedResourceUpdated()));
                disconnect(resource, SIGNAL(Deleted(const Resource *)), this, SIGNAL(LinkedResourceUpdated()));
            }
        }
    }
    QStringList bookpaths = GetPathsToLinkedResources();
    if (!bookpaths.isEmpty()) {
        if (m_Keeper) {
            QList<Resource*> linked_resources = m_Keeper->GetLinkedResources(bookpaths);
            foreach(Resource* resource, linked_resources) {
                connect(resource, SIGNAL(ResourceUpdatedOnDisk()),    this, SIGNAL(LinkedResourceUpdated()));
                connect(resource, SIGNAL(Deleted(const Resource *)), this, SIGNAL(LinkedResourceUpdated()));
            }
        }
    }
    m_LinkedBookPaths = bookpaths;
}

bool HTMLResource::DeleteCSStyles(QList<CSSInfo::CSSSelector *> css_selectors)
{
    HTMLStyleInfo htmlcss_info(GetText());
    // Search for selectors with the same definition and line and remove from text
    const QString &new_resource_text = htmlcss_info.removeMatchingSelectors(css_selectors);

    if (!new_resource_text.isNull()) {
        // At least one of the selector(s) was removed.
        SetText(new_resource_text);
        emit Modified();
        return true;
    }

    return false;
}
