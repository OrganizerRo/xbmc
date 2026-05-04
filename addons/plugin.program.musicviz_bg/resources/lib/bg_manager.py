# -*- coding: utf-8 -*-
"""
bg_manager.py — Config persistence and skin-string management.

Config is stored as JSON in the addon data directory:
  {
    "directories": ["/path/one", "/path/two"],
    "background": {"path": "/path/to/file.jpg", "type": "image"}
  }

Two skin string variables are written to communicate with MusicVisualisation.xml:
  musicviz_bg_path  — absolute path of the selected background file
  musicviz_bg_type  — "image" or "video"
"""
from __future__ import unicode_literals

import json
import os

import xbmc
import xbmcvfs

_SKIN_STR_PATH = 'musicviz_bg_path'
_SKIN_STR_TYPE = 'musicviz_bg_type'
_CONFIG_FILE = 'config.json'


def _translate_path(path):
    try:
        return xbmcvfs.translatePath(path)
    except AttributeError:
        return xbmc.translatePath(path)


def _ensure_dir(dirpath):
    if not xbmcvfs.exists(dirpath):
        xbmcvfs.mkdirs(dirpath)


def _config_path(data_dir):
    return os.path.join(_translate_path(data_dir), _CONFIG_FILE)


def load_config(data_dir):
    """Return the full config dict, or a blank default if it does not exist."""
    path = _config_path(data_dir)
    if xbmcvfs.exists(path):
        try:
            f = xbmcvfs.File(path)
            raw = f.read()
            f.close()
            return json.loads(raw)
        except (ValueError, IOError):
            pass
    return {'directories': [], 'background': {}}


def save_config(data_dir, config):
    """Persist the config dict to disk. Returns True on success."""
    _ensure_dir(_translate_path(data_dir))
    path = _config_path(data_dir)
    try:
        f = xbmcvfs.File(path, 'w')
        f.write(json.dumps(config, indent=2))
        f.close()
        return True
    except IOError:
        return False


# ---------------------------------------------------------------------------
# Directory helpers
# ---------------------------------------------------------------------------

def get_dirs(data_dir):
    """Return the list of configured background-source directories."""
    return load_config(data_dir).get('directories', [])


def add_dir(data_dir, dirpath):
    """Add *dirpath* to the directory list. Returns True if it was new."""
    config = load_config(data_dir)
    dirs = config.setdefault('directories', [])
    if dirpath in dirs:
        return False
    dirs.append(dirpath)
    save_config(data_dir, config)
    return True


def remove_dir(data_dir, dirpath):
    """Remove *dirpath* from the directory list. Returns True if it existed."""
    config = load_config(data_dir)
    dirs = config.get('directories', [])
    if dirpath not in dirs:
        return False
    dirs.remove(dirpath)
    save_config(data_dir, config)
    return True


# ---------------------------------------------------------------------------
# Background helpers
# ---------------------------------------------------------------------------

def get_background(data_dir):
    """Return the current background dict, e.g. {"path": "...", "type": "image"}."""
    return load_config(data_dir).get('background', {})


def set_background(data_dir, path, bg_type):
    """Save background to config and apply skin strings."""
    config = load_config(data_dir)
    config['background'] = {'path': path, 'type': bg_type}
    save_config(data_dir, config)
    _apply_skin_strings(path, bg_type)


def clear_background(data_dir):
    """Remove the custom background and restore fanart."""
    config = load_config(data_dir)
    config['background'] = {}
    save_config(data_dir, config)
    _clear_skin_strings()


def apply_saved_background(data_dir):
    """Re-apply skin strings from saved config (call on plugin start)."""
    bg = get_background(data_dir)
    if bg.get('path'):
        _apply_skin_strings(bg['path'], bg.get('type', 'image'))
    else:
        _clear_skin_strings()


# ---------------------------------------------------------------------------
# Skin-string helpers
# ---------------------------------------------------------------------------

def _apply_skin_strings(path, bg_type):
    xbmc.executebuiltin('Skin.SetString(%s,%s)' % (_SKIN_STR_PATH, path))
    xbmc.executebuiltin('Skin.SetString(%s,%s)' % (_SKIN_STR_TYPE, bg_type))


def _clear_skin_strings():
    xbmc.executebuiltin('Skin.Reset(%s)' % _SKIN_STR_PATH)
    xbmc.executebuiltin('Skin.Reset(%s)' % _SKIN_STR_TYPE)
