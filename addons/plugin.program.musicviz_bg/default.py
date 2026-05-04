# -*- coding: utf-8 -*-
"""
default.py — Music Viz Background Manager main entry point.

A Kodi program plugin that lets users browse directories of images and videos,
preview them, and set any file as the custom background for the
MusicVisualisation window in the Estouchy skin.

URL routes
----------
plugin://plugin.program.musicviz_bg/
    Root menu.

plugin://plugin.program.musicviz_bg/?action=manage_dirs
    List configured directories; add / remove entries.

plugin://plugin.program.musicviz_bg/?action=add_dir
    Open the folder-browser dialog and add the chosen directory.

plugin://plugin.program.musicviz_bg/?action=remove_dir&dir=<encoded>
    Remove a directory from the list (called via RunPlugin context item).

plugin://plugin.program.musicviz_bg/?action=browse
    Browse media files in *all* configured directories.

plugin://plugin.program.musicviz_bg/?action=browse&dir=<encoded>
    Browse media files in a single directory.

plugin://plugin.program.musicviz_bg/?action=preview&path=<encoded>&type=<t>
    Preview a file: fullscreen for images, play for videos.

plugin://plugin.program.musicviz_bg/?action=set_bg&path=<encoded>&type=<t>
    Set the file as the MusicVisualisation background.

plugin://plugin.program.musicviz_bg/?action=clear_bg
    Clear the custom background and restore the default artist fanart.

License: GPL-2.0-or-later
"""
from __future__ import unicode_literals

import os
import sys

try:
    from urllib import urlencode
    from urlparse import parse_qsl
except ImportError:
    from urllib.parse import urlencode, parse_qsl

import xbmc
import xbmcgui
import xbmcplugin
import xbmcaddon
import xbmcvfs


def _translate_path(path):
    """Translate a Kodi virtual path, compatible with Leia and Matrix+."""
    try:
        return xbmcvfs.translatePath(path)
    except AttributeError:
        return xbmc.translatePath(path)


# ---------------------------------------------------------------------------
# Globals
# ---------------------------------------------------------------------------

ADDON = xbmcaddon.Addon()
ADDON_ID = ADDON.getAddonInfo('id')
HANDLE = int(sys.argv[1])
BASE_URL = sys.argv[0]
ADDON_PATH = _translate_path(ADDON.getAddonInfo('path'))
DATA_DIR = 'special://profile/addon_data/%s/' % ADDON_ID

sys.path.insert(0, os.path.join(ADDON_PATH, 'resources', 'lib'))

from bg_manager import (        # noqa: E402
    apply_saved_background,
    get_dirs, add_dir, remove_dir,
    get_background, set_background, clear_background,
)
from media_scanner import scan_directory, scan_all_dirs  # noqa: E402


# ---------------------------------------------------------------------------
# URL helpers
# ---------------------------------------------------------------------------

def _url(**params):
    return BASE_URL + '?' + urlencode(params)


def _get_params():
    return dict(parse_qsl(sys.argv[2][1:]))


# ---------------------------------------------------------------------------
# Item factories
# ---------------------------------------------------------------------------

def _dir_item(label, path, is_folder=True):
    """Return a (url, ListItem, is_folder) tuple for a directory-like entry."""
    li = xbmcgui.ListItem(label)
    li.setArt({'icon': 'DefaultFolder.png', 'thumb': 'DefaultFolder.png'})
    return path, li, is_folder


def _action_item(label, action, **extra_params):
    """Return a (url, ListItem, False) tuple for a non-folder action entry."""
    li = xbmcgui.ListItem(label)
    li.setArt({'icon': 'DefaultAddonProgram.png'})
    return _url(action=action, **extra_params), li, False


def _media_item(item, current_bg_path):
    """
    Build a ListItem for a media file dict with thumbnail and context menu.

    *item* is a dict with keys 'path', 'name', 'type' (from media_scanner).
    """
    path = item['path']
    name = item['name']
    mtype = item['type']
    is_current = (path == current_bg_path)

    label = ('[COLOR=green][BG][/COLOR] ' if is_current else '') + name

    li = xbmcgui.ListItem(label)
    li.setArt({'thumb': path, 'icon': path})
    li.setInfo('pictures', {'title': name})

    set_url = _url(action='set_bg', path=path, type=mtype)
    preview_url = _url(action='preview', path=path, type=mtype)

    li.addContextMenuItems([
        (ADDON.getLocalizedString(32010), 'RunPlugin(%s)' % set_url),
        (ADDON.getLocalizedString(32011), 'RunPlugin(%s)' % preview_url),
    ], replaceItems=False)

    return _url(action='set_bg', path=path, type=mtype), li, False


# ---------------------------------------------------------------------------
# Views
# ---------------------------------------------------------------------------

def root_menu():
    """Show the top-level plugin menu."""
    bg = get_background(DATA_DIR)
    bg_path = bg.get('path', '')
    bg_type = bg.get('type', '')

    items = []

    # Manage Directories
    url, li, folder = _dir_item(
        ADDON.getLocalizedString(32001),
        _url(action='manage_dirs'),
    )
    items.append((url, li, folder))

    # Browse Media
    url, li, folder = _dir_item(
        ADDON.getLocalizedString(32002),
        _url(action='browse'),
    )
    items.append((url, li, folder))

    # Current Background status (informational, clicking opens browse)
    if bg_path:
        label = '%s: [COLOR=green]%s[/COLOR]' % (
            ADDON.getLocalizedString(32003),
            os.path.basename(bg_path),
        )
        url, li, folder = _dir_item(label, _url(action='browse'), is_folder=True)
    else:
        label = '%s: [COLOR=gray]%s[/COLOR]' % (
            ADDON.getLocalizedString(32003),
            ADDON.getLocalizedString(32004),
        )
        url, li, folder = _dir_item(label, _url(action='browse'), is_folder=True)
    items.append((url, li, folder))

    # Clear Background (only shown when a bg is set)
    if bg_path:
        url, li, folder = _action_item(
            ADDON.getLocalizedString(32005),
            'clear_bg',
        )
        items.append((url, li, folder))

    xbmcplugin.setPluginCategory(HANDLE, ADDON.getAddonInfo('name'))
    xbmcplugin.addDirectoryItems(HANDLE, items)
    xbmcplugin.endOfDirectory(HANDLE)


