# -*- coding: utf-8 -*-
"""
media_scanner.py — Scan directories for image and video files.
"""
from __future__ import unicode_literals

import os

import xbmcvfs

IMAGE_EXTS = frozenset([
    'jpg', 'jpeg', 'png', 'gif', 'bmp', 'webp', 'tif', 'tiff',
])

VIDEO_EXTS = frozenset([
    'mp4', 'mkv', 'avi', 'mov', 'wmv', 'flv', 'webm', 'm4v',
    'ts', 'mpg', 'mpeg', 'divx', 'ogv',
])

_ALL_EXTS = IMAGE_EXTS | VIDEO_EXTS


def media_type(filename):
    """Return 'image', 'video', or None based on the file extension."""
    ext = os.path.splitext(filename)[1].lstrip('.').lower()
    if ext in IMAGE_EXTS:
        return 'image'
    if ext in VIDEO_EXTS:
        return 'video'
    return None


def scan_directory(dirpath):
    """
    Return a sorted list of media items found directly inside *dirpath*.

    Each item is a dict::

        {"path": "/absolute/path/to/file.jpg", "name": "file.jpg", "type": "image"}

    Only immediate children are returned (non-recursive).
    """
    items = []
    try:
        dirs, files = xbmcvfs.listdir(dirpath)
    except Exception:
        return items

    for filename in sorted(files):
        mtype = media_type(filename)
        if mtype is None:
            continue
        # Build the full path respecting trailing slash
        sep = '' if dirpath.endswith('/') or dirpath.endswith(os.sep) else '/'
        full_path = dirpath + sep + filename
        items.append({'path': full_path, 'name': filename, 'type': mtype})

    return items


def scan_all_dirs(directories):
    """
    Return a combined, directory-sorted list of media items from all *directories*.

    Items from each directory are grouped together in the order the directories
    were added.
    """
    results = []
    for dirpath in directories:
        results.extend(scan_directory(dirpath))
    return results
