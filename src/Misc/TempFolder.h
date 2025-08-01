/************************************************************************
**
**  Copyright (C) 2011-2025 Kevin B. Hendricks, Stratford, Ontario Canada
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

#pragma once
#ifndef TEMPFOLDER_H
#define TEMPFOLDER_H

#include <QtCore/QString>
#include <QtCore/QTemporaryDir>

/**
 * A RAII wrapper around a temp folder. Creating an object of this
 * class creates a temp folder on the disk. When the object is deleted,
 * so is the folder and everything in it.
 */
class TempFolder
{

public:

    /**
     * Constructors.
     */
    TempFolder();

    TempFolder(const QString base_path);

    /**
     * Destructor. Deletes the temp folder on disk
     * and all the files in it. The deletion is performed
     * asynchronously in a background thread.
     */
    ~TempFolder();

    /**
     * Returns the full path to the temp folder. No trailing slash
     * is provided.
     *
     * @return The full path to the temp folder.
     */
    QString GetPath();

    /**
     * Returns the full path to the Sigil scratchpad folder.
     * This is the folder where other temp folders are created.
     *
     * @return Full path to the scratchpad.
     */
    static QString GetPathToSigilScratchpad();

private:

    // We turn these of since TempFolder is an identity class.
    TempFolder &operator= (const TempFolder &);
    TempFolder(const TempFolder &);

    /**
     * Provides the template for QTemporaryDir for new temporary
     * folders.
     *
     * @return Absolute path to a new temp folder template.
     */
    static QString GetNewTempFolderTemplate();
    static QString GetNewTempFolderTemplateFromBasePath(const QString base_path);

    /**
     * Deletes the folder specified and all the files
     * (and folders, recursively) in it.
     *
     * @param fullfolderpath The full path to the folder to delete.
     * @return \c true if the operation is successful.
     */
    static bool DeleteFolderAndFiles(const QString &fullfolderpath);


    ///////////////////////////////
    // PRIVATE MEMBER VARIABLES
    ///////////////////////////////

    /**
     * The full path to the temp folder.
     */
    QTemporaryDir m_tempDir;
};


#endif // TEMPFOLDER_H