def show_manage_dirs():
    """List configured directories with an Add option and per-dir Remove context."""
    dirs = get_dirs(DATA_DIR)
    items = []

    # Add Directory entry
    url, li, folder = _action_item(
        ADDON.getLocalizedString(32006),
        'add_dir',
    )
    items.append((url, li, folder))

    for d in dirs:
        browse_url = _url(action='browse', dir=d)
        li = xbmcgui.ListItem(d)
        li.setArt({'icon': 'DefaultFolder.png', 'thumb': 'DefaultFolder.png'})
        remove_url = _url(action='remove_dir', dir=d)
        li.addContextMenuItems([
            (ADDON.getLocalizedString(32007), 'RunPlugin(%s)' % remove_url),
        ], replaceItems=False)
        items.append((browse_url, li, True))

    xbmcplugin.setPluginCategory(HANDLE, ADDON.getLocalizedString(32001))
    xbmcplugin.addDirectoryItems(HANDLE, items)
    xbmcplugin.endOfDirectory(HANDLE)


# dialog.browse() returns this sentinel when the user cancels
_BROWSE_CANCELLED = 'files'


def do_add_dir():
    """Open a folder-browser dialog and add the chosen directory."""
    dialog = xbmcgui.Dialog()
    chosen = dialog.browse(0, ADDON.getLocalizedString(32008), 'files')
    if chosen and chosen != _BROWSE_CANCELLED:
        added = add_dir(DATA_DIR, chosen)
        if added:
            xbmcgui.Dialog().notification(
                ADDON.getAddonInfo('name'),
                ADDON.getLocalizedString(32020),
                xbmcgui.NOTIFICATION_INFO,
                3000,
            )
        # Refresh the manage_dirs listing
        xbmc.executebuiltin('Container.Refresh')


def do_remove_dir(dirpath):
    """Remove a directory from the list and refresh."""
    remove_dir(DATA_DIR, dirpath)
    xbmc.executebuiltin('Container.Refresh')


def show_browse(dirpath=None):
    """
    Show media files.  If *dirpath* is given, show only that directory;
    otherwise show all configured directories combined.
    """
    bg = get_background(DATA_DIR)
    bg_path = bg.get('path', '')

    if dirpath:
        media_items = scan_directory(dirpath)
        category = dirpath
    else:
        dirs = get_dirs(DATA_DIR)
        if not dirs:
            xbmcgui.Dialog().notification(
                ADDON.getAddonInfo('name'),
                ADDON.getLocalizedString(32021),
                xbmcgui.NOTIFICATION_WARNING,
                4000,
            )
            media_items = []
            category = ADDON.getLocalizedString(32002)
        else:
            media_items = scan_all_dirs(dirs)
            category = ADDON.getLocalizedString(32002)

    items = [_media_item(m, bg_path) for m in media_items]

    xbmcplugin.setPluginCategory(HANDLE, category)
    xbmcplugin.setContent(HANDLE, 'images')
    xbmcplugin.addDirectoryItems(HANDLE, items)
    xbmcplugin.endOfDirectory(HANDLE)


def do_preview(path, mtype):
    """Preview a file: fullscreen for images, play for videos."""
    if mtype == 'video':
        xbmc.Player().play(path)
    else:
        xbmc.executebuiltin('ShowPicture(%s)' % path)


def do_set_bg(path, mtype):
    """Set the selected file as the MusicVisualisation background."""
    set_background(DATA_DIR, path, mtype)
    xbmcgui.Dialog().notification(
        ADDON.getAddonInfo('name'),
        ADDON.getLocalizedString(32022),
        xbmcgui.NOTIFICATION_INFO,
        3000,
    )
    xbmc.executebuiltin('Container.Refresh')


def do_clear_bg():
    """Clear the custom background and restore artist fanart."""
    clear_background(DATA_DIR)
    xbmcgui.Dialog().notification(
        ADDON.getAddonInfo('name'),
        ADDON.getLocalizedString(32023),
        xbmcgui.NOTIFICATION_INFO,
        3000,
    )
    xbmc.executebuiltin('Container.Refresh')


# ---------------------------------------------------------------------------
# Router
# ---------------------------------------------------------------------------

def router(params):
    action = params.get('action', 'root')

    if action == 'root':
        root_menu()
    elif action == 'manage_dirs':
        show_manage_dirs()
    elif action == 'add_dir':
        do_add_dir()
    elif action == 'remove_dir':
        do_remove_dir(params['dir'])
    elif action == 'browse':
        show_browse(params.get('dir'))
    elif action == 'preview':
        do_preview(params['path'], params.get('type', 'image'))
    elif action == 'set_bg':
        do_set_bg(params['path'], params.get('type', 'image'))
    elif action == 'clear_bg':
        do_clear_bg()
    else:
        root_menu()


if __name__ == '__main__':
    # Re-apply the saved background skin strings every time the plugin starts
    # (restores them after a skin reload or Kodi restart).
    apply_saved_background(DATA_DIR)
    router(_get_params())
